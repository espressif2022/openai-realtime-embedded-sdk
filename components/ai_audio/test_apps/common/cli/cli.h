
#ifndef __CLI_H__
#define __CLI_H__

#include "esp_console.h"
#include "esp_err.h"
#include "afe_proc.h"

esp_err_t cli_init(char *prompt);
esp_err_t cli_register_afe(afe_proc_handle_t afe);

#endif
