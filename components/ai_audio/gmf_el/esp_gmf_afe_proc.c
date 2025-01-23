#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"

#include "esp_gmf_audio_element.h"
#include "esp_gmf_data_bus.h"
#include "esp_gmf_element.h"
#include "esp_gmf_err.h"
#include "esp_gmf_job.h"
#include "esp_gmf_new_databus.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_obj.h"
#include "esp_gmf_pbuf.h"
#include "esp_gmf_port.h"

#include "afe_proc.h"
#include "esp_gmf_afe_proc.h"

#define WITH_PBUF (true)

typedef struct {
    esp_gmf_audio_element_t parent;
    esp_gmf_db_handle_t afe_in_db;
#if WITH_PBUF
    esp_gmf_pbuf_handle_t out_pbuf;
#else
    QueueHandle_t out_q;
#endif
} esp_gmf_afe_proc_t;

typedef struct {
    void *p;
    size_t dlen;
} afe_output_data_t;

static const char *TAG = "GMF_AFE_PROC";

static esp_gmf_err_t esp_gmf_afe_proc_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
{
    int ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t self = (esp_gmf_element_handle_t)ctx;
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
                esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
            }
            default:
                break;
        }
    }
    return ret;
}

static void esp_gmf_afe_result_proc(afe_fetch_result_t *result, void *user_ctx)
{
    esp_gmf_afe_proc_t *ai_audio = (esp_gmf_afe_proc_t *)user_ctx;
    if (result->data_size) {
#if WITH_PBUF
        ESP_LOGD(TAG, "result %d", result->data_size);
        esp_gmf_data_bus_block_t blk = { 0 };
        esp_gmf_pbuf_acquire_write(ai_audio->out_pbuf, &blk, result->data_size, portMAX_DELAY);
        memcpy(blk.buf, result->data, result->data_size);
        blk.valid_size = result->data_size;
        esp_gmf_pbuf_release_write(ai_audio->out_pbuf, &blk, 0);
#else
        void *tmp = esp_gmf_oal_calloc(1, result->data_size);
        memcpy(tmp, result->data, result->data_size);
        afe_output_data_t msg = {
            .p = tmp,
            .dlen = result->data_size,
        };
        xQueueSend(ai_audio->out_q, &msg, 0);
#endif
    }
}

static int32_t esp_gmf_afe_read_cb(void *buffer, int buf_sz, void *user_ctx, TickType_t ticks)
{
    esp_gmf_afe_proc_t *ai_audio = (esp_gmf_afe_proc_t *)user_ctx;
    esp_gmf_data_bus_block_t blk = { 0 };
    blk.buf = buffer;
    blk.buf_length = buf_sz;
    ESP_LOGD(TAG, "Feed %u", blk.buf_length);
    esp_gmf_db_acquire_read(ai_audio->afe_in_db, &blk, buf_sz, ESP_GMF_MAX_DELAY);
    esp_gmf_db_release_read(ai_audio->afe_in_db, &blk, ESP_GMF_MAX_DELAY);
    return buf_sz;
}

static esp_gmf_err_t esp_gmf_afe_proc_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    ESP_GMF_MEM_CHECK(TAG, cfg, { return ESP_GMF_ERR_INVALID_ARG; });
    ESP_GMF_MEM_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG; });
    esp_gmf_afe_proc_cfg_t *gmf_afe_proc_cfg = (esp_gmf_afe_proc_cfg_t *)cfg;
    esp_gmf_obj_handle_t new_obj = NULL;
    int ret = ESP_GMF_ERR_OK;
    ret = esp_gmf_afe_proc_init(gmf_afe_proc_cfg, &new_obj);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    ret = esp_gmf_afe_proc_cast(gmf_afe_proc_cfg, new_obj);
    *handle = (void *)new_obj;
    ESP_LOGI(TAG, "New an object,%s-%p", OBJ_GET_TAG(new_obj), new_obj);
    return ret;
}

static esp_gmf_err_t esp_gmf_afe_proc_destroy(esp_gmf_audio_element_handle_t self)
{
    esp_gmf_oal_free(OBJ_GET_CFG(self));
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(self);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_afe_proc_open(esp_gmf_audio_element_handle_t self, void *para)
{
    esp_gmf_afe_proc_t *ai_audio = (esp_gmf_afe_proc_t *)self;
    esp_gmf_afe_proc_cfg_t *cfg = OBJ_GET_CFG(self);
    size_t buf_size = 0;
    afe_proc_get_chunk_size(cfg->afe, &buf_size);
    esp_gmf_db_new_ringbuf(2, buf_size, &ai_audio->afe_in_db);
#if WITH_PBUF
    esp_gmf_pbuf_create(10, &ai_audio->out_pbuf);
#else
    ai_audio->out_q = xQueueCreate(2, sizeof(afe_output_data_t));
#endif
    afe_proc_result_cb_register(cfg->afe, esp_gmf_afe_result_proc, ai_audio);
    afe_proc_set_read_cb(cfg->afe, esp_gmf_afe_read_cb, ai_audio);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_afe_proc_close(esp_gmf_audio_element_handle_t self, void *para)
{
    esp_gmf_afe_proc_t *ai_audio = (esp_gmf_afe_proc_t *)self;
    esp_gmf_afe_proc_cfg_t *cfg = OBJ_GET_CFG(self);
    if (ai_audio->afe_in_db) {
        esp_gmf_db_deinit(ai_audio->afe_in_db);
        ai_audio->afe_in_db = NULL;
    }
#if WITH_PBUF
    if (ai_audio->out_pbuf) {
        esp_gmf_pbuf_destroy(ai_audio->out_pbuf);
        ai_audio->out_pbuf = NULL;
    }
#else
    if (ai_audio->out_q) {
        vQueueDelete(ai_audio->out_q);
        ai_audio->out_q = NULL;
    }
#endif
    afe_proc_set_read_cb(cfg->afe, NULL, NULL);
    afe_proc_result_cb_unregister(cfg->afe, esp_gmf_afe_result_proc);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_afe_proc_proc(esp_gmf_audio_element_handle_t self, void *para)
{
    int ret = 0;
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    esp_gmf_afe_proc_t *ai_audio = (esp_gmf_afe_proc_t *)self;
    esp_gmf_afe_proc_cfg_t *cfg = OBJ_GET_CFG(self);
    size_t wanted_size = 0;
    afe_proc_get_chunk_size(cfg->afe, &wanted_size);
    ret = esp_gmf_port_acquire_in(in_port, &in_load, wanted_size, ESP_GMF_MAX_DELAY);
    if (ret < 0) {
        ESP_LOGD(TAG, "Read data error, ret:%d, line:%d", ret, __LINE__);
        return 0;
    }
    ESP_LOGV(TAG, "valid_size %u", in_load->valid_size);
    esp_gmf_data_bus_block_t blk = { 0 };
    esp_gmf_db_acquire_write(ai_audio->afe_in_db, &blk, in_load->valid_size, ESP_GMF_MAX_DELAY);
    blk.buf = in_load->buf;
    blk.valid_size = in_load->valid_size;
    esp_gmf_db_release_write(ai_audio->afe_in_db, &blk, ESP_GMF_MAX_DELAY);

#if WITH_PBUF
    memset(&blk, 0x00, sizeof(esp_gmf_data_bus_block_t));
    ret = esp_gmf_pbuf_acquire_read(ai_audio->out_pbuf, &blk, 0, 0);
    if (ret > 0) {
        if (blk.valid_size) {
            out_load = in_load;
            ESP_LOGV(TAG, "msg %p, %u", blk.buf, blk.valid_size);
            esp_gmf_port_acquire_out(out_port, &out_load, blk.valid_size, ESP_GMF_MAX_DELAY);
            memcpy(out_load->buf, blk.buf, blk.valid_size);
            out_load->valid_size = blk.valid_size;
            esp_gmf_port_release_out(out_port, out_load, ESP_GMF_MAX_DELAY);
        }
        esp_gmf_pbuf_release_read(ai_audio->out_pbuf, &blk, 0);
    }
#else
    afe_output_data_t msg = { 0 };
    xQueueReceive(ai_audio->out_q, &msg, 0);
    if (msg.dlen) {
        out_load = in_load;
        ESP_LOGV(TAG, "msg %p, %u", msg.p, msg.dlen);
        esp_gmf_port_acquire_out(out_port, &out_load, msg.dlen, ESP_GMF_MAX_DELAY);
        memcpy(out_load->buf, msg.p, msg.dlen);
        out_load->valid_size = msg.dlen;
        esp_gmf_port_release_out(out_port, out_load, ESP_GMF_MAX_DELAY);
        esp_gmf_oal_free(msg.p);
    }
#endif
    esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_afe_proc_init(void *config, esp_gmf_obj_handle_t *handle)
{
    ESP_GMF_MEM_CHECK(TAG, config, { return ESP_GMF_ERR_INVALID_ARG; });
    ESP_GMF_MEM_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG; });
    esp_gmf_afe_proc_t *ai_audio = esp_gmf_oal_calloc(1, sizeof(esp_gmf_afe_proc_t));
    ESP_GMF_MEM_CHECK(TAG, ai_audio, { return ESP_GMF_ERR_MEMORY_LACK; });
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)ai_audio;

    esp_gmf_afe_proc_cfg_t *obj_cfg = esp_gmf_oal_calloc(1, sizeof(esp_gmf_afe_proc_cfg_t));
    memcpy(obj_cfg, config, sizeof(esp_gmf_afe_proc_cfg_t));
    esp_gmf_obj_set_config(obj, obj_cfg, sizeof(esp_gmf_afe_proc_cfg_t));
    int ret = esp_gmf_obj_set_tag(obj, "afe_proc");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto __failed, "Failed set OBJ tag");

    obj->new = esp_gmf_afe_proc_new;
    obj->delete = esp_gmf_afe_proc_destroy;

    esp_gmf_element_cfg_t el_cfg = {
        .cb = NULL,
        .dependency = true,
        .in_attr.cap = ESP_GMF_EL_PORT_CAP_SINGLE,
        .out_attr.cap = ESP_GMF_EL_PORT_CAP_SINGLE,
        .in_attr.type = ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE,
        .out_attr.type = ESP_GMF_PORT_TYPE_BYTE | ESP_GMF_PORT_TYPE_BLOCK,
    };
    ret = esp_gmf_audio_el_init(ai_audio, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto __failed, "Failed Initialie audio el");
    *handle = obj;
    ESP_LOGI(TAG, "Create afe proc, %s-%p", OBJ_GET_TAG(obj), obj);

    return ESP_GMF_ERR_OK;

__failed:
    return ESP_GMF_ERR_FAIL;
}

esp_gmf_err_t esp_gmf_afe_proc_cast(void *config, esp_gmf_obj_handle_t handle)
{
    ESP_GMF_MEM_CHECK(TAG, config, { return ESP_GMF_ERR_INVALID_ARG; });
    ESP_GMF_MEM_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG; });

    esp_gmf_afe_proc_cfg_t *new_cfg = esp_gmf_oal_calloc(1, sizeof(esp_gmf_afe_proc_cfg_t));
    memcpy(new_cfg, config, sizeof(esp_gmf_afe_proc_cfg_t));
    ESP_GMF_MEM_CHECK(TAG, new_cfg, { return ESP_GMF_ERR_MEMORY_LACK; });

    // Free memory before overwriting
    esp_gmf_oal_free(OBJ_GET_CFG(handle));
    esp_gmf_obj_set_config(handle, new_cfg, sizeof(*config));

    esp_gmf_audio_element_t *esp_gmf_afe_proc_el = (esp_gmf_audio_element_t *)handle;
    esp_gmf_afe_proc_el->base.ops.open = esp_gmf_afe_proc_open;
    esp_gmf_afe_proc_el->base.ops.process = esp_gmf_afe_proc_proc;
    esp_gmf_afe_proc_el->base.ops.close = esp_gmf_afe_proc_close;
    esp_gmf_afe_proc_el->base.ops.event_receiver = esp_gmf_afe_proc_received_event_handler;

    return ESP_GMF_ERR_OK;
}
