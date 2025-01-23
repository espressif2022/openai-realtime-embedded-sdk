#include <stdint.h>

#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_err.h"

#include "afe_proc.h"
#include "cli.h"

static afe_proc_handle_t afe_handle;

uint32_t save_flag = 0;

static struct {
    struct arg_int *suspend;
    struct arg_int *wakenet;
    struct arg_int *aec;
    struct arg_int *se;
    struct arg_int *debug;
    struct arg_end *end;
} afe_args;

static int cmd_do_afe(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&afe_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, afe_args.end, argv[0]);
        return 0;
    }
    if (afe_args.suspend->count != 0) {
        afe_proc_suspend(afe_handle, afe_args.suspend->ival[0]);
    }
    if (afe_args.wakenet->count != 0) {
        afe_proc_func_ctrl(afe_handle, AI_FUNC_WAKENET, afe_args.wakenet->ival[0]);
    }
    if (afe_args.aec->count != 0) {
        afe_proc_func_ctrl(afe_handle, AI_FUNC_AEC, afe_args.aec->ival[0]);
    }
    if (afe_args.se->count != 0) {
        afe_proc_func_ctrl(afe_handle, AI_FUNC_SE, afe_args.se->ival[0]);
    }
    if (afe_args.debug->count != 0) {
        save_flag = afe_args.debug->ival[0];
    }
    return 0;
}

esp_err_t cli_register_afe(afe_proc_handle_t afe)
{
    afe_handle = afe;

    afe_args.suspend = arg_int0("s", "suspend", "<suspend>", "suspend the afe process");
    afe_args.wakenet = arg_int0("w", "wakenet", "<wakenet>", "Wakenet ctrl");
    afe_args.aec = arg_int0("a", "aec", "<aec>", "AEC ctrl");
    afe_args.se = arg_int0("e", "se", "<se>", "SE ctrl");
    afe_args.debug = arg_int0("d", "debug", "<debug>", "debug ctrl");
    afe_args.end = arg_end(1);
    const esp_console_cmd_t afe_cmd = {
        .command = "afe",
        .help = "afe test cmd",
        .hint = NULL,
        .func = &cmd_do_afe,
        .argtable = &afe_args
    };

    return esp_console_cmd_register(&afe_cmd);
}
