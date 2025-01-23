#pragma once

#include "esp_gmf_err.h"
#include "esp_gmf_obj.h"
#include "afe_proc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    afe_proc_handle_t afe;
} esp_gmf_afe_proc_cfg_t;

esp_gmf_err_t esp_gmf_afe_proc_init(void *config, esp_gmf_obj_handle_t *handle);
esp_gmf_err_t esp_gmf_afe_proc_cast(void *config, esp_gmf_obj_handle_t handle);

#ifdef __cplusplus
}
#endif
