/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Define log tag
#define TAG "Msg"

// Define function pointer type for message handlers
typedef void (*MessageHandlerFunc)(const cJSON *json, const char *message);

// Define a structure to map message types to handler functions
typedef struct {
  const char *type;
  MessageHandlerFunc handler;
} MessageHandlerMapping;

// Handle response.audio.delta: Processes incremental audio message
void handle_response_audio_delta(const cJSON *json, const char *message) {
  const cJSON *delta = cJSON_GetObjectItem(json, "delta");
  if (delta && cJSON_IsString(delta)) {
    static int printf_once = 0;
    if (printf_once < 1) {
      printf_once++;
      ESP_LOGI(TAG, "response.audio:\r\n%.100s", message);
      // ESP_LOGI(TAG, "response.audio:\r\n%s\r\n", delta->valuestring);
    } else {
      // ESP_LOGI(TAG, "response.audio");
    }
  }
}

// Handle response.audio.done: Marks the end of audio transmission
void handle_response_audio_done(const cJSON *json, const char *message) {
  // TODO: Add logic to handle completion of audio response
}

// Handle response.text.delta: Processes incremental text message
void handle_response_text_delta(const cJSON *json, const char *message) {
  // TODO: Add logic to process text delta messages
}

// Handle response.text.done: Marks the end of text response
void handle_response_text_done(const cJSON *json, const char *message) {
  // TODO: Add logic to handle completion of text response
}

// Handle response.audio_transcript.delta: Processes incremental audio
// transcript message
void handle_response_audio_transcript_delta(const cJSON *json,
                                            const char *message) {
  // ESP_LOGI(TAG, "response.audio_transcript.delta");
  cJSON *delta_item = cJSON_GetObjectItem(json, "delta");
  if (delta_item != NULL && cJSON_IsString(delta_item)) {
    // printf("delta: %s\n", delta_item->valuestring);
  } else {
    printf("delta field not found or is not a string\n");
  }
}

// Handle response.audio_transcript.done: Marks the end of audio transcript
void handle_response_audio_transcript_done(const cJSON *json,
                                           const char *message) {
  // TODO: Add logic to handle completion of audio transcript
}

// Handle response.content_part.added: Handles new content part addition
void handle_response_content_part_added(const cJSON *json,
                                        const char *message) {
  // TODO: Add logic to process added content parts
}

// Handle response.content_part.done: Marks the completion of a content part
void handle_response_content_part_done(const cJSON *json, const char *message) {
  // TODO: Add logic to handle completion of content parts
}

// Handle response.function_call_arguments.delta: Processes incremental function
// call arguments
void handle_response_function_call_arguments_delta(const cJSON *json,
                                                   const char *message) {
  // TODO: Add logic to process delta of function call arguments
}

// Handle response.function_call_arguments.done: Marks the end of function call
// arguments
void handle_response_function_call_arguments_done(const cJSON *json,
                                                  const char *message) {
  ESP_LOGI(TAG, "response.function_call_arguments.done");

  cJSON *arguments_item = cJSON_GetObjectItem(json, "arguments");
  if (!cJSON_IsString(arguments_item)) {
    printf("Failed to find arguments.\n");
    return;
  }

  const char *arguments_string = arguments_item->valuestring;
  cJSON *arguments = cJSON_Parse(arguments_string);
  if (arguments == NULL) {
    printf("Failed to parse arguments JSON.\n");
    return;
  }

  cJSON *light_state = cJSON_GetObjectItem(arguments, "LightState");
  if (light_state && cJSON_IsBool(light_state)) {
    printf("LightState: %s\n", cJSON_IsTrue(light_state) ? "true" : "false");
  } else {
    printf("Failed to parse LightState.\n");
  }

  cJSON *light_color = cJSON_GetObjectItem(arguments, "LightColor");
  if (light_color && cJSON_IsObject(light_color)) {
    cJSON *red = cJSON_GetObjectItem(light_color, "red");
    cJSON *green = cJSON_GetObjectItem(light_color, "green");
    cJSON *blue = cJSON_GetObjectItem(light_color, "blue");

    if (cJSON_IsNumber(red) && cJSON_IsNumber(green) && cJSON_IsNumber(blue)) {
      printf("LightColor: red=%d, green=%d, blue=%d\n", red->valueint,
             green->valueint, blue->valueint);
    } else {
      printf("Failed to parse LightColor values.\n");
    }
  } else {
    printf("Failed to parse LightColor.\n");
  }

  cJSON_Delete(arguments);
}

// Handle response.done: Marks the completion of a response
void handle_response_done(const cJSON *json, const char *message) {
  ESP_LOGI(TAG, "%s", __FUNCTION__);
  cJSON *response = cJSON_GetObjectItemCaseSensitive(json, "response");
  if (response != NULL) {
    cJSON *output = cJSON_GetObjectItemCaseSensitive(response, "output");
    if (cJSON_IsArray(output)) {
      cJSON *item = NULL;
      cJSON_ArrayForEach(item, output) {
        cJSON *item_type = cJSON_GetObjectItemCaseSensitive(item, "type");
        cJSON *status = cJSON_GetObjectItemCaseSensitive(item, "status");
        if (cJSON_IsString(item_type) && cJSON_IsString(status) &&
            strcmp(item_type->valuestring, "message") == 0 &&
            strcmp(status->valuestring, "completed") == 0) {
          printf("status: %s\n", status->valuestring);
          cJSON *role = cJSON_GetObjectItemCaseSensitive(item, "role");
          if (cJSON_IsString(role)) {
            printf("Role: %s\n", role->valuestring);
          }

          cJSON *content = cJSON_GetObjectItemCaseSensitive(item, "content");
          if (cJSON_IsArray(content)) {
            cJSON *content_item = NULL;
            cJSON_ArrayForEach(content_item, content) {
              cJSON *content_type =
                  cJSON_GetObjectItemCaseSensitive(content_item, "type");
              cJSON *text =
                  cJSON_GetObjectItemCaseSensitive(content_item, "text");

              if (cJSON_IsString(content_type) &&
                  strcmp(content_type->valuestring, "text") == 0 &&
                  cJSON_IsString(text)) {
                printf("Text:\r\n%s\n", text->valuestring);
              }
            }
          }
        }
      }
    }
  }
}

// Handle input_audio_buffer.speech_started: Detects when speech starts
void handle_input_audio_buffer_speech_started(const cJSON *json,
                                              const char *message) {
  ESP_LOGW(TAG, "%s", __FUNCTION__);
}

// Handle input_audio_buffer.speech_stopped: Detects when speech stops
void handle_input_audio_buffer_speech_stopped(const cJSON *json,
                                              const char *message) {
  ESP_LOGI(TAG, "%s", __FUNCTION__);
}

// Handle input_audio_buffer.committed: Handles committed input audio buffer
void handle_input_audio_buffer_committed(const cJSON *json,
                                         const char *message) {
  ESP_LOGI(TAG, "%s", __FUNCTION__);
}

// Handle input_audio_buffer.cleared: Handles cleared input audio buffer
void handle_input_audio_buffer_cleared(const cJSON *json, const char *message) {
  ESP_LOGI(TAG, "%s", __FUNCTION__);
}

// Handle response.output_item.added: Processes newly added output items
void handle_response_output_item_added(const cJSON *json, const char *message) {
  ESP_LOGI(TAG, "%s", __FUNCTION__);
}

// Handle response.output_item.done: Marks the completion of an output item
void handle_response_output_item_done(const cJSON *json, const char *message) {
  ESP_LOGI(TAG, "%s", __FUNCTION__);
  const cJSON *item = cJSON_GetObjectItem(json, "item");
  if (item) {
    const cJSON *role = cJSON_GetObjectItem(item, "role");
    const cJSON *status = cJSON_GetObjectItem(item, "status");
    const cJSON *content = cJSON_GetObjectItem(item, "content");

    if (role && cJSON_IsString(role)) {
      printf("  Role: %s\n", role->valuestring);
    }
    if (status && cJSON_IsString(status)) {
      printf("  Status: %s\n", status->valuestring);
    }
    if (content && cJSON_IsArray(content)) {
      cJSON *c = NULL;
      cJSON_ArrayForEach(c, content) {
        const cJSON *content_type = cJSON_GetObjectItem(c, "type");
        const cJSON *transcript = cJSON_GetObjectItem(c, "transcript");
        if (content_type && cJSON_IsString(content_type)) {
          printf("    Type: %s\n", content_type->valuestring);
          if (strcmp(content_type->valuestring, "audio") == 0 && transcript &&
              cJSON_IsString(transcript)) {
            printf("    Transcript: %s\n", transcript->valuestring);
            // ui_set_label(transcript->valuestring);
          } else {
            printf("    unknown\n");
          }
        }
      }
    }
  }
}

// Handle session.created: Handles creation of a new session
void handle_session_created(const cJSON *json, const char *message) {
  ESP_LOGI(TAG, "%s\r\n%s", __FUNCTION__, message);
}

// Handle session.updated: Handles session updates
void handle_session_updated(const cJSON *json, const char *message) {
  ESP_LOGI(TAG, "%s\r\n%s", __FUNCTION__, message);
}

// Handle conversation.item.created: Handles creation of a new conversation item
void handle_conversation_item_created(const cJSON *json, const char *message) {
  ESP_LOGI(TAG, "%s", __FUNCTION__);
}

// Handle response.created: Handles creation of a new response
void handle_response_created(const cJSON *json, const char *message) {
  ESP_LOGI(TAG, "%s", __FUNCTION__);
}

// Handle rate_limits.updated: Updates rate limits information
void handle_rate_limits_updated(const cJSON *json, const char *message) {
  ESP_LOGI(TAG, "%s", __FUNCTION__);
}

// Handle unknown message types: Default fallback handler
void handle_unknown_message(const cJSON *json, const char *message) {
  ESP_LOGW(TAG, "unknown type: %s", message);
}

// Handle response.error: Handles error messages from the response
void handle_response_error(const cJSON *json, const char *message) {
  ESP_LOGE(TAG, "on error: %s\r\n", message);
}

static MessageHandlerMapping message_handlers[] = {
    // Response-related handlers
    {"response.audio.delta", handle_response_audio_delta},
    {"response.audio.done", handle_response_audio_done},
    {"response.text.delta", handle_response_text_delta},
    {"response.text.done", handle_response_text_done},
    {"response.audio_transcript.delta", handle_response_audio_transcript_delta},
    {"response.audio_transcript.done", handle_response_audio_transcript_done},
    {"response.content_part.added", handle_response_content_part_added},
    {"response.content_part.done", handle_response_content_part_done},
    {"response.function_call_arguments.delta",
     handle_response_function_call_arguments_delta},
    {"response.function_call_arguments.done",
     handle_response_function_call_arguments_done},
    {"response.done", handle_response_done},

    // Input audio buffer handlers
    {"input_audio_buffer.speech_started",
     handle_input_audio_buffer_speech_started},
    {"input_audio_buffer.speech_stopped",
     handle_input_audio_buffer_speech_stopped},
    {"input_audio_buffer.committed", handle_input_audio_buffer_committed},
    {"input_audio_buffer.cleared", handle_input_audio_buffer_cleared},

    // Response output item handlers
    {"response.output_item.added", handle_response_output_item_added},
    {"response.output_item.done", handle_response_output_item_done},

    // Session-related handlers
    {"session.created", handle_session_created},
    {"session.updated", handle_session_updated},

    // Conversation and rate limit handlers
    {"conversation.item.created", handle_conversation_item_created},
    {"response.created", handle_response_created},
    {"rate_limits.updated", handle_rate_limits_updated},

    // Default and unknown handlers
    {"error", handle_response_error},
    // Default handler for unmatched types
    {NULL, handle_unknown_message}};

void on_message_parse(const char *message) {
  cJSON *json = cJSON_Parse(message);
  if (!json) {
    ESP_LOGE(TAG, "JSON parse error: %s", cJSON_GetErrorPtr());
    return;
  }

  const cJSON *type = cJSON_GetObjectItem(json, "type");
  if (!type || !cJSON_IsString(type)) {
    ESP_LOGE(TAG, "Invalid or missing type field: %s", message);
    cJSON_Delete(json);
    return;
  }

  for (MessageHandlerMapping *handler = message_handlers; handler->type != NULL;
       handler++) {
    if (strcmp(type->valuestring, handler->type) == 0) {
      handler->handler(json, message);
      cJSON_Delete(json);
      return;
    }
  }

  handle_unknown_message(json, message);
  cJSON_Delete(json);
}

typedef struct {
  char *msg;
  size_t len;
  void *userdata;
  uint16_t sid;
} data_channel_msg_args_t;

void parse_data_channel_message(void *priv_data) {
  data_channel_msg_args_t *args = (data_channel_msg_args_t *)priv_data;
  char *msg = args->msg;

  on_message_parse(msg);

  // Clean up the allocated memory
  heap_caps_free(args->msg);
  heap_caps_free(args);
}

void oai_on_message(char *msg, size_t len, void *userdata, uint16_t sid) {
  static char *partial_msg = NULL;  // Buffer for incomplete JSON messages
  static size_t partial_len = 0;    // Length of the current partial message

  // Check if this is a continuation of an incomplete message
  if (partial_msg) {
    // Reallocate memory to append new data
    char *new_msg =
        (char *)heap_caps_malloc(partial_len + len + 1, MALLOC_CAP_SPIRAM);
    if (!new_msg) {
      ESP_LOGE(TAG, "Failed to allocate memory for message continuation");
      heap_caps_free(partial_msg);
      partial_msg = NULL;
      partial_len = 0;
      return;
    }
    memcpy(new_msg, partial_msg, partial_len);
    memcpy(new_msg + partial_len, msg, len);
    new_msg[partial_len + len] = '\0';

    heap_caps_free(partial_msg);
    partial_msg = new_msg;
    partial_len += len;
  } else {
    // Allocate new buffer for the first part of the message
    partial_msg = (char *)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM);
    if (!partial_msg) {
      ESP_LOGE(TAG, "Failed to allocate memory for message");
      return;
    }
    memcpy(partial_msg, msg, len);
    partial_msg[len] = '\0';
    partial_len = len;
  }

  // Check if the message ends with '}'
  if (partial_msg[partial_len - 1] != '}') {
    ESP_LOGW(TAG, "Message incomplete, waiting for the next packet...");
    return;
  }

  // Allocate the args on the heap
  data_channel_msg_args_t *args = (data_channel_msg_args_t *)heap_caps_malloc(
      sizeof(data_channel_msg_args_t), MALLOC_CAP_SPIRAM);
  if (!args) {
    ESP_LOGE(TAG, "Failed to allocate memory for args");
    heap_caps_free(partial_msg);
    partial_msg = NULL;
    partial_len = 0;
    return;
  }

  // Prepare the args
  args->msg = partial_msg;
  args->len = partial_len;
  args->userdata = userdata;
  args->sid = sid;

  // Add the task to the work queue
  parse_data_channel_message((void *)args);

  // Reset the partial message buffer
  partial_msg = NULL;
  partial_len = 0;
}
