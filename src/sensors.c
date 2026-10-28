/*
 * Si7021 temperature/humidity and GL5528 light sensor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>

#include "sensors.h"

LOG_MODULE_REGISTER(sensors, LOG_LEVEL_INF);

#define SI7021_NODE DT_ALIAS(dht0)
#define USER_NODE   DT_PATH(zephyr_user)

/* Si7021 datasheet: tPU from VDD >= 1.9 V to ready for a conversion is 80 ms max. */
#define SI7021_POWER_ON_DELAY_MS 80
#define SI7021_CMD_RESET         0xFE
/* Electronic ID, second access: SNB_3, SNB_2, CRC, SNB_1, SNB_0, CRC (Table 12). */
#define SI7021_CMD_EID2_0        0xFC
#define SI7021_CMD_EID2_1        0xC9
#define SI7021_EID2_RESP_LEN     6
#define SI7021_DEVICE_ID         0x15

/*
 * UULP_VBAT_GPIO_n pad control lives in the MCU retention block at
 * ret + 0x1C + 4n. Bit 6 hands the pad to the NWP when set; the GPIO driver
 * never clears it.
 */
#define UULP_PAD_CTRL_OFFSET 0x1C
#define UULP_PAD_OUT_BIT     5
#define UULP_PAD_SELECT_BIT  6

#define ADC_VREF_VOLTS 3.3f

/*
 * GL5528 LDR from GPIO_25 to GND, 4.7 kOhm pull-up to the ADC reference:
 * V = VREF * R_ldr / (R_ldr + R_pullup).
 */
#define LDR_PULLUP_OHMS 4700.0f
/*
 * GL5528 datasheet: 10-20 kOhm at 10 lux, gamma 0.7 (10 to 100 lux). The reading
 * is used against a 2000 lux threshold; part-to-part spread moves that point to
 * 1300-3600 lux, so set R10 from a lux meter reading near 2000 lux.
 */
#define LDR_R10_OHMS  15000.0f
#define LDR_GAMMA     0.7f
#define LIGHT_LUX_MAX 100000

static const struct device *const si7021 = DEVICE_DT_GET(SI7021_NODE);
static const struct i2c_dt_spec si7021_i2c = I2C_DT_SPEC_GET(SI7021_NODE);
static const struct gpio_dt_spec sensor_enable = GPIO_DT_SPEC_GET(USER_NODE, sensor_enable_gpios);
static const struct adc_dt_spec ldr_adc = ADC_DT_SPEC_GET(USER_NODE);

static mem_addr_t sensor_enable_pad_ctrl(void)
{
	return DT_REG_ADDR_BY_NAME(DT_GPIO_CTLR(USER_NODE, sensor_enable_gpios), ret) +
	       UULP_PAD_CTRL_OFFSET + 4U * sensor_enable.pin;
}

/*
 * The Si7021 sits on the WSTK mainboard, isolated from the ULP I2C bus until
 * SENSOR_ENABLE is driven high by the M4.
 */
static int si7021_power_on(void)
{
	const mem_addr_t ctrl = sensor_enable_pad_ctrl();
	int ret;

	if (!gpio_is_ready_dt(&sensor_enable)) {
		LOG_ERR("SENSOR_ENABLE GPIO controller not ready");
		return -ENODEV;
	}

	sys_clear_bit(ctrl, UULP_PAD_SELECT_BIT);

	/* The receiver is enabled so the pad level can be read back. */
	ret = gpio_pin_configure_dt(&sensor_enable, GPIO_OUTPUT_ACTIVE | GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure SENSOR_ENABLE: %d", ret);
		return ret;
	}

	LOG_INF("SENSOR_ENABLE ctrl=0x%08x level=%d", sys_read32(ctrl),
		gpio_pin_get_dt(&sensor_enable));

	if (!sys_test_bit(ctrl, UULP_PAD_OUT_BIT)) {
		LOG_ERR("SENSOR_ENABLE output latch is still 0");
		return -EIO;
	}

	k_msleep(SI7021_POWER_ON_DELAY_MS);
	return 0;
}

static int si7021_probe(void)
{
	const uint8_t reset = SI7021_CMD_RESET;
	const uint8_t eid2[] = {SI7021_CMD_EID2_0, SI7021_CMD_EID2_1};
	uint8_t resp[SI7021_EID2_RESP_LEN];
	int ret;

	if (!device_is_ready(si7021)) {
		LOG_ERR("Si7021 driver not ready");
		return -ENODEV;
	}

	ret = i2c_write_dt(&si7021_i2c, &reset, sizeof(reset));
	if (ret < 0) {
		LOG_ERR("Failed to reset Si7021: %d", ret);
		return ret;
	}
	k_msleep(SI7021_POWER_ON_DELAY_MS);

	ret = i2c_write_read_dt(&si7021_i2c, eid2, sizeof(eid2), resp, sizeof(resp));
	if (ret < 0) {
		LOG_ERR("Failed to read Si7021 device ID: %d", ret);
		return ret;
	}

	if (resp[0] == SI7021_DEVICE_ID) {
		LOG_INF("Si7021 detected, device ID 0x%02x", resp[0]);
	} else {
		LOG_WRN("Unexpected sensor on the bus: device ID 0x%02x, expected 0x%02x (Si7021)",
			resp[0], SI7021_DEVICE_ID);
	}
	return 0;
}

static int light_adc_init(void)
{
	int ret;

	if (!adc_is_ready_dt(&ldr_adc)) {
		LOG_ERR("ADC not ready");
		return -ENODEV;
	}

	ret = adc_channel_setup_dt(&ldr_adc);
	if (ret < 0) {
		LOG_ERR("ADC channel setup failed: %d", ret);
		return ret;
	}

	LOG_INF("ADC channel %u on GPIO_25 ready", ldr_adc.channel_id);
	return 0;
}

int sensors_init(void)
{
	int ret;

	ret = si7021_power_on();
	if (ret < 0) {
		return ret;
	}

	ret = si7021_probe();
	if (ret < 0) {
		return ret;
	}

	return light_adc_init();
}

int si7021_read(float *temperature_c, float *humidity_pct)
{
	struct sensor_value temp;
	struct sensor_value hum;
	int ret;

	ret = sensor_sample_fetch(si7021);
	if (ret < 0) {
		return ret;
	}

	ret = sensor_channel_get(si7021, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	if (ret < 0) {
		return ret;
	}

	ret = sensor_channel_get(si7021, SENSOR_CHAN_HUMIDITY, &hum);
	if (ret < 0) {
		return ret;
	}

	*temperature_c = sensor_value_to_float(&temp);
	/* The datasheet formula can leave 0-100 %RH slightly near the rails. */
	*humidity_pct = CLAMP(sensor_value_to_float(&hum), 0.0f, 100.0f);
	return 0;
}

/* LDR power law: R = R10 * (lux / 10)^-gamma, inverted for lux. */
static int ldr_volts_to_lux(float volts)
{
	/* A full-scale reading means an open LDR or total darkness. */
	if (volts >= ADC_VREF_VOLTS * 0.999f) {
		return 0;
	}
	if (volts <= 0.0f) {
		return LIGHT_LUX_MAX;
	}

	const float r_ldr = LDR_PULLUP_OHMS * volts / (ADC_VREF_VOLTS - volts);
	const float lux = 10.0f * powf(LDR_R10_OHMS / r_ldr, 1.0f / LDR_GAMMA);

	if (lux >= (float)LIGHT_LUX_MAX) {
		return LIGHT_LUX_MAX;
	}
	return (int)(lux + 0.5f);
}

int light_read(float *volts, int *lux)
{
	int16_t sample = 0;
	struct adc_sequence sequence = {
		.buffer = &sample,
		.buffer_size = sizeof(sample),
	};
	int32_t mv;
	int ret;

	(void)adc_sequence_init_dt(&ldr_adc, &sequence);

	ret = adc_read_dt(&ldr_adc, &sequence);
	if (ret < 0) {
		return ret;
	}

	/* The driver applies the eFuse gain/offset calibration and returns 0-4095. */
	mv = sample;
	ret = adc_raw_to_millivolts_dt(&ldr_adc, &mv);
	if (ret < 0) {
		return ret;
	}

	*volts = (float)mv / 1000.0f;
	*lux = ldr_volts_to_lux(*volts);
	LOG_DBG("ADC count=%d, %d mV", sample, mv);
	return 0;
}
