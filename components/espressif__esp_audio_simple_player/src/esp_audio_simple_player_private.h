/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2025 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#pragma once

#include "esp_gmf_pool.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_audio_simple_player.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief Structure representing the audio simple player instance
 */
typedef struct {
    esp_gmf_pool_handle_t      pool;        /*!< Handle to the element pool used for ASP */
    esp_gmf_pipeline_handle_t  pipe;        /*!< Handle to the audio pipeline */
    esp_asp_state_t            state;       /*!< Current state of the player (running, paused, stopped, etc.) */
    void                      *work_task;   /*!< Pointer to the player's worker task */
    esp_asp_cfg_t              cfg;         /*!< Configuration parameters for the player */
    esp_asp_event_func         event_cb;    /*!< Callback function for player events */
    void                      *user_ctx;    /*!< User context passed to event callbacks */
    void                      *wait_event;  /*!< Event used for task synchronization */
} esp_audio_simple_player_t;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
