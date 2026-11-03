/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>

#include "lux.h"

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
#define LDR_R10_OHMS 15000.0f
#define LDR_GAMMA    0.7f

/* LDR power law: R = R10 * (lux / 10)^-gamma, inverted for lux. */
int ldr_volts_to_lux(float volts)
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
