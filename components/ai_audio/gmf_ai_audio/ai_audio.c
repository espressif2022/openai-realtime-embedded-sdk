#include <stdbool.h>
#include <stdint.h>

#include "ai_audio_common.h"
#include "esp_check.h"
#include "esp_err.h"

#include "esp_gmf_io.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_task.h"

#include "esp_gmf_afe_proc.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_ch_picker.h"
#include "esp_gmf_element.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_rate_cvt.h"

#include "afe_proc.h"
#include "ai_audio.h"
#include "voice_cmd.h"
#include "wakeup.h"

typedef struct _ai_audio {
    srmodel_list_t *models;
    afe_proc_handle_t afe_proc_handle;
    wakeup_handle_t wakeup_det_handle;
    vcmd_handle_t vcmd_det_handle;
    esp_gmf_pool_handle_t pool;
    esp_gmf_pipeline_handle_t pipe;
} ai_audio_t;

static const char *TAG = "AI_Audio";

static esp_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGD(TAG, "CB: RECV Pipeline EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    return 0;
}

static bool ai_audio_need_ch_picker(ai_audio_cfg_t *cfg)
{
    if (cfg->afe_cfg->pcm_config.total_ch_num == cfg->src_info.ch_num) {
        if (cfg->src_info.ch_arrangement[cfg->src_info.ch_num - 1] == CH_CONTENT_REF) {
            return false;
        }
    }
    return true;
}

static void ai_audio_check_ch_arrangement(uint8_t mic_num, uint8_t ref_num, uint32_t *arrangement, char *out)
{
    uint8_t mic_found = 0;
    uint8_t ref_found = 0;
    for (uint32_t i = 0; i < MAX_INPUT_CH; i++) {
        if (arrangement[i] == CH_CONTENT_MIC) {
            if (mic_found < mic_num) {
                out[mic_found * 2] = i + '0';
                out[mic_found * 2 + 1] = ',';
                mic_found++;
            }
        }
        if (arrangement[i] == CH_CONTENT_REF) {
            out[mic_num * 2 + (ref_found++)] = i + '0';
        }
    }
    ESP_LOGW(TAG, "ch arragement [ %s ]", out);
}

esp_err_t ai_audio_create(ai_audio_cfg_t *cfg, ai_audio_handle_t *handle)
{
    esp_err_t ret = ESP_OK;
    char *els[10] = { NULL };
    int8_t el_cnt = 0;

    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "AI Audio Create invalid cfg");
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "AI Audio Create invalid handle");
    ai_audio_t *ai_audio = esp_gmf_oal_calloc(1, sizeof(ai_audio_t));
    ESP_RETURN_ON_FALSE(ai_audio, ESP_ERR_NO_MEM, TAG, "AI Audio calloc failed");

    /* Create GMF pool */
    esp_gmf_pool_init(&ai_audio->pool);

    /* Rate cvt */
    if (cfg->src_info.sample_rate != cfg->afe_cfg->pcm_config.sample_rate) {
        esp_ae_rate_cvt_cfg_t rate_cvt_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
        rate_cvt_cfg.dest_rate = 16000;
        rate_cvt_cfg.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY;
        esp_gmf_element_handle_t rate_hd = NULL;
        esp_gmf_rate_cvt_init(&rate_cvt_cfg, &rate_hd);
        esp_gmf_pool_register_element(ai_audio->pool, rate_hd, NULL);
        els[el_cnt++] = "rate_cvt";
    }
    /* Bit cvt */
    if (cfg->src_info.bits != 16) {
        esp_ae_bit_cvt_cfg_t bit_cvt_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
        bit_cvt_cfg.dest_bits = 16;
        esp_gmf_element_handle_t bit_hd = NULL;
        esp_gmf_bit_cvt_init(&bit_cvt_cfg, &bit_hd);
        esp_gmf_pool_register_element(ai_audio->pool, bit_hd, NULL);
        els[el_cnt++] = "bit_cvt";
    }
    /* channel picker */
    if (ai_audio_need_ch_picker(cfg)) {
        esp_gmf_element_handle_t picker_handle = NULL;
        ch_picker_cfg_t chp_cfg = { 0 };
        ai_audio_check_ch_arrangement(cfg->afe_cfg->pcm_config.mic_num,
                                      cfg->afe_cfg->pcm_config.ref_num,
                                      cfg->src_info.ch_arrangement,
                                      chp_cfg.ch_choice);
        esp_gmf_ch_picker_init(&chp_cfg, &picker_handle);
        esp_gmf_pool_register_element(ai_audio->pool, picker_handle, NULL);
        els[el_cnt++] = "ch_picker";
    }

    /* AFE Base */
    ESP_GOTO_ON_FALSE(cfg->partition, ESP_ERR_INVALID_ARG, __err, TAG, "AFE Model partition NULL");
    ai_audio->models = esp_srmodel_init("model");
    ESP_GOTO_ON_FALSE(ai_audio->models, ESP_ERR_INVALID_ARG, __err, TAG, "AFE Models NOT found");
    char *wn_name = NULL;
    char *wn_name_2 = NULL;
    if (cfg->wn_wakeword == NULL) {
        for (int i = 0; i < ai_audio->models->num; i++) {
            if (strstr(ai_audio->models->model_name[i], ESP_WN_PREFIX) != NULL) {
                if (wn_name == NULL) {
                    wn_name = ai_audio->models->model_name[i];
                    ESP_LOGI(TAG, "The first wakenet model: %s\n", wn_name);
                } else if (wn_name_2 == NULL) {
                    wn_name_2 = ai_audio->models->model_name[i];
                    ESP_LOGI(TAG, "The second wakenet model: %s\n", wn_name_2);
                }
            }
        }
    } else {
        wn_name = esp_srmodel_filter(ai_audio->models, ESP_WN_PREFIX, cfg->wn_wakeword);
        ESP_GOTO_ON_FALSE(wn_name, ESP_ERR_INVALID_STATE, __err, TAG,
                          "Please enable wakenet model and select wake word (%s) by menuconfig!",
                          cfg->wn_wakeword);
    }

    ESP_GOTO_ON_FALSE(cfg->afe_cfg, ESP_ERR_INVALID_ARG, __err, TAG, "AFE CFG NULL");
    cfg->afe_cfg->wakenet_model_name = wn_name;
    cfg->afe_cfg->wakenet_model_name_2 = wn_name_2;
    afe_proc_cfg_t afe_proc_cfg = AFE_PROC_CFG_DEFAULT(cfg->afe_cfg, NULL, NULL, ai_audio->models);
    ESP_GOTO_ON_ERROR(afe_proc_create(&afe_proc_cfg, &ai_audio->afe_proc_handle), __err, TAG, "AFE Proc Create failed");

    /* AFE GMF element */
    esp_gmf_element_handle_t esp_gmf_afe_proc_handle = NULL;
    esp_gmf_afe_proc_cfg_t esp_gmf_ai_proc_cfg = {
        .afe = ai_audio->afe_proc_handle
    };
    esp_gmf_afe_proc_init(&esp_gmf_ai_proc_cfg, &esp_gmf_afe_proc_handle);
    esp_gmf_pool_register_element(ai_audio->pool, esp_gmf_afe_proc_handle, NULL);
    els[el_cnt++] = "afe_proc";

    /*  AFE proc plugin: Wakeup state machine */
    if (cfg->wakeup_det) {
        wakeup_cfg_t wake_cfg = {
            .wakeup_time = cfg->wakeup_time,
            .wakeup_end = cfg->wakeup_end,
            .vad_start = cfg->vad_start,
            .vad_off = cfg->vad_off,
            .afe_handle = ai_audio->afe_proc_handle,
            .event_cb = cfg->event_cb,
            .user_data = cfg->event_user_ctx,
        };
        wakeup_create(&wake_cfg, &ai_audio->wakeup_det_handle);
    }

    /*  AFE proc plugin: voice command detection */
    if (cfg->vocie_cmd_det) {
        vcmd_cfg_t vcmd_cfg = {
            .afe_handle = ai_audio->afe_proc_handle,
            .event_cb = cfg->event_cb,
            .user_data = cfg->event_user_ctx,
            .mn_language = cfg->mn_language,
            .models = ai_audio->models,
        };
        vcmd_create(&vcmd_cfg, &ai_audio->vcmd_det_handle);
    }

    /* pipeline */
    ESP_GMF_POOL_SHOW_ITEMS(ai_audio->pool);
    esp_gmf_pool_new_pipeline(ai_audio->pool,
                              NULL,
                              (const char **)els,
                              el_cnt,
                              NULL,
                              &ai_audio->pipe);
    esp_gmf_pipeline_reg_el_port(ai_audio->pipe, els[0], ESP_GMF_IO_DIR_READER, cfg->in_port);
    esp_gmf_pipeline_reg_el_port(ai_audio->pipe, els[el_cnt - 1], ESP_GMF_IO_DIR_WRITER, cfg->out_port);
    esp_gmf_info_sound_t info = {
        .sample_rates = cfg->src_info.sample_rate,
        .channels = cfg->src_info.ch_num,
        .bits = cfg->src_info.bits,
    };
    esp_gmf_pipeline_report_info(ai_audio->pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));

    esp_gmf_task_cfg_t task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    task_cfg.ctx = NULL;
    task_cfg.cb = NULL;
    task_cfg.thread.core = 1;
    esp_gmf_task_handle_t work_task = NULL;
    esp_gmf_task_init(&task_cfg, &work_task);

    esp_gmf_pipeline_bind_task(ai_audio->pipe, work_task);
    esp_gmf_pipeline_loading_jobs(ai_audio->pipe);
    esp_gmf_pipeline_set_event(ai_audio->pipe, _pipeline_event, NULL);
    esp_gmf_pipeline_run(ai_audio->pipe);

    *handle = ai_audio;

    return ret;
__err:
    return ret;
}

esp_err_t ai_audio_destroy(ai_audio_handle_t handle)
{
    return ESP_OK;
}

esp_err_t ai_audio_vcmd_det_begin(ai_audio_handle_t handle)
{
    if (handle->vcmd_det_handle) {
        return vcmd_det_begin(handle->vcmd_det_handle);
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t ai_audio_vcmd_det_cancel(ai_audio_handle_t handle)
{
    if (handle->vcmd_det_handle) {
        return vcmd_det_cancel(handle->vcmd_det_handle);
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t ai_audio_func_ctrl(ai_audio_handle_t handle, uint32_t func, bool enable)
{
    if (handle->afe_proc_handle) {
        return afe_proc_func_ctrl(handle->afe_proc_handle, func, enable);
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t ai_audio_suspend(ai_audio_handle_t handle, bool suspend)
{
    if (handle->afe_proc_handle) {
        ESP_RETURN_ON_ERROR(afe_proc_suspend(handle->afe_proc_handle, suspend),
                            TAG, "ai_audio suspend failed");
    }
    if (handle->pipe) {
        if (suspend) {
            ESP_RETURN_ON_ERROR(esp_gmf_pipeline_pause(handle->pipe),
                                TAG, "ai audio pipeline pause failed");
        } else {
            ESP_RETURN_ON_ERROR(esp_gmf_pipeline_resume(handle->pipe),
                                TAG, "ai audio pipeline resume failed");
        }
    }
    return ESP_OK;
}

esp_err_t ai_audio_get_wakeup_handle(ai_audio_handle_t handle, void **wakeup)
{
    if (handle->wakeup_det_handle) {
        *wakeup = handle->wakeup_det_handle;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t ai_audio_get_vcmd_handle(ai_audio_handle_t handle, void **vcmd)
{
    if (handle->vcmd_det_handle) {
        *vcmd = handle->vcmd_det_handle;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}