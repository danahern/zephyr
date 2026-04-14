# Copyright (c) 2025 Infineon Technologies AG,
# or an affiliate of Infineon Technologies AG.
#
# SPDX-License-Identifier: Apache-2.0

if(SB_CONFIG_BOARD_KIT_PSE84_EVAL_PSE846GPS2DBZC4A_M55)
  # Allow the main (M55) application to ship its own M33 companion by
  # dropping a `<APP_DIR>/../<APP_NAME>_m33/` directory next to itself
  # (CMakeLists.txt + prj.conf + src/). If present, use it; otherwise
  # fall back to the bare-minimum samples/basic/minimal that just
  # runs ifx_pse84_cm55_startup() via soc_late_init_hook and idles.
  #
  # Why this hook exists:
  #   Phase 4 of the pse84_assistant project needs the M33 to host the
  #   Zephyr Bluetooth stack (HCI UART → onboard CYW55513 AIROC). That
  #   requires real application code on M33, not the empty main() of
  #   samples/basic/minimal. Other PSE84 apps in the tree
  #   (pse84_cartoon_test, pse84_video_test, pse84_display,
  #   pse84_i2c_test, pse84_psram_test) don't drop a *_m33/ dir so
  #   they keep the old fallback behaviour.
  get_filename_component(_m55_app_name ${APP_DIR} NAME)
  set(_m33_companion_candidate "${APP_DIR}/../${_m55_app_name}_m33")
  if(EXISTS "${_m33_companion_candidate}/CMakeLists.txt")
    set(_m33_companion_source "${_m33_companion_candidate}")
    message(STATUS "kit_pse84_eval: using local M33 companion at "
                   "${_m33_companion_source}")
  else()
    set(_m33_companion_source "${ZEPHYR_BASE}/samples/basic/minimal")
    message(STATUS "kit_pse84_eval: using default M33 companion "
                   "(samples/basic/minimal)")
  endif()

  ExternalZephyrProject_Add(
    APPLICATION enable_cm55
    SOURCE_DIR ${_m33_companion_source}
    BOARD kit_pse84_eval/pse846gps2dbzc4a/m33
  )

  set_config_bool(enable_cm55 CONFIG_SOC_PSE84_M55_ENABLE 1)
endif()
