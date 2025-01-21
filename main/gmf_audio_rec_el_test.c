/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2025 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
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
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/sdmmc_host.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_io_http.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_setup_pool.h"
#include "esp_gmf_setup_peripheral.h"
#include "esp_gmf_audio_helper.h"
#include "esp_audio_simple_player.h"
#include "esp_audio_simple_player_advance.h"
#include "esp_codec_dev.h"
#include "esp_gmf_app_sys.h"
#include "esp_gmf_fifo.h"
#include "tcp_server.h"
#include "esp_gmf_data_bus.h"

#ifdef MEDIA_LIB_MEM_TEST
#include "media_lib_adapter.h"
#include "media_lib_mem_trace.h"
#endif /* MEDIA_LIB_MEM_TEST */

#include "esp_opus_enc.h"
#include "main.h"

static const char *TAG = "gmf";

esp_gmf_fifo_handle_t player_fifo = NULL;
esp_gmf_fifo_handle_t record_fifo = NULL;

esp_codec_dev_handle_t play_dev = NULL;
esp_codec_dev_handle_t record_dev = NULL;

static esp_err_t _pipeline_rec_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    // The warning messages are used to make the content more noticeable.
    ESP_LOGW(TAG, "RECV el:%s-%p, type:%x, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    return 0;
}

int record_read_opus_data(uint8_t *data, int data_size)
{
    esp_gmf_data_bus_block_t blk = {0};

    // ESP_LOGI(TAG, "%s, esp_gmf_fifo_acquire_read:%d, ctx:%p", __func__, data_size, ctx);
    int ret = esp_gmf_fifo_acquire_read(record_fifo, &blk, data_size, portMAX_DELAY);
    memcpy(data, blk.buf, data_size);

    esp_gmf_fifo_release_read(record_fifo, &blk, 0);
    return ret;
}

void record_send_opus_data(uint8_t *data, int len)
{
    esp_gmf_data_bus_block_t blk = {0};

    esp_gmf_fifo_acquire_write(record_fifo, &blk, len, portMAX_DELAY);
    memcpy((void *)blk.buf, (void *)data, len);
    blk.valid_size = len;
    if (len == 0) {
        blk.is_last = true;
    }
    // ESP_LOGI(TAG, "Record write, size:%d", len);
    esp_gmf_fifo_release_write(record_fifo, &blk, portMAX_DELAY);
}

static int file_acquire_write(void *handle, esp_gmf_data_bus_block_t *blk, uint32_t wanted_size, int block_ticks)
{
    if (blk->buf) {
        return wanted_size;
    }
    // ESP_LOGI(TAG, "%s-%d, size:%d, blk:%p", __func__, __LINE__, (int)wanted_size, blk);
    return wanted_size;
}

static int file_release_write(void *handle, esp_gmf_data_bus_block_t *blk, int block_ticks)
{
    int ret = 0;
    if (blk->valid_size) {
        // tcp_server_send(blk->buf, blk->valid_size);
        record_send_opus_data(blk->buf, blk->valid_size);
        ret = blk->valid_size;
    }
    return ret;
}

void player_send_opus_data(uint8_t *data, int len)
{
    esp_gmf_data_bus_block_t blk = {0};

    esp_gmf_fifo_acquire_write(player_fifo, &blk, len, portMAX_DELAY);
    memcpy((void *)blk.buf, (void *)data, len);
    blk.valid_size = len;
    if (len == 0) {
        blk.is_last = true;
    }
    // ESP_LOGI(TAG, "play write, size:%d", len);
    // tcp_server_send(data, len);
    esp_gmf_fifo_release_write(player_fifo, &blk, portMAX_DELAY);
}

static int player_in_data_cb(uint8_t *data, int data_size, void *ctx)
{
    esp_gmf_fifo_handle_t fifo = (esp_gmf_fifo_handle_t)ctx;
    esp_gmf_data_bus_block_t blk = {0};

    // ESP_LOGI(TAG, "%s, esp_gmf_fifo_acquire_read:%d, ctx:%p", __func__, data_size, ctx);
    int ret = esp_gmf_fifo_acquire_read(fifo, &blk, data_size, portMAX_DELAY);

    // ESP_LOGI(TAG, "fetch, want size:%d(%d)", data_size, ret);
    memcpy(data, blk.buf, data_size);

    esp_gmf_fifo_release_read(fifo, &blk, 0);

    return ret;
}

void player_out(uint8_t *data, int data_size)
{
    esp_codec_dev_write(play_dev, data, data_size);
}

int record_in(uint8_t *data, int data_size)
{
    return esp_codec_dev_read(record_dev, data, data_size);
}

static int player_out_data_cb(uint8_t *data, int data_size, void *ctx)
{
    esp_codec_dev_handle_t dev = (esp_codec_dev_handle_t)ctx;
    esp_codec_dev_write(dev, data, data_size);
    // tcp_server_send(data, data_size);
    return data_size;
}

static int mock_event_callback(esp_asp_event_pkt_t *event, void *ctx)
{
    if (event->type == ESP_ASP_EVENT_TYPE_MUSIC_INFO) {
        esp_asp_music_info_t info = {0};
        memcpy(&info, event->payload, event->payload_size);
        ESP_LOGW(TAG, "Get info, rate:%d, channels:%d, bits:%d", info.sample_rate, info.channels, info.bits);
    } else if (event->type == ESP_ASP_EVENT_TYPE_STATE) {
        esp_asp_state_t st = 0;
        memcpy(&st, event->payload, event->payload_size);
        ESP_LOGW(TAG, "Get State, %d,%s", st, esp_audio_simple_player_state_to_str(st));
        if (ctx && ((st == ESP_ASP_STATE_STOPPED) || (st == ESP_ASP_STATE_FINISHED) || (st == ESP_ASP_STATE_ERROR))) {
            xSemaphoreGive((SemaphoreHandle_t)ctx);
        }
    }
    return 0;
}

static void create_record_pipeline(esp_gmf_pipeline_handle_t *pipe_rec, esp_gmf_pool_handle_t pool)
{
    const char *uri_rec = "/sdcard/esp_gmf_rec1.opus";

    esp_gmf_pool_new_pipeline(pool, "codec_dev_rx", (const char *[]) {"encoder"}, 1, NULL, pipe_rec);
    assert(*pipe_rec);

    int out_file = 0;
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(file_acquire_write, file_release_write, NULL, &out_file, 0, ESP_GMF_MAX_DELAY);
    esp_gmf_pipeline_reg_el_port(*pipe_rec, "encoder", ESP_GMF_IO_DIR_WRITER, out_port);

    esp_gmf_task_cfg_t cfg_rec = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg_rec.ctx = NULL;
    cfg_rec.cb = NULL;
    cfg_rec.thread.stack = 30 * 1024;
    cfg_rec.thread.core = 0;
    esp_gmf_task_handle_t work_rec_task = NULL;
    esp_gmf_task_init(&cfg_rec, &work_rec_task);
    assert(work_rec_task);

    esp_gmf_pipeline_bind_task(*pipe_rec, work_rec_task);
    esp_gmf_pipeline_loading_jobs(*pipe_rec);
    esp_gmf_pipeline_set_event(*pipe_rec, _pipeline_rec_event, NULL);

    esp_gmf_element_handle_t enc_handle = NULL;
    esp_gmf_pipeline_set_out_uri(*pipe_rec, uri_rec);
    esp_gmf_pipeline_get_el_by_name(*pipe_rec, "encoder", &enc_handle);

    esp_audio_type_t audio_type = 0;
    esp_gmf_audio_helper_get_audio_type_by_uri(uri_rec, &audio_type);

    esp_gmf_info_sound_t info = {
        .sample_rates = 16000,
        .channels = 1,
        .bits = 16,
    };
    esp_gmf_audio_helper_reconfig_enc_by_type(audio_type, &info, (esp_audio_enc_config_t *)OBJ_GET_CFG(enc_handle));

    esp_audio_enc_config_t *cfg = (esp_audio_enc_config_t *)OBJ_GET_CFG(enc_handle);
    esp_opus_enc_config_t *opus_enc_cfg = cfg->cfg;
    ESP_LOGI(TAG, "## sample_rates:%d", cfg->type);
    ESP_LOGI(TAG, "## channels:%d", opus_enc_cfg->channel);
    ESP_LOGI(TAG, "## bitrate:%d", opus_enc_cfg->bitrate);
    opus_enc_cfg->bitrate = 10000 * 3;

    esp_gmf_pipeline_report_info(*pipe_rec, ESP_GMF_INFO_SOUND, &info, sizeof(info));
}

void start_gmf_task(void)
{
    ESP_LOGI(TAG, "start_gmf_task");

    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);

    esp_gmf_setup_periph_i2c(0);
    esp_gmf_setup_periph_aud_info aud_info_play = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .port_num = 0,
    };

    esp_gmf_setup_periph_aud_info aud_info_record = {
        .sample_rate = 16000,
        .channel = 1,
        .bits_per_sample = 16,
        .port_num = 0,
    };

    esp_gmf_setup_periph_codec(&aud_info_play, &aud_info_record, &play_dev, &record_dev);
    assert(play_dev && record_dev);

    esp_codec_dev_set_out_vol(play_dev, 70.0);

    /**
     * Create player pipeline
     */
    esp_gmf_fifo_create(10, 1, &player_fifo);
    ESP_LOGI(TAG, "player_fifo:%p", player_fifo);

    esp_asp_cfg_t cfg = {
        .in.cb = player_in_data_cb,
        .in.user_ctx = player_fifo,
        .out.cb = player_out_data_cb,
        .out.user_ctx = play_dev,
        .task_prio = 5,
        .task_stack = 40 * 1024,
        .task_core = 0,
    };
    esp_asp_handle_t handle = NULL;
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &handle);
    err = esp_audio_simple_player_set_event(handle, mock_event_callback, NULL);

    /**
     * Create record pipeline
     */
    esp_gmf_fifo_create(10, 1, &record_fifo);
    ESP_LOGI(TAG, "record_fifo:%p", record_fifo);

    esp_gmf_pipeline_handle_t pipe_rec = NULL;
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    assert(pool);

    pool_register_audio_codecs(pool);
    pool_register_audio_effects(pool);
    pool_register_io(pool);
    pool_register_codec_dev_io(pool, play_dev, record_dev);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

#if (1 ==USE_GMF)
    create_record_pipeline(&pipe_rec, pool);
#endif
    /**
     * Run player
     */
    const char *uri = "raw://sdcard/test.opus";
    err = esp_audio_simple_player_run(handle, uri);

#if (1 ==USE_GMF)
    esp_gmf_pipeline_run(pipe_rec);
#endif
}
