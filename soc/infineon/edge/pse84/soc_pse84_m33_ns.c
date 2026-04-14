/*
 * Copyright (c) 2026 Infineon Technologies AG,
 * or an affiliate of Infineon Technologies AG.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Infineon PSOC EDGE84 soc.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#define PSE84_CPU_FREQ_HZ DT_PROP(DT_PATH(cpus, cpu_0), clock_frequency)


void soc_early_init_hook(void)
{
	/* Initialize SystemCoreClock variable. */
	SystemCoreClockSetup(PSE84_CPU_FREQ_HZ, PSE84_CPU_FREQ_HZ);
}

void soc_late_init_hook(void)
{
	/*
	 * In the NS build TF-M already handled TrustZone / PPC / MPC /
	 * SMIF setup and released CM55 (unless IFX_HALT_CM55=1 in the
	 * TF-M config). Nothing for Zephyr NS to do here.
	 */
}
