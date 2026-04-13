/*
 * Copyright (c) 2025 Infineon Technologies AG,
 * or an affiliate of Infineon Technologies AG.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pse84_boot.h"

#if defined(CONFIG_INFINEON_SMIF_OCTAL) || defined(CONFIG_INFINEON_SMIF_PSRAM)
#include "cy_smif.h"
#include "cy_smif_memslot.h"
#include "mtb_serial_memory.h"
#include "mtb_hal_clock.h"
#include "cy_mpc.h"
#endif

#if defined(CONFIG_INFINEON_SMIF_OCTAL)
extern cy_stc_smif_block_config_t smif0BlockConfig;
#endif

#if defined(CONFIG_INFINEON_SMIF_PSRAM)
/* State for mtb_serial_memory_setup. Kept static so the lib's async paths
 * can dereference them for the life of the program.
 */
static mtb_serial_memory_t ifx_pse84_psram_obj;
static cy_stc_smif_mem_context_t ifx_pse84_psram_mem_context;
static cy_stc_smif_mem_info_t ifx_pse84_psram_mem_info;

/* CLK_HF4 drives SMIF1 peripheral clock on PSE84. mtb_serial_memory_setup
 * divides this to decide SFDP-probe frequency; a NULL clock would trigger
 * an ASSERT inside the library.
 */
static const mtb_hal_hf_clock_t ifx_pse84_psram_clock_ref = {
	.inst_num = 4U, /* CLK_HF4 */
};
static const mtb_hal_clock_t ifx_pse84_psram_clock = {
	.clock_ref = &ifx_pse84_psram_clock_ref,
	.interface = &mtb_hal_clock_hf_interface,
};

/* Hand-built HyperBUS memslot for the S70KS1283. The Zephyr cycfg's
 * smif1BlockConfig is either NULL (non-OCTAL stub) or OPI DDR (OCTAL
 * variant, wrong protocol for this HyperBUS-boot-mode chip), so we build
 * the block_config locally and feed it to mtb_serial_memory_setup.
 */
static cy_stc_smif_hbmem_device_config_t ifx_pse84_psram_hb_cfg = {
	.xipReadCmd = CY_SMIF_HB_READ_CONTINUOUS_BURST,
	.xipWriteCmd = CY_SMIF_HB_WRITE_CONTINUOUS_BURST,
	.hbDevType = CY_SMIF_HB_SRAM,
	.memSize = CY_SMIF_DEVICE_16M_BYTE,
	.dummyCycles = 6U,
};
static cy_stc_smif_mem_config_t ifx_pse84_psram_memCfg = {
	.slaveSelect = CY_SMIF_SLAVE_SELECT_2,
	.flags = CY_SMIF_FLAG_HYPERBUS_DEVICE |
		 CY_SMIF_FLAG_MEMORY_MAPPED | CY_SMIF_FLAG_WR_EN,
	.dataSelect = CY_SMIF_DATA_SEL0,
	.baseAddress = 0x64000000U,
	.memMappedSize = 0x1000000U,
	.hbdeviceCfg = &ifx_pse84_psram_hb_cfg,
};
static cy_stc_smif_mem_config_t *ifx_pse84_psram_memConfigs[1] = {
	&ifx_pse84_psram_memCfg,
};
static const cy_stc_smif_block_config_t ifx_pse84_psram_blockCfg = {
	.memCount = 1U,
	.memConfig = ifx_pse84_psram_memConfigs,
	.majorVersion = CY_SMIF_DRV_VERSION_MAJOR,
	.minorVersion = CY_SMIF_DRV_VERSION_MINOR,
};

/* Initialize SMIF1 for the S70KS1283 HyperRAM via mtb_serial_memory_setup.
 *
 * Prior iteration (commit bc3a58bfdb9) called Cy_SMIF_HyperBus_InitDevice
 * directly — that hung M33 inside some downstream PDL call, leaving SMIF1
 * stuck-busy and the chip SWD-unresponsive. The Infineon reference example
 * (mtb-example-psoc-edge-psram-xip) uses mtb_serial_memory_setup, which
 * internally handles RX capture mode switching, SFDP probe or HyperBUS
 * direct init, and clock-aware dummy cycle programming. Routing through it
 * removes whatever step in the manual sequence was wedging SMIF1.
 *
 * After this, 0x64000000 (NS) / 0x74000000 (Sec) is read/write memory-mapped
 * to 16 MB of PSRAM at HyperBUS DDR speed.
 */
static void ifx_pse84_psram_init(void)
{
	cy_stc_smif_context_t smif_ctx = {0};
	static const cy_stc_smif_config_t smif1_config = {
		.mode = (uint32_t)CY_SMIF_NORMAL,
		.deselectDelay = 7U,
		.rxClockSel = (uint32_t)CY_SMIF_SEL_INVERTED_FEEDBACK_CLK,
		.blockEvent = (uint32_t)CY_SMIF_BUS_ERROR,
	};

	/* SMIF1 is not used by ROM, so no teardown needed — just init. */
	Cy_SMIF_Disable(SMIF1_CORE);
	(void)Cy_SMIF_Init(SMIF1_CORE, &smif1_config, 10000U, &smif_ctx);
	Cy_SMIF_Enable(SMIF1_CORE, &smif_ctx);

	/* mtb_serial_memory_setup does:
	 *  - Cy_SMIF_SetDataSelect for all memConfig[] entries
	 *  - If DETECT_SFDP flag: temp-switch to NORMAL_SPI, run SFDP probe,
	 *    program XIP registers based on probe result.
	 *  - If HYPERBUS_DEVICE flag (no SFDP): call the HyperBus init path
	 *    and set RX capture mode to XSPI_HYPERBUS_WITH_DQS.
	 * We take the HYPERBUS_DEVICE path (ifx_pse84_psram_memCfg above).
	 */
	(void)mtb_serial_memory_setup(&ifx_pse84_psram_obj,
				      MTB_SERIAL_MEMORY_CHIP_SELECT_2,
				      SMIF1_CORE,
				      &ifx_pse84_psram_clock,
				      &ifx_pse84_psram_mem_context,
				      &ifx_pse84_psram_mem_info,
				      &ifx_pse84_psram_blockCfg);

	/* Cypress PDL hardcodes RD/WR_DUMMY_CTL.PRESENT2 = 1 (fixed latency).
	 * S70KS1283 boots in *variable* initial latency mode — PSE84 arch ref
	 * manual §31.4.x says PRESENT2 must be 2 for the XIP block to emit the
	 * variable-latency marker. Patch after setup returns.
	 */
	{
		SMIF_DEVICE_Type volatile *dev =
			Cy_SMIF_GetDeviceBySlot(SMIF1_CORE,
						CY_SMIF_SLAVE_SELECT_2);
		uint32_t rd = SMIF_DEVICE_RD_DUMMY_CTL(dev);
		uint32_t wr = SMIF_DEVICE_WR_DUMMY_CTL(dev);
		rd = (rd & ~SMIF_CORE_DEVICE_RD_DUMMY_CTL_PRESENT2_Msk) |
		     (2UL << SMIF_CORE_DEVICE_RD_DUMMY_CTL_PRESENT2_Pos);
		wr = (wr & ~SMIF_CORE_DEVICE_WR_DUMMY_CTL_PRESENT2_Msk) |
		     (2UL << SMIF_CORE_DEVICE_WR_DUMMY_CTL_PRESENT2_Pos);
		SMIF_DEVICE_RD_DUMMY_CTL(dev) = rd;
		SMIF_DEVICE_WR_DUMMY_CTL(dev) = wr;
	}

	/* Put SMIF1 into XIP (memory-mapped) mode so 0x64000000 reads/writes
	 * translate to HyperBUS transactions.
	 */
	Cy_SMIF_SetMode(SMIF1_CORE, CY_SMIF_MEMORY);

	/* Program the SMIF1 cache block region 0 for the 16 MB HyperRAM
	 * aperture AFTER XIP is live. CM55 M-AXI reads traverse the cache
	 * block; if no region covers the transaction address, reads are
	 * forwarded non-cacheable and HyperBUS passthrough bus-errors.
	 * Registers per PSE84 register ref manual §SMIF_CACHE_BLOCK.MMIO.
	 * Using write-through read/write-allocate — write-back caching of
	 * HyperRAM isn't necessary and keeps the coherency story simpler.
	 */
	{
		static const cy_stc_smif_cache_config_t psram_cache_cfg = {
			.enabled = true,
			.cache_retention_on = true,
			.cache_region_0 = {
				.enabled = true,
				.start_address = 0x64000000U,
				.end_address = 0x64000000U + 0x01000000U,
				.cache_attributes = CY_SMIF_CACHEABLE_WT_RWA,
			},
		};
		(void)Cy_SMIF_InitCache(
			(SMIF_CACHE_BLOCK_Type *)SMIF1_CACHE_BLOCK,
			&psram_cache_cfg);
	}

	/* Configure MPC for SMIF1 PSRAM for all protection contexts that
	 * need access (CM33S=2, CM33NS=2/5, CM55=5, Secure=7). Can't do
	 * this in the static m55_mpc_regions array because cy_mpc_init
	 * runs before SMIF1 is powered — the MPC register block lives
	 * inside SMIF1 and bus-faults until SMIF1 is clocked.
	 */
	{
		/* Wide-open: NS RW for every PC (0-7), plus secure RW.
		 * Narrow later once we know which PC M55 actually runs in.
		 */
		static const cy_stc_mpc_rot_cfg_t psram_mpc_cfgs[] = {
			{ .pc = CY_MPC_PC_0, .secure = CY_MPC_NON_SECURE, .access = CY_MPC_ACCESS_RW },
			{ .pc = CY_MPC_PC_1, .secure = CY_MPC_NON_SECURE, .access = CY_MPC_ACCESS_RW },
			{ .pc = CY_MPC_PC_2, .secure = CY_MPC_NON_SECURE, .access = CY_MPC_ACCESS_RW },
			{ .pc = CY_MPC_PC_3, .secure = CY_MPC_NON_SECURE, .access = CY_MPC_ACCESS_RW },
			{ .pc = CY_MPC_PC_4, .secure = CY_MPC_NON_SECURE, .access = CY_MPC_ACCESS_RW },
			{ .pc = CY_MPC_PC_5, .secure = CY_MPC_NON_SECURE, .access = CY_MPC_ACCESS_RW },
			{ .pc = CY_MPC_PC_6, .secure = CY_MPC_NON_SECURE, .access = CY_MPC_ACCESS_RW },
			{ .pc = CY_MPC_PC_7, .secure = CY_MPC_NON_SECURE, .access = CY_MPC_ACCESS_RW },
		};
		for (uint32_t i = 0;
		     i < sizeof(psram_mpc_cfgs) / sizeof(psram_mpc_cfgs[0]);
		     i++) {
			(void)Cy_Mpc_ConfigRotMpcStruct(
				(MPC_Type *)SMIF1_CACHE_BLOCK_CACHEBLK_AHB_MPC0,
				0x00000000U, 0x01000000U, &psram_mpc_cfgs[i]);
			(void)Cy_Mpc_ConfigRotMpcStruct(
				(MPC_Type *)SMIF1_CORE_AXI_MPC0,
				0x00000000U, 0x01000000U, &psram_mpc_cfgs[i]);
		}
	}

	/* M33 Secure self-test: write/read canary at 0x74000000 (PSRAM secure
	 * alias). If this faults, M33 handler will print via UART and we'll
	 * see it. If it succeeds, init just returns and M55 gets to run.
	 * Result isn't shared with M55 — we rely on the observation of what
	 * happens here (UART crash dump vs. clean return).
	 */
	{
		volatile uint32_t *psram_s = (volatile uint32_t *)0x74000000U;
		(void)psram_s[0];
		psram_s[0] = 0xCAFEBABEU;
		(void)psram_s[0];
		__DSB();
	}
}
#endif /* CONFIG_INFINEON_SMIF_PSRAM */

#if defined(CONFIG_INFINEON_SMIF_OCTAL)
/* State for mtb_serial_memory_setup. Kept static so the library's async
 * paths can dereference them for the life of the program.
 */
static mtb_serial_memory_t ifx_pse84_octal_obj;
static cy_stc_smif_mem_context_t ifx_pse84_octal_mem_context;
static cy_stc_smif_mem_info_t ifx_pse84_octal_mem_info;

/* CLK_HF3 drives SMIF0 peripheral clock on PSE84. mtb_serial_memory_setup
 * divides this to decide SFDP-probe frequency; a NULL clock would trigger
 * an ASSERT inside the library.
 */
static const mtb_hal_hf_clock_t ifx_pse84_octal_clock_ref = {
	.inst_num = 3U,
};
static const mtb_hal_clock_t ifx_pse84_octal_clock = {
	.clock_ref = &ifx_pse84_octal_clock_ref,
	.interface = &mtb_hal_clock_hf_interface,
};

/* Transition SMIF0 from Quad SDR (ROM default on CS1) to Octal DDR on CS0.
 *
 * Uses mtb_serial_memory_setup (same path as PSRAM / the serial-flash
 * training-manual example). The library's internal flow handles
 * Cy_SMIF_MemInit, MemOctalEnable, and RX capture mode switching in the
 * right order. The earlier hand-rolled teardown sequence (CacheDisable +
 * SetMode(NORMAL) + BusyCheck spin + DeInit + re-init ...) that replaced
 * this simpler call in commit 5a7501e57a4 turned out to hang in the
 * BusyCheck spin — route through the library for the same reason PSRAM
 * got fixed by `mtb_serial_memory_setup`.
 *
 * pse84_boot.c and cy_smif.c (and callees) are both relocated to SRAM,
 * so the whole call chain executes without XIP fetches even while SMIF0
 * is briefly reconfigured.
 */
static void ifx_pse84_smif_octal_init(void)
{
	(void)mtb_serial_memory_setup(&ifx_pse84_octal_obj,
				      MTB_SERIAL_MEMORY_CHIP_SELECT_0,
				      SMIF0_CORE,
				      &ifx_pse84_octal_clock,
				      &ifx_pse84_octal_mem_context,
				      &ifx_pse84_octal_mem_info,
				      &smif0BlockConfig);
}
#endif /* CONFIG_INFINEON_SMIF_OCTAL */

#if defined(CONFIG_SOC_PSE84_M55_ENABLE)
void ifx_pse84_cm55_startup(void)
{
	/* Note: CONFIG_INFINEON_SMIF_OCTAL=y no longer triggers a runtime
	 * SMIF0 Quad→Octal transition here. That transition is performed by
	 * extended boot at reset, driven by the OEM policy
	 * (smif_chip_select: 0, smif_data_width: 8). See
	 * docs/pse84_octal_policy_enablement.md and arch ref §17.2.4.2.2.
	 * OCTAL=y still controls: cycfg_qspi_memslot selection, M55 MPC
	 * region size (58 MB vs 11 MB in pse84_s_protection.c), and
	 * partition overlay availability. We keep the static
	 * ifx_pse84_smif_octal_init function in case a future flow needs
	 * it — it's unreferenced when OCTAL=y in this config.
	 */

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

#if defined(CONFIG_INFINEON_SMIF_PSRAM)
	/* Initialize SMIF1 for the 16 MB S70KS1283 HyperRAM.
	 * SMIF1 is independent from SMIF0 — no teardown needed.
	 * After this, 0x64000000 is read/write XIP to 16 MB PSRAM.
	 */
	ifx_pse84_psram_init();
#endif

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
