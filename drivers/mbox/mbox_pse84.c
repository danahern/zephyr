/*
 * Copyright (c) 2026 PSE84 Voice Assistant contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 0b.1 skeleton driver: wraps Infineon's Cy_IPC_Drv_* PDL into
 * Zephyr's mbox driver API so that CONFIG_IPC_SERVICE_BACKEND_ICMSG
 * can use a native PSE84 doorbell.
 *
 * Scope notes (see pse84_assistant/docs/ipc_design.md §"Skeleton status"):
 *   - MTU = 4 bytes. One u32 passes through the IPC DATA register on
 *     send; shared-memory payload lives in the icmsg ring.
 *   - Each DT instance owns up to 2 PDL channels (one per direction).
 *   - Not instantiated by any board overlay yet. Phase 0b.2 adds the
 *     DT nodes; Phase 0b.3 adds the M33 peer image.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <cy_ipc_drv.h>

LOG_MODULE_REGISTER(mbox_pse84, CONFIG_MBOX_LOG_LEVEL);

#define DT_DRV_COMPAT infineon_pse84_mbox

/* Phase 0b.1 cap: at most one channel per direction. Keeps the ISR
 * dispatch loop and enabled-mask arithmetic trivial.
 */
#define MBOX_PSE84_MAX_CHANNELS 2U

struct mbox_pse84_config {
	uintptr_t reg;
	const uint32_t *channels;
	size_t num_channels;
	uint32_t intr_index;
	void (*irq_config_func)(void);
};

struct mbox_pse84_data {
	const struct device *dev;
	mbox_callback_t cb[MBOX_PSE84_MAX_CHANNELS];
	void *user_data[MBOX_PSE84_MAX_CHANNELS];
	uint32_t enabled_mask;
};

static inline bool channel_valid(const struct mbox_pse84_config *cfg, uint32_t cell)
{
	return cell < cfg->num_channels;
}

static int mbox_pse84_send(const struct device *dev, mbox_channel_id_t cell,
			   const struct mbox_msg *msg)
{
	const struct mbox_pse84_config *cfg = dev->config;
	uint32_t value = 0U;

	if (!channel_valid(cfg, cell)) {
		return -EINVAL;
	}

	if (msg != NULL) {
		if (msg->size > sizeof(uint32_t)) {
			return -EMSGSIZE;
		}
		if (msg->size > 0U && msg->data != NULL) {
			memcpy(&value, msg->data, msg->size);
		}
	}

	IPC_STRUCT_Type *ipc = Cy_IPC_Drv_GetIpcBaseAddress(cfg->channels[cell]);

	/* Acquire the channel, write the 32-bit signal, release-notify
	 * the peer. Matches the PDL SendMsgWord primitive but split so
	 * we can inject a WriteDataValue (which is what icmsg-style
	 * doorbells want when MTU > 0 signalling is used).
	 */
	Cy_IPC_Drv_AcquireNotify(ipc, 0U);
	Cy_IPC_Drv_WriteDataValue(ipc, value);
	Cy_IPC_Drv_ReleaseNotify(ipc, 1U << cfg->intr_index);

	return 0;
}

static int mbox_pse84_register_callback(const struct device *dev, mbox_channel_id_t cell,
					mbox_callback_t cb, void *user_data)
{
	const struct mbox_pse84_config *cfg = dev->config;
	struct mbox_pse84_data *data = dev->data;

	if (!channel_valid(cfg, cell)) {
		return -EINVAL;
	}

	data->cb[cell] = cb;
	data->user_data[cell] = user_data;

	return 0;
}

static int mbox_pse84_mtu_get(const struct device *dev)
{
	ARG_UNUSED(dev);

	/* 32-bit DATA register carries the entire signalling value. */
	return (int)sizeof(uint32_t);
}

static uint32_t mbox_pse84_max_channels_get(const struct device *dev)
{
	const struct mbox_pse84_config *cfg = dev->config;

	return (uint32_t)cfg->num_channels;
}

static int mbox_pse84_set_enabled(const struct device *dev, mbox_channel_id_t cell, bool enable)
{
	const struct mbox_pse84_config *cfg = dev->config;
	struct mbox_pse84_data *data = dev->data;

	if (!channel_valid(cfg, cell)) {
		return -EINVAL;
	}

	bool was_enabled = (data->enabled_mask & BIT(cell)) != 0U;

	if (was_enabled == enable) {
		return -EALREADY;
	}

	if (enable) {
		data->enabled_mask |= BIT(cell);
	} else {
		data->enabled_mask &= ~BIT(cell);
	}

	return 0;
}

static void mbox_pse84_isr(const void *arg)
{
	const struct device *dev = arg;
	const struct mbox_pse84_config *cfg = dev->config;
	struct mbox_pse84_data *data = dev->data;

	IPC_INTR_STRUCT_Type *intr = Cy_IPC_Drv_GetIntrBaseAddr(cfg->intr_index);
	uint32_t status = Cy_IPC_Drv_GetInterruptStatusMasked(intr);
	uint32_t release_mask = Cy_IPC_Drv_ExtractReleaseMask(status);

	for (size_t i = 0; i < cfg->num_channels; i++) {
		uint32_t ch_bit = 1U << cfg->channels[i];

		if ((release_mask & ch_bit) == 0U) {
			continue;
		}

		/* Clear the pending release bit before dispatching so a
		 * back-to-back notify from the peer is not lost. */
		Cy_IPC_Drv_ClearInterrupt(intr, ch_bit, 0U);

		if ((data->enabled_mask & BIT(i)) == 0U) {
			continue;
		}

		IPC_STRUCT_Type *ipc = Cy_IPC_Drv_GetIpcBaseAddress(cfg->channels[i]);
		uint32_t value = Cy_IPC_Drv_ReadDataValue(ipc);
		struct mbox_msg msg = {
			.data = &value,
			.size = sizeof(value),
		};

		if (data->cb[i] != NULL) {
			data->cb[i](dev, (mbox_channel_id_t)i, data->user_data[i], &msg);
		}
	}
}

static int mbox_pse84_init(const struct device *dev)
{
	const struct mbox_pse84_config *cfg = dev->config;
	struct mbox_pse84_data *data = dev->data;

	if (cfg->num_channels == 0U || cfg->num_channels > MBOX_PSE84_MAX_CHANNELS) {
		LOG_ERR("channels-used length %zu out of range", cfg->num_channels);
		return -EINVAL;
	}

	data->dev = dev;
	data->enabled_mask = 0U;

	/* Unmask release-notify for every channel this instance owns so
	 * the IRQ can fire once NVIC is enabled. The per-channel
	 * set_enabled() gate decides whether the callback actually runs.
	 */
	IPC_INTR_STRUCT_Type *intr = Cy_IPC_Drv_GetIntrBaseAddr(cfg->intr_index);
	uint32_t release_mask = 0U;

	for (size_t i = 0; i < cfg->num_channels; i++) {
		release_mask |= 1U << cfg->channels[i];
	}
	Cy_IPC_Drv_SetInterruptMask(intr, release_mask, 0U);

	cfg->irq_config_func();

	return 0;
}

static DEVICE_API(mbox, mbox_pse84_driver_api) = {
	.send = mbox_pse84_send,
	.register_callback = mbox_pse84_register_callback,
	.mtu_get = mbox_pse84_mtu_get,
	.max_channels_get = mbox_pse84_max_channels_get,
	.set_enabled = mbox_pse84_set_enabled,
};

#define MBOX_PSE84_INIT(inst)                                                                \
	static void mbox_pse84_irq_config_##inst(void)                                       \
	{                                                                                    \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority),                 \
			    mbox_pse84_isr, DEVICE_DT_INST_GET(inst), 0);                    \
		irq_enable(DT_INST_IRQN(inst));                                              \
	}                                                                                    \
                                                                                             \
	static const uint32_t mbox_pse84_channels_##inst[] =                                 \
		DT_INST_PROP(inst, channels_used);                                           \
                                                                                             \
	BUILD_ASSERT(ARRAY_SIZE(mbox_pse84_channels_##inst) <= MBOX_PSE84_MAX_CHANNELS,       \
		     "infineon,pse84-mbox supports at most 2 channels per instance");        \
                                                                                             \
	static const struct mbox_pse84_config mbox_pse84_cfg_##inst = {                      \
		.reg = DT_INST_REG_ADDR(inst),                                               \
		.channels = mbox_pse84_channels_##inst,                                      \
		.num_channels = ARRAY_SIZE(mbox_pse84_channels_##inst),                      \
		.intr_index = DT_INST_PROP(inst, intr_index),                                \
		.irq_config_func = mbox_pse84_irq_config_##inst,                             \
	};                                                                                   \
                                                                                             \
	static struct mbox_pse84_data mbox_pse84_data_##inst;                                \
                                                                                             \
	DEVICE_DT_INST_DEFINE(inst, mbox_pse84_init, NULL, &mbox_pse84_data_##inst,          \
			      &mbox_pse84_cfg_##inst, POST_KERNEL,                           \
			      CONFIG_MBOX_INIT_PRIORITY, &mbox_pse84_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MBOX_PSE84_INIT)
