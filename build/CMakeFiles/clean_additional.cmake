# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "bootloader/bootloader.bin"
  "bootloader/bootloader.elf"
  "bootloader/bootloader.map"
  "config/sdkconfig.cmake"
  "config/sdkconfig.h"
  "esp-idf/esptool_py/flasher_args.json.in"
  "esp-idf/mbedtls/x509_crt_bundle"
  "flash_app_args"
  "flash_bootloader_args"
  "flash_project_args"
  "flasher_args.json"
  "image0.S"
  "image1.S"
  "image2.S"
  "image3.S"
  "image4.S"
  "image5.S"
  "image6.S"
  "image7.S"
  "image8.S"
  "image9.S"
  "ldgen_libraries"
  "ldgen_libraries.in"
  "person_detection.bin"
  "person_detection.map"
  "project_elf_src_esp32.c"
  "x509_crt_bundle.S"
  )
endif()
