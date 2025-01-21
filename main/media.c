/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "main.h"

#if CONFIG_OPENAI_BOARD_ESP_BOX == 0
#include <driver/i2s.h>
#endif
#include <opus.h>
#include "esp_log.h"
#include "esp_err.h"

#include "mmap_generate_wav.h"
#include "tcp_server.h"

static const char *TAG = "DISP";

#define OPUS_OUT_BUFFER_SIZE 1276  // 1276 bytes is recommended by opus_encode

#if CONFIG_OPENAI_BOARD_ESP32_S3  // Default ESP32-S3 board config
#define SAMPLE_RATE 8000
#define BUFFER_SAMPLES 320

#define I2S_DATA_OUT_PORT I2S_NUM_0
#define I2S_DATA_IN_PORT I2S_NUM_1
#define MCLK_PIN 0
#define DAC_BCLK_PIN 15
#define DAC_LRCLK_PIN 16
#define DAC_DATA_PIN 17
#define ADC_BCLK_PIN 38
#define ADC_LRCLK_PIN 39
#define ADC_DATA_PIN 40
#elif CONFIG_OPENAI_BOARD_M5_ATOMS3R  // ATOMS3R board with Atomic EchoBase
#include "es8311.h"
#define SAMPLE_RATE (8000 *2)  //! EchoBase not support 8K sample rate :
#define BUFFER_SAMPLES (320 * 2)

#if ESP_SPARKBOT
#define I2C_PORT I2C_NUM_1
#define I2C_FREQ_HZ 400000
#define I2C_SCL_PIN GPIO_NUM_5
#define I2C_SDA_PIN GPIO_NUM_4

#define I2S_DATA_OUT_PORT I2S_NUM_1
#define I2S_DATA_IN_PORT I2S_DATA_OUT_PORT
// #define MCLK_PIN        GPIO_NUM_45
#define MCLK_PIN        -1
#define DAC_BCLK_PIN  GPIO_NUM_39
#define DAC_LRCLK_PIN GPIO_NUM_41
#define DAC_DATA_PIN  GPIO_NUM_42
#define ADC_DATA_PIN  GPIO_NUM_40
#else
#define I2C_PORT I2C_NUM_1
#define I2C_FREQ_HZ 400000
#define I2C_SCL_PIN 39
#define I2C_SDA_PIN 38

#define I2S_DATA_OUT_PORT I2S_NUM_1
#define I2S_DATA_IN_PORT I2S_DATA_OUT_PORT
#define MCLK_PIN -1
#define DAC_BCLK_PIN  8
#define DAC_LRCLK_PIN 6
#define DAC_DATA_PIN  5
#define ADC_DATA_PIN  7
#endif

es8311_handle_t es8311_handle = NULL;
#elif CONFIG_OPENAI_BOARD_ESP_BOX

// #define BUFFER_SAMPLES      (320 * 2)
// #define SAMPLE_RATE         (8000 * 2)
#define BUFFER_SAMPLES      (320 * 1)
#define SAMPLE_RATE         (8000 * 1)

#define DEFAULT_VOLUME  (90)

extern mmap_assets_handle_t asset_lottie;

extern void player_out(uint8_t *data, int data_size);
extern int record_in(uint8_t *data, int data_size);

#endif

extern int record_read_opus_data(uint8_t *data, int data_size);
extern void player_send_opus_data(uint8_t *data, int len);

#define OPUS_ENCODER_BITRATE 30000
#define OPUS_ENCODER_COMPLEXITY 0

void oai_completed_event()
{
    const void *data = mmap_assets_get_mem(asset_lottie, MMAP_WAV_OPENAI_WELCOME_WAV);
    size_t size = mmap_assets_get_size(asset_lottie, MMAP_WAV_OPENAI_WELCOME_WAV);

#if CONFIG_OPENAI_BOARD_ESP_BOX
    // player_out((void*)data + 44, size);
#else
    size_t bytes_written = 0;
    // i2s_write(I2S_DATA_OUT_PORT, (void*)data + 44, size, &bytes_written, portMAX_DELAY);
#endif
}

void oai_init_audio_capture()
{
#if CONFIG_OPENAI_BOARD_ESP32_S3
    i2s_config_t i2s_config_out = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S_MSB,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = BUFFER_SAMPLES,
        .use_apll = 1,
        .tx_desc_auto_clear = true,
    };
    if (i2s_driver_install(I2S_DATA_OUT_PORT, &i2s_config_out, 0, NULL) != ESP_OK) {
        printf("Failed to configure I2S driver for audio output");
        return;
    }

    i2s_pin_config_t pin_config_out = {
        .mck_io_num = MCLK_PIN,
        .bck_io_num = DAC_BCLK_PIN,
        .ws_io_num = DAC_LRCLK_PIN,
        .data_out_num = DAC_DATA_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };
    if (i2s_set_pin(I2S_DATA_OUT_PORT, &pin_config_out) != ESP_OK) {
        printf("Failed to set I2S pins for audio output");
        return;
    }
    i2s_zero_dma_buffer(I2S_DATA_OUT_PORT);

    i2s_config_t i2s_config_in = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S_MSB,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = BUFFER_SAMPLES,
        .use_apll = 1,
    };
    if (i2s_driver_install(I2S_DATA_IN_PORT, &i2s_config_in, 0, NULL) != ESP_OK) {
        printf("Failed to configure I2S driver for audio input");
        return;
    }

    i2s_pin_config_t pin_config_in = {
        .mck_io_num = MCLK_PIN,
        .bck_io_num = ADC_BCLK_PIN,
        .ws_io_num = ADC_LRCLK_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = ADC_DATA_PIN,
    };
    if (i2s_set_pin(I2S_DATA_IN_PORT, &pin_config_in) != ESP_OK) {
        printf("Failed to set I2S pins for audio input");
        return;
    }
#elif CONFIG_OPENAI_BOARD_M5_ATOMS3R
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master =
        {
            .clk_speed = I2C_FREQ_HZ,
        }
    };

    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);

    es8311_handle = es8311_create(I2C_PORT, ES8311_ADDRRES_0);
    if (es8311_handle == NULL) {
        printf("Failed to create ES8311 handle");
        return;
    }

    es8311_clock_config_t clk_cfg = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = false,
        .sample_frequency = SAMPLE_RATE,
    };

    if (es8311_init(es8311_handle, &clk_cfg, ES8311_RESOLUTION_32,
                    ES8311_RESOLUTION_32) != ESP_OK) {
        printf("Failed to initialize ES8311");
        return;
    }

    es8311_voice_volume_set(es8311_handle, 75, NULL);
    // es8311_voice_volume_set(es8311_handle, 100, NULL);
    es8311_microphone_config(es8311_handle, false);
    // es8311_microphone_gain_set(es8311_handle, ES8311_MIC_GAIN_30DB);

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ALL_LEFT,  //! Important for EchoBase, play failed,echo ok.
        //   .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  //! Important for EchoBase , play ok,echo failed.
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = BUFFER_SAMPLES,
        .use_apll = 1,
        .tx_desc_auto_clear = true,
    };
    if (i2s_driver_install(I2S_DATA_IN_PORT, &i2s_config, 0, NULL) != ESP_OK) {
        printf("Failed to configure I2S driver for audio input/output");
        return;
    }

    i2s_pin_config_t pin_config_out = {
        .mck_io_num   = MCLK_PIN,
        .bck_io_num   = DAC_BCLK_PIN,
        .ws_io_num    = DAC_LRCLK_PIN,
        .data_out_num = DAC_DATA_PIN,
        .data_in_num  = ADC_DATA_PIN,
    };
    if (i2s_set_pin(I2S_DATA_IN_PORT, &pin_config_out) != ESP_OK) {
        printf("Failed to set I2S pins for audio output");
        return;
    }
    i2s_zero_dma_buffer(I2S_DATA_IN_PORT);

#elif CONFIG_OPENAI_BOARD_ESP_BOX
    ESP_LOGI(TAG, "CONFIG_OPENAI_BOARD_ESP_BOX");

    extern void i2s_enc_file(void);
    extern void start_gmf_task(void);

    start_gmf_task();
#endif
}

opus_int16 *output_buffer = NULL;
OpusDecoder *opus_decoder = NULL;

void oai_init_audio_decoder()
{
    int decoder_error = 0;
    opus_decoder = opus_decoder_create(SAMPLE_RATE, 2, &decoder_error);
    // opus_decoder = opus_decoder_create(SAMPLE_RATE, 1, &decoder_error);
    if (decoder_error != OPUS_OK) {
        printf("Failed to create OPUS decoder");
        return;
    }

    output_buffer = (opus_int16 *)malloc(BUFFER_SAMPLES * sizeof(opus_int16));
}

#include "esp_timer.h"

typedef struct {
    int64_t start;
    int64_t acc;
    char str1[10];
    char str2[10];
} PerfCounter;

static PerfCounter perf_counters;

static void perfmon_start(const char* fmt1, const char* fmt2, ...)
{
    va_list args;
    va_start(args, fmt2);
    vsnprintf(perf_counters.str1, sizeof(perf_counters.str1), fmt1, args);
    vsnprintf(perf_counters.str2, sizeof(perf_counters.str2), fmt2, args);
    va_end(args);

    perf_counters.start = esp_timer_get_time();
}

static void perfmon_end(int count)
{
    perf_counters.acc = esp_timer_get_time() - perf_counters.start;
    printf("Perf ctr, [%-15s][%-15s]: %.2f ms\n",
           perf_counters.str1, perf_counters.str2, ((float)perf_counters.acc / count) / 1000);
}

void oai_audio_decode(uint8_t *data, size_t size, void* userdata)
{
    if(size != 26){
        perfmon_end(1);
        perfmon_start("agent", "send_opus");
        // ESP_LOGI(TAG, "agent:%d", size);
    } else {
        // ESP_LOGW(TAG, "agent:%d", size);
    }

    int decoded_size = 0;
#if USE_GMF
    player_send_opus_data(data, size);
#else
    decoded_size = opus_decode(opus_decoder, data, size, output_buffer, BUFFER_SAMPLES, 0);
    player_out((uint8_t *)output_buffer, BUFFER_SAMPLES * sizeof(opus_int16));
    // ESP_LOGI(TAG, "opus_decode:%d, %d(%d)", size, decoded_size, BUFFER_SAMPLES * sizeof(opus_int16));
#endif
}

OpusEncoder *opus_encoder = NULL;
opus_int16 *encoder_input_buffer = NULL;
uint8_t *encoder_output_buffer = NULL;

void oai_init_audio_encoder()
{
    int encoder_error;
    opus_encoder = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP,
                                       &encoder_error);
    if (encoder_error != OPUS_OK) {
        printf("Failed to create OPUS encoder");
        return;
    }

    if (opus_encoder_init(opus_encoder, SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP) !=
            OPUS_OK) {
        printf("Failed to initialize OPUS encoder");
        return;
    }

    opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(OPUS_ENCODER_BITRATE));
    opus_encoder_ctl(opus_encoder, OPUS_SET_COMPLEXITY(OPUS_ENCODER_COMPLEXITY));
    opus_encoder_ctl(opus_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    encoder_input_buffer = (opus_int16 *)malloc(BUFFER_SAMPLES);
    encoder_output_buffer = (uint8_t *)malloc(OPUS_OUT_BUFFER_SIZE);
}

void oai_send_audio(PeerConnection *peer_connection)
{

#if CONFIG_OPENAI_BOARD_ESP_BOX

#if USE_GMF
    int encoded_size = record_read_opus_data((uint8_t *)encoder_input_buffer, BUFFER_SAMPLES);
    ESP_LOGI(TAG, "encoded_size:%d, %d", encoded_size, BUFFER_SAMPLES);
    peer_connection_send_audio(peer_connection, (const uint8_t*)oai_encoder_input_buffer, encoded_size);
#else
    record_in((uint8_t *)encoder_input_buffer, BUFFER_SAMPLES);
    int encoded_size =
        opus_encode(opus_encoder, encoder_input_buffer, BUFFER_SAMPLES / 2,
                    encoder_output_buffer, OPUS_OUT_BUFFER_SIZE);
    // ESP_LOGI(TAG, "encoded_bytes out:%d -> %d", BUFFER_SAMPLES, (int)encoded_size);
    peer_connection_send_audio(peer_connection, (const uint8_t*)encoder_output_buffer, encoded_size);
    // peer_connection_send_audio(peer_connection, (const uint8_t*)encoder_input_buffer, encoded_size);
#endif

#else
    size_t bytes_read = 0;
    i2s_read(I2S_DATA_IN_PORT, encoder_input_buffer, BUFFER_SAMPLES, &bytes_read,
             portMAX_DELAY);
#endif
    // tcp_server_send((uint8_t *)encoder_input_buffer, BUFFER_SAMPLES);
}
