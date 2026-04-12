/*
 * Copyright (c) 2025 Infineon Technologies AG,
 * or an affiliate of Infineon Technologies AG.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pse84_s_protection.h"

const cy_stc_mpc_rot_cfg_t m33s_mpc_cfg[] = {
	{
		.pc = CY_MPC_PC_2,
		.secure = CY_MPC_SECURE,
		.access = CY_MPC_ACCESS_RW,
	},
	{
		.pc = CY_MPC_PC_7,
		.secure = CY_MPC_SECURE,
		.access = CY_MPC_ACCESS_RW,
	},
};

const cy_stc_mpc_rot_cfg_t m33_mpc_cfg[] = {
	{
		.pc = CY_MPC_PC_2,
		.secure = CY_MPC_NON_SECURE,
		.access = CY_MPC_ACCESS_RW,
	},
	{
		.pc = CY_MPC_PC_5,
		.secure = CY_MPC_NON_SECURE,
		.access = CY_MPC_ACCESS_RW,
	},
	{
		.pc = CY_MPC_PC_7,
		.secure = CY_MPC_NON_SECURE,
		.access = CY_MPC_ACCESS_RW,
	},
};

const cy_stc_mpc_rot_cfg_t m55_mpc_cfg[] = {
	{
		.pc = CY_MPC_PC_2,
		.secure = CY_MPC_NON_SECURE,
		.access = CY_MPC_ACCESS_RW,
	},
	{
		.pc = CY_MPC_PC_6,
		.secure = CY_MPC_NON_SECURE,
		.access = CY_MPC_ACCESS_RW,
	},
	{
		.pc = CY_MPC_PC_7,
		.secure = CY_MPC_NON_SECURE,
		.access = CY_MPC_ACCESS_RW,
	},
};

const cy_stc_mpc_rot_cfg_t m33nsc_mpc_cfg[] = {
	{
		.pc = CY_MPC_PC_2,
		.secure = CY_MPC_SECURE,
		.access = CY_MPC_ACCESS_R,
	},
	{
		.pc = CY_MPC_PC_5,
		.secure = CY_MPC_SECURE,
		.access = CY_MPC_ACCESS_R,
	},
	{
		.pc = CY_MPC_PC_6,
		.secure = CY_MPC_SECURE,
		.access = CY_MPC_ACCESS_R,
	},
	{
		.pc = CY_MPC_PC_7,
		.secure = CY_MPC_SECURE,
		.access = CY_MPC_ACCESS_R,
	},
};

const cy_stc_mpc_rot_cfg_t m33_m55_mpc_cfg[] = {
	{
		.pc = CY_MPC_PC_2,
		.secure = CY_MPC_NON_SECURE,
		.access = CY_MPC_ACCESS_RW,
	},
	{
		.pc = CY_MPC_PC_5,
		.secure = CY_MPC_NON_SECURE,
		.access = CY_MPC_ACCESS_RW,
	},
	{
		.pc = CY_MPC_PC_6,
		.secure = CY_MPC_NON_SECURE,
		.access = CY_MPC_ACCESS_RW,
	},
	{
		.pc = CY_MPC_PC_7,
		.secure = CY_MPC_NON_SECURE,
		.access = CY_MPC_ACCESS_RW,
	},
};

const cy_stc_mpc_regions_t m33s_mpc_regions[] = {
	{
		.base = (MPC_Type *)RRAMC0_MPC0,
		.offset = 0x00011000,
		.size = 0x0004A000,
	},
	{
		.base = (MPC_Type *)SMIF0_CACHE_BLOCK_CACHEBLK_AHB_MPC0,
		.offset = 0x00100000,
		.size = 0x00200000,
	},
	{
		.base = (MPC_Type *)SMIF0_CORE_AXI_MPC0,
		.offset = 0x00100000,
		.size = 0x00200000,
	},
	{
		.base = (MPC_Type *)RAMC0_MPC0,
		.offset = 0x00001000,
		.size = 0x00057000,
	},
};

const cy_stc_mpc_regions_t m33_mpc_regions[] = {
	{
		.base = (MPC_Type *)SMIF0_CACHE_BLOCK_CACHEBLK_AHB_MPC0,
		.offset = 0x00300000,
		.size = 0x00240000,
	},
	{
		.base = (MPC_Type *)SMIF0_CORE_AXI_MPC0,
		.offset = 0x00300000,
		.size = 0x00240000,
	},
	{
		.base = (MPC_Type *)RAMC0_MPC0,
		.offset = 0x00058000,
		.size = 0x00028000,
	},
	{
		.base = (MPC_Type *)RAMC1_MPC0,
		.offset = 0x00000000,
		.size = 0x0003D000,
	},
};

/* Octal SMIF aperture is 64 MB (CY_XIP_PORT0_SIZE). The MPC has an
 * off-by-one bug at the exact boundary (offset + size == maxSize
 * triggers a cascading vector-table HardFault); cap at 58 MB to stay
 * comfortably under the wall while still covering the useful range
 * (m33s_xip/m33_xip/m55_xip + a lot of growing room).
 *
 * The default Quad SDR aperture is only 16 MB so the plain 11 MB
 * region is still correct in that configuration.
 */
const cy_stc_mpc_regions_t m55_mpc_regions[] = {
	{
		.base = (MPC_Type *)SMIF0_CACHE_BLOCK_CACHEBLK_AHB_MPC0,
		.offset = 0x00500000,
#if defined(CONFIG_INFINEON_SMIF_OCTAL)
		.size = 0x03A00000,
#else
		.size = 0x00B00000,
#endif
	},
	{
		.base = (MPC_Type *)SMIF0_CORE_AXI_MPC0,
		.offset = 0x00500000,
#if defined(CONFIG_INFINEON_SMIF_OCTAL)
		.size = 0x03A00000,
#else
		.size = 0x00B00000,
#endif
	},
	{
		.base = (MPC_Type *)SOCMEM_SRAM_MPC0,
		.offset = 0x00000000,
		.size = 0x00040000,
	},
};

const cy_stc_mpc_regions_t m33nsc_mpc_regions[] = {0};

const cy_stc_mpc_regions_t m33_m55_mpc_regions[] = {
	{
		.base = (MPC_Type *)RRAMC0_MPC0,
		.offset = 0x0005B000,
		.size = 0x00008000,
	},
	{
		.base = (MPC_Type *)SOCMEM_SRAM_MPC0,
		.offset = 0x00040000,
		.size = 0x004C0000,
	},
	{
		.base = (MPC_Type *)RAMC1_MPC0,
		.offset = 0x0003D000,
		.size = 0x00043000,
	},
};

const cy_stc_mpc_resp_cfg_t cy_response_mpcs[] = {
	{
		.base = (MPC_Type *)SOCMEM_SRAM_MPC0,
		.response = CY_MPC_BUS_ERR,
	},
	{
		.base = (MPC_Type *)RAMC0_MPC0,
		.response = CY_MPC_BUS_ERR,
	},
	{
		.base = (MPC_Type *)RAMC1_MPC0,
		.response = CY_MPC_BUS_ERR,
	},
	{
		.base = (MPC_Type *)SMIF0_CACHE_BLOCK_CACHEBLK_AHB_MPC0,
		.response = CY_MPC_BUS_ERR,
	},
	{
		.base = (MPC_Type *)SMIF0_CORE_AXI_MPC0,
		.response = CY_MPC_BUS_ERR,
	},
};

const size_t cy_response_mpcs_count = sizeof(cy_response_mpcs) / sizeof(cy_stc_mpc_resp_cfg_t);

const cy_stc_mpc_unified_t unified_mpc_domains[] = {
	{
		.regions = m33s_mpc_regions,
		.region_count = sizeof(m33s_mpc_regions) / sizeof(cy_stc_mpc_regions_t),
		.cfg = m33s_mpc_cfg,
		.cfg_count = sizeof(m33s_mpc_cfg) / sizeof(cy_stc_mpc_rot_cfg_t),
	},
	{
		.regions = m33_mpc_regions,
		.region_count = sizeof(m33_mpc_regions) / sizeof(cy_stc_mpc_regions_t),
		.cfg = m33_mpc_cfg,
		.cfg_count = sizeof(m33_mpc_cfg) / sizeof(cy_stc_mpc_rot_cfg_t),
	},
	{
		.regions = m55_mpc_regions,
		.region_count = sizeof(m55_mpc_regions) / sizeof(cy_stc_mpc_regions_t),
		.cfg = m55_mpc_cfg,
		.cfg_count = sizeof(m55_mpc_cfg) / sizeof(cy_stc_mpc_rot_cfg_t),
	},
	{
		.regions = m33nsc_mpc_regions,
		.region_count = sizeof(m33nsc_mpc_regions) / sizeof(cy_stc_mpc_regions_t),
		.cfg = m33nsc_mpc_cfg,
		.cfg_count = sizeof(m33nsc_mpc_cfg) / sizeof(cy_stc_mpc_rot_cfg_t),
	},
	{
		.regions = m33_m55_mpc_regions,
		.region_count = sizeof(m33_m55_mpc_regions) / sizeof(cy_stc_mpc_regions_t),
		.cfg = m33_m55_mpc_cfg,
		.cfg_count = sizeof(m33_m55_mpc_cfg) / sizeof(cy_stc_mpc_rot_cfg_t),
	},
};

const size_t unified_mpc_domains_count = sizeof(unified_mpc_domains) / sizeof(cy_stc_mpc_unified_t);

const cy_stc_ppc_attribute_t cycfg_unused_ppc_cfg = {
	.pcMask = 0xFF,
	.secAttribute = CY_PPC_NON_SECURE,
	.privAttribute = CY_PPC_NONPRIV,
};

cy_rslt_t cy_ppc_unsecure_init(PPC_Type *base, cy_en_prot_region_t start, cy_en_prot_region_t end)
{
	cy_rslt_t ret = Cy_Ppc_InitPpc(base, CY_PPC_BUS_ERR);
	if (ret != CY_PPC_SUCCESS) {
		return ret;
	}

	/* Iterate ALL regions in [start..end]. The enum has gaps (invalid values
	 * between valid ones); Cy_Ppc_ConfigAttrib returns BAD_PARAM for those.
	 * Don't bail out on per-region errors — continue so that valid regions
	 * later in the range still get configured (e.g. GFXSS at 0x9B-0x9D in PERI1).
	 */
	for (cy_en_prot_region_t region = start; region <= end; region++) {
		/* Not sure why yet, but writing to these two cause a fault. Skip for now... */
		if (region == PROT_PERI1_PPC1_PPC_PPC_SECURE ||
		    region == PROT_PERI1_PPC1_PPC_PPC_NONSECURE) {
			continue;
		}

		cy_rslt_t r = Cy_Ppc_ConfigAttrib(base, region, &cycfg_unused_ppc_cfg);

		if (r == CY_RSLT_SUCCESS) {
			(void)Cy_Ppc_SetPcMask(base, region, PPC_PC_MASK_ALL_ACCESS);
		}
		/* Ignore BAD_PARAM for invalid regions in enum gaps */
	}
	return CY_PPC_SUCCESS;
}

cy_rslt_t cy_ppc0_init(void)
{
	return cy_ppc_unsecure_init(PPC0, PROT_PERI0_START, PROT_PERI0_END);
}

cy_rslt_t cy_ppc1_init(void)
{
	return cy_ppc_unsecure_init(PPC1, PROT_PERI1_START, PROT_PERI1_END);
}
