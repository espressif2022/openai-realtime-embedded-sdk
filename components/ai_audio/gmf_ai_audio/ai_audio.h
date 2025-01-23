#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_gmf_port.h"
#include "ai_audio_common.h"

typedef struct _ai_audio *ai_audio_handle_t;

enum {
    CH_CONTENT_IDLE = -1,
    CH_CONTENT_MIC,
    CH_CONTENT_REF
};

typedef struct {
    struct {
        uint32_t sample_rate;
        uint32_t bits;
        uint32_t ch_num;
        uint32_t ch_arrangement[MAX_INPUT_CH]; // { CH_CONTENT_MIC, CH_CONTENT_REF, CH_CONTENT_MIC, CH_CONTENT_IDLE }
    } src_info;

    char *partition;
    afe_config_t *afe_cfg; /*!< Configuration of AFE */
    int feed_core;         /*!< Core id of feed task */
    int feed_prio;         /*!< Priority of feed task*/
    int feed_stack;        /*!< Stack size of feed task */
    int fetch_core;        /*!< Core id of fetch task */
    int fetch_prio;        /*!< Priority of fetch task */
    int fetch_stack;       /*!< Stack size of fetch task */
    char *wn_wakeword;     /*!< Wake Word for WakeNet to load. This is useful when multiple Wake Words are selected in sdkconfig. Setting this to NULL will use the first found model. */

    bool wakeup_det;
    int wakeup_time; /*!< Unit:ms. The duration that the wakeup state remains when VAD is not triggered */
    int vad_start;   /*!< Unit:ms. Consecutive speech frame will be judged to vad start*/
    int vad_off;     /*!< Unit:ms. When the silence time exceeds this value, it is determined as AUDIO_REC_VAD_END state */
    int wakeup_end;  /*!< Unit:ms. When the silence time after AUDIO_REC_VAD_END state exceeds this value, it is determined as AUDIO_REC_WAKEUP_END */

    bool vocie_cmd_det;
    char *mn_language;

    esp_gmf_port_handle_t in_port;
    esp_gmf_port_handle_t out_port;
    ai_audio_event_cb_t event_cb;
    void *event_user_ctx;
} ai_audio_cfg_t;

#define DEFAULT_AI_AUDIO_CFG()                                  \
    {                                                           \
        .src_info = {                                           \
            .sample_rate = 48000, /* Default sample rate */     \
            .bits = 16,           /* Default bit depth */       \
            .ch_num = 4,                                        \
            .ch_arrangement = {                                 \
                CH_CONTENT_REF,                                 \
                CH_CONTENT_MIC,                                 \
                CH_CONTENT_IDLE,                                \
                CH_CONTENT_MIC,                                 \
            }                                                   \
        },                                                      \
        .partition = "model",                                   \
        .afe_cfg = NULL,                                        \
        .feed_core = 0,       /* Default core ID for feed */    \
        .feed_prio = 5,       /* Default feed task priority */  \
        .feed_stack = 2048,   /* Default feed stack size */     \
        .fetch_core = 0,      /* Default core ID for fetch */   \
        .fetch_prio = 5,      /* Default fetch task priority */ \
        .fetch_stack = 2048,  /* Default fetch stack size */    \
        .wn_wakeword = NULL,  /* Default to first wake word */  \
        .wakeup_det = false,                                    \
        .wakeup_time = 10000,  /* Default wakeup duration */    \
        .vad_start = 100,     /* Default VAD start time */      \
        .vad_off = 800,       /* Default silence duration */    \
        .wakeup_end = 2000,   /* Default wakeup end time */     \
        .vocie_cmd_det = false,                                 \
        .mn_language = "cn", /* Default language to NULL */     \
        .in_port = NULL,                                        \
        .event_cb = NULL,                                       \
        .event_user_ctx = NULL,                                 \
    }

esp_err_t ai_audio_create(ai_audio_cfg_t *cfg, ai_audio_handle_t *handle);
esp_err_t ai_audio_destroy(ai_audio_handle_t handle);
esp_err_t ai_audio_destroy(ai_audio_handle_t handle);
esp_err_t ai_audio_vcmd_det_begin(ai_audio_handle_t handle);
esp_err_t ai_audio_vcmd_det_cancel(ai_audio_handle_t handle);
esp_err_t ai_audio_func_ctrl(ai_audio_handle_t handle, uint32_t func, bool enable);
esp_err_t ai_audio_suspend(ai_audio_handle_t handle, bool suspend);
esp_err_t ai_audio_get_wakeup_handle(ai_audio_handle_t handle, void **wakeup);
esp_err_t ai_audio_get_vcmd_handle(ai_audio_handle_t handle, void **vcmd);