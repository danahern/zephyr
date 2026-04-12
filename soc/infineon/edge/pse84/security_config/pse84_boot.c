/*
 * Copyright (c) 2025 Infineon Technologies AG,
 * or an affiliate of Infineon Technologies AG.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pse84_boot.h"

#if defined(CONFIG_INFINEON_SMIF_OCTAL)
#include "mtb_serial_memory.h"
#include "mtb_hal_clock.h"

extern cy_stc_smif_block_config_t smif0BlockConfig;

static mtb_serial_memory_t ifx_pse84_octal_serial_memory_obj;
static cy_stc_smif_mem_context_t ifx_pse84_octal_smif_mem_context;
static cy_stc_smif_mem_info_t ifx_pse84_octal_smif_mem_info;

static const mtb_hal_hf_clock_t ifx_pse84_octal_flash_clock_ref = {
	.inst_num = 3U,
};

static const mtb_hal_clock_t ifx_pse84_octal_smif_clock = {
	.clock_ref = &ifx_pse84_octal_flash_clock_ref,
	.interface = &mtb_hal_clock_hf_interface,
};

/* Transition SMIF0 from Quad SDR (ROM default) to Octal DDR.
 *
 * Must run before cy_mpc_init so the M55 MPC can be programmed for the
 * octal 64 MB aperture. The whole call chain down through
 * mtb_serial_memory_setup -> Cy_SMIF_MemInit -> Cy_SMIF_MemOctalEnable
 * writes SMIF and chip registers; those writes briefly put SMIF into
 * MMIO mode where XIP fetches would return garbage. pse84_boot.c and
 * mtb_serial_memory.c are both relocated to SRAM to cover the window.
 */
static void ifx_pse84_smif_octal_init(void)
{
	(void)mtb_serial_memory_setup(&ifx_pse84_octal_serial_memory_obj,
				      MTB_SERIAL_MEMORY_CHIP_SELECT_0,
				      SMIF0_CORE,
				      &ifx_pse84_octal_smif_clock,
				      &ifx_pse84_octal_smif_mem_context,
				      &ifx_pse84_octal_smif_mem_info,
				      &smif0BlockConfig);
}
#endif /* CONFIG_INFINEON_SMIF_OCTAL */

#if defined(CONFIG_SOC_PSE84_M55_ENABLE)
void ifx_pse84_cm55_startup(void)
{
#if defined(CONFIG_INFINEON_SMIF_OCTAL)
	/* Switch SMIF0 to Octal DDR before touching the MPC — the MPC
	 * limit check uses CY_XIP_PORT0_SIZE which is 64 MB, but the
	 * physical aperture must already be the octal 64 MB before the
	 * M55 region can be programmed for that size.
	 */
	ifx_pse84_smif_octal_init();
#endif

	/* SAU Init */
	cy_sau_init();

	/* Setup System Control Block */
	SysCtrlBlk_Setup();
	/* Setup NS NVIC interrupts */
	NVIC_NS_Setup();

#if defined(__FPU_USED) && (__FPU_USED == 1U) && defined(TZ_FPU_NS_USAGE) && (TZ_FPU_NS_USAGE == 1U)
	/* FPU initialization */
	initFPU();
#endif

	/* Enable global interrupts */
	__enable_irq();

	/* Enables PD1 power domain */
	Cy_System_EnablePD1();

	/* Enables APP_MMIO_TCM memory for CM55 core */
	Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_CM55_TCM_512K_PERI_NR, CY_MMIO_CM55_TCM_512K_GROUP_NR,
				     CY_MMIO_CM55_TCM_512K_SLAVE_NR,
				     CY_MMIO_CM55_TCM_512K_CLK_HF_NR);

	Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_SMIF0_PERI_NR, CY_MMIO_SMIF0_GROUP_NR,
				     CY_MMIO_SMIF0_SLAVE_NR, CY_MMIO_SMIF0_CLK_HF_NR);

	Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_SMIF01_PERI_NR, CY_MMIO_SMIF01_GROUP_NR,
				     CY_MMIO_SMIF01_SLAVE_NR, CY_MMIO_SMIF01_CLK_HF_NR);

	/* Enable GFXSS peripheral group (GPU, DC, MIPI-DSI) for CM55 */
	Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_GFXSS_GPU_PERI_NR, CY_MMIO_GFXSS_GPU_GROUP_NR,
				     CY_MMIO_GFXSS_GPU_SLAVE_NR, CY_MMIO_GFXSS_GPU_CLK_HF_NR);
	Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_GFXSS_DC_PERI_NR, CY_MMIO_GFXSS_DC_GROUP_NR,
				     CY_MMIO_GFXSS_DC_SLAVE_NR, CY_MMIO_GFXSS_DC_CLK_HF_NR);
	Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_GFXSS_MIPIDSI_PERI_NR, CY_MMIO_GFXSS_MIPIDSI_GROUP_NR,
				     CY_MMIO_GFXSS_MIPIDSI_SLAVE_NR,
				     CY_MMIO_GFXSS_MIPIDSI_CLK_HF_NR);

	/* Enable SOCMEM */
	Cy_SysEnableSOCMEM(true);

	/* Configure MPC for NS */
	cy_mpc_init();

	/* Reduce deepsleep wakeup time in hardware */
	cy_pd_pdcm_clear_dependency(CY_PD_PDCM_APPCPUSS, CY_PD_PDCM_SYSCPU);

	/* Clear SYSCPU and APPCPU power domain dependency set by boot code */
	cy_pd_pdcm_clear_dependency(CY_PD_PDCM_APPCPU, CY_PD_PDCM_SYSCPU);

	/* Enable CM55 */
	Cy_SysEnableCM55(MXCM55, DT_REG_ADDR(DT_NODELABEL(m55_xip)), CM55_BOOT_WAIT_TIME_USEC);

	/* System Domain Idle Power Mode Configuration */
	Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);

	/* SoCMEM Idle Power Mode Configuration */
	Cy_SysPm_SetSOCMEMDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);

	/* Configure PPC for NS*/
	cy_ppc0_init();
	cy_ppc1_init();

#ifdef CONFIG_CORTEX_M_SYSTICK
	sys_clock_disable();
#endif

	for (;;) {
	}
}
#endif
