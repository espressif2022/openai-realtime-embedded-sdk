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
#include "esp_afe_config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "hal/i2s_types.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_types.h"

#include "esp_gmf_data_bus.h"
#include "esp_gmf_io.h"
#include "esp_gmf_io_codec_dev.h"
#include "esp_gmf_port.h"

#include "ai_audio.h"
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

#define VOICE2FILE         (false)
#define DEFAULT_SAMPLERATE (48000)
#define DEFAULT_BITS       (32)
#define DEFAULT_CHANNEL    (2)

static const char *TAG = "REC_SDCARD";
static i2s_chan_handle_t rx_handle = NULL;
static esp_codec_dev_handle_t record_dev = NULL;
static esp_gmf_io_handle_t codec_dev;
static ai_audio_handle_t ai_audio = NULL;
static bool speeching = false;
static bool wakeup = false;

const audio_codec_data_if_t *data_if = NULL;
const audio_codec_ctrl_if_t *in_ctrl_if = NULL;
const audio_codec_if_t *in_codec_if = NULL;

void ai_audio_event_cb(ai_audio_evt_t *event, void *user_data)
{
    switch (event->type) {
        case WAKEUP_START: {
            wakeup = true;
            wakeup_info_t *info = event->event_data;
            ESP_LOGI(TAG, "WAKEUP_START [%d : %d]", info->wake_word_index, info->wakenet_model_index);
            ai_audio_vcmd_det_cancel(ai_audio);
            ai_audio_vcmd_det_begin(ai_audio);
            break;
        }
        case WAKEUP_END: {
            wakeup = false;
            ESP_LOGI(TAG, "WAKEUP_END");
            ai_audio_vcmd_det_cancel(ai_audio);
            break;
        }
        case VAD_START: {
            speeching = true;
            ESP_LOGI(TAG, "VAD_START");
            break;
        }
        case VAD_END: {
            speeching = false;
            ESP_LOGI(TAG, "VAD_END");
            break;
        }
        case VCMD_DECT_TIMEOUT: {
            ESP_LOGI(TAG, "VCMD_DECT_TIMEOUT");
            break;
        }
        default: {
            vcmd_info_t *info = event->event_data;
            ESP_LOGW(TAG, "command %d, phrase_id %d, prob %f, str: %s",
                     event->type, info->phrase_id, info->prob, info->str);
            break;
        }
    }
}

void pool_register_rec_codec_dev()
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
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(DEFAULT_SAMPLERATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(DEFAULT_BITS, I2S_SLOT_MODE_STEREO),
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
        .sample_rate = DEFAULT_SAMPLERATE,
        .channel = DEFAULT_CHANNEL,
        .bits_per_sample = DEFAULT_BITS,
    };
    ret = esp_codec_dev_open(record_dev, &rec_cfg);

    codec_dev_io_cfg_t codec_dev_cfg = ESP_GMF_IO_CODEC_DEV_CFG_DEFAULT();
    codec_dev_cfg.dir = ESP_GMF_IO_DIR_READER;
    codec_dev_cfg.dev = record_dev;
    codec_dev_cfg.name = "codec_dev";
    esp_gmf_io_codec_dev_init(&codec_dev_cfg, &codec_dev);
    esp_gmf_io_codec_dev_cast(&codec_dev_cfg, codec_dev);
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

static void voice_2_file(uint8_t *buffer, int len)
{
#if VOICE2FILE == true
#define MAX_FNAME_LEN (50)

    static FILE *fp = NULL;
    static int fcnt = 0;

    if (speeching) {
        if (!fp) {
            if (fp == NULL) {
                char fname[MAX_FNAME_LEN] = { 0 };
                snprintf(fname, MAX_FNAME_LEN - 1, "/sdcard/f%d.pcm", fcnt++);
                fp = fopen(fname, "wb");
                if (!fp) {
                    ESP_LOGE(TAG, "File open failed");
                }
            }
        }
        if (len) {
            fwrite(buffer, len, 1, fp);
        }
    } else {
        if (fp) {
            ESP_LOGI(TAG, "File closed");
            fclose(fp);
            fp = NULL;
        }
    }
#endif
}

static int ai_audio_acquire_write(void *handle, esp_gmf_data_bus_block_t *blk, int wanted_size, int block_ticks)
{
    ESP_LOGD(TAG, "acquire write");
    return wanted_size;
}

static int ai_audio_release_write(void *handle, esp_gmf_data_bus_block_t *blk, int block_ticks)
{
    ESP_LOGD(TAG, "release write");
    voice_2_file(blk->buf, blk->valid_size);
    return blk->valid_size;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    pool_register_rec_codec_dev();
    void *card = NULL;
    esp_gmf_setup_periph_sdmmc(&card);

    afe_config_t afe_cfg = AFE_CONFIG_DEFAULT();
    ai_audio_cfg_t ai_aud_cfg = DEFAULT_AI_AUDIO_CFG();
    ai_aud_cfg.src_info.sample_rate = DEFAULT_SAMPLERATE;
    ai_aud_cfg.afe_cfg = &afe_cfg;
    ai_aud_cfg.wakeup_det = true;
    ai_aud_cfg.wakeup_end = 2000;
    ai_aud_cfg.vocie_cmd_det = true;
    ai_aud_cfg.event_cb = ai_audio_event_cb;
    ai_aud_cfg.event_user_ctx = NULL;
    ai_aud_cfg.in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_io_acquire_read,
                                                  esp_gmf_io_release_read,
                                                  NULL,
                                                  codec_dev,
                                                  2048,
                                                  ESP_GMF_MAX_DELAY);
    ai_aud_cfg.out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ai_audio_acquire_write,
                                                    ai_audio_release_write,
                                                    NULL,
                                                    NULL,
                                                    2048,
                                                    100);
    ai_audio_create(&ai_aud_cfg, &ai_audio);

    cli_init("Audio >");
}
