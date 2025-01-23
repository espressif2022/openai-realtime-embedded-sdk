#pragma once

#include "esp_err.h"

#include "afe_proc.h"
#include "ai_audio_common.h"
#include "model_path.h"

typedef struct __voice_cmd *vcmd_handle_t;


typedef struct {
    afe_proc_handle_t afe_handle;      /**/
    ai_audio_event_cb_t event_cb; /**/
    void *user_data;              /**/
    char *mn_language;
    srmodel_list_t *models;
} vcmd_cfg_t;

esp_err_t vcmd_create(vcmd_cfg_t *cfg, vcmd_handle_t *handle);
esp_err_t vcmd_det_begin(vcmd_handle_t handle);
esp_err_t vcmd_det_cancel(vcmd_handle_t handle);
esp_err_t vcmd_destroy(vcmd_handle_t handle);
