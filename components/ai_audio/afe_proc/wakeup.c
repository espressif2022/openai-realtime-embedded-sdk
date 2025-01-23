#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include "esp_afe_sr_iface.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "esp_check.h"
#include "esp_heap_caps.h"

#include "afe_proc.h"
#include "ai_audio_common.h"
#include "wakeup.h"

typedef enum {
    ST_IDLE,
    ST_WAKEUP,
    ST_WAIT_FOR_SPEECH,
    ST_SPEECHING,
    ST_WAIT_FOR_SILENCE,
    ST_WAIT_FOR_SLEEP,
} wakeup_state_t;

typedef enum {
    ET_NOISE_DECT,
    ET_SPEECH_DECT,
    ET_WWE_DECT,
    ET_WAKEUP_TIMER_EXPIRED,
    ET_VAD_TIMER_EXPIRED,
    ET_UNKNOWN
} wakeup_event_t;

typedef struct __wakeup {
    wakeup_cfg_t cfg;
    wakeup_state_t state;
    esp_timer_handle_t wakeup_timer;
    esp_timer_handle_t vad_timer;
    SemaphoreHandle_t lock;
} wakeup_t;

#define state_set(st) (wakeup->state = st)

static const char *TAG = "WAKEUP";
static char *state_str[] = {
    "ST_IDLE",
    "ST_WAKEUP",
    "ST_WAIT_FOR_SPEECH",
    "ST_SPEECHING",
    "ST_WAIT_FOR_SILENCE",
    "ST_WAIT_FOR_SLEEP",
};
static char *event_str[] = {
    "ET_NOISE_DECT",
    "ET_SPEECH_DECT",
    "ET_WWE_DECT",
    "ET_WAKEUP_TIMER_EXPIRED",
    "ET_VAD_TIMER_EXPIRED",
    "ET_UNKNOWN"
};

static void state_update(wakeup_handle_t wakeup, wakeup_event_t event, void *event_data, size_t len);

static wakeup_event_t result_2_event(afe_fetch_result_t *result)
{
    if (result->wakeup_state == WAKENET_DETECTED) {
        return ET_WWE_DECT;
    }
    if (result->vad_state == AFE_VAD_SILENCE) {
        return ET_NOISE_DECT;
    }
    if (result->vad_state == AFE_VAD_SPEECH) {
        return ET_SPEECH_DECT;
    }
    return ET_UNKNOWN;
}

static void result_2_wakeup_info(afe_fetch_result_t *result, wakeup_info_t *info)
{
    info->wake_word_index = result->wake_word_index;
    info->wakenet_model_index = result->wakenet_model_index;
    info->data_volume = result->data_volume;
}

static void state_reset(wakeup_t *wakeup)
{
    esp_timer_stop(wakeup->wakeup_timer);
    esp_timer_stop(wakeup->vad_timer);
    wakeup->state = ST_IDLE;
}

static void state_2_user(wakeup_t *wakeup, int event, void *event_data, size_t dlen)
{
    ai_audio_evt_t ai_event = {
        .type = event,
        .event_data = event_data,
        .data_len = dlen,
    };
    wakeup->cfg.event_cb(&ai_event, wakeup->cfg.user_data);
}

static void wakeup_timer_start(wakeup_t *wakeup)
{
    int timeout = 0;
    if (wakeup->state == ST_WAKEUP) {
        timeout = wakeup->cfg.wakeup_time;
    } else if (wakeup->state == ST_WAIT_FOR_SLEEP) {
        timeout = wakeup->cfg.wakeup_end;
    }
    esp_timer_stop(wakeup->wakeup_timer);
    if (timeout) {
        esp_timer_start_once(wakeup->wakeup_timer, timeout * 1000);
    }
}

static void wakeup_timer_expired(void *arg)
{
    wakeup_t *wakeup = arg;
    state_update(wakeup, ET_WAKEUP_TIMER_EXPIRED, NULL, 0);
}

static void vad_timer_start(wakeup_t *wakeup)
{
    int timeout = 0;
    if (wakeup->state == ST_WAIT_FOR_SPEECH) {
        timeout = wakeup->cfg.vad_start;
    } else if (wakeup->state == ST_WAIT_FOR_SILENCE) {
        timeout = wakeup->cfg.vad_off;
    }
    esp_timer_stop(wakeup->vad_timer);
    if (timeout) {
        esp_timer_start_once(wakeup->vad_timer, timeout * 1000);
    }
}

static void vad_timer_expired(void *arg)
{
    wakeup_t *wakeup = arg;
    state_update(wakeup, ET_VAD_TIMER_EXPIRED, NULL, 0);
}

static void state_update(wakeup_handle_t wakeup, wakeup_event_t event, void *event_data, size_t len)
{
    afe_proc_state_t afe_state = { 0 };
    static wakeup_event_t last_event = ET_UNKNOWN;
    if (last_event != event) {
        ESP_LOGV(TAG, "Recorder update state, cur %s, event %s", state_str[wakeup->state], event_str[event]);
        last_event = event;
    } else {
        return;
    }
    xSemaphoreTake(wakeup->lock, portMAX_DELAY);
    if (event == ET_WWE_DECT && wakeup->state != ST_IDLE) {
        state_reset(wakeup);
    }
    afe_proc_get_state(wakeup->cfg.afe_handle, &afe_state);
    switch (wakeup->state) {
        case ST_IDLE: {
            if (event == ET_WWE_DECT) {
                state_set(ST_WAKEUP);
                wakeup_timer_start(wakeup);
                state_2_user(wakeup, WAKEUP_START, event_data, len);
            } else if (event == ET_SPEECH_DECT && afe_state.wakeup_enable == false) {
                if (wakeup->cfg.vad_start) {
                    state_set(ST_WAIT_FOR_SPEECH);
                    vad_timer_start(wakeup);
                } else {
                    state_set(ST_SPEECHING);
                    state_2_user(wakeup, VAD_START, NULL, 0);
                }
            }
            break;
        }
        case ST_WAKEUP: {
            if (event == ET_SPEECH_DECT) {
                if (wakeup->cfg.vad_start) {
                    state_set(ST_WAIT_FOR_SPEECH);
                    vad_timer_start(wakeup);
                } else {
                    state_set(ST_SPEECHING);
                    state_2_user(wakeup, VAD_START, NULL, 0);
                }
            } else if (event == ET_WAKEUP_TIMER_EXPIRED) {
                state_set(ST_IDLE);
                esp_timer_stop(wakeup->wakeup_timer);
                state_2_user(wakeup, WAKEUP_END, NULL, 0);
            }
            break;
        }
        case ST_WAIT_FOR_SPEECH: {
            if (event == ET_NOISE_DECT) {
                state_set(ST_WAKEUP);
            } else if (event == ET_VAD_TIMER_EXPIRED) {
                state_set(ST_SPEECHING);
                esp_timer_stop(wakeup->wakeup_timer);
                state_2_user(wakeup, VAD_START, NULL, 0);
            } else if (event == ET_WAKEUP_TIMER_EXPIRED) {
                state_set(ST_IDLE);
                esp_timer_stop(wakeup->vad_timer);
                state_2_user(wakeup, WAKEUP_END, NULL, 0);
            }
            break;
        }
        case ST_SPEECHING: {
            if (event == ET_NOISE_DECT) {
                if (wakeup->cfg.vad_off) {
                    state_set(ST_WAIT_FOR_SILENCE);
                    vad_timer_start(wakeup);
                } else {
                    state_set(ST_WAIT_FOR_SLEEP);
                    wakeup_timer_start(wakeup);
                    state_2_user(wakeup, VAD_END, NULL, 0);
                }
            }
            break;
        }
        case ST_WAIT_FOR_SILENCE: {
            if (event == ET_SPEECH_DECT) {
                state_set(ST_SPEECHING);
                esp_timer_stop(wakeup->vad_timer);
            } else if (event == ET_VAD_TIMER_EXPIRED) {
                if (afe_state.wakeup_enable) {
                    state_set(ST_WAIT_FOR_SLEEP);
                    wakeup_timer_start(wakeup);
                } else {
                    state_set(ST_IDLE);
                }
                state_2_user(wakeup, VAD_END, NULL, 0);
            }
            break;
        }
        case ST_WAIT_FOR_SLEEP: {
            if (event == ET_SPEECH_DECT) {
                state_set(ST_WAIT_FOR_SPEECH);
                vad_timer_start(wakeup);
            } else if (event == ET_WAKEUP_TIMER_EXPIRED) {
                state_set(ST_IDLE);
                esp_timer_stop(wakeup->vad_timer);
                state_2_user(wakeup, WAKEUP_END, NULL, 0);
            }
            break;
        }
        default:
            break;
    }
    xSemaphoreGive(wakeup->lock);
}

static void afe_monitor(afe_fetch_result_t *result, void *user_ctx)
{
    wakeup_event_t event = result_2_event(result);
    if (event == ET_WWE_DECT) {
        wakeup_info_t info = { 0 };
        result_2_wakeup_info(result, &info);
        state_update(user_ctx, event, &info, sizeof(wakeup_info_t));
    } else {
        state_update(user_ctx, event, NULL, 0);
    }
}

esp_err_t wakeup_create(wakeup_cfg_t *cfg, wakeup_handle_t *handle)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "Wakeup create cfg NULL");
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Wakeup create handle NULL");

    wakeup_t *wakeup = heap_caps_calloc_prefer(1, sizeof(wakeup_t), 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(wakeup, ESP_ERR_NO_MEM, TAG, "Wakeup create calloc failed");
    memcpy(&wakeup->cfg, cfg, sizeof(wakeup_cfg_t));

    wakeup->lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(wakeup->lock, ESP_ERR_NO_MEM, TAG, "Wakeup lock create failed");

    esp_timer_create_args_t wakeup_timer_cfg = {
        .callback = wakeup_timer_expired,
        .arg = wakeup,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wakeup_timer",
    };
    ESP_GOTO_ON_ERROR(esp_timer_create(&wakeup_timer_cfg, &wakeup->wakeup_timer), __err, TAG, "Create wakeup timer failed");

    esp_timer_create_args_t vad_timer_cfg = {
        .callback = vad_timer_expired,
        .arg = wakeup,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "vad_timer",
    };
    ESP_GOTO_ON_ERROR(esp_timer_create(&vad_timer_cfg, &wakeup->vad_timer), __err, TAG, "Create vad timer failed");
    ESP_GOTO_ON_ERROR(afe_proc_result_cb_register(wakeup->cfg.afe_handle, afe_monitor, wakeup), __err, TAG, "Result process register failed");
    *handle = wakeup;
    return ret;
__err:
    wakeup_destroy(wakeup);
    return ret;
}

esp_err_t wakeup_destroy(wakeup_handle_t handle)
{
    return ESP_OK;
}
