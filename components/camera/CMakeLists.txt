set(camera_requires "board")
set(camera_priv "")

if(BOARD_HAS_CAMERA OR IDF_TARGET STREQUAL "esp32s3")
    list(APPEND camera_requires "esp32-camera")
    list(APPEND camera_priv "driver" "espressif__esp_io_expander")
endif()

idf_component_register(
    SRCS "camera.c"
    INCLUDE_DIRS "include"
    REQUIRES ${camera_requires}
    PRIV_REQUIRES ${camera_priv}
)
