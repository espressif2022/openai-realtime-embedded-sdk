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

#include <string.h>
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static bool monitor_run;

static void sys_monitor_task(void *para)
{
    while (monitor_run) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        if (monitor_run == false) {
            break;
        }
        ESP_GMF_MEM_SHOW("MONITOR");
        esp_gmf_oal_sys_get_real_time_stats(1000);
    }
    vTaskDelete(NULL);
}

void esp_gmf_app_sys_monitor_start(void)
{
    monitor_run = true;
    xTaskCreatePinnedToCore(sys_monitor_task, "sys_monitor_task", (4 * 1024), NULL, 1, NULL, 1);
}

void esp_gmf_app_sys_monitor_stop(void)
{
    monitor_run = false;
}
