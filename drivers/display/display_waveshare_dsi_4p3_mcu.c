/*
 * Copyright (c) 2026 Dan Ahern
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Waveshare 4.3" MIPI-DSI panel with on-board MCU.
 *
 * The panel board hosts an ICN6211 DSI-to-RGB bridge and a small MCU at
 * I2C 0x45 that sequences the bridge/LCD power. The MCU is only reachable
 * on I2C after the DSI link is already transmitting, so this driver must
 * run its init *after* the parent display controller has brought up DSI.
 *
 * That ordering is expressed with the standard Zephyr 'zephyr,deferred-init'
 * property: the panel node is not initialized by Zephyr at boot. The parent
 * display controller calls device_init() on the panel phandle after its own
 * Cy_GFXSS_Init plus a stabilization delay.
 *
 * MCU register set (distinct from the bare ICN6211 boards handled by the
 * existing waveshare,dsi2dpi binding):
 *   0x80  device ID (read; 0xC3)
 *   0x81  power-on (write 0x04)
 *   0x85  display enable (0x00 = off, 0x01 = on)
 *   0x86  brightness (0x00 = off, 0xFF = max)
 */

#define DT_DRV_COMPAT waveshare_dsi_4p3_mcu

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(waveshare_dsi_4p3_mcu, CONFIG_DISPLAY_LOG_LEVEL);

#define PANEL_REG_POWER      0x81
#define PANEL_REG_ENABLE     0x85
#define PANEL_REG_BRIGHTNESS 0x86

#define PANEL_POWER_ON   0x04
#define PANEL_ENABLE_OFF 0x00
#define PANEL_ENABLE_ON  0x01

struct waveshare_dsi_4p3_mcu_config {
	struct i2c_dt_spec i2c;
	uint8_t default_brightness;
};

static int waveshare_dsi_4p3_mcu_write_reg(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct waveshare_dsi_4p3_mcu_config *config = dev->config;
	uint8_t buf[2] = {reg, val};

	return i2c_write_dt(&config->i2c, buf, sizeof(buf));
}

static int waveshare_dsi_4p3_mcu_init(const struct device *dev)
{
	const struct waveshare_dsi_4p3_mcu_config *config = dev->config;
	int ret;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	/* Demo sequence:
	 *   CTRL=0x00 (disable), CTRL=0x01 (enable), POWERON=0x04, BRIGHT=N.
	 * Each write is separated by a ~100 ms delay in the vendor demo,
	 * which the MCU appears to need between state transitions.
	 */
	ret = waveshare_dsi_4p3_mcu_write_reg(dev, PANEL_REG_ENABLE, PANEL_ENABLE_OFF);
	if (ret < 0) {
		LOG_ERR("disable failed: %d", ret);
		return ret;
	}
	k_msleep(100);

	ret = waveshare_dsi_4p3_mcu_write_reg(dev, PANEL_REG_ENABLE, PANEL_ENABLE_ON);
	if (ret < 0) {
		LOG_ERR("enable failed: %d", ret);
		return ret;
	}
	k_msleep(100);

	ret = waveshare_dsi_4p3_mcu_write_reg(dev, PANEL_REG_POWER, PANEL_POWER_ON);
	if (ret < 0) {
		LOG_ERR("power-on failed: %d", ret);
		return ret;
	}
	k_msleep(100);

	ret = waveshare_dsi_4p3_mcu_write_reg(dev, PANEL_REG_BRIGHTNESS,
					     config->default_brightness);
	if (ret < 0) {
		LOG_ERR("brightness failed: %d", ret);
		return ret;
	}

	LOG_INF("Waveshare 4.3\" panel initialized (brightness=%u)",
		config->default_brightness);
	return 0;
}

#define WAVESHARE_DSI_4P3_MCU_DEFINE(inst)                                                         \
	static const struct waveshare_dsi_4p3_mcu_config waveshare_dsi_4p3_mcu_config_##inst = {   \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.default_brightness = DT_INST_PROP(inst, default_brightness),                      \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, waveshare_dsi_4p3_mcu_init, NULL, NULL,                        \
			      &waveshare_dsi_4p3_mcu_config_##inst, POST_KERNEL,                   \
			      CONFIG_DISPLAY_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(WAVESHARE_DSI_4P3_MCU_DEFINE)
