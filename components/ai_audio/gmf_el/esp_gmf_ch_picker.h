#pragma once

#include "esp_gmf_err.h"
#include "esp_gmf_obj.h"

#include "ai_audio_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ch_choice[MAX_INPUT_CH * 4];
} ch_picker_cfg_t;

esp_gmf_err_t esp_gmf_ch_picker_init(ch_picker_cfg_t *cfg, esp_gmf_obj_handle_t *out_handle);
esp_gmf_err_t esp_gmf_ch_picker_cast(ch_picker_cfg_t *cfg, esp_gmf_obj_handle_t handle);

#ifdef __cplusplus
}
#endif
