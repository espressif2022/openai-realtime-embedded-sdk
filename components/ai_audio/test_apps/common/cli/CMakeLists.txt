set(srcs
    "cli.c"
    "cli_sys.c"
    "cli_sr.c"
    )

idf_component_register(SRCS ${srcs}
    INCLUDE_DIRS "./"
    REQUIRES console driver ai_audio
    )
