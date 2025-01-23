
idf_component_register(
    SRC_DIRS "./gmf_el" "./gmf_ai_audio" "./afe_proc"
    INCLUDE_DIRS "./include" "./gmf_el" "./gmf_ai_audio" "./afe_proc"
    REQUIRES esp-sr esp_timer esp_audio_effects gmf_audio
)
