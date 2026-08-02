cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32p4-pico-view)


set(FACE_ASSETS_BIN "${CMAKE_BINARY_DIR}/face_assets.bin")
build_speaker_assets_bin("assets" "360_360" "${FACE_ASSETS_BIN}" ${CONFIG_MMAP_FILE_NAME_LENGTH})
esptool_py_flash_to_partition(flash "assets" "${FACE_ASSETS_BIN}")
