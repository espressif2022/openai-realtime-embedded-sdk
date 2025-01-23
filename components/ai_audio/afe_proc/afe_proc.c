#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_private/freertos_idf_additions_priv.h"

#include "esp_afe_sr_models.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_nsn_models.h"

#include "afe_proc.h"
#include "ai_audio_common.h"

#define MAX_AFE_RESULT_PROC (5)
#define AFE_RUN_EVENT       (BIT0)
typedef struct {
    TaskHandle_t task;
    bool running;
} afe_proc_task_t;

typedef struct __afe {
    afe_proc_task_t feed;
    afe_proc_task_t fetch;
    ai_audio_data_read_cb_t read_cb;
    void *read_ctx;
    esp_afe_sr_data_t *afe_data;
    srmodel_list_t *models;
    struct {
        afe_proc_result_proc_t proc;
        void *result_ctx;
    } result_proc[MAX_AFE_RESULT_PROC];
    afe_proc_state_t state;
    EventGroupHandle_t ctrl_events;
} afe_proc_t;

static const char *TAG = "AFE_PROC";
static const esp_afe_sr_iface_t *esp_afe = &ESP_AFE_SR_HANDLE;

static void feed_task(void *arg)
{
    afe_proc_t *afe_proc = (afe_proc_t *)arg;
    int chan_num = esp_afe->get_total_channel_num(afe_proc->afe_data);
    int chunksize = esp_afe->get_feed_chunksize(afe_proc->afe_data);
    const int buf_size = chunksize * chan_num * sizeof(uint16_t);
    ESP_LOGI(TAG, "ch %d, chunk %d, buf size %d", chan_num, chunksize, buf_size);
    int16_t *buf = heap_caps_calloc_prefer(1, buf_size, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_INTERNAL);
    assert(buf);

    afe_proc->feed.running = true;

    while (afe_proc->feed.running) {
        xEventGroupWaitBits(afe_proc->ctrl_events, AFE_RUN_EVENT, false, true, portMAX_DELAY);

        int rlen = afe_proc->read_cb(buf, buf_size, afe_proc->read_ctx, portMAX_DELAY);
        if (rlen == buf_size) {
            esp_afe->feed(afe_proc->afe_data, buf);
        } else {
            ESP_LOGE(TAG, "afe read failed %d", rlen);
        }
    }

    heap_caps_free(buf);
    vTaskDelete(NULL);
}

static void fetch_task(void *arg)
{
    afe_proc_t *afe_proc = (afe_proc_t *)arg;
    afe_proc->fetch.running = true;

    while (afe_proc->fetch.running) {
        xEventGroupWaitBits(afe_proc->ctrl_events, AFE_RUN_EVENT, false, true, portMAX_DELAY);

        afe_fetch_result_t *result = esp_afe->fetch(afe_proc->afe_data);
        for (uint32_t i = 0; i < MAX_AFE_RESULT_PROC; i++) {
            if (afe_proc->result_proc[i].proc != NULL) {
                afe_proc->result_proc[i].proc(result, afe_proc->result_proc[i].result_ctx);
            }
        }
    }
    vTaskDelete(NULL);
}

esp_err_t afe_proc_result_cb_register(afe_proc_handle_t handle, afe_proc_result_proc_t proc, void *ctx)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "proc reg error handle");
    for (uint32_t i = 0; i < MAX_AFE_RESULT_PROC; i++) {
        if (handle->result_proc[i].proc == NULL) {
            handle->result_proc[i].proc = proc;
            handle->result_proc[i].result_ctx = ctx;
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t afe_proc_result_cb_unregister(afe_proc_handle_t handle, afe_proc_result_proc_t proc)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "proc reg error handle");
    for (uint32_t i = 0; i < MAX_AFE_RESULT_PROC; i++) {
        if (handle->result_proc[i].proc == proc) {
            handle->result_proc[i].proc = NULL;
            handle->result_proc[i].result_ctx = NULL;
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t afe_proc_destroy(afe_proc_handle_t handle)
{
    return ESP_OK;
}

esp_err_t afe_proc_create(afe_proc_cfg_t *cfg, afe_proc_handle_t *handle)
{
    esp_err_t ret = ESP_OK;

    afe_proc_t *afe_proc = heap_caps_calloc_prefer(1, sizeof(afe_proc_t), 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_INTERNAL);
    ESP_GOTO_ON_FALSE(afe_proc, ESP_ERR_NO_MEM, __err, TAG, "afe_proc_create no memory");
    afe_proc->read_cb = cfg->read_cb;
    afe_proc->read_ctx = cfg->read_ctx;
    afe_proc->models = cfg->models;

    int need_models = (cfg->afe_cfg->wakenet_init || cfg->afe_cfg->afe_ns_mode);
    if (need_models) {
        ESP_GOTO_ON_FALSE(afe_proc->models, ESP_ERR_NOT_FOUND, __err, TAG, "models needed but not found");
    }

    if (cfg->afe_cfg->wakenet_init) {
        char *wn_name = NULL;
        char *wn_name_2 = NULL;
        if (cfg->wn_wakeword == NULL) {
            for (int i = 0; i < afe_proc->models->num; i++) {
                if (strstr(afe_proc->models->model_name[i], ESP_WN_PREFIX) != NULL) {
                    if (wn_name == NULL) {
                        wn_name = afe_proc->models->model_name[i];
                        ESP_LOGI(TAG, "The first wakenet model: %s\n", wn_name);
                    } else if (wn_name_2 == NULL) {
                        wn_name_2 = afe_proc->models->model_name[i];
                        ESP_LOGI(TAG, "The second wakenet model: %s\n", wn_name_2);
                    }
                }
            }
            ESP_GOTO_ON_FALSE(wn_name || wn_name_2, ESP_ERR_NOT_FOUND, __err, TAG, "No wakenet found");
        } else {
            wn_name = esp_srmodel_filter(afe_proc->models, ESP_WN_PREFIX, cfg->wn_wakeword);
            ESP_GOTO_ON_FALSE(wn_name, ESP_ERR_NOT_FOUND, __err, TAG,
                              "Please enable wakenet model and select wake word (%s) by menuconfig!", cfg->wn_wakeword);
        }
        cfg->afe_cfg->wakenet_model_name = wn_name;
        cfg->afe_cfg->wakenet_model_name_2 = wn_name_2;
    }

    if (cfg->afe_cfg->afe_ns_mode) {
        char *ns_name = esp_srmodel_filter(afe_proc->models, ESP_NSNET_PREFIX, NULL);
        ESP_GOTO_ON_FALSE(ns_name, ESP_ERR_NOT_FOUND, __err, TAG, "No nsnet found");
        cfg->afe_cfg->afe_ns_model_name = ns_name;
    }

    afe_proc->afe_data = esp_afe->create_from_config(cfg->afe_cfg);
    ESP_GOTO_ON_FALSE(afe_proc->afe_data, ESP_ERR_NOT_FINISHED, __err, TAG, "AFE create failed");

    afe_proc->ctrl_events = xEventGroupCreate();
    ESP_GOTO_ON_FALSE(afe_proc->ctrl_events, ESP_ERR_NOT_FINISHED, __err, TAG, "AFE create events failed");
    if (afe_proc->read_cb) {
        xEventGroupSetBits(afe_proc->ctrl_events, AFE_RUN_EVENT);
    }

#if (configSUPPORT_STATIC_ALLOCATION == 1)
    prvTaskCreateDynamicPinnedToCoreWithCaps(feed_task,
                                             "feed",
                                             cfg->feed_stack,
                                             afe_proc,
                                             cfg->feed_prio,
                                             cfg->feed_core,
                                             (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                                             &afe_proc->feed.task);
    ESP_GOTO_ON_FALSE(afe_proc->feed.task, ESP_ERR_NO_MEM, __err, TAG, "create feed task failed");

    prvTaskCreateDynamicPinnedToCoreWithCaps(fetch_task,
                                             "fetch",
                                             cfg->fetch_stack,
                                             afe_proc,
                                             cfg->fetch_prio,
                                             cfg->fetch_core,
                                             (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                                             &afe_proc->fetch.task);
    ESP_GOTO_ON_FALSE(afe_proc->fetch.task, ESP_ERR_NO_MEM, __err, TAG, "create fetch task failed");
#else
    xTaskCreatePinnedToCore(feed_task, "feed", cfg->feed_stack, afe, cfg->feed_prio, &afe_proc->feed.task, cfg->feed_core);
    ESP_GOTO_ON_FALSE(afe_proc->feed.task, ESP_ERR_NO_MEM, __err, TAG, "create feed task failed");

    xTaskCreatePinnedToCore(fetch_task, "fetch", cfg->fetch_stack, afe, cfg->fetch_prio, &afe_proc->fetch.task, cfg->fetch_core);
    ESP_GOTO_ON_FALSE(afe_proc->fetch.task, ESP_ERR_NO_MEM, __err, TAG, "create fetch task failed");
#endif

    afe_proc->state.wakeup_enable = cfg->afe_cfg->wakenet_init;
    afe_proc->state.aec_enable = cfg->afe_cfg->aec_init;
    afe_proc->state.se_enable = cfg->afe_cfg->se_init;
    afe_proc->state.vad_enable = (cfg->afe_cfg->vad_mode != 0 ? true : false);
    afe_proc->state.vc_enable = cfg->afe_cfg->voice_communication_init;

    *handle = afe_proc;
    return ret;
__err:
    afe_proc_destroy(afe_proc);
    return ret;
}

esp_err_t afe_proc_get_state(afe_proc_handle_t handle, afe_proc_state_t *st)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "afe_proc_get_state invalid handle");
    memcpy(st, &(handle->state), sizeof(afe_proc_state_t));
    return ESP_OK;
}

esp_err_t afe_proc_suspend(afe_proc_handle_t handle, bool suspend)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "AFE suspend: handle NULL");
    ESP_LOGI(TAG, "afe_proc_suspend %d", suspend);
    if (suspend) {
        xEventGroupClearBits(handle->ctrl_events, AFE_RUN_EVENT);
    } else {
        xEventGroupSetBits(handle->ctrl_events, AFE_RUN_EVENT);
    }

    return ESP_OK;
}

esp_err_t afe_proc_func_ctrl(afe_proc_handle_t handle, uint32_t func, bool enable)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "AFE suspend: handle NULL");
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG, "AFE Ctrl [%lu, %d]", func, enable);
    switch (func) {
        case AI_FUNC_WAKENET: {
            if (enable) {
                ret = esp_afe->enable_wakenet(handle->afe_data);
            } else {
                ret = esp_afe->disable_wakenet(handle->afe_data);
            }
            ESP_LOGI(TAG, "wakenet ctrl ret %d", ret);
            if (ret >= 0) {
                handle->state.wakeup_enable = ret;
            }
            break;
        }
        case AI_FUNC_AEC: {
            if (enable) {
                ret = esp_afe->enable_aec(handle->afe_data);
            } else {
                ret = esp_afe->disable_aec(handle->afe_data);
            }
            ESP_LOGI(TAG, "aec ctrl ret %d", ret);
            if (ret >= 0) {
                handle->state.aec_enable = ret;
            }
            break;
        }
        case AI_FUNC_SE: {
            if (enable) {
                ret = esp_afe->enable_se(handle->afe_data);
            } else {
                ret = esp_afe->disable_se(handle->afe_data);
            }
            ESP_LOGI(TAG, "se ctrl ret %d", ret);
            if (ret >= 0) {
                handle->state.se_enable = ret;
            }
            break;
        }
        default:
            ESP_LOGW(TAG, "afe func ctrl: %lu not suppport", func);
            break;
    }

    return ret;
}

esp_err_t afe_proc_set_read_cb(afe_proc_handle_t handle, ai_audio_data_read_cb_t read_cb, void *read_ctx)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "AFE set read: handle NULL");
    afe_proc_suspend(handle, true);
    handle->read_cb = read_cb;
    handle->read_ctx = read_ctx;
    if (handle->read_cb) {
        afe_proc_suspend(handle, false);
    }
    return ESP_OK;
}

esp_err_t afe_proc_get_chunk_size(afe_proc_handle_t handle, size_t *size)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "AFE get feed size: handle NULL");
    int chan_num = esp_afe->get_total_channel_num(handle->afe_data);
    int chunksize = esp_afe->get_feed_chunksize(handle->afe_data);
    *size = chunksize * chan_num * sizeof(uint16_t);
    return ESP_OK;
}
