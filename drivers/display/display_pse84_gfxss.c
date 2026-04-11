/*
 * Copyright (c) 2026 Dan Ahern
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Infineon PSE84 GFXSS (Graphics Subsystem) display controller driver.
 *
 * Wraps the Cypress HAL Cy_GFXSS_Init call and exposes the standard Zephyr
 * display_driver_api. Operates the DSI link in VIDEO mode: after init the
 * DC autonomously and continuously scans the framebuffer. display_write()
 * copies pixels into the FB and cleans the D-cache so the DC picks them up
 * on the next vsync.
 *
 * IMPORTANT: never call Cy_GFXSS_Transfer_Frame() in video mode — it's a
 * DBI command-mode helper that rewrites GCREGFRAMEBUFFERSTRIDE, resets
 * GCREGFRAMEBUFFERSIZE to a per-slice height, and increments
 * GCREGFRAMEBUFFERADDRESS inside a DBI push loop. Doing that on a video-
 * mode setup produces fine vertical stripe/noise artifacts.
 */

#define DT_DRV_COMPAT infineon_pse84_gfxss

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/dt-bindings/display/panel.h>

#include "cy_pdl.h"
#include "cy_graphics.h"

LOG_MODULE_REGISTER(display_pse84_gfxss, CONFIG_DISPLAY_LOG_LEVEL);

struct pse84_gfxss_config {
	GFXSS_Type *base;
	uint32_t fb_addr;
	uint32_t fb_size;
	uint16_t width;         /* visible panel width in pixels */
	uint16_t height;
	uint16_t stride_pixels; /* framebuffer row stride in pixels (>= width) */
	uint16_t pixel_clock_khz;
	uint16_t hsync_width;
	uint16_t hfp;
	uint16_t hbp;
	uint16_t vsync_width;
	uint16_t vfp;
	uint16_t vbp;
	uint16_t per_lane_mbps;
	uint8_t num_lanes;
	const struct device *panel;
	uint32_t panel_init_delay_ms;
};

struct pse84_gfxss_data {
	cy_stc_gfx_config_t gfx_config;
	cy_stc_gfx_dc_config_t dc_config;
	cy_stc_gfx_layer_config_t graphics_layer;
	cy_stc_gfx_layer_config_t overlay0_layer;
	cy_stc_gfx_layer_config_t overlay1_layer;
	cy_stc_gfx_gpu_cfg_t gpu_config;
	cy_stc_mipidsi_display_params_t dsi_params;
	cy_stc_mipidsi_config_t dsi_config;
	cy_stc_gfx_context_t gfx_context;
	bool blanking;
};

static void pse84_gfxss_build_hal_config(const struct device *dev)
{
	const struct pse84_gfxss_config *config = dev->config;
	struct pse84_gfxss_data *data = dev->data;

	data->graphics_layer = (cy_stc_gfx_layer_config_t){
		.layer_type = GFX_LAYER_GRAPHICS,
		.buffer_address = (gctADDRESS *)config->fb_addr,
		.uv_buffer_address = (gctADDRESS *)config->fb_addr,
		.input_format_type = vivRGB565,
		.tiling_type = vivLINEAR,
		.pos_x = 0,
		.pos_y = 0,
		/* Layer 'width' is the DMA fetch stride; use stride_pixels so
		 * rows are 128-byte aligned even when the panel's visible
		 * width is not.
		 */
		.width = config->stride_pixels,
		.height = config->height,
		.zorder = 0,
		.layer_enable = true,
		.visibility = true,
	};

	/* Overlays exist in the HAL config but are disabled. */
	data->overlay0_layer = (cy_stc_gfx_layer_config_t){
		.layer_type = GFX_LAYER_OVERLAY0,
		.buffer_address = (gctADDRESS *)config->fb_addr,
		.uv_buffer_address = (gctADDRESS *)config->fb_addr,
		.input_format_type = vivRGB565,
		.tiling_type = vivLINEAR,
		.width = 1,
		.height = 1,
		.layer_enable = false,
		.visibility = false,
	};
	data->overlay1_layer = (cy_stc_gfx_layer_config_t){
		.layer_type = GFX_LAYER_OVERLAY1,
		.buffer_address = (gctADDRESS *)config->fb_addr,
		.uv_buffer_address = (gctADDRESS *)config->fb_addr,
		.input_format_type = vivRGB565,
		.tiling_type = vivLINEAR,
		.width = 1,
		.height = 1,
		.layer_enable = false,
		.visibility = false,
	};

	data->dc_config = (cy_stc_gfx_dc_config_t){
		.gfx_layer_config = &data->graphics_layer,
		.ovl0_layer_config = &data->overlay0_layer,
		.ovl1_layer_config = &data->overlay1_layer,
		.display_type = GFX_DISP_TYPE_DSI_DPI,
		.display_format = vivD24,
		.display_size = vivDISPLAY_CUSTOMIZED,
		/* DC output width must equal the DSI hactive below. */
		.display_width = config->stride_pixels,
		.display_height = config->height,
	};

	data->gpu_config = (cy_stc_gfx_gpu_cfg_t){.enable = true};

	data->dsi_params = (cy_stc_mipidsi_display_params_t){
		.pixel_clock = config->pixel_clock_khz,
		/* hdisplay must match the layer/DC stride so every pixel the
		 * DC emits has a corresponding byte in the framebuffer.
		 */
		.hdisplay = config->stride_pixels,
		.hsync_width = config->hsync_width,
		.hfp = config->hfp,
		.hbp = config->hbp,
		.vdisplay = config->height,
		.vsync_width = config->vsync_width,
		.vfp = config->vfp,
		.vbp = config->vbp,
		.polarity_flags = 0,
	};

	data->dsi_config = (cy_stc_mipidsi_config_t){
		.virtual_ch = 0,
		.num_of_lanes = config->num_lanes,
		.per_lane_mbps = config->per_lane_mbps,
		.dpi_fmt = CY_MIPIDSI_FMT_RGB888,
		.dsi_mode = DSI_VIDEO_MODE,
		.max_phy_clk = 2500000000UL,
		.mode_flags = VID_MODE_TYPE_BURST | ENABLE_LOW_POWER_CMD | ENABLE_LOW_POWER,
		.display_params = &data->dsi_params,
	};

	data->gfx_config = (cy_stc_gfx_config_t){
		.dc_cfg = &data->dc_config,
		.gpu_cfg = &data->gpu_config,
		.mipi_dsi_cfg = &data->dsi_config,
		.display_update_type = GFX_DOUBLE_BUFFER,
		/* clockHz is the DC input clock (CLK_HF1 = 400 MHz). The HAL
		 * uses it to compute the DC pixel clock divider. Read it at
		 * runtime so we pick up whatever the board DT set.
		 */
		.clockHz = Cy_SysClk_ClkHfGetFrequency(1U),
	};
}

static int pse84_gfxss_init_panel(const struct device *dev)
{
	const struct pse84_gfxss_config *config = dev->config;
	int ret;

	if (config->panel == NULL) {
		return 0;
	}

	/* The panel device is marked zephyr,deferred-init in DT so Zephyr
	 * does not run its POST_KERNEL init — the panel's on-board MCU
	 * only responds over I2C after the DSI link is already active.
	 * Wait for DSI stabilization, then trigger the panel init via the
	 * standard device_init() API.
	 */
	k_msleep(config->panel_init_delay_ms);

	ret = device_init(config->panel);
	if (ret == -EALREADY) {
		/* Panel wasn't marked deferred — its init already ran at
		 * POST_KERNEL and presumably succeeded. Not fatal.
		 */
		return 0;
	}
	return ret;
}

static int pse84_gfxss_init(const struct device *dev)
{
	const struct pse84_gfxss_config *config = dev->config;
	struct pse84_gfxss_data *data = dev->data;
	cy_en_gfx_status_t rc;

	LOG_INF("Initializing PSE84 GFXSS: %ux%u RGB565 (stride %u) FB@%p (%u bytes)",
		config->width, config->height, config->stride_pixels,
		(void *)(uintptr_t)config->fb_addr, config->fb_size);
	LOG_INF("clocks: HF1=%u HF10=%u HF12=%u",
		(unsigned int)Cy_SysClk_ClkHfGetFrequency(1U),
		(unsigned int)Cy_SysClk_ClkHfGetFrequency(10U),
		(unsigned int)Cy_SysClk_ClkHfGetFrequency(12U));

	/* The driver depends on the board DT having enabled the DSI D-PHY
	 * PLL reference clock chain (typically IFX_EXT → clk_pathN →
	 * clk_hf12). Bail out rather than crash deep in the HAL if HF12 is
	 * not running — the board overlay needs fixing.
	 */
	if (Cy_SysClk_ClkHfGetFrequency(12U) == 0U) {
		LOG_ERR("CLK_HF12 is not running; check that the board DT has "
			"enabled a clock source on clk_hf12 (the DSI D-PHY "
			"PLL reference)");
		return -EIO;
	}

	/* Step 1: build the HAL config structs from the device-tree props. */
	pse84_gfxss_build_hal_config(dev);

	/* Step 2: call the Cypress HAL. */
	rc = Cy_GFXSS_Init(config->base, &data->gfx_config, &data->gfx_context);
	if (rc != CY_GFX_SUCCESS) {
		LOG_ERR("Cy_GFXSS_Init failed: %d", (int)rc);
		return -EIO;
	}

	/* Step 3: set the framebuffer pointer. In video mode the DC scans
	 * autonomously from here on, so no Transfer_Frame call is needed.
	 */
	(void)Cy_GFXSS_Set_FrameBuffer(config->base, (uint32_t *)(uintptr_t)config->fb_addr,
				       &data->gfx_context);

	/* Step 4: wake the panel MCU over I2C, if a panel phandle is provided. */
	return pse84_gfxss_init_panel(dev);
}

static int pse84_gfxss_write(const struct device *dev, const uint16_t x, const uint16_t y,
			     const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct pse84_gfxss_config *config = dev->config;
	const uint8_t *src = buf;
	const uint32_t bytes_per_pixel = 2U; /* RGB565 only for now */
	uint32_t stride_bytes = (uint32_t)config->stride_pixels * bytes_per_pixel;
	uint32_t src_stride_bytes = (uint32_t)desc->pitch * bytes_per_pixel;
	uint32_t row_copy_bytes = (uint32_t)desc->width * bytes_per_pixel;
	uint32_t max_x = (uint32_t)x + desc->width;
	uint32_t max_y = (uint32_t)y + desc->height;
	uint8_t *fb_row0;
	uint8_t *fb;
	uint32_t dirty_span_bytes;

	if (max_x > config->width || max_y > config->height) {
		LOG_ERR("write out of bounds: (%u,%u) %ux%u vs %ux%u", x, y, desc->width,
			desc->height, config->width, config->height);
		return -EINVAL;
	}
	if (desc->buf_size < (size_t)desc->pitch * desc->height * bytes_per_pixel) {
		LOG_ERR("buffer too small: %u", (unsigned int)desc->buf_size);
		return -EINVAL;
	}

	fb_row0 = (uint8_t *)(uintptr_t)config->fb_addr + (uint32_t)y * stride_bytes +
		  (uint32_t)x * bytes_per_pixel;
	fb = fb_row0;
	for (uint32_t row = 0; row < desc->height; row++) {
		memcpy(fb, src, row_copy_bytes);
		fb += stride_bytes;
		src += src_stride_bytes;
	}

	/* CM55 has D-cache enabled and the framebuffer is in SOCMEM. Clean
	 * only the dirty range so the DC DMA sees our writes on the next
	 * vsync. SCB_CleanDCache_by_Addr() rounds down to a cache-line
	 * boundary and up by the size, so we only need to pass the first
	 * dirty byte and a length that covers every row touched by memcpy
	 * above (including any stride tail bytes the cache line spans).
	 */
	dirty_span_bytes = ((uint32_t)desc->height - 1U) * stride_bytes + row_copy_bytes;
	SCB_CleanDCache_by_Addr((uint32_t *)fb_row0, (int32_t)dirty_span_bytes);
	__DSB();
	__ISB();
	return 0;
}

static void pse84_gfxss_get_capabilities(const struct device *dev,
					 struct display_capabilities *capabilities)
{
	const struct pse84_gfxss_config *config = dev->config;

	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->x_resolution = config->width;
	capabilities->y_resolution = config->height;
	capabilities->supported_pixel_formats = PIXEL_FORMAT_RGB_565;
	capabilities->current_pixel_format = PIXEL_FORMAT_RGB_565;
	capabilities->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

static int pse84_gfxss_set_pixel_format(const struct device *dev,
					const enum display_pixel_format pixel_format)
{
	ARG_UNUSED(dev);
	if (pixel_format != PIXEL_FORMAT_RGB_565) {
		return -ENOTSUP;
	}
	return 0;
}

static int pse84_gfxss_blanking_off(const struct device *dev)
{
	const struct pse84_gfxss_config *config = dev->config;
	struct pse84_gfxss_data *data = dev->data;

	config->base->GFXSS_DC.MXDC.CTL |= GFXSS_DC_MXDC_CTL_ENABLED_Msk;
	data->blanking = false;
	return 0;
}

static int pse84_gfxss_blanking_on(const struct device *dev)
{
	const struct pse84_gfxss_config *config = dev->config;
	struct pse84_gfxss_data *data = dev->data;

	config->base->GFXSS_DC.MXDC.CTL &= ~GFXSS_DC_MXDC_CTL_ENABLED_Msk;
	data->blanking = true;
	return 0;
}

static DEVICE_API(display, pse84_gfxss_api) = {
	.blanking_on = pse84_gfxss_blanking_on,
	.blanking_off = pse84_gfxss_blanking_off,
	.write = pse84_gfxss_write,
	.get_capabilities = pse84_gfxss_get_capabilities,
	.set_pixel_format = pse84_gfxss_set_pixel_format,
};

/* Associate the GFXSS with a panel by searching DT for any enabled
 * instance of a known panel compatible. Using DEVICE_DT_GET_ANY here
 * (rather than a 'panel' phandle property in the binding) means the EDT
 * init-priority validator does not see a GFXSS → panel dependency, so
 * the panel can be marked 'zephyr,deferred-init' without blocking the
 * non-deferred GFXSS init.
 */
#if DT_HAS_COMPAT_STATUS_OKAY(waveshare_dsi_4p3_mcu)
#define PSE84_GFXSS_PANEL(inst) DEVICE_DT_GET_ANY(waveshare_dsi_4p3_mcu)
#else
#define PSE84_GFXSS_PANEL(inst) NULL
#endif

#define PSE84_GFXSS_FB_NODE(inst) DT_INST_PHANDLE(inst, framebuffer)

#define PSE84_GFXSS_DEFINE(inst)                                                                   \
	static struct pse84_gfxss_data pse84_gfxss_data_##inst;                                    \
	static const struct pse84_gfxss_config pse84_gfxss_config_##inst = {                       \
		.base = (GFXSS_Type *)DT_INST_REG_ADDR(inst),                                      \
		.fb_addr = DT_REG_ADDR(PSE84_GFXSS_FB_NODE(inst)),                                 \
		.fb_size = DT_REG_SIZE(PSE84_GFXSS_FB_NODE(inst)),                                 \
		.width = DT_INST_PROP(inst, width),                                                \
		.height = DT_INST_PROP(inst, height),                                              \
		.stride_pixels = DT_INST_PROP_OR(inst, stride_pixels, DT_INST_PROP(inst, width)),  \
		.pixel_clock_khz = DT_INST_PROP(inst, pixel_clock_khz),                            \
		.hsync_width = DT_INST_PROP(inst, hsync_width),                                    \
		.hfp = DT_INST_PROP(inst, hfp),                                                    \
		.hbp = DT_INST_PROP(inst, hbp),                                                    \
		.vsync_width = DT_INST_PROP(inst, vsync_width),                                    \
		.vfp = DT_INST_PROP(inst, vfp),                                                    \
		.vbp = DT_INST_PROP(inst, vbp),                                                    \
		.per_lane_mbps = DT_INST_PROP(inst, per_lane_mbps),                                \
		.num_lanes = DT_INST_PROP(inst, num_lanes),                                        \
		.panel = PSE84_GFXSS_PANEL(inst),                                                  \
		.panel_init_delay_ms = CONFIG_DISPLAY_PSE84_GFXSS_PANEL_INIT_DELAY_MS,             \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, pse84_gfxss_init, NULL, &pse84_gfxss_data_##inst,              \
			      &pse84_gfxss_config_##inst, POST_KERNEL,                             \
			      CONFIG_DISPLAY_INIT_PRIORITY, &pse84_gfxss_api);

DT_INST_FOREACH_STATUS_OKAY(PSE84_GFXSS_DEFINE)
