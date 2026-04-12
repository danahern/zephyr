# Copyright (c) 2025 Infineon Technologies AG,
# or an affiliate of Infineon Technologies AG.
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_SOC_FAMILY_INFINEON_EDGE)
  set(zephyr_ifx_cycfg_dir ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/zephyr-ifx-cycfg/kit_pse84_eval)

  zephyr_include_directories(${zephyr_ifx_cycfg_dir})
  zephyr_include_directories(${zephyr_ifx_cycfg_dir}/device_headers)

  # Pick the S25FS128S Quad (stock) or S28HS01GT Octal DDR memslot based
  # on CONFIG_INFINEON_SMIF_OCTAL. Only one of the two files is compiled
  # because both define the same smif0BlockConfig / smif0MemConfigs /
  # smif1BlockConfig symbols that drivers/flash/flash_infineon_serial_memory_qspi.c
  # links against.
  if(CONFIG_INFINEON_SMIF_OCTAL)
    zephyr_library_sources(${zephyr_ifx_cycfg_dir}/cycfg_qspi_memslot_octal.c)
  else()
    zephyr_library_sources(${zephyr_ifx_cycfg_dir}/cycfg_qspi_memslot.c)
  endif()

  zephyr_library_sources(${zephyr_ifx_cycfg_dir}/ifx_cycfg_init.c)
  zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_AUTANALOG_SAR_ADC
    ${zephyr_ifx_cycfg_dir}/ifx_autanalog.c)
endif()

if(CONFIG_SOC_SERIES_PSC3)
  set(zephyr_ifx_cycfg_dir ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/zephyr-ifx-cycfg/soc_psc3)

  zephyr_include_directories(${zephyr_ifx_cycfg_dir})
  zephyr_library_sources_ifdef(CONFIG_USE_INFINEON_HPPASS_SAR_ADC ${zephyr_ifx_cycfg_dir}/ifx_hppass_analog.c)
  zephyr_library_sources(${zephyr_ifx_cycfg_dir}/ifx_cycfg_init.c)
endif()
