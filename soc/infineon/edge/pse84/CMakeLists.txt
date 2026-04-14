# Copyright (c) 2025 Infineon Technologies AG,
# or an affiliate of Infineon Technologies AG.
#
# SPDX-License-Identifier: Apache-2.0

include(${ZEPHYR_BASE}/soc/infineon/edge/pse84/pse84_metadata.cmake)

if(CONFIG_CPU_CORTEX_M33 AND CONFIG_TRUSTED_EXECUTION_SECURE)
  zephyr_sources(soc_pse84_m33_s.c)

  zephyr_sources(security_config/pse84_s_mpc.c)
  zephyr_sources(security_config/pse84_s_protection.c)
  zephyr_sources(security_config/pse84_s_system.c)
  zephyr_sources(security_config/pse84_s_sau.c)

  set(unsigned_hex ${ZEPHYR_BINARY_DIR}/${KERNEL_NAME}.hex)
  set(signed_hex ${ZEPHYR_BINARY_DIR}/${KERNEL_NAME}.signed.hex)

  pse84_add_metadata_secure_hex(${unsigned_hex} ${signed_hex})
elseif(CONFIG_CPU_CORTEX_M33)
  zephyr_sources(soc_pse84_m33_ns.c)
elseif(CONFIG_CPU_CORTEX_M55)
  zephyr_sources(soc_pse84_m55.c)
endif()

zephyr_include_directories(security_config)

# pse84_boot.c drives the Secure-side TrustZone partition setup, SMIF
# mode transition, and CM55 release from CPU_WAIT. It pulls in
# partition_ARMCM33.h which only exists for Secure builds, so gate it
# on Secure M33 or the pure M55 companion (which doesn't go through
# the TrustZone path but does reuse the SMIF helpers). In the M33NS
# build TF-M owns the equivalent bring-up; we skip this file.
if(NOT (CONFIG_CPU_CORTEX_M33 AND NOT CONFIG_TRUSTED_EXECUTION_SECURE))
  zephyr_sources(security_config/pse84_boot.c)
endif()

# Relocate pse84_boot.c to SRAM when Octal SMIF is selected. The startup hook
# calls Cy_SMIF_MemInit to transition SMIF0 from Quad to Octal mode; that
# transient makes XIP fetches unreliable, so the caller has to live in SRAM
# too (the PDL's cy_smif*.c files are already relocated via hal_infineon).
if(CONFIG_INFINEON_SMIF_OCTAL OR CONFIG_INFINEON_SMIF_PSRAM)
  if(CONFIG_CPU_CORTEX_M33 AND CONFIG_TRUSTED_EXECUTION_SECURE)
    set(ifx_pse84_sram_code M33SCODE)
  elseif(CONFIG_CPU_CORTEX_M33)
    set(ifx_pse84_sram_code M33CODE)
  else()
    set(ifx_pse84_sram_code ITCM)
  endif()
  zephyr_code_relocate(FILES security_config/pse84_boot.c
                       LOCATION ${ifx_pse84_sram_code})
endif()

zephyr_sources(mpu_regions.c)

zephyr_sources_ifdef(CONFIG_PM power.c)

zephyr_include_directories(.)

if(${ZEPHYR_TOOLCHAIN_VARIANT} STREQUAL "armclang")
  zephyr_library_sources(cy_syslib_ext.S)
endif()

# Add sections
if(CONFIG_CPU_CORTEX_M33)
  zephyr_linker_sources(SECTIONS shared_mem_sec.ld)
else()
  zephyr_linker_sources(SECTIONS shared_mem.ld)
endif()

set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/soc/infineon/edge/pse84/linker_exclude_syslib.ld CACHE INTERNAL "")

zephyr_linker_sources_ifdef(CONFIG_CPU_CORTEX_M33 RWDATA rwdata.ld)
zephyr_linker_sources_ifdef(CONFIG_CPU_CORTEX_M55 RWDATA rwdata.ld)

zephyr_linker_sources_ifdef(CONFIG_CPU_CORTEX_M55 ITCM_SECTION itcm_mem.ld)
zephyr_linker_sources_ifdef(CONFIG_CPU_CORTEX_M33 RAMFUNC_SECTION ramfunc_mem.ld)
