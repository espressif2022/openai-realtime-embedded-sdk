#include "main.h"

#include <esp_event.h>
#include <esp_log.h>
#include <peer.h>

#ifndef LINUX_BUILD
#include "bsp/esp-bsp.h"
#include "nvs_flash.h"

static char *TAG = "main";
LV_IMG_DECLARE(OpenAI_logo);

esp_err_t app_ui_start(void) {
  /* Initialize display and LVGL */
  bsp_display_cfg_t custom_cfg = {
      .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
      .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
      .trans_size = BSP_LCD_H_RES * 10,  // in SRAM, DMA-capable
      .double_buffer = 0,
      .flags = {
          .buff_dma = false,
          .buff_spiram = true,
      }};
  custom_cfg.lvgl_port_cfg.task_stack = 1024 * 4;
  custom_cfg.lvgl_port_cfg.task_affinity = 1;
  bsp_display_start_with_config(&custom_cfg);

  /* Turn on display backlight */
  bsp_display_backlight_on();

  ESP_LOGI(TAG, "Display initialized");
  bsp_display_lock(0);

  lv_obj_t *obj_bg = lv_obj_create(lv_scr_act());
  lv_obj_set_size(obj_bg, LV_HOR_RES, LV_VER_RES);
  lv_obj_set_align(obj_bg, LV_ALIGN_CENTER);
  lv_obj_clear_flag(obj_bg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(obj_bg, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_width(obj_bg, 0, 0);

  lv_obj_t *ui_emoji = lv_img_create(obj_bg);
  lv_obj_set_width(ui_emoji, 240);
  lv_obj_set_height(ui_emoji, 240);
  lv_obj_set_align(ui_emoji, LV_ALIGN_CENTER);
  lv_img_set_src(ui_emoji, &OpenAI_logo);

  bsp_display_unlock();

  return ESP_OK;
}

extern "C" void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_event_loop_create_default());

  app_ui_start();

  peer_init();
  oai_init_audio_capture();
  oai_init_audio_decoder();
  oai_wifi();
  oai_webrtc();
}
#else
int main(void) {
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  peer_init();
  oai_webrtc();
}
#endif
