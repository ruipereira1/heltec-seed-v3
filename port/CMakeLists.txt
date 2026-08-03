# Camada especifica do ESP32-S3 / Heltec V3.
idf_component_register(
    SRCS "hsv3_rng.c"
         "hsv3_oled.c"
         "hsv3_font.c"
         "hsv3_buttons.c"
         "hsv3_radio_off.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer bootloader_support core)
