/* Record MP3 file from SD Card

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>

#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "hal/i2s_types.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_types.h"
#include "esp_gmf_afe_proc.h"
#include "esp_gmf_ch_picker.h"
#include "esp_gmf_element.h"
#include "esp_gmf_io_codec_dev.h"
#include "esp_gmf_io_file.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_aec.h"

#include "afe_proc.h"
#include "cli.h"

#define ESP_GMF_SD_PIN_CLK (GPIO_NUM_15)
#define ESP_GMF_SD_PIN_CMD (GPIO_NUM_7)
#define ESP_GMF_SD_PIN_D0  (GPIO_NUM_4)
#define ESP_GMF_SD_PIN_D1  (GPIO_NUM_NC)
#define ESP_GMF_SD_PIN_D2  (GPIO_NUM_NC)
#define ESP_GMF_SD_PIN_D3  (GPIO_NUM_NC)
#define ESP_GMF_SD_PIN_D4  (GPIO_NUM_NC)
#define ESP_GMF_SD_PIN_D5  (GPIO_NUM_NC)
#define ESP_GMF_SD_PIN_D6  (GPIO_NUM_NC)
#define ESP_GMF_SD_PIN_D7  (GPIO_NUM_NC)
#define ESP_GMF_SD_PIN_CD  (GPIO_NUM_NC)
#define ESP_GMF_SD_PIN_WP  (GPIO_NUM_NC)
#define ESP_GMF_SD_WIDTH   (1)

static const char *TAG = "REC_SDCARD";
static i2s_chan_handle_t rx_handle = NULL;
static esp_codec_dev_handle_t record_dev = NULL;
const audio_codec_data_if_t *data_if = NULL;
const audio_codec_ctrl_if_t *in_ctrl_if = NULL;
const audio_codec_if_t *in_codec_if = NULL;

static esp_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGE(TAG, "CB: RECV Pipeline EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    return 0;
}

void pool_register_rec_codec_dev(esp_gmf_pool_handle_t pool)
{
    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_cfg.sda_io_num = GPIO_NUM_17;
    i2c_cfg.scl_io_num = GPIO_NUM_18;
    int ret = i2c_param_config(0, &i2c_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Install I2C failed");
        return;
    }
    i2c_driver_install(0, i2c_cfg.mode, 0, 0, 0);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ret = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return, "New the RX I2S handle failed");
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(32, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
                     .mclk = GPIO_NUM_16,
                     .bclk = GPIO_NUM_9,
                     .ws = GPIO_NUM_45,
                     .dout = GPIO_NUM_8,
                     .din = GPIO_NUM_10,
                     },
    };
    i2s_channel_init_std_mode(rx_handle, &std_cfg);
    audio_codec_i2s_cfg_t i2s_cfg = {
        .tx_handle = NULL,
        .rx_handle = rx_handle,
    };
    data_if = audio_codec_new_i2s_data(&i2s_cfg);

    audio_codec_i2c_cfg_t i2c_ctrl_cfg = { .addr = ES7210_CODEC_DEFAULT_ADDR, .port = 0 };
    in_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_ctrl_cfg);
    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = in_ctrl_if,
        .mic_selected = ES7120_SEL_MIC1 | ES7120_SEL_MIC2 | ES7120_SEL_MIC3,
    };
    in_codec_if = es7210_codec_new(&es7210_cfg);
    esp_codec_dev_cfg_t dev_cfg = { 0 };
    dev_cfg.codec_if = in_codec_if;
    dev_cfg.data_if = data_if;
    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    record_dev = esp_codec_dev_new(&dev_cfg);
    ret = esp_codec_dev_set_in_gain(record_dev, 30);
    esp_codec_dev_sample_info_t rec_cfg = {
        .sample_rate = 48000,
        .channel = 2,
        .bits_per_sample = 32,
    };
    ret = esp_codec_dev_open(record_dev, &rec_cfg);

    codec_dev_io_cfg_t codec_dev_cfg = ESP_GMF_IO_CODEC_DEV_CFG_DEFAULT();
    codec_dev_cfg.dir = ESP_GMF_IO_DIR_READER;
    codec_dev_cfg.dev = record_dev;
    codec_dev_cfg.name = "codec_dev_rx";
    esp_gmf_io_handle_t codec_dev = NULL;
    esp_gmf_io_codec_dev_init(&codec_dev_cfg, &codec_dev);
    esp_gmf_pool_register_io(pool, codec_dev, NULL);
}

void esp_gmf_setup_periph_sdmmc(void **out_card)
{
    sdmmc_card_t *card = NULL;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
#if defined CONFIG_IDF_TARGET_ESP32P4
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;
    esp_err_t ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create a new on-chip LDO power control driver");
        return;
    }
    host.pwr_ctrl_handle = pwr_ctrl_handle;
#endif /* defined CONFIG_IDF_TARGET_ESP32P4 */

#if defined CONFIG_IDF_TARGET_ESP32
    gpio_config_t sdcard_pwr_pin_cfg = {
        .pin_bit_mask = 1UL << GPIO_NUM_13,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&sdcard_pwr_pin_cfg);
    gpio_set_level(GPIO_NUM_13, 0);
#endif /* defined CONFIG_IDF_TARGET_ESP32 */

    slot_config.width = ESP_GMF_SD_WIDTH;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 12 * 1024
    };
#if SOC_SDMMC_USE_GPIO_MATRIX
    slot_config.clk = ESP_GMF_SD_PIN_CLK;
    slot_config.cmd = ESP_GMF_SD_PIN_CMD;
    slot_config.d0 = ESP_GMF_SD_PIN_D0;
    slot_config.d1 = ESP_GMF_SD_PIN_D1;
    slot_config.d2 = ESP_GMF_SD_PIN_D2;
    slot_config.d3 = ESP_GMF_SD_PIN_D3;
    slot_config.d4 = ESP_GMF_SD_PIN_D4;
    slot_config.d5 = ESP_GMF_SD_PIN_D5;
    slot_config.d6 = ESP_GMF_SD_PIN_D6;
    slot_config.d7 = ESP_GMF_SD_PIN_D7;
    slot_config.cd = ESP_GMF_SD_PIN_CD;
    slot_config.wp = ESP_GMF_SD_PIN_WP;
#endif /* SOC_SDMMC_USE_GPIO_MATRIX */
#if defined CONFIG_IDF_TARGET_ESP32P4
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
#endif /* defined CONFIG_IDF_TARGET_ESP32P4 */
    esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    *out_card = card;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);

    srmodel_list_t *models = esp_srmodel_init("model");
    afe_config_t afe_cfg = AFE_CONFIG_DEFAULT();
    afe_cfg.wakenet_init = false;
    afe_cfg.vad_init = false;
    afe_cfg.aec_init = true;
    afe_proc_cfg_t user_cfg = AFE_PROC_CFG_DEFAULT(&afe_cfg, NULL, NULL, models);
    afe_proc_handle_t afe_handle = NULL;
    afe_proc_create(&user_cfg, &afe_handle);

    void *card = NULL;
    esp_gmf_setup_periph_sdmmc(&card);

    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    pool_register_rec_codec_dev(pool);

    esp_ae_rate_cvt_cfg_t rate_cvt_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    rate_cvt_cfg.dest_rate = 16000;
    rate_cvt_cfg.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY;
    esp_gmf_element_handle_t rate_hd = NULL;
    esp_gmf_rate_cvt_init(&rate_cvt_cfg, &rate_hd);
    esp_gmf_pool_register_element(pool, rate_hd, NULL);

    esp_gmf_element_handle_t picker_handle = NULL;
    ch_picker_cfg_t chp_cfg = {
        .ch_choice = "1, 3, 0",
    };
    esp_gmf_ch_picker_init(&chp_cfg, &picker_handle);
    esp_gmf_pool_register_element(pool, picker_handle, NULL);

    esp_gmf_element_handle_t gmf_afe_proc_handle = NULL;
    esp_gmf_afe_proc_cfg_t gmf_afe_proc_cfg = {
        .afe = afe_handle
    };
    esp_gmf_afe_proc_init(&gmf_afe_proc_cfg, &gmf_afe_proc_handle);
    esp_gmf_pool_register_element(pool, gmf_afe_proc_handle, NULL);

    esp_gmf_element_handle_t gmf_aec_handle = NULL;
    esp_gmf_aec_cfg_t gmf_aec_cfg = {
        .frame_len = 16,
        .nch = 1,
        .mode = 3
    };
    esp_gmf_aec_init(&gmf_aec_cfg, &gmf_aec_handle);
    esp_gmf_pool_register_element(pool, gmf_aec_handle, NULL);

    file_io_cfg_t fs_cfg = FILE_IO_CFG_DEFAULT();
    fs_cfg.dir = ESP_GMF_IO_DIR_WRITER;
    esp_gmf_io_handle_t fs = NULL;
    esp_gmf_io_file_init(&fs_cfg, &fs);
    esp_gmf_pool_register_io(pool, fs, NULL);

    ESP_GMF_POOL_SHOW_ITEMS(pool);
    esp_gmf_pipeline_handle_t pipe = NULL;

    const char *name[] = { "rate_cvt", "ch_picker", "afe_proc" };
    esp_gmf_pool_new_pipeline(pool, "codec_dev_rx", name, sizeof(name) / sizeof(char *), "file", &pipe);
    if (pipe == NULL) {
        ESP_LOGE(TAG, "There is no pipeline");
        return;
    }
    esp_gmf_info_sound_t info = {
        .sample_rates = 48000,
        .channels = 4,
        .bits = 16,
    };
    esp_gmf_pipeline_report_info(pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));
    esp_gmf_pipeline_set_out_uri(pipe, "/sdcard/gmf.pcm");

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.thread.core = 1;
    esp_gmf_task_handle_t work_task = NULL;
    esp_gmf_task_init(&cfg, &work_task);

    esp_gmf_pipeline_bind_task(pipe, work_task);
    esp_gmf_pipeline_loading_jobs(pipe);
    esp_gmf_pipeline_set_event(pipe, _pipeline_event, NULL);
    esp_gmf_pipeline_run(pipe);

    cli_register_afe(afe_handle);
    cli_init("Audio >");

    vTaskDelay(100000 / portTICK_PERIOD_MS);
    esp_gmf_pipeline_stop(pipe);

    esp_gmf_task_deinit(work_task);
    esp_gmf_pipeline_destroy(pipe);
    esp_gmf_pool_deinit(pool);
}
