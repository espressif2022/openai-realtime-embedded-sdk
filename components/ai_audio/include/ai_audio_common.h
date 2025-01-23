#pragma once

#include "esp_types.h"
#include <stdint.h>

#define MAX_INPUT_CH (4)
#define VCMD_MAX_LEN (256)

enum {
    AI_FUNC_WAKENET,
    AI_FUNC_VAD,
    AI_FUNC_AEC,
    AI_FUNC_SE,
};

/**
 * @brief Information when wakeup state detected, event data for "WAKEUP_START"
 */
typedef struct {
    float data_volume;                /*!< Volume of input audio, the unit is decibel(dB) */
    int wake_word_index;              /*!< Wake word index which start from 1 */
    int wakenet_model_index;          /*!< Wakenets index which start from 1 */
} wakeup_info_t;

/**
 * @brief Information when voice command detected, event data for `VCMD_DECTECTED`
 */
typedef struct {
    int phrase_id;          /*!< Phrase ID */
    float prob;             /*!< probability */
    char str[VCMD_MAX_LEN]; /*!< Command string */
} vcmd_info_t;

typedef struct {

    /**
    * @brief AI audio event type
    */
    enum {
        WAKEUP_START = -100, /*!< Wakeup start */
        WAKEUP_END,          /*!< Wakeup stop */
        VAD_START,           /*!< Vad start */
        VAD_END,             /*!< Vad stop */
        VCMD_DECT_TIMEOUT,
        VCMD_DECTECTED = 0   /*!< Form 0 is the id of the voice commands detected by Multinet*/
        /* DO NOT add items below this line */
    } type;                            /*!< Event type */
    void *event_data;                  /*!< Event data */
    size_t data_len;                   /*!< Length of event data */
} ai_audio_evt_t;

typedef void (*ai_audio_event_cb_t)(ai_audio_evt_t *event, void *user_data);
typedef int32_t (*ai_audio_data_read_cb_t)(void *buffer, int buf_sz, void *user_ctx, uint32_t ticks);
