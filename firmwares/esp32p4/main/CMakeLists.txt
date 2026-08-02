idf_component_register(
    SRCS
        "main.c"
        "usb_descriptors.c"
        "panel.c"
        "panel_task.c"
        "i2c_bus.c"
        "touch.c"
        "haptics.c"
        "ota.c"
        "auth.c"
        "face.c"
        "gen/pv_wire.pb.c"
    INCLUDE_DIRS "."
    REQUIRES
        nanopb           # protobuf runtime for the CTRL messages (gen/pv_wire.pb.*)
        esp_lcd
        driver
        espressif2022__esp_emote_expression  # idle facial-animation engine (face.c)
        esp_driver_i2c
        esp_driver_spi
        esp_driver_gpio
        esp_driver_ledc  # PWM backlight brightness (panel.c)
        esp_timer
        app_update       # esp_ota_ops (firmware update)
        esp_partition    # partition lookup for factory/recovery
        esp_app_format   # esp_app_get_description (version string)
        mbedtls          # SHA-256 over the streamed image + PSA ECDSA sign (auth.c)
        efuse            # eFuse ECDSA identity key discovery (auth.c)
        esp_hw_support   # core HW support (pulled in transitively by the drivers)
)

# Builds the factory recovery image (idf.py -DPV_RECOVERY=ON build), which skips
# the touch task and renders nothing but the OTA progress screen. Recovery is a
# BUILD-time property, not "whichever image runs from the `factory` partition" --
# see pv_ota_is_recovery() in ota.c -- so a plain `idf.py flash`, which targets
# factory, still behaves as a full app. This flag is also the hook for trimming
# the image further.
if(PV_RECOVERY)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PV_RECOVERY=1)
endif()
