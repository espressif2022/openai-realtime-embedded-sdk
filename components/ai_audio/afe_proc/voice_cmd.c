#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"

#include "afe_proc.h"
#include "esp_check.h"
#include "esp_heap_caps.h"

#include "esp_err.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_process_sdkconfig.h"
#include "model_path.h"
#include "voice_cmd.h"

typedef struct __voice_cmd {
    model_iface_data_t *mn_handle;
    ai_audio_event_cb_t event_cb;
    void *user_data;
    bool detecting;
    SemaphoreHandle_t lock;
} vcmd_t;

static const char *TAG = "VCMD";
static const esp_mn_iface_t *multinet = NULL;

static void state_2_user(vcmd_t *vcmd, int event, void *event_data, size_t dlen)
{
    ai_audio_evt_t ai_event = {
        .type = event,
        .event_data = event_data,
        .data_len = dlen,
    };
    vcmd->event_cb(&ai_event, vcmd->user_data);
}

esp_err_t vcmd_det_begin(vcmd_handle_t handle)
{
    xSemaphoreTake(handle->lock, portMAX_DELAY);
    handle->detecting = true;
    xSemaphoreGive(handle->lock);
    return ESP_OK;
}

esp_err_t vcmd_det_cancel(vcmd_handle_t handle)
{
    xSemaphoreTake(handle->lock, portMAX_DELAY);
    handle->detecting = false;
    multinet->clean(handle->mn_handle);
    xSemaphoreGive(handle->lock);
    return ESP_OK;
}

static void afe_montor(afe_fetch_result_t *result, void *user_ctx)
{
    vcmd_t *vcmd = user_ctx;

    if (vcmd->detecting) {
        esp_mn_state_t mn_state = multinet->detect(vcmd->mn_handle, result->data);
        switch (mn_state) {
            case ESP_MN_STATE_DETECTED: {
                esp_mn_results_t *mn_result = multinet->get_results(vcmd->mn_handle);
                vcmd_info_t vcmd_info = {
                    .phrase_id = mn_result->phrase_id[0],
                    .prob = mn_result->prob[0]
                };
                memcpy(vcmd_info.str, mn_result->string, VCMD_MAX_LEN);
                state_2_user(vcmd, vcmd_info.phrase_id, &vcmd_info, sizeof(vcmd_info_t));

                break;
            }
            case ESP_MN_STATE_TIMEOUT: {
                xSemaphoreTake(vcmd->lock, portMAX_DELAY);
                vcmd->detecting = false;
                xSemaphoreGive(vcmd->lock);
                state_2_user(vcmd, VCMD_DECT_TIMEOUT, NULL, 0);
                break;
            }
            default:
                break;
        }
    }
}

esp_err_t vcmd_create(vcmd_cfg_t *cfg, vcmd_handle_t *handle)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "Voice cmd create: cfg NULL");
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Voice cmd create: handle NULL");

    char *mn_name = esp_srmodel_filter(cfg->models, ESP_MN_PREFIX, cfg->mn_language);
    ESP_RETURN_ON_FALSE(mn_name, ESP_ERR_NOT_FOUND, TAG, "Voice cmd create: MN not found");
    multinet = esp_mn_handle_from_name(mn_name);

    vcmd_t *vcmd = heap_caps_calloc_prefer(1, sizeof(vcmd_t), 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_INTERNAL);
    ESP_GOTO_ON_FALSE(vcmd, ESP_ERR_NO_MEM, __err, TAG, "Vocice cmd calloc failed");
    vcmd->event_cb = cfg->event_cb;
    vcmd->user_data = cfg->user_data;
    vcmd->mn_handle = multinet->create(mn_name, 5760);
    ESP_GOTO_ON_FALSE(vcmd->mn_handle, ESP_FAIL, __err, TAG, "MN handle create failed");
    vcmd->lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(vcmd->lock, ESP_ERR_NO_MEM, TAG, "Voice cmd create: lock create failed");

    esp_mn_commands_update_from_sdkconfig((esp_mn_iface_t *)multinet, vcmd->mn_handle);
    afe_proc_result_cb_register(cfg->afe_handle, afe_montor, vcmd);
    *handle = vcmd;
    return ret;
__err:
    vcmd_destroy(vcmd);
    return ret;
}

esp_err_t vcmd_destroy(vcmd_handle_t handle)
{
    return ESP_OK;
}