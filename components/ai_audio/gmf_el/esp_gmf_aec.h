#pragma once

#include "esp_gmf_err.h"
#include "esp_gmf_obj.h"

typedef struct {
    int frame_len;
    int nch;
    int mode;
} esp_gmf_aec_cfg_t;

esp_gmf_err_t esp_gmf_aec_init(esp_gmf_aec_cfg_t *cfg, esp_gmf_obj_handle_t *out_handle);
esp_gmf_err_t esp_gmf_aec_cast(esp_gmf_aec_cfg_t *cfg, esp_gmf_obj_handle_t handle);
