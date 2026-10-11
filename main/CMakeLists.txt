idf_component_register(
    SRCS "main.c" "settings.c" "display.c" "net.c" "portal.c" "web.c" "console.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_http_server
                  console esp_driver_gpio esp_timer lwip
)
