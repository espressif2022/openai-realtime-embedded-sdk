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

#include "esp_log.h"
#include "esp_gmf_pool.h"
#include "esp_audio_simple_player_private.h"

static const char *TAG = "ASP_ADVANCE";

esp_gmf_err_t esp_audio_simple_player_register_io(esp_asp_handle_t handle, esp_gmf_io_handle_t io)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    int ret = esp_gmf_pool_register_io(player->pool, io, NULL);
    return ret;
}

esp_gmf_err_t esp_audio_simple_player_register_el(esp_asp_handle_t handle, esp_gmf_element_handle_t element)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    int ret = esp_gmf_pool_register_element(player->pool, element, NULL);
    return ret;
}

esp_gmf_err_t esp_audio_simple_player_set_pipeline(esp_asp_handle_t handle, const char *in_name,
                                                     const char *el_name[], int num_of_el_name, const char *out_name)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    int ret = esp_gmf_pool_new_pipeline(player->pool, in_name, el_name, num_of_el_name, out_name, &player->pipe);
    return ret;
}
