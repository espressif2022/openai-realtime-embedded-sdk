#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_gmf_ch_picker.h"
#include "ai_audio_common.h"

#include "esp_gmf_audio_element.h"
#include "esp_gmf_err.h"
#include "esp_gmf_oal_mem.h"
#include "esp_log.h"

#define CHUNK_SAMPLES (512)

typedef struct {
    esp_gmf_audio_element_t parent;
    uint8_t source_ch;
    uint32_t bits;
    int32_t groups[MAX_INPUT_CH][MAX_INPUT_CH];
    int32_t group_sizes[MAX_INPUT_CH];
    int32_t group_count;
    int32_t ch_count;
} ch_picker_t;

static const char *TAG = "CH_PICKER";

static esp_gmf_err_t ch_picker_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
{
    int ret = ESP_GMF_ERR_OK;
    ch_picker_t *self = (esp_gmf_element_handle_t)ctx;
    esp_gmf_element_handle_t el = evt->from;
    if (evt->type != ESP_GMF_EVT_TYPE_REPORT_INFO) {
        return ret;
    }
    esp_gmf_event_state_t state = -1;
    esp_gmf_element_get_state(self, &state);
    esp_gmf_element_handle_t prev = NULL;
    esp_gmf_element_get_prev_el(self, &prev);
    if ((state == ESP_GMF_EVENT_STATE_NONE) || (prev == el)) {
        switch (evt->sub) {
            case ESP_GMF_INFO_SOUND: {
                    esp_gmf_info_sound_t info = {0};
                    memcpy(&info, evt->payload, evt->payload_size);
                    self->source_ch = info.channels;
                    self->bits = info.bits;
                    esp_gmf_audio_el_set_snd_info(self, &info);
                    // Change the state to ESP_GMF_EVENT_STATE_INITIALIZED, then add to working list.
                    esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
                }
            default:
                break;
        }
    }
    return ret;
}

static esp_gmf_err_t ch_picker_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    ESP_GMF_MEM_CHECK(TAG, cfg, { return ESP_GMF_ERR_INVALID_ARG; });
    ESP_GMF_MEM_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG; });
    ch_picker_cfg_t *ch_picker_cfg = (ch_picker_cfg_t *)cfg;
    esp_gmf_obj_handle_t new_obj = NULL;
    int ret = ESP_GMF_ERR_OK;
    ret = esp_gmf_ch_picker_init(ch_picker_cfg, &new_obj);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    ret = esp_gmf_ch_picker_cast(ch_picker_cfg, new_obj);
    *handle = (void *)new_obj;
    ESP_LOGI(TAG, "New an object,%s-%p", OBJ_GET_TAG(new_obj), new_obj);
    return ret;
}

static esp_gmf_err_t ch_picker_destroy(esp_gmf_audio_element_handle_t self)
{
    esp_gmf_oal_free(OBJ_GET_CFG(self));
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(self);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t parse_channel_order(esp_gmf_audio_element_handle_t self, char *order_str, int32_t num_channels)
{
    ch_picker_t *ch_picker = (ch_picker_t *)self;
    char temp[10];
    int temp_index = 0;

    for (int i = 0; i < strlen(order_str); i++) {
        if (order_str[i] == ',' || order_str[i] == '|' || order_str[i] == ' ') {
            if (temp_index > 0) {
                temp[temp_index] = '\0';
                int channel = atoi(temp);
                if (channel >= 0 && channel < num_channels) {
                    ch_picker->groups[ch_picker->group_count][ch_picker->group_sizes[ch_picker->group_count]++] = channel;
                    ch_picker->ch_count++;
                }
                temp_index = 0;
            }
            if (order_str[i] == '|') {
                ch_picker->group_count++;
            }
        } else if (order_str[i] >= '0' && order_str[i] <= '9') {
            temp[temp_index++] = order_str[i];
        }
    }
    if (temp_index > 0) {
        temp[temp_index] = '\0';
        int channel = atoi(temp);
        if (channel >= 0 && channel < num_channels) {
            ch_picker->groups[ch_picker->group_count][ch_picker->group_sizes[ch_picker->group_count]++] = channel;
            ch_picker->ch_count++;
        }
    }
    char log[100] = { 0 };
    uint32_t len = snprintf(log, 100, "\nSorted channel order:\n");
    for (int i = 0; i <= ch_picker->group_count; i++) {
        len += snprintf(&log[len], 100 - len, "  Group %d:\n", i);
        for (int j = 0; j < ch_picker->group_sizes[i]; j++) {
            len += snprintf(&log[len], 100 - len, "\tChannel %ld\n", ch_picker->groups[i][j]);
        }
    }
    ESP_LOGW(TAG, "%s", log);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t ch_picker_open(esp_gmf_audio_element_handle_t self, void *para)
{
    ch_picker_t *ch_picker = (ch_picker_t *)self;
    ch_picker_cfg_t *cfg = OBJ_GET_CFG(self);

    esp_gmf_info_sound_t snd_info = {0};
    esp_gmf_audio_el_get_snd_info(self, &snd_info);
    parse_channel_order(self, cfg->ch_choice, ch_picker->source_ch);
    snd_info.bits = 16;
    snd_info.channels = ch_picker->ch_count;
    esp_gmf_element_notify_snd_info(self, &snd_info);
    return ESP_GMF_JOB_ERR_OK;
}

static void process_pcm_data(esp_gmf_audio_element_handle_t self, uint16_t *pcm_data, int num_samples, uint16_t *sorted_pcm_data)
{
    ch_picker_t *ch_picker = (ch_picker_t *)self;
    uint16_t(*arr2d)[ch_picker->source_ch] = pcm_data;

    int sorted_index = 0;
    for (int i = 0; i <= ch_picker->group_count; i++) {
        if (ch_picker->group_sizes[i] == 1) {
            int channel = ch_picker->groups[i][0];
            for (int j = 0; j < num_samples; j++) {
                sorted_pcm_data[sorted_index++] = arr2d[j][channel];
            }
        } else {
            int idx = 0;
            while (idx < num_samples) {
                for (int j = 0; j < ch_picker->group_sizes[i]; j++) {
                    int channel = ch_picker->groups[i][j];
                    sorted_pcm_data[sorted_index++] = arr2d[idx][channel];
                }
                idx++;
            }
        }
    }
}

static esp_gmf_job_err_t ch_picker_process(esp_gmf_audio_element_handle_t self, void *para)
{
    ch_picker_t *ch_picker = (ch_picker_t *)self;
    int ret = 0;
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    size_t read_size = ch_picker->source_ch * (ch_picker->bits / 8) * CHUNK_SAMPLES;

    ret = esp_gmf_port_acquire_in(in_port, &in_load, read_size, ESP_GMF_MAX_DELAY);
    if (ret < 0) {
        ESP_LOGE(TAG, "Read data error, ret:%d, line:%d", ret, __LINE__);
        return ret == ESP_GMF_IO_ABORT ? ESP_GMF_JOB_ERR_OK : ESP_GMF_JOB_ERR_FAIL;
    }
    size_t samples = in_load->valid_size / ch_picker->source_ch / (ch_picker->bits / 8);
    size_t write_size =  samples * ch_picker->ch_count * (ch_picker->bits / 8);
    ESP_LOGD(TAG, "channel picker in_size : %d, samples : %u, write_size : %u", in_load->valid_size, samples, write_size);
    // out_load = in_load;
    ret = esp_gmf_port_acquire_out(out_port, &out_load,
                               write_size,
                               ESP_GMF_MAX_DELAY);
    if (ret < 0) {
        ESP_LOGE(TAG, "Write data error, ret:%d, line:%d", ret, __LINE__);
        return ret == ESP_GMF_IO_ABORT ? ESP_GMF_JOB_ERR_OK : ESP_GMF_JOB_ERR_FAIL;
    }
    if (ch_picker->bits == 16) {
        process_pcm_data(self, (uint16_t *)in_load->buf, samples, (uint16_t *)out_load->buf);
    } else {
        ESP_LOGE(TAG, " %lu bits not support", ch_picker->bits);
        return ESP_GMF_ERR_NOT_SUPPORT;
    }

    out_load->valid_size = write_size;
    out_load->is_done = in_load->is_done;
    ret = out_load->valid_size;

    esp_gmf_audio_el_update_file_pos((esp_gmf_element_handle_t)self, out_load->valid_size);
    if (in_load->is_done) {
        ret = ESP_GMF_JOB_ERR_DONE;
        ESP_LOGI(TAG, "The channel picker done, o_len:%u", out_load->valid_size);
    }
    esp_gmf_port_release_out(out_port, out_load, ESP_GMF_MAX_DELAY);
    esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
    return ret;
}

static esp_gmf_job_err_t ch_picker_close(esp_gmf_audio_element_handle_t self, void *para)
{
    return ESP_GMF_JOB_ERR_OK;
}

esp_gmf_err_t esp_gmf_ch_picker_init(ch_picker_cfg_t *config, esp_gmf_obj_handle_t *handle)
{
    ESP_GMF_MEM_CHECK(TAG, config, { return ESP_GMF_ERR_INVALID_ARG; });
    ESP_GMF_MEM_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG; });

    ch_picker_t *ch_picker = esp_gmf_oal_calloc(1, sizeof(ch_picker_t));
    ESP_GMF_MEM_CHECK(TAG, ch_picker, { return ESP_GMF_ERR_MEMORY_LACK; });
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)ch_picker;

    ch_picker_cfg_t *obj_cfg = esp_gmf_oal_calloc(1, sizeof(ch_picker_cfg_t));
    memcpy(obj_cfg, config, sizeof(ch_picker_cfg_t));
    esp_gmf_obj_set_config(obj, obj_cfg, sizeof(ch_picker_cfg_t));
    int ret = esp_gmf_obj_set_tag(obj, "ch_picker");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto __failed, "Failed set OBJ tag");

    obj->new = ch_picker_new;
    obj->delete = ch_picker_destroy;

    esp_gmf_element_cfg_t el_cfg = {
        .cb = NULL,
        .dependency = true,
        .in_attr.cap = ESP_GMF_EL_PORT_CAP_SINGLE,
        .out_attr.cap = ESP_GMF_EL_PORT_CAP_SINGLE,
        .in_attr.type = ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE,
        .out_attr.type = ESP_GMF_PORT_TYPE_BYTE | ESP_GMF_PORT_TYPE_BLOCK,
    };
    ret = esp_gmf_audio_el_init(ch_picker, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto __failed, "Failed Initialie audio el");
    *handle = obj;
    ESP_LOGI(TAG, "Create channel picker, %s-%p", OBJ_GET_TAG(obj), obj);

    return ESP_GMF_ERR_OK;

__failed:
    esp_gmf_obj_delete(obj);
    esp_gmf_oal_free(ch_picker);
    return ret;
}

esp_gmf_err_t esp_gmf_ch_picker_cast(ch_picker_cfg_t *config, esp_gmf_obj_handle_t handle)
{
    ESP_GMF_MEM_CHECK(TAG, config, { return ESP_GMF_ERR_INVALID_ARG; });
    ESP_GMF_MEM_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG; });

    ch_picker_cfg_t *new_cfg = esp_gmf_oal_calloc(1, sizeof(ch_picker_cfg_t));
    memcpy(new_cfg, config, sizeof(ch_picker_cfg_t));
    ESP_GMF_MEM_CHECK(TAG, new_cfg, { return ESP_GMF_ERR_MEMORY_LACK; });

    // Free memory before overwriting
    esp_gmf_oal_free(OBJ_GET_CFG(handle));
    esp_gmf_obj_set_config(handle, new_cfg, sizeof(*config));

    esp_gmf_audio_element_t *ch_picker_el = (esp_gmf_audio_element_t *)handle;
    ch_picker_el->base.ops.open = ch_picker_open;
    ch_picker_el->base.ops.process = ch_picker_process;
    ch_picker_el->base.ops.close = ch_picker_close;
    ch_picker_el->base.ops.event_receiver = ch_picker_received_event_handler;

    return ESP_GMF_ERR_OK;
}
