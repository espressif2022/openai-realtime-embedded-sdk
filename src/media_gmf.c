#if (CONFIG_OPENAI_WITH_GMF == 1)

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_gmf_err.h"
#include "driver/sdmmc_host.h"
#include "esp_audio_simple_player.h"
#include "esp_audio_simple_player_advance.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_gmf_app_sys.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_audio_helper.h"
#include "esp_gmf_data_bus.h"
#include "esp_gmf_element.h"
#include "esp_gmf_fifo.h"
#include "esp_gmf_io_http.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_setup_peripheral.h"
#include "esp_gmf_setup_pool.h"
#include "esp_log.h"

#ifdef MEDIA_LIB_MEM_TEST
#include "media_lib_adapter.h"
#include "media_lib_mem_trace.h"
#endif /* MEDIA_LIB_MEM_TEST */

#include "esp_opus_enc.h"
#include "afe_proc.h"
#include "esp_gmf_ch_picker.h"
#include "esp_gmf_afe_proc.h"
#include "esp_gmf_aec.h"
#include "main.h"

static const char *TAG = "gmf";

#define BUFFER_SAMPLES (320 * 2)
#define SAMPLE_RATE (8000 * 2)

static esp_gmf_fifo_handle_t oai_plr_dec_fifo = NULL;
static esp_gmf_fifo_handle_t oai_rec_enc_fifo = NULL;

static esp_codec_dev_handle_t oai_plr_handle = NULL;
static esp_codec_dev_handle_t oai_rec_handle = NULL;

static int16_t *oai_encoder_input_buffer = NULL;

static afe_proc_handle_t afe_handle = NULL;


static esp_gmf_err_t oai_record_event_callback(esp_gmf_event_pkt_t *event,
                                               void *ctx) {
  ESP_LOGI(TAG, "RECV el:%s-%p, type:%x, sub:%s, payload:%p, size:%d,%p",
           "OBJ_GET_TAG(event->from)", event->from, (int)event->type,
           esp_gmf_event_get_state_str((esp_gmf_event_state_t)event->sub),
           event->payload, event->payload_size, ctx);
  return ESP_GMF_ERR_OK;
}

static int oai_player_event_callback(esp_asp_event_pkt_t *event, void *ctx) {
  if (event->type == ESP_ASP_EVENT_TYPE_MUSIC_INFO) {
    esp_asp_music_info_t info;
    memcpy(&info, event->payload, event->payload_size);
    ESP_LOGI(TAG, "Get info, rate:%d, channels:%d, bits:%d", info.sample_rate,
             info.channels, info.bits);
  } else if (event->type == ESP_ASP_EVENT_TYPE_STATE) {
    esp_asp_state_t st = ESP_ASP_STATE_NONE;
    memcpy(&st, event->payload, event->payload_size);
    ESP_LOGI(TAG, "Get State, %d,%s", st,
             esp_audio_simple_player_state_to_str(st));
    if (ctx &&
        ((st == ESP_ASP_STATE_STOPPED) || (st == ESP_ASP_STATE_FINISHED) ||
         (st == ESP_ASP_STATE_ERROR))) {
      xSemaphoreGive((SemaphoreHandle_t)ctx);
    }
  }
  return 0;
}

int oai_record_read_enc(uint8_t *data, int data_size) {
  esp_gmf_data_bus_block_t blk = {0};

  int ret = esp_gmf_fifo_acquire_read(oai_rec_enc_fifo, &blk, data_size,
                                      portMAX_DELAY);
  memcpy(data, blk.buf, data_size);
  esp_gmf_fifo_release_read(oai_rec_enc_fifo, &blk, 0);

  return ret;
}

void oai_record_write_enc(uint8_t *data, int len) {
  esp_gmf_data_bus_block_t blk = {0};

  esp_gmf_fifo_acquire_write(oai_rec_enc_fifo, &blk, len, portMAX_DELAY);
  memcpy((void *)blk.buf, (void *)data, len);
  blk.valid_size = len;
  if (len == 0) {
    blk.is_last = true;
  }
  esp_gmf_fifo_release_write(oai_rec_enc_fifo, &blk, portMAX_DELAY);
}

static int oai_encoder_acquire_write(void *handle,
                                     esp_gmf_data_bus_block_t *blk,
                                     uint32_t wanted_size, int block_ticks) {
  if (blk->buf) {
    return wanted_size;
  }
  return wanted_size;
}

static int oai_encoder_release_write(void *handle,
                                     esp_gmf_data_bus_block_t *blk,
                                     int block_ticks) {
  int ret = 0;
  if (blk->valid_size) {
    ESP_LOGI(TAG, "oai_encoder_release_write, size:%d", blk->valid_size);
    oai_record_write_enc(blk->buf, blk->valid_size);
    ret = blk->valid_size;
  }
  return ret;
}

static int oai_player_read_dec(uint8_t *data, int data_size, void *ctx) {
  esp_gmf_data_bus_block_t blk = {0};

  int ret = esp_gmf_fifo_acquire_read(oai_plr_dec_fifo, &blk, data_size,
                                      portMAX_DELAY);
  memcpy(data, blk.buf, data_size);
  esp_gmf_fifo_release_read(oai_plr_dec_fifo, &blk, 0);

  return ret;
}

void oai_player_write_dec(uint8_t *data, int len) {
  esp_gmf_data_bus_block_t blk = {0};

  esp_gmf_fifo_acquire_write(oai_plr_dec_fifo, &blk, len, portMAX_DELAY);
  memcpy((void *)blk.buf, (void *)data, len);
  blk.valid_size = len;
  if (len == 0) {
    blk.is_last = true;
  }
  esp_gmf_fifo_release_write(oai_plr_dec_fifo, &blk, portMAX_DELAY);
}

static int oai_player_write_out(uint8_t *data, int data_size, void *ctx) {
  esp_codec_dev_handle_t dev = (esp_codec_dev_handle_t)ctx;
  esp_codec_dev_write(dev, data, data_size);
  return data_size;
}

static esp_gmf_err_t oai_record_pipeline_create(
    esp_gmf_pipeline_handle_t *pipe_rec, esp_gmf_pool_handle_t pool) {
  esp_gmf_err_t ret = ESP_GMF_ERR_OK;
  esp_gmf_pipeline_handle_t pipe = NULL;

#if 1
    const char *name[] = { "ch_picker", "afe_proc", "encoder"};

  ret = esp_gmf_pool_new_pipeline(pool, "codec_dev_rx",name, sizeof(name) / sizeof(char *), NULL, &pipe);
  ESP_GMF_RET_ON_ERROR(TAG, ret, goto cleanup,
                       "esp_gmf_pool_new_pipeline failed(0x%x)", ret);

  esp_gmf_port_handle_t out_port;
  out_port = (esp_gmf_port_handle_t)NEW_ESP_GMF_PORT_OUT_BYTE(
      (void *)oai_encoder_acquire_write, (void *)oai_encoder_release_write,
      NULL, &out_port, 0, ESP_GMF_MAX_DELAY);
  ret = esp_gmf_pipeline_reg_el_port(pipe, "encoder", ESP_GMF_IO_DIR_WRITER, out_port);
  ESP_GMF_RET_ON_ERROR(TAG, ret, goto cleanup,
                       "esp_gmf_pipeline_reg_el_port failed(0x%x)", ret);
#else
    sdmmc_card_t *card = NULL;
    esp_gmf_setup_periph_sdmmc((void **)&card);

    const char *name[] = { "ch_picker", "afe_proc"};

  ret = esp_gmf_pool_new_pipeline(pool, "codec_dev_rx",name, sizeof(name) / sizeof(char *), "file", &pipe);
  ESP_GMF_RET_ON_ERROR(TAG, ret, goto cleanup,
                       "esp_gmf_pool_new_pipeline failed(0x%x)", ret);

    esp_gmf_info_sound_t info = {
        .sample_rates = 16000,
        .channels = 4,
        .bits = 16,
    };
    esp_gmf_pipeline_report_info(pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));
    esp_gmf_pipeline_set_out_uri(pipe, "/sdcard/gmf.pcm");

#endif
  esp_gmf_task_cfg_t cfg_rec = DEFAULT_ESP_GMF_TASK_CONFIG();
  cfg_rec.thread.stack = 30 * 1024;
  cfg_rec.thread.core = 1;
  esp_gmf_task_handle_t work_rec_task = NULL;

  ret = esp_gmf_task_init(&cfg_rec, &work_rec_task);
  ESP_GMF_RET_ON_ERROR(TAG, ret, goto cleanup, "esp_gmf_task_init failed(0x%x)",
                       ret);

  esp_gmf_pipeline_bind_task(pipe, work_rec_task);
  esp_gmf_pipeline_loading_jobs(pipe);
  esp_gmf_pipeline_set_event(pipe, oai_record_event_callback, NULL);

  esp_gmf_element_handle_t enc_handle = NULL;
  esp_gmf_pipeline_set_out_uri(pipe, "/sdcard/openAI.opus");
  esp_gmf_pipeline_get_el_by_name(pipe, "encoder", &enc_handle);

  esp_audio_type_t audio_type = ESP_AUDIO_TYPE_UNSUPPORT;
  esp_gmf_audio_helper_get_audio_type_by_uri("/sdcard/openAI.opus",
                                             &audio_type);

  esp_gmf_info_sound_t info = {
      .sample_rates = SAMPLE_RATE,
      .channels = 4,
      .bits = 16,
  };
  esp_gmf_audio_helper_reconfig_enc_by_type(
      audio_type, &info, (esp_audio_enc_config_t *)OBJ_GET_CFG(enc_handle));

  esp_audio_enc_config_t *cfg =
      (esp_audio_enc_config_t *)OBJ_GET_CFG(enc_handle);
  esp_opus_enc_config_t *opus_enc_cfg = (esp_opus_enc_config_t *)cfg->cfg;
  opus_enc_cfg->bitrate = 10000 * 3;
  opus_enc_cfg->enable_vbr = true;

  ret = esp_gmf_pipeline_report_info(pipe, ESP_GMF_INFO_SOUND, &info,
                                     sizeof(info));
  ESP_GMF_RET_ON_ERROR(TAG, ret, goto cleanup,
                       "esp_gmf_pipeline_report_info failed(0x%x)", ret);

  *pipe_rec = pipe;
  return ret;

cleanup:
  if (pipe) {
    esp_gmf_pipeline_stop(pipe);
  }
  if (work_rec_task) {
    esp_gmf_task_deinit(work_rec_task);
  }
  if (pipe) {
    esp_gmf_pipeline_destroy(pipe);
  }
  return ret;
}

void oai_init_audio_capture(void) {
  esp_gmf_setup_periph_i2c(0);

  esp_gmf_setup_periph_aud_info audio_play_config = {
      .sample_rate = SAMPLE_RATE,
      .channel = 2,
      .bits_per_sample = 16,
      .port_num = 0,
  };

  esp_gmf_setup_periph_aud_info audio_record_config = {
      .sample_rate = SAMPLE_RATE,
      .channel = 2,
      .bits_per_sample = 32,
      .port_num = 0,
  };

  esp_gmf_setup_periph_codec(&audio_play_config, &audio_record_config,
                             &oai_plr_handle, &oai_rec_handle);
  assert(oai_plr_handle && oai_rec_handle);

  esp_codec_dev_set_out_vol(oai_plr_handle, 60.0);

    afe_config_t afe_cfg = AFE_CONFIG_DEFAULT();
    afe_cfg.wakenet_init = false;
    afe_cfg.vad_init = false;
    afe_cfg.aec_init = true;
    afe_cfg.pcm_config.mic_num = 1;
    afe_cfg.pcm_config.ref_num = 1;
    afe_cfg.pcm_config.total_ch_num = 2;
    afe_proc_cfg_t user_cfg = AFE_PROC_CFG_DEFAULT(&afe_cfg, NULL, NULL, NULL);
    afe_proc_create(&user_cfg, &afe_handle);
}

void oai_init_audio_decoder(void) {
  esp_err_t err = ESP_GMF_ERR_OK;
  esp_asp_handle_t player_handle = NULL;

  err = esp_gmf_fifo_create(10, 1, &oai_plr_dec_fifo);
  ESP_GMF_RET_ON_ERROR(TAG, err, goto cleanup,
                       "oai_plr_dec_fifo init failed (0x%x)", err);

  esp_asp_cfg_t player_cfg = {
      .in.cb = oai_player_read_dec,
      .in.user_ctx = oai_plr_dec_fifo,
      .out.cb = oai_player_write_out,
      .out.user_ctx = oai_plr_handle,
      .task_prio = 5,
      .task_stack = 30 * 1024,
      .task_core = 1,
  };

  err = esp_audio_simple_player_new(&player_cfg, &player_handle);
  ESP_GMF_RET_ON_ERROR(TAG, err, goto cleanup,
                       "simple_player init failed (0x%x)", err);

  err = esp_audio_simple_player_set_event(player_handle,
                                          oai_player_event_callback, NULL);
  ESP_GMF_RET_ON_ERROR(TAG, err, goto cleanup, "set_event failed (0x%x)", err);

  err = esp_audio_simple_player_run(player_handle, "raw://sdcard/openAI.opus");
  ESP_GMF_RET_ON_ERROR(TAG, err, goto cleanup, "run failed (0x%x)", err);

  return;

cleanup:
  if (player_handle) {
    esp_audio_simple_player_destroy(player_handle);
  }
  if (oai_plr_dec_fifo) {
    esp_gmf_fifo_destroy(oai_plr_dec_fifo);
  }
}

void oai_audio_decode(uint8_t *data, size_t size) {
  oai_player_write_dec(data, size);
}

void oai_init_audio_encoder(void) {
  esp_err_t err = ESP_GMF_ERR_OK;

  err = esp_gmf_fifo_create(10, 1, &oai_rec_enc_fifo);
  ESP_GMF_RET_ON_ERROR(TAG, err, goto cleanup,
                       "oai_rec_enc_fifo init failed (0x%x)", err);

  esp_gmf_pipeline_handle_t record_pipeline = NULL;
  esp_gmf_pool_handle_t pool_handle = NULL;
  err = esp_gmf_pool_init(&pool_handle);
  ESP_GMF_RET_ON_ERROR(TAG, err, goto cleanup,
                       "esp_gmf_pool_init failed (0x%x)", err);

  pool_register_audio_codecs(pool_handle);
//   pool_register_audio_effects(pool_handle);
  pool_register_io(pool_handle);
  pool_register_codec_dev_io(pool_handle, oai_plr_handle, oai_rec_handle);

    esp_gmf_element_handle_t picker_handle = NULL;
    ch_picker_cfg_t chp_cfg = {
        // .ch_choice = "1, 3, 0",
        .ch_choice = "1, 0",
    };
    esp_gmf_ch_picker_init(&chp_cfg, &picker_handle);
    esp_gmf_pool_register_element(pool_handle, picker_handle, NULL);

    esp_gmf_element_handle_t gmf_afe_proc_handle = NULL;
    esp_gmf_afe_proc_cfg_t gmf_afe_proc_cfg = {
        .afe = afe_handle
    };
    esp_gmf_afe_proc_init(&gmf_afe_proc_cfg, &gmf_afe_proc_handle);
    esp_gmf_pool_register_element(pool_handle, gmf_afe_proc_handle, NULL);

    esp_gmf_element_handle_t gmf_aec_handle = NULL;
    esp_gmf_aec_cfg_t gmf_aec_cfg = {
        .frame_len = 16,
        .nch = 1,
        .mode = 3
    };
    esp_gmf_aec_init(&gmf_aec_cfg, &gmf_aec_handle);
    esp_gmf_pool_register_element(pool_handle, gmf_aec_handle, NULL);

  ESP_GMF_POOL_SHOW_ITEMS(pool_handle);

  err = oai_record_pipeline_create(&record_pipeline, pool_handle);
  ESP_GMF_RET_ON_ERROR(TAG, err, goto cleanup,
                       "oai_record_pipeline_create failed (0x%x)", err);

  err = esp_gmf_pipeline_run(record_pipeline);
  ESP_GMF_RET_ON_ERROR(TAG, err, goto cleanup,
                       "esp_gmf_pipeline_run failed (0x%x)", err);

  oai_encoder_input_buffer = (int16_t *)malloc(BUFFER_SAMPLES);
  return;

cleanup:
  if (oai_rec_enc_fifo) {
    esp_gmf_fifo_destroy(oai_rec_enc_fifo);
  }
  if (pool_handle) {
    esp_gmf_pool_deinit(pool_handle);
  }
  if (oai_encoder_input_buffer) {
    free(oai_encoder_input_buffer);
  }
}

void oai_send_audio(PeerConnection *peer_connection) {
  int encoded_size =
      oai_record_read_enc((uint8_t *)oai_encoder_input_buffer, BUFFER_SAMPLES);
  peer_connection_send_audio(
      peer_connection, (const uint8_t *)oai_encoder_input_buffer, encoded_size);
}

#endif
