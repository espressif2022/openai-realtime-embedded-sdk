/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_bit_defs.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_types.h"

#include "afe_proc.h"
#include "ai_audio_common.h"
#include "cli.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "driver/sdmmc_host.h"
#include "esp_afe_config.h"
#include "model_path.h"
#include "voice_cmd.h"
#include "wakeup.h"

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

#define DEFAULT_SAMPLERATE (16000)
#define DEFAULT_BITS       (32)
#define DEFAULT_CHANNEL    (2)

static const char *TAG = "AIAudio";
static wakeup_handle_t wake_handle = NULL;
static vcmd_handle_t vcmd_handle = NULL;
static FILE *fp_raw = NULL;
static FILE *fp_result = NULL;
static i2s_chan_handle_t rx_handle = NULL;
static esp_codec_dev_handle_t record_dev = NULL;
static uint16_t *i2s_buf = NULL;

const audio_codec_data_if_t *data_if = NULL;
const audio_codec_ctrl_if_t *in_ctrl_if = NULL;
const audio_codec_if_t *in_codec_if = NULL;

extern uint32_t save_flag;

static void init_codec_dev()
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
}

void init_sdmmc(void **out_card)
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

static int32_t afe_read(void *buffer, int buf_sz, void *user_ctx, TickType_t ticks)
{
    size_t chunk_size = buf_sz / sizeof(uint16_t) / 3;
    size_t rsize = chunk_size * sizeof(int16_t) * 4;
    if (!i2s_buf) {
        i2s_buf = heap_caps_malloc(rsize, MALLOC_CAP_SPIRAM);
    }
    if (i2s_buf) {
        esp_codec_dev_read(record_dev, i2s_buf, rsize);
        for (int i = 0; i < chunk_size; i++) {
            int16_t ref = i2s_buf[4 * i + 0];
            ((uint16_t *)buffer)[3 * i + 0] = i2s_buf[4 * i + 1];
            ((uint16_t *)buffer)[3 * i + 1] = i2s_buf[4 * i + 3];
            ((uint16_t *)buffer)[3 * i + 2] = ref;
        }
        if (fp_raw == NULL && (save_flag & BIT0)) {
            fp_raw = fopen("/sdcard/raw.pcm", "w");
        }
        if (fp_raw && (save_flag & BIT0)) {
            // FatfsComboWrite(buffer, buf_sz, 1, fp_raw);
            fwrite(buffer, buf_sz, 1, fp_raw);
        }
        if (fp_raw && !(save_flag & BIT0)) {
            fflush(fp_raw);
            fclose(fp_raw);
            fp_raw = NULL;
        }
        return buf_sz;
    } else {
        return 0;
    }
}

void ai_audio_event_cb(ai_audio_evt_t *event, void *user_data)
{
    switch (event->type) {
        case WAKEUP_START: {
            wakeup_info_t *info = event->event_data;
            ESP_LOGI(TAG, "WAKEUP_START [%d : %d]", info->wake_word_index, info->wakenet_model_index);
            vcmd_det_cancel(vcmd_handle);
            vcmd_det_begin(vcmd_handle);
            break;
        }
        case WAKEUP_END: {
            ESP_LOGI(TAG, "WAKEUP_END");
            vcmd_det_cancel(vcmd_handle);
            break;
        }
        case VAD_START: {
            ESP_LOGI(TAG, "VAD_START");
            // vcmd_det_begin(vcmd_handle);
            break;
        }
        case VAD_END: {
            ESP_LOGI(TAG, "VAD_END");
            // vcmd_det_cancel(vcmd_handle);
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

void afe_result(afe_fetch_result_t *result, void *user_ctx)
{
    ESP_LOGV(TAG, "d %p, l %d", result->data, result->data_size);
    if (fp_result == NULL && (save_flag & BIT1)) {
        fp_result = fopen("/sdcard/result.pcm", "w");
    }
    if (fp_result && (save_flag & BIT1)) {
        fwrite(result->data, result->data_size, 1, fp_result);
    }
    if (fp_result && !(save_flag & BIT1)) {
        fflush(fp_result);
        fclose(fp_result);
        fp_result = NULL;
    }
}

void app_main()
{
    init_codec_dev();
    void *card = NULL;
    init_sdmmc(&card);

    srmodel_list_t *models = esp_srmodel_init("model");
    char *wn_name = NULL;
    char *wn_name_2 = NULL;

    if (models != NULL) {
        for (int i = 0; i < models->num; i++) {
            if (strstr(models->model_name[i], ESP_WN_PREFIX) != NULL) {
                if (wn_name == NULL) {
                    wn_name = models->model_name[i];
                    printf("The first wakenet model: %s\n", wn_name);
                } else if (wn_name_2 == NULL) {
                    wn_name_2 = models->model_name[i];
                    printf("The second wakenet model: %s\n", wn_name_2);
                }
            }
        }
    } else {
        printf("Please enable wakenet model and select wake word by menuconfig!\n");
        return;
    }
    afe_config_t afe_cfg = AFE_CONFIG_DEFAULT();
    afe_proc_handle_t afe_handle = NULL;
    afe_proc_cfg_t user_cfg = AFE_PROC_CFG_DEFAULT(&afe_cfg, afe_read, &afe_handle, models);
    afe_proc_create(&user_cfg, &afe_handle);

    wakeup_cfg_t wake_cfg = {
        .wakeup_time = 10000,
        .wakeup_end = 900,
        .vad_start = 160,
        .vad_off = 1000,
        .afe_handle = afe_handle,
        .event_cb = ai_audio_event_cb,
        .user_data = NULL,
    };
    wakeup_create(&wake_cfg, &wake_handle);

    vcmd_cfg_t vcmd_cfg = {
        .afe_handle = afe_handle,
        .event_cb = ai_audio_event_cb,
        .user_data = NULL,
        .mn_language = "cn",
        .models = models,
    };
    vcmd_create(&vcmd_cfg, &vcmd_handle);

    afe_proc_result_cb_register(afe_handle, afe_result, NULL);

    cli_register_afe(afe_handle);
    cli_init("Audio >");
}
