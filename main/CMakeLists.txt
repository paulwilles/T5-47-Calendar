idf_component_register(SRCS "button_input.c"
                            "display_layer.c"
                            "wifi_manager.c"
                            "helper_service.c"
                            "main.c"
                            "../vendor/LilyGo-EPD47/src/epd_driver.c"
                            "../vendor/LilyGo-EPD47/src/font.c"
                            "../vendor/LilyGo-EPD47/src/ed047tc1.c"
                            "../vendor/LilyGo-EPD47/src/i2s_data_bus.c"
                            "../vendor/LilyGo-EPD47/src/rmt_pulse.c"
                            "../vendor/LilyGo-EPD47/src/zlib/uncompr.c"
                            "../vendor/LilyGo-EPD47/src/zlib/inflate.c"
                            "../vendor/LilyGo-EPD47/src/zlib/inftrees.c"
                            "../vendor/LilyGo-EPD47/src/zlib/inffast.c"
                            "../vendor/LilyGo-EPD47/src/zlib/zutil.c"
                            "../vendor/LilyGo-EPD47/src/zlib/adler32.c"
                            "../vendor/LilyGo-EPD47/src/zlib/crc32.c"
                       PRIV_REQUIRES spi_flash nvs_flash esp_wifi esp_event esp_netif esp_http_client json lwip driver esp_timer esp_lcd
                       INCLUDE_DIRS "." "../vendor/LilyGo-EPD47/src")

# font.c calls uncompress() from the bundled zlib above; no external -lz needed.
