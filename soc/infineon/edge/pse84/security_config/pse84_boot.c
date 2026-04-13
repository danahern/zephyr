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
	/* Full SMIF teardown + reinit for CS0 octal, following the
	 * ifx-mcuboot-pse84 platform_memory_init() reference sequence.
	 * Must run entirely from SRAM (pse84_boot.c is code_relocate'd
	 * to M33SCODE when CONFIG_INFINEON_SMIF_OCTAL is set).
	 *
	 * The PSE84 SMIF has TWO independent cache layers:
	 *   A) SMIF_CACHE_BLOCK (V6 AXI cache) — Cy_SMIF_*_All_Cache()
	 *   B) Legacy fast/slow cache — Cy_SMIF_Cache{Enable,Invalidate}()
	 * Both must be flushed/invalidated before the transition.
	 */
	cy_stc_smif_context_t smif_ctx = {0};
	cy_stc_smif_mem_config_t const *memCfg = smif0BlockConfig.memConfig[0];
	static const cy_stc_smif_config_t smif_config = {
		.mode = (uint32_t)CY_SMIF_NORMAL,
		.deselectDelay = 7U,
		.rxClockSel = (uint32_t)CY_SMIF_SEL_INVERTED_FEEDBACK_CLK,
		.blockEvent = (uint32_t)CY_SMIF_BUS_ERROR,
	};
	unsigned int key;
	bool cache_was_on = false;

	key = irq_lock();

	/* 1. Clean + invalidate the V6 SMIF CACHE_BLOCK (AXI cache).
	 *    Writes back dirty lines, then discards all entries.
	 *    Must happen BEFORE disabling SMIF or touching device slots.
	 */
#if defined(SMIF0_CACHE_BLOCK_CACHEBLK_AHB_MPC0)
	{
		bool status = false;

		(void)Cy_SMIF_IsCacheEnabled(
			(SMIF_CACHE_BLOCK_Type *)SMIF0_CACHE_BLOCK, &status);
		if (status) {
			cache_was_on = true;
			Cy_SMIF_Clean_And_Invalidate_All_Cache(
				(SMIF_CACHE_BLOCK_Type *)SMIF0_CACHE_BLOCK);
		}
	}
#endif

	/* 2. Disable + invalidate legacy fast/slow caches. */
	Cy_SMIF_CacheDisable(SMIF0_CORE, CY_SMIF_CACHE_BOTH);
	Cy_SMIF_CachePrefetchingDisable(SMIF0_CORE, CY_SMIF_CACHE_BOTH);
	Cy_SMIF_CacheInvalidate(SMIF0_CORE, CY_SMIF_CACHE_BOTH);

	/* 3. Exit XIP mode. */
	Cy_SMIF_SetMode(SMIF0_CORE, CY_SMIF_NORMAL);

	/* 4. Wait for SMIF idle, then full disable + deinit.
	 *    DeInit zeros ALL device slots (CS0 + CS1), resets CTL/CTL2.
	 */
	while (Cy_SMIF_BusyCheck(SMIF0_CORE)) {
	}
	Cy_SMIF_Disable(SMIF0_CORE);
	Cy_SMIF_DeInit(SMIF0_CORE);

	/* 5. Re-init SMIF controller from scratch (no device slots yet). */
	(void)Cy_SMIF_Init(SMIF0_CORE, &smif_config, 10000U, &smif_ctx);

	/* 6. Set data select for CS0 (octal data lines). */
	Cy_SMIF_SetDataSelect(SMIF0_CORE, memCfg->slaveSelect,
			      memCfg->dataSelect);

	/* 7. Enable SMIF (starts DLL lock). */
	Cy_SMIF_Enable(SMIF0_CORE, &smif_ctx);

	/* 8. Init CS0 memory slot — writes XIP device registers since
	 *    XIP_MODE is still 0 from step 5 (Init sets NORMAL mode).
	 */
	(void)Cy_SMIF_MemInit(SMIF0_CORE, &smif0BlockConfig, &smif_ctx);

	/* 9. Send OPI DDR enable command to the S28HS01GT chip. */
	(void)Cy_SMIF_MemOctalEnable(SMIF0_CORE, memCfg,
				     CY_SMIF_DDR, &smif_ctx);

	/* 10. For DDR capture: set RX capture mode to xSPI/HyperBus
	 *     with DQS. Must disable SMIF first (CTL2 can't be written
	 *     while ENABLED + XIP_MODE are both 1).
	 */
	Cy_SMIF_Disable(SMIF0_CORE);
	Cy_SMIF_SetRxCaptureMode(SMIF0_CORE,
				 CY_SMIF_SEL_XSPI_HYPERBUS_WITH_DQS,
				 memCfg->slaveSelect);
	Cy_SMIF_Enable(SMIF0_CORE, &smif_ctx);

	/* 11. Invalidate legacy caches one more time, then switch to
	 *     XIP mode. The first XIP fetch after this reads CS0 in
	 *     OPI DDR mode.
	 */
	Cy_SMIF_CacheInvalidate(SMIF0_CORE, CY_SMIF_CACHE_BOTH);
	Cy_SMIF_SetMode(SMIF0_CORE, CY_SMIF_MEMORY);

	/* 12. Re-enable caches. */
	Cy_SMIF_CacheEnable(SMIF0_CORE, CY_SMIF_CACHE_BOTH);
	Cy_SMIF_CachePrefetchingEnable(SMIF0_CORE, CY_SMIF_CACHE_BOTH);

	irq_unlock(key);
}
#endif /* CONFIG_INFINEON_SMIF_OCTAL */

#if defined(CONFIG_SOC_PSE84_M55_ENABLE)
void ifx_pse84_cm55_startup(void)
{
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

#if defined(CONFIG_INFINEON_SMIF_OCTAL)
	/* Switch SMIF0 from CS1 (quad) to CS0 (octal). All XIP-resident
	 * helpers have completed above while SMIF was still on CS1.
	 */
	ifx_pse84_smif_octal_init();
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
