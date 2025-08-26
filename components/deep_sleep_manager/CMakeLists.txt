idf_component_register(
    SRCS "deep_sleep_manager.c"
    INCLUDE_DIRS "."
    REQUIRES "driver" "esp_system" "freertos" "switch"
    PRIV_REQUIRES "soc"
)