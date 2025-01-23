
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_aec.h"
#include "esp_log.h"

#include "esp_gmf_aec.h"
#include "esp_gmf_audio_element.h"
#include "esp_gmf_element.h"
#include "esp_gmf_err.h"
#include "esp_gmf_node.h"
#include "esp_gmf_oal_mem.h"

typedef struct {
    esp_gmf_audio_element_t parent;
    aec_handle_t aec_handle;
    uint8_t *buffer;
} gmf_aec_t;

static const char *TAG = "GMF_AEC";

static esp_gmf_err_t gmf_aec_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, cfg, { return ESP_GMF_ERR_INVALID_ARG; });
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG; });
    *handle = NULL;
    esp_gmf_aec_cfg_t *aec_cfg = (esp_gmf_aec_cfg_t *)cfg;
    esp_gmf_obj_handle_t new_obj = NULL;
    esp_gmf_err_t ret = esp_gmf_aec_init(aec_cfg, &new_obj);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    ret = esp_gmf_aec_cast(aec_cfg, new_obj);
    if (ret != ESP_GMF_ERR_OK) {
        esp_gmf_obj_delete(new_obj);
        return ret;
    }
    *handle = (void *)new_obj;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t gmf_aec_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, evt, { return ESP_GMF_ERR_INVALID_ARG; });
    ESP_GMF_NULL_CHECK(TAG, ctx, { return ESP_GMF_ERR_INVALID_ARG; });
    esp_gmf_element_handle_t self = (esp_gmf_element_handle_t)ctx;
    esp_gmf_element_handle_t el = evt->from;
    esp_gmf_event_state_t state = ESP_GMF_EVENT_STATE_NONE;
    esp_gmf_element_get_state(self, &state);
    esp_gmf_element_handle_t prev = NULL;
    esp_gmf_element_get_prev_el(self, &prev);
    if ((state == ESP_GMF_EVENT_STATE_NONE) || (prev == el)) {
        if (evt->sub == ESP_GMF_INFO_SOUND) {
            esp_gmf_info_sound_t info = { 0 };
            memcpy(&info, evt->payload, evt->payload_size);
            ESP_LOGD(TAG, "RECV info, from: %s-%p, next: %p, self: %s-%p, type: %x, state: %s, rate: %d, ch: %d, bits: %d",
                     OBJ_GET_TAG(el), el, esp_gmf_node_for_next((esp_gmf_node_t *)el), OBJ_GET_TAG(self), self, evt->type,
                     esp_gmf_event_get_state_str(state), info.sample_rates, info.channels, info.bits);
            if (info.sample_rates != 16000 || info.bits != 16) {
                return ESP_GMF_ERR_NOT_SUPPORT;
            }
            esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
        }
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t gmf_aec_destroy(esp_gmf_audio_element_handle_t self)
{
    if (self != NULL) {
        gmf_aec_t *gmf_aec = (gmf_aec_t *)self;
        if (gmf_aec->aec_handle) {
            aec_destroy(gmf_aec->aec_handle);
        }
        if (gmf_aec->buffer) {
            esp_gmf_oal_free(gmf_aec->buffer);
        }
        ESP_LOGD(TAG, "Destroyed");
        esp_gmf_audio_el_deinit(self);
        esp_gmf_oal_free(self);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t gmf_aec_open(esp_gmf_audio_element_handle_t self, void *para)
{
    gmf_aec_t *gmf_aec = (gmf_aec_t *)self;
    esp_gmf_aec_cfg_t *cfg = OBJ_GET_CFG(self);
    gmf_aec->aec_handle = aec_pro_create(cfg->frame_len, cfg->nch, cfg->mode);
    gmf_aec->buffer = esp_gmf_oal_calloc(1, 16 * 2 * cfg->frame_len * (cfg->nch + 1));
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t gmf_aec_process(esp_gmf_audio_element_handle_t self, void *para)
{
    gmf_aec_t *gmf_aec = (gmf_aec_t *)self;
    esp_gmf_aec_cfg_t *cfg = OBJ_GET_CFG(self);
    int out_len = -1;
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    esp_gmf_err_io_t load_ret = esp_gmf_port_acquire_in(in_port, &in_load, 16 * 2 * cfg->frame_len * (cfg->nch + 1), ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, load_ret, out_len, { goto __quit; });
    uint32_t total_samples = in_load->valid_size / 2 / (cfg->nch + 1);
    uint32_t mic_dlen = total_samples * 2 * cfg->nch;
    uint16_t *ref = &in_load->buf[mic_dlen];
    uint32_t frame_size = cfg->frame_len * (cfg->nch + 1) * 2;
    uint32_t proc_samples = 0;
    ESP_LOGD(TAG, "tsamples %lu, mic_dlen %lu, frame_size %lu", total_samples, mic_dlen, frame_size);
    if (in_load->valid_size > frame_size) {
        load_ret = esp_gmf_port_acquire_out(out_port, &out_load, mic_dlen, ESP_GMF_MAX_DELAY);
        ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, { goto __quit; });
#if 1
        while (proc_samples < total_samples) {
            ESP_LOGD(TAG, "proc %lu, %lu", proc_samples, proc_samples * 2 * cfg->nch);
            aec_process(gmf_aec->aec_handle,
                        (int16_t *)&in_load->buf[proc_samples * 2 * cfg->nch],
                        (int16_t *)&ref[proc_samples * 2],
                        (int16_t *)&out_load->buf[proc_samples * 2 * cfg->nch]);
            proc_samples += cfg->frame_len;
        }
        // TODO: cache the left data
#else
        memcpy(out_load->buf, in_load->buf, mic_dlen);
#endif
    }
    out_load->valid_size = mic_dlen;
    out_load->is_done = in_load->is_done;
    out_len = out_load->valid_size;

    // ESP_LOGI(TAG, "proc %lu, samples %lu", proc_samples, total_samples);
__quit:
    if (out_load != NULL) {
        load_ret = esp_gmf_port_release_out(out_port, out_load, ESP_GMF_MAX_DELAY);
        ESP_GMF_PORT_RELEASE_OUT_CHECK(TAG, load_ret, out_len, NULL);
    }
    if (in_load != NULL) {
        load_ret = esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
        ESP_GMF_PORT_RELEASE_IN_CHECK(TAG, load_ret, out_len, NULL);
    }
    return out_len;
}

static esp_gmf_job_err_t gmf_aec_close(esp_gmf_audio_element_handle_t self, void *para)
{
    return ESP_GMF_JOB_ERR_OK;
}

esp_gmf_err_t esp_gmf_aec_init(esp_gmf_aec_cfg_t *config, esp_gmf_obj_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, config, { return ESP_GMF_ERR_INVALID_ARG; });
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG; });
    *handle = NULL;
    gmf_aec_t *gmf_aec = esp_gmf_oal_calloc(1, sizeof(gmf_aec_t));
    ESP_GMF_MEM_VERIFY(TAG, gmf_aec, { return ESP_GMF_ERR_MEMORY_LACK; }, "aec", sizeof(gmf_aec_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)gmf_aec;
    obj->new = gmf_aec_new;
    obj->delete = gmf_aec_destroy;

    esp_gmf_aec_cfg_t *obj_cfg = esp_gmf_oal_calloc(1, sizeof(esp_gmf_aec_cfg_t));
    memcpy(obj_cfg, config, sizeof(esp_gmf_aec_cfg_t));
    esp_gmf_err_t ret = esp_gmf_obj_set_config(obj, obj_cfg, sizeof(esp_gmf_aec_cfg_t));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto __failed, "Failed set OBJ configuration");
    ret = esp_gmf_obj_set_tag(obj, "aec");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto __failed, "Failed set OBJ tag");
    esp_gmf_element_cfg_t el_cfg = { 0 };
    ESP_GMF_ELEMENT_CFG(el_cfg, true, ESP_GMF_EL_PORT_CAP_SINGLE, ESP_GMF_EL_PORT_CAP_MULTI,
                        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_PORT_TYPE_BYTE | ESP_GMF_PORT_TYPE_BLOCK);
    esp_gmf_audio_el_init(gmf_aec, &el_cfg);
    *handle = obj;
    return ESP_GMF_ERR_OK;
__failed:
    esp_gmf_obj_delete(obj);
    return ret;
}

esp_gmf_err_t esp_gmf_aec_cast(esp_gmf_aec_cfg_t *config, esp_gmf_obj_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, config, { return ESP_GMF_ERR_INVALID_ARG; });
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG; });

    esp_gmf_aec_cfg_t *new_cfg = esp_gmf_oal_calloc(1, sizeof(esp_gmf_aec_cfg_t));
    memcpy(new_cfg, config, sizeof(esp_gmf_aec_cfg_t));
    ESP_GMF_MEM_CHECK(TAG, new_cfg, { return ESP_GMF_ERR_MEMORY_LACK; });

    // Free memory before overwriting
    esp_gmf_oal_free(OBJ_GET_CFG(handle));
    esp_gmf_obj_set_config(handle, new_cfg, sizeof(*config));

    esp_gmf_audio_element_handle_t aec = (esp_gmf_audio_element_handle_t)handle;
    ESP_GMF_ELEMENT_GET(aec)->ops.open = gmf_aec_open;
    ESP_GMF_ELEMENT_GET(aec)->ops.process = gmf_aec_process;
    ESP_GMF_ELEMENT_GET(aec)->ops.close = gmf_aec_close;
    ESP_GMF_ELEMENT_GET(aec)->ops.event_receiver = gmf_aec_received_event_handler;

    return ESP_GMF_ERR_OK;
}
