/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GHANODE_LUX_H_
#define GHANODE_LUX_H_

#define LIGHT_LUX_MAX 100000

/*
 * Converts the voltage across the GL5528 LDR (4.7 kOhm pull-up to the 3.3 V
 * ADC reference) to lux. Full scale (open LDR or darkness) gives 0; the
 * result is capped at LIGHT_LUX_MAX.
 */
int ldr_volts_to_lux(float volts);

#endif /* GHANODE_LUX_H_ */
