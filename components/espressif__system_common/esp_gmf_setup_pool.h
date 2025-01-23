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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief  Register codec device io to gmf element pool
 *
 * @param[in]  pool        Handle of gmf pool
 * @param[in]  play_dev    Handle of play codec device
 * @param[in]  record_dev  Handle of record codec device
 */
void pool_register_codec_dev_io(esp_gmf_pool_handle_t pool, void *play_dev, void *record_dev);

/**
 * @brief  Register IO to gmf element pool
 *
 * @param[in]  pool  Handle of gmf pool
 */
void pool_register_io(esp_gmf_pool_handle_t pool);

/**
 * @brief  Register audio codec element to gmf element pool
 *
 * @param[in]  pool  Handle of gmf pool
 */
void pool_register_audio_codecs(esp_gmf_pool_handle_t pool);

/**
 * @brief  Unregister audio codec
 */
void pool_unregister_audio_codecs(void);

/**
 * @brief  Register audio effect element to gmf element pool
 *
 * @param[in]  pool  Handle of gmf pool
 */
void pool_register_audio_effects(esp_gmf_pool_handle_t pool);

/**
 * @brief  Register image element to gmf element pool
 *
 * @param[in]  pool  Handle of gmf pool
 */
void pool_register_image(esp_gmf_pool_handle_t pool);

/**
 * @brief  Register all the gmf element to gmf element pool
 *
 * @param[in]  pool        Handle of gmf pool
 * @param[in]  play_dev    Handle of play codec device
 * @param[in]  record_dev  Handle of record codec device
 */
void pool_register_all(esp_gmf_pool_handle_t pool, void *play_dev, void *codec_dev);

#ifdef __cplusplus
}
#endif /* __cplusplus */
