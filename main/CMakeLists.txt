idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES switch deep_sleep_manager wifi_setup log esp_netif
)