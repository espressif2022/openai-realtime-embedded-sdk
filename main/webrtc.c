/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#ifndef LINUX_BUILD
#include <driver/i2s.h>
#include <opus.h>
#endif

#include <esp_event.h>
#include <esp_log.h>
#include <string.h>

#include "main.h"

#define TICK_INTERVAL 10

PeerConnection *peer_connection = NULL;

#ifndef LINUX_BUILD
StaticTask_t task_buffer;
void oai_send_audio_task(void *user_data)
{
    oai_init_audio_encoder();

    while (1) {
        oai_send_audio(peer_connection);
        vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
    }
}
#endif

static void oai_onconnectionstatechange_task(PeerConnectionState state,
                                             void *user_data)
{
    ESP_LOGI(LOG_TAG, "PeerConnectionState: %s",
             peer_connection_state_to_string(state));

    if (state == PEER_CONNECTION_DISCONNECTED ||
            state == PEER_CONNECTION_CLOSED) {
#ifndef LINUX_BUILD
        esp_restart();
#endif
    } else if (state == PEER_CONNECTION_CONNECTED) {
#ifndef LINUX_BUILD
        StackType_t *stack_memory = (StackType_t *)heap_caps_malloc(
                                        40000 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
        xTaskCreateStaticPinnedToCore(oai_send_audio_task, "audio_publisher", 40000,
                                      NULL, 4, stack_memory, &task_buffer, 0);
#endif
    } else if (state == PEER_CONNECTION_COMPLETED) {
        oai_completed_event();
    }
}

static void peer_loop_task(void *arg)
{
    while (1) {
        peer_connection_loop(peer_connection);
        vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
        // vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void oai_on_icecandidate_task(char *description, void *user_data)
{
    char local_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
    oai_http_request(description, local_buffer);
    peer_connection_set_remote_description(peer_connection, local_buffer);
}

extern void oai_audio_decode(uint8_t *data, size_t size, void* userdata);

void oai_webrtc()
{
    PeerConfiguration peer_connection_config = {
        .ice_servers = {},
        .audio_codec = CODEC_OPUS,
        .video_codec = CODEC_NONE,
        .datachannel = DATA_CHANNEL_NONE,
        .onaudiotrack = oai_audio_decode,
        .onvideotrack = NULL,
        .on_request_keyframe = NULL,
        .user_data = NULL,
    };

    peer_connection = peer_connection_create(&peer_connection_config);
    if (peer_connection == NULL) {
        ESP_LOGE(LOG_TAG, "Failed to create peer connection");
#ifndef LINUX_BUILD
        esp_restart();
#endif
    }

    peer_connection_oniceconnectionstatechange(peer_connection,
                                               oai_onconnectionstatechange_task);
    peer_connection_onicecandidate(peer_connection, oai_on_icecandidate_task);
    peer_connection_create_offer(peer_connection);

    // xTaskCreatePinnedToCore(&peer_loop_task, "peer connect task", 16 * 1024, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(&peer_loop_task, "peer connect task", 16 * 1024, NULL, 1, NULL, 1);
}
