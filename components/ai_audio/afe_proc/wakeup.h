#pragma once

#include "esp_err.h"

#include "afe_proc.h"
#include "ai_audio_common.h"

typedef struct __wakeup *wakeup_handle_t;

typedef struct {
    afe_proc_handle_t afe_handle; /**/
    ai_audio_event_cb_t event_cb; /**/
    void *user_data;              /**/
    int wakeup_time;              /*!< Unit:ms. The duration that the wakeup state remains when VAD is not triggered */
    int vad_start;                /*!< Unit:ms. Consecutive speech frame will be judged to vad start*/
    int vad_off;                  /*!< Unit:ms. When the silence time exceeds this value, it is determined as AUDIO_REC_VAD_END state */
    int wakeup_end;               /*!< Unit:ms. When the silence time after AUDIO_REC_VAD_END state exceeds this value, it is determined as AUDIO_REC_WAKEUP_END */
} wakeup_cfg_t;

esp_err_t wakeup_create(wakeup_cfg_t *cfg, wakeup_handle_t *handle);
esp_err_t wakeup_destroy(wakeup_handle_t handle);
