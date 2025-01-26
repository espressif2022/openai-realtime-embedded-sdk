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

#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_gmf_new_databus.h"
#include "esp_log.h"
#include "esp_gmf_obj.h"
#include "esp_gmf_err.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_element.h"

#include "esp_audio_simple_player_private.h"
#include "esp_audio_simple_dec.h"
#include "esp_gmf_uri_parser.h"
#include "audio_simple_player_pool.h"
#include "esp_gmf_audio_helper.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec_default.h"

#define PIPELINE_STOPPED_BIT  BIT(0)
#define PIPELINE_FINISHED_BIT BIT(1)
#define PIPELINE_ERROR_BIT    BIT(2)

static const char *TAG = "AUD_SIMP_PLAYER";

// String representation of the states
const char *esp_asp_state_strings[] = {
    "ESP_AUD_SIMPLE_PLAYER_NONE",
    "ESP_AUD_SIMPLE_PLAYER_RUNNING",
    "ESP_AUD_SIMPLE_PLAYER_PAUSED",
    "ESP_AUD_SIMPLE_PLAYER_STOPPED",
    "ESP_AUD_SIMPLE_PLAYER_FINISHED",
    "ESP_AUD_SIMPLE_PLAYER_ERROR"};

static const char *el_names[] = {
    "aud_simp_dec",
#ifdef CONFIG_ESP_AUDIO_SIMPLE_PLAYER_RESAMPLE_EN
    "rate_cvt",
#endif  /* CONFIG_ESP_AUDIO_SIMPLE_PLAYER_RESAMPLE_EN */
#ifdef CONFIG_ESP_AUDIO_SIMPLE_PLAYER_CH_CVT_EN
    "ch_cvt",
#endif  /* CONFIG_ESP_AUDIO_SIMPLE_PLAYER_CH_CVT_EN */
#ifdef CONFIG_ESP_AUDIO_SIMPLE_PLAYER_BIT_CVT_EN
    "bit_cvt",
#endif  /* CONFIG_ESP_AUDIO_SIMPLE_PLAYER_BIT_CVT_EN */
};

static inline void _get_asp_st(esp_gmf_event_state_t in, esp_asp_state_t *out)
{
    if (in == ESP_GMF_EVENT_STATE_RUNNING) {
        *out = ESP_ASP_STATE_RUNNING;
    } else if (in == ESP_GMF_EVENT_STATE_PAUSED) {
        *out = ESP_ASP_STATE_PAUSED;
    } else if (in == ESP_GMF_EVENT_STATE_STOPPED) {
        *out = ESP_ASP_STATE_STOPPED;
    } else if (in == ESP_GMF_EVENT_STATE_FINISHED) {
        *out = ESP_ASP_STATE_FINISHED;
    } else if (in == ESP_GMF_EVENT_STATE_ERROR) {
        *out = ESP_ASP_STATE_ERROR;
    }
}

static esp_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGD(TAG, "CB: RECV Pipeline EVT: el:%s-%p, type:%x, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)ctx;
    esp_asp_event_pkt_t user_evt = {0};
    if (player->event_cb == NULL) {
        return ESP_GMF_ERR_OK;
    }
    if ((event->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) && (event->sub > ESP_GMF_EVENT_STATE_OPENING)) {
        _get_asp_st((esp_gmf_event_state_t)event->sub, &player->state);
        user_evt.type = ESP_ASP_EVENT_TYPE_STATE;
        user_evt.payload = &player->state;
        user_evt.payload_size = sizeof(player->state);
        player->event_cb(&user_evt, player->user_ctx);
        if (player->wait_event) {
            if (event->sub == ESP_GMF_EVENT_STATE_STOPPED) {
                xEventGroupSetBits((EventGroupHandle_t)player->wait_event, PIPELINE_STOPPED_BIT);
            } else if (event->sub == ESP_GMF_EVENT_STATE_FINISHED) {
                xEventGroupSetBits((EventGroupHandle_t)player->wait_event, PIPELINE_FINISHED_BIT);
            } else if (event->sub == ESP_GMF_EVENT_STATE_ERROR) {
                xEventGroupSetBits((EventGroupHandle_t)player->wait_event, PIPELINE_ERROR_BIT);
            }
        }
    } else if (event->type == ESP_GMF_EVT_TYPE_REPORT_INFO) {
        esp_gmf_info_sound_t esp_gmf_info = {0};
        memcpy(&esp_gmf_info, event->payload, event->payload_size);
        esp_asp_music_info_t info = {0};
        info.sample_rate = esp_gmf_info.sample_rates;
        info.bps = esp_gmf_info.bps;
        info.channels = esp_gmf_info.channels;
        info.bits = esp_gmf_info.bits;

        user_evt.type = ESP_ASP_EVENT_TYPE_MUSIC_INFO;
        user_evt.payload = &info;
        user_evt.payload_size = sizeof(info);
        player->event_cb(&user_evt, player->user_ctx);
    }
    return ESP_GMF_ERR_OK;
}

static int asp_func_acquire_read(void *handle, esp_gmf_data_bus_block_t *blk, uint32_t wanted_size, int block_ticks)
{
    if (blk->buf == NULL) {
        return ESP_FAIL;
    }
    esp_asp_func_t *func = (esp_asp_func_t *)handle;
    int ret = func->cb(blk->buf, wanted_size, func->user_ctx);
    blk->valid_size = ret;
    ESP_LOGD(TAG, "%s, vld:%d, blk:%p", __func__, blk->valid_size, blk);
    // if (ret != wanted_size) {
    //     ret = 0;
    if (ret == 0) {
        blk->is_last = true;
    }
    return ret;
}

static int asp_func_release_read(void *handle, esp_gmf_data_bus_block_t *blk, int block_ticks)
{
    blk->valid_size = 0;
    return ESP_OK;
}

static int asp_func_acquire_write(void *handle, esp_gmf_data_bus_block_t *blk, uint32_t wanted_size, int block_ticks)
{
    if (blk->buf) {
        return wanted_size;
    }
    return wanted_size;
}

static int asp_func_release_write(void *handle, esp_gmf_data_bus_block_t *blk, int block_ticks)
{
    int ret = 0;
    ESP_LOGD(TAG, "%s, vld:%d, blk:%p", __func__, blk->valid_size, blk);
    esp_asp_func_t *func = (esp_asp_func_t *)handle;
    if (blk->valid_size) {
        ret = func->cb(blk->buf, blk->valid_size, func->user_ctx);
    }
    return ret;
}

static int __setup_pipeline(esp_audio_simple_player_t *player, const char *uri)
{
    esp_gmf_uri_t *uri_st = NULL;
    int ret = 0;
    esp_gmf_uri_parse(uri, &uri_st);
    if ((uri_st == NULL) || (uri_st->path == NULL) || (uri_st->scheme == NULL)) {
        ESP_LOGE(TAG, "The URI is invalid, uri:%s", uri);
        return ESP_GMF_ERR_INVALID_URI;
    }
    char *in_str = uri_st->scheme;
    if (strcasecmp(in_str, "https") == 0) {
        in_str[strlen(in_str) - 1] = 0;
    }
    if (strncasecmp(uri_st->scheme, "raw", strlen("raw")) == 0) {
        if (player->cfg.in.cb == NULL) {
            ESP_LOGE(TAG, "No registered in raw callback, uri:%s", uri);
            esp_gmf_uri_free(uri_st);
            return ESP_GMF_ERR_NOT_SUPPORT;
        }
        in_str = NULL;
    }

    if (player->pipe == NULL) {
        esp_gmf_pool_new_pipeline(player->pool, in_str, el_names, sizeof(el_names) / sizeof(char *), NULL, &player->pipe);
        if (player->pipe == NULL) {
            ESP_LOGE(TAG, "Failed to create an new pipeline");
            esp_gmf_uri_free(uri_st);
            return ESP_GMF_ERR_FAIL;
        }
        if (in_str == NULL) {
            esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(asp_func_acquire_read, asp_func_release_read, NULL, &player->cfg.in, 1024, ESP_GMF_MAX_DELAY);
            ESP_GMF_CHECK(TAG, in_port, goto __setup_pipe_err, "Failed to create in port");
            ret = esp_gmf_pipeline_reg_el_port(player->pipe, OBJ_GET_TAG(player->pipe->head_el), ESP_GMF_IO_DIR_READER, in_port);
            ESP_GMF_RET_ON_ERROR(TAG, ret, goto __setup_pipe_err, "Failed to register in port for head element, ret:%x", ret);
        }
        esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(asp_func_acquire_write, asp_func_release_write, NULL, &player->cfg.out, 2048, ESP_GMF_MAX_DELAY);
        ESP_GMF_CHECK(TAG, out_port, goto __setup_pipe_err, "Failed to create out port");
        ret = esp_gmf_pipeline_reg_el_port(player->pipe, OBJ_GET_TAG(player->pipe->last_el), ESP_GMF_IO_DIR_WRITER, out_port);
        ESP_GMF_RET_ON_ERROR(TAG, ret, goto __setup_pipe_err, "Failed to register out port for tail element, ret:%x", ret);
    } else {
        esp_gmf_pipeline_reset(player->pipe);
        esp_gmf_io_handle_t out_io = NULL;
        esp_gmf_pipeline_get_in(player->pipe, &out_io);
        if (out_io == NULL) {
            esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(asp_func_acquire_write, asp_func_release_write, NULL, &player->cfg.out, 2048, ESP_GMF_MAX_DELAY);
            ESP_GMF_CHECK(TAG, out_port, goto __setup_pipe_err, "Failed to create out port on exist pipeline");
            ret = esp_gmf_pipeline_reg_el_port(player->pipe, OBJ_GET_TAG(player->pipe->last_el), ESP_GMF_IO_DIR_WRITER, out_port);
            ESP_GMF_RET_ON_ERROR(TAG, ret, goto __setup_pipe_err, "Failed to register out port for tail element, ret:%x", ret);
        }

        esp_gmf_io_handle_t in_io = NULL;
        esp_gmf_pipeline_get_in(player->pipe, &in_io);
        if ((in_io == NULL) || (strncasecmp(OBJ_GET_TAG(in_io), in_str, strlen(in_str)) != 0)) {
            esp_gmf_io_handle_t new_io = NULL;

            esp_gmf_pool_new_io(player->pool, in_str, ESP_GMF_IO_DIR_READER, &new_io);
            ESP_GMF_CHECK(TAG, new_io, goto __setup_pipe_err, "Failed to create IN IO instance");
            esp_gmf_pipeline_replace_in(player->pipe, new_io);
            if (in_io) {
                esp_gmf_obj_delete(in_io);
                esp_gmf_element_unregister_in_port(player->pipe->head_el, NULL);
            }
            esp_gmf_io_type_t io_type = 0;
            esp_gmf_io_get_type(new_io, &io_type);
            esp_gmf_port_handle_t in_port = NULL;
            if (io_type == ESP_GMF_IO_TYPE_BYTE) {
                in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_io_acquire_read, esp_gmf_io_release_read, NULL, new_io,
                                                   (ESP_GMF_ELEMENT_GET(player->pipe->head_el)->in_attr.size), ESP_GMF_MAX_DELAY);
            } else if (io_type == ESP_GMF_IO_TYPE_BLOCK) {
                in_port = NEW_ESP_GMF_PORT_IN_BLOCK(esp_gmf_io_acquire_read, esp_gmf_io_release_read, NULL, new_io,
                                                    (ESP_GMF_ELEMENT_GET(player->pipe->head_el)->in_attr.size), ESP_GMF_MAX_DELAY);
            } else {
                ESP_LOGE(TAG, "The IN type is incorrect,%d, [%p-%s]", io_type, new_io, OBJ_GET_TAG(new_io));
                ret = ESP_GMF_ERR_NOT_SUPPORT;
                goto __setup_pipe_err;
            }
            ESP_GMF_NULL_CHECK(TAG, in_port, {ret = ESP_GMF_ERR_MEMORY_LACK; goto __setup_pipe_err;});
            ret = esp_gmf_element_register_in_port((esp_gmf_element_handle_t)player->pipe->head_el, in_port);
            ESP_GMF_RET_ON_ERROR(TAG, ret, goto __setup_pipe_err, "Failed to register in port for head element, ret:%x", ret);
            ESP_LOGD(TAG, "TO link IN port, [%p-%s],new:%p", new_io, OBJ_GET_TAG(new_io), in_port);
        }
    }
    esp_gmf_pipeline_bind_task(player->pipe, player->work_task);
    esp_gmf_element_handle_t dec_el = NULL;
    ret = esp_gmf_pipeline_get_el_by_name(player->pipe, "aud_simp_dec", &dec_el);
    ESP_GMF_RET_ON_ERROR(TAG, ret, goto __setup_pipe_err, "There is no decoder in pipeline");
    ret = esp_gmf_audio_helper_reconfig_dec_by_uri((char *)uri_st->path, OBJ_GET_CFG(dec_el));
    ESP_GMF_RET_ON_ERROR(TAG, ret, goto __setup_pipe_err, "The audio format does not support, ret:%x, path:%p", ret, uri_st->path);
    ret = esp_gmf_pipeline_set_in_uri(player->pipe, uri);
    ESP_GMF_RET_ON_ERROR(TAG, ret, goto __setup_pipe_err, "Failed set URI for in stream, ret:%x", ret);
    ret = esp_gmf_pipeline_loading_jobs(player->pipe);
    ESP_GMF_RET_ON_ERROR(TAG, ret, goto __setup_pipe_err, "Failed loading jobs for pipeline, ret:%x", ret);

    esp_audio_simple_dec_cfg_t *cfg = (esp_audio_simple_dec_cfg_t *)OBJ_GET_CFG(dec_el);
    esp_opus_dec_cfg_t *opus_dec_cfg = cfg->dec_cfg;
    opus_dec_cfg->channel = 2;
    opus_dec_cfg->sample_rate = 16000;

__setup_pipe_err:
    esp_gmf_uri_free(uri_st);
    return ret;
}

esp_gmf_err_t esp_audio_simple_player_new(esp_asp_cfg_t *cfg, esp_asp_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, cfg, { return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, cfg->out.cb, { return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)esp_gmf_oal_calloc(1, sizeof(esp_audio_simple_player_t));
    ESP_GMF_CHECK(TAG, player, return ESP_GMF_ERR_MEMORY_LACK, "No memory to create a new simple player");
    esp_gmf_pool_init(&player->pool);
    if (player->pool == NULL) {
        ESP_LOGE(TAG, "Failed to create the ASP pool");
        esp_gmf_oal_free(player);
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();

    asp_pool_register_audio(player);
    asp_pool_register_io(player);
    memcpy(&player->cfg, cfg, sizeof(player->cfg));
    esp_gmf_task_cfg_t task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    task_cfg.ctx = NULL;
    task_cfg.cb = NULL;
    if (cfg->task_stack > 0) {
        task_cfg.thread.stack = cfg->task_stack;
    }
    if (cfg->task_prio > 0) {
        task_cfg.thread.prio = cfg->task_prio;
    }
    task_cfg.thread.core = cfg->task_core;
    esp_gmf_task_init(&task_cfg, &player->work_task);
    if (player->work_task == NULL) {
        ESP_LOGE(TAG, "Failed to create the pipeline task");
        esp_gmf_pool_deinit(player->pool);
        esp_gmf_oal_free(player);
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    esp_gmf_task_set_timeout(player->work_task, 3000);
    *handle = player;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_audio_simple_player_set_event(esp_asp_handle_t handle, const esp_asp_event_func event_cb, void *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    player->event_cb = event_cb;
    player->user_ctx = ctx;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_audio_simple_player_run(esp_asp_handle_t handle, const char *uri)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, uri, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    if ((player->state == ESP_ASP_STATE_RUNNING) || (player->state == ESP_ASP_STATE_PAUSED)) {
        ESP_LOGE(TAG, "The player still running, call stop first, st:%d", player->state);
        return ESP_GMF_ERR_INVALID_STATE;
    }
    int ret = __setup_pipeline(player, uri);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to setup pipeline, ret:%x", ret);
    player->state = ESP_ASP_STATE_NONE;
    esp_gmf_pipeline_set_event(player->pipe, _pipeline_event, player);
    ret = esp_gmf_pipeline_run(player->pipe);
    return ret;
}

esp_gmf_err_t esp_audio_simple_player_run_to_end(esp_asp_handle_t handle, const char *uri)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, uri, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    if ((player->state == ESP_ASP_STATE_RUNNING) || (player->state == ESP_ASP_STATE_PAUSED)) {
        ESP_LOGE(TAG, "The player still running, call stop first, st:%d", player->state);
        return ESP_GMF_ERR_INVALID_STATE;
    }
    if (player->wait_event == NULL) {
        player->wait_event = (void *)xEventGroupCreate();
        ESP_GMF_NULL_CHECK(TAG, player->wait_event, return ESP_GMF_ERR_MEMORY_LACK);
    }
    int ret = __setup_pipeline(player, uri);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to setup pipeline on sync play, ret:%x", ret);
    esp_gmf_pipeline_set_event(player->pipe, _pipeline_event, player);
    ret = esp_gmf_pipeline_run(player->pipe);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Run pipeline failed, ret: %x", ret);

    player->state = ESP_ASP_STATE_NONE;
    xEventGroupClearBits(player->wait_event, PIPELINE_ERROR_BIT | PIPELINE_STOPPED_BIT | PIPELINE_FINISHED_BIT);
    EventBits_t uxBits = xEventGroupWaitBits(player->wait_event, PIPELINE_ERROR_BIT | PIPELINE_STOPPED_BIT | PIPELINE_FINISHED_BIT,
                                             pdTRUE, pdFALSE, portMAX_DELAY);
    if (uxBits & PIPELINE_ERROR_BIT) {
        return ESP_GMF_ERR_FAIL;
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_audio_simple_player_stop(esp_asp_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    return esp_gmf_pipeline_stop(player->pipe);
}

esp_gmf_err_t esp_audio_simple_player_pause(esp_asp_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    return esp_gmf_pipeline_pause(player->pipe);
}

esp_gmf_err_t esp_audio_simple_player_resume(esp_asp_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    return esp_gmf_pipeline_resume(player->pipe);
}

esp_gmf_err_t esp_audio_simple_player_get_state(esp_asp_handle_t handle, esp_asp_state_t *state)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, state, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    *state = player->state;
    return ESP_GMF_ERR_OK;
}

const char *esp_audio_simple_player_state_to_str(esp_asp_state_t state)
{
    if (state >= ESP_ASP_STATE_NONE && state <= ESP_ASP_STATE_ERROR) {
        return esp_asp_state_strings[state];
    }
    return "UNKNOWN";  // Handle invalid state
}

esp_gmf_err_t esp_audio_simple_player_destroy(esp_asp_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    if ((player->state == ESP_ASP_STATE_RUNNING) || (player->state == ESP_ASP_STATE_PAUSED)) {
        ESP_LOGW(TAG, "The player still running, call stop first, st: % d", player->state);
        esp_audio_simple_player_stop(handle);
        xEventGroupClearBits(player->wait_event, PIPELINE_ERROR_BIT | PIPELINE_STOPPED_BIT | PIPELINE_FINISHED_BIT);
        xEventGroupWaitBits(player->wait_event, PIPELINE_ERROR_BIT | PIPELINE_STOPPED_BIT | PIPELINE_FINISHED_BIT,
                            pdTRUE, pdFALSE, portMAX_DELAY);
    }
    if (player->wait_event) {
        vEventGroupDelete(player->wait_event);
    }
    esp_audio_dec_unregister_default();
    esp_audio_simple_dec_unregister_default();
    esp_gmf_task_deinit(player->work_task);
    esp_gmf_pipeline_destroy(player->pipe);
    esp_gmf_pool_deinit(player->pool);
    esp_gmf_oal_free(player);

    return ESP_GMF_ERR_OK;
}