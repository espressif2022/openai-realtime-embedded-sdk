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

#include "esp_audio_simple_player.h"
#include "esp_gmf_io.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Register an IO handle with the audio simple player
 *
 * @note   It is called after `esp_audio_simple_player_new` and before `esp_audio_simple_player_run`
 *         The registered IO handle is destroyed only by `esp_audio_simple_player_destroy`
 *
 * @param[in]  handle  The handle to the audio simple player instance
 * @param[in]  io      The IO handle to register
 *
 * @return
 *       - ESP_GMF_OK               Successfully registered the IO handle
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument(s), such as a NULL handle or io
 *       - ESP_GMF_ERR_FAIL         Failed to register the IO handle due to other internal errors
 */
esp_gmf_err_t esp_audio_simple_player_register_io(esp_asp_handle_t handle, esp_gmf_io_handle_t io);

/**
 * @brief  Register an element handle with the audio simple player
 *
 * @note
 *      - It is called after `esp_audio_simple_player_new` and before `esp_audio_simple_player_run`
 *      - The registered element handle is destroyed only by `esp_audio_simple_player_destroy`
 *
 * @param[in]  handle   The handle to the audio simple player instance
 * @param[in]  element  The element handle to register
 *
 * @return
 *       - ESP_GMF_OK               Successfully registered the element handle
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument(s), such as a NULL handle or element
 *       - ESP_GMF_ERR_FAIL         Failed to register the element handle due to other internal errors
 */
esp_gmf_err_t esp_audio_simple_player_register_el(esp_asp_handle_t handle, esp_gmf_element_handle_t element);

/**
 * @brief  Sets up the pipeline using the input name and element names for the ESP Audio Simple Player to function
 *
 * @note
 *     - This function must be called after initializing the ESP Audio Simple Player but before `esp_audio_simple_player_run`
 *     - Ensure that all specified element names (`in_name`, `el_name`) correspond to valid and properly registered components in the audio pipeline
 *     - After this API is called, the esp_audio_simple_player_run only runs the pipeline; it no longer sets up the pipeline using the URI
 *     - The sets pipline handle is destroyed only by `esp_audio_simple_player_destroy`
 *
 * @param  handle          The handle to the ESP Audio Simple Player instance
 * @param  in_name         The name of the input element in the pipeline
 * @param  el_name[]       An array of names representing the intermediate elements
 *                         in the pipeline (e.g., audio processors, effects)
 * @param  num_of_el_name  The number of intermediate elements specified in the `el_name` array
 *
 * @return
 *       - ESP_GMF_OK               Successfully registered the element handle
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument(s), such as a NULL handle or element
 *       - ESP_GMF_ERR_FAIL         Failed to register the element handle due to other internal errors
 */
esp_gmf_err_t esp_audio_simple_player_set_pipeline(esp_asp_handle_t handle, const char *in_name, const char *el_name[], int num_of_el_name);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
