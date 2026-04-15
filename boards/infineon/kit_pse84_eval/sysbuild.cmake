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
  # Prefer <APP_NAME>_m33ns (M33 non-secure, TF-M as the secure image)
  # so day-to-day iteration doesn't fight extended-boot + OEM-signed M33S
  # rollback. Fall back to <APP_NAME>_m33 (Secure) for apps that actually
  # need Secure access, and finally to samples/basic/minimal.
  get_filename_component(_m55_app_name ${APP_DIR} NAME)
  set(_m33ns_companion_candidate "${APP_DIR}/../${_m55_app_name}_m33ns")
  set(_m33_companion_candidate   "${APP_DIR}/../${_m55_app_name}_m33")
  if(EXISTS "${_m33ns_companion_candidate}/CMakeLists.txt")
    set(_m33_companion_source "${_m33ns_companion_candidate}")
    set(_m33_companion_board  kit_pse84_eval/pse846gps2dbzc4a/m33/ns)
    message(STATUS "kit_pse84_eval: using local M33NS companion at "
                   "${_m33_companion_source}")
  elseif(EXISTS "${_m33_companion_candidate}/CMakeLists.txt")
    set(_m33_companion_source "${_m33_companion_candidate}")
    set(_m33_companion_board  kit_pse84_eval/pse846gps2dbzc4a/m33)
    message(STATUS "kit_pse84_eval: using local M33 (Secure) companion at "
                   "${_m33_companion_source}")
  else()
    set(_m33_companion_source "${ZEPHYR_BASE}/samples/basic/minimal")
    set(_m33_companion_board  kit_pse84_eval/pse846gps2dbzc4a/m33)
    message(STATUS "kit_pse84_eval: using default M33 companion "
                   "(samples/basic/minimal)")
  endif()

  ExternalZephyrProject_Add(
    APPLICATION enable_cm55
    SOURCE_DIR ${_m33_companion_source}
    BOARD ${_m33_companion_board}
  )

  # Only force CM55 startup on the fallback (samples/basic/minimal)
  # companion — that image literally has nothing else to do and needs
  # ifx_pse84_cm55_startup() in soc_late_init_hook. For real app
  # companions loaded from RRAM, extended-boot already ran CM55
  # release and MPC init, so re-running it double-faults M33.
  if(_m33_companion_source STREQUAL "${ZEPHYR_BASE}/samples/basic/minimal")
    set_config_bool(enable_cm55 CONFIG_SOC_PSE84_M55_ENABLE 1)
  endif()
endif()
