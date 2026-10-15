# Audited ES8311-only source subset. No registry/network access at build time.
function(verify_codec_source relative expected)
  file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/upstream/${relative}" actual)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Pinned codec source changed: ${relative}")
  endif()
endfunction()
include("${CMAKE_CURRENT_LIST_DIR}/upstream-pins.cmake")
idf_component_register(
  SRCS upstream/esp_codec_dev.c upstream/esp_codec_dev_if.c upstream/esp_codec_dev_vol.c
       upstream/audio_codec_sw_vol.c upstream/platform/esp_codec_dev_os.c upstream/device/es8311/es8311.c
  INCLUDE_DIRS upstream/include upstream/interface upstream/device/include
  PRIV_INCLUDE_DIRS upstream upstream/device/priv_include
  REQUIRES freertos esp_common log)
