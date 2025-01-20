/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "main.h"
#include <sys/time.h>
#include <inttypes.h>

#include <esp_event.h>
#include <esp_log.h>
#include <peer.h>

#include "nvs_flash.h"

#include "tcp_server.h"
#include "mmap_generate_wav.h"

static const char *TAG = "app_main";

mmap_assets_handle_t asset_lottie;

#define ARRAY_SIZE_OFFSET                   8   // Increase this if audio_sys_get_real_time_stats returns ESP_ERR_INVALID_SIZE
#define AUDIO_SYS_TASKS_ELAPSED_TIME_MS  1000   // Period of stats measurement

const char *task_state[] = {
    "Running",
    "Ready",
    "Blocked",
    "Suspended",
    "Deleted"
};

/** @brief
 * "Extr": Allocated task stack from psram, "Intr": Allocated task stack from internal
 */
const char *task_stack[] = {"Extr", "Intr"};

int audio_sys_get_tick_by_time_ms(int ms)
{
    return (ms / portTICK_PERIOD_MS);
}

int64_t audio_sys_get_time_ms(void)
{
    struct timeval tm;
    gettimeofday(&tm, NULL);
    int64_t milliseconds = tm.tv_sec * 1000LL + tm.tv_usec / 1000;
    return milliseconds;
}

esp_err_t audio_sys_get_real_time_stats(void)
{
#if (CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID && CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
    TaskStatus_t *start_array = NULL, *end_array = NULL;
    UBaseType_t start_array_size, end_array_size;
    uint32_t start_run_time, end_run_time;
    uint32_t task_elapsed_time, percentage_time;
    esp_err_t ret;

    // Allocate array to store current task states
    start_array_size = uxTaskGetNumberOfTasks()  + ARRAY_SIZE_OFFSET;
    start_array = (TaskStatus_t *)malloc(sizeof(TaskStatus_t) * start_array_size);
    // Get current task states
    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);
    if (start_array_size == 0) {
        ESP_LOGE(TAG, "Insufficient array size for uxTaskGetSystemState. Trying increasing ARRAY_SIZE_OFFSET");
        ret = ESP_FAIL;
        // goto exit;
    }

    vTaskDelay(pdMS_TO_TICKS(AUDIO_SYS_TASKS_ELAPSED_TIME_MS));

    // Allocate array to store tasks states post delay
    end_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    end_array = (TaskStatus_t *)malloc(sizeof(TaskStatus_t) * end_array_size);

    // Get post delay task states
    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);
    if (end_array_size == 0) {
        ESP_LOGE(TAG, "Insufficient array size for uxTaskGetSystemState. Trying increasing ARRAY_SIZE_OFFSET");
        ret = ESP_FAIL;
        // goto exit;
    }

    // Calculate total_elapsed_time in units of run time stats clock period.
    uint32_t total_elapsed_time = (end_run_time - start_run_time);
    if (total_elapsed_time == 0) {
        ESP_LOGE(TAG, "Delay duration too short. Trying increasing AUDIO_SYS_TASKS_ELAPSED_TIME_MS");
        ret = ESP_FAIL;
        // goto exit;
    }

    ESP_LOGE(TAG, "| Task              | Run Time    | Per | Prio | HWM       | State   | CoreId   | Stack ");

    // Match each task in start_array to those in the end_array
    for (int i = 0; i < start_array_size; i++) {
        for (int j = 0; j < end_array_size; j++) {
            if (start_array[i].xHandle == end_array[j].xHandle) {

                task_elapsed_time = end_array[j].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
                percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * portNUM_PROCESSORS);
                ESP_LOGE(TAG, "| %-17s | %-11d |%2d%%  | %-4u | %-9u | %-7s | %-8x | %s",
                         start_array[i].pcTaskName, (int)task_elapsed_time, (int)percentage_time, start_array[i].uxCurrentPriority,
                         (int)start_array[i].usStackHighWaterMark, task_state[(start_array[i].eCurrentState)],
                         start_array[i].xCoreID, task_stack[esp_ptr_internal(pxTaskGetStackStart(start_array[i].xHandle))]);

                // Mark that task have been matched by overwriting their handles
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
    }

    // Print unmatched tasks
    for (int i = 0; i < start_array_size; i++) {
        if (start_array[i].xHandle != NULL) {
            ESP_LOGE(TAG, "| %s | Deleted", start_array[i].pcTaskName);
        }
    }
    for (int i = 0; i < end_array_size; i++) {
        if (end_array[i].xHandle != NULL) {
            ESP_LOGE(TAG, "| %s | Created", end_array[i].pcTaskName);
        }
    }
    printf("\n");
    ret = ESP_OK;

// exit:    // Common return path
    if (start_array) {
        free(start_array);
        start_array = NULL;
    }
    if (end_array) {
        free(end_array);
        end_array = NULL;
    }
    return ret;
#else
    ESP_LOGW(TAG, "Please enable `CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID` and `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` in menuconfig");
    return ESP_FAIL;
#endif
}

static void monitor_task(void *pvParameters)
{
    while (1) {
        audio_sys_get_real_time_stats();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
            ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    const mmap_assets_config_t config_lottie = {
        .partition_label = "animation",
        .max_files = MMAP_WAV_FILES,
        .checksum = MMAP_WAV_CHECKSUM,
        .flags = {
            .mmap_enable = true,
            .app_bin_check = true,
        },
    };

    mmap_assets_new(&config_lottie, &asset_lottie);
    ESP_LOGI(TAG, "[%s]stored_files:%d", config_lottie.partition_label, mmap_assets_get_stored_files(asset_lottie));

    // xTaskCreatePinnedToCore(monitor_task, "monitor_task", 4096, NULL, 1, NULL, 0);

    oai_init_audio_capture();

    peer_init();
    oai_init_audio_decoder();
    oai_wifi();
    // tcp_server_wait_for_connection();
    oai_webrtc();
}
