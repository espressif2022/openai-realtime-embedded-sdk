#pragma once

#include "esp_types.h"

#include "esp_afe_sr_iface.h"
#include "esp_err.h"

#include "model_path.h"
#include "ai_audio_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __afe *afe_proc_handle_t;
typedef void (*afe_proc_result_proc_t)(afe_fetch_result_t *result, void *user_ctx);

typedef struct {
    afe_config_t *afe_cfg; /*!< Configuration of AFE */
    int feed_core;         /*!< Core id of feed task */
    int feed_prio;         /*!< Priority of feed task*/
    int feed_stack;        /*!< Stack size of feed task */
    int fetch_core;        /*!< Core id of fetch task */
    int fetch_prio;        /*!< Priority of fetch task */
    int fetch_stack;       /*!< Stack size of fetch task */
    char *wn_wakeword;     /*!< Wake Word for WakeNet to load. This is useful when multiple Wake Words are selected in sdkconfig. Setting this to NULL will use the first found model. */
    ai_audio_data_read_cb_t read_cb;
    void *read_ctx;
    srmodel_list_t *models;
} afe_proc_cfg_t;

#define AFE_PROC_CFG_DEFAULT(_afe_cfg, _read_cb, _read_ctx, _models) \
    {                                                                \
        .afe_cfg = _afe_cfg,                                         \
        .feed_core = 0,                                              \
        .feed_prio = 5,                                              \
        .feed_stack = 5 * 1024,                                      \
        .fetch_core = 1,                                             \
        .fetch_prio = 5,                                             \
        .fetch_stack = 5 * 1024,                                     \
        .read_cb = _read_cb,                                         \
        .read_ctx = _read_ctx,                                       \
        .models = _models                                            \
    }

typedef struct {
    bool wakeup_enable;
    bool vad_enable;
    bool ns_enable;
    bool aec_enable;
    bool se_enable;
    bool vc_enable;
} afe_proc_state_t;

esp_err_t afe_proc_create(afe_proc_cfg_t *cfg, afe_proc_handle_t *handle);
esp_err_t afe_proc_destroy(afe_proc_handle_t handle);
esp_err_t afe_proc_result_cb_register(afe_proc_handle_t handle, afe_proc_result_proc_t proc, void *user_ctx);
esp_err_t afe_proc_result_cb_unregister(afe_proc_handle_t handle, afe_proc_result_proc_t proc);
esp_err_t afe_proc_get_state(afe_proc_handle_t handle, afe_proc_state_t *st);
esp_err_t afe_proc_suspend(afe_proc_handle_t handle, bool suspend);
esp_err_t afe_proc_func_ctrl(afe_proc_handle_t handle, uint32_t func, bool enable); // TODO: func -> alg
esp_err_t afe_proc_set_read_cb(afe_proc_handle_t handle, ai_audio_data_read_cb_t read, void *read_ctx);
esp_err_t afe_proc_get_chunk_size(afe_proc_handle_t handle, size_t *size);

#ifdef __cplusplus
}
#endif
