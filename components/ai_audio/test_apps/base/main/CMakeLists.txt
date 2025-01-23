set(requires
    ai_audio
    console
    cli
    fatfs
    )

idf_component_register(SRCS main.c
                       REQUIRES ${requires})

