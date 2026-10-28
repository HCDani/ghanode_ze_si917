/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GHANODE_SENSORS_H_
#define GHANODE_SENSORS_H_

/* Powers the Si7021, checks it answers, and sets up the light ADC channel. */
int sensors_init(void);

int si7021_read(float *temperature_c, float *humidity_pct);

int light_read(float *volts, int *lux);

#endif /* GHANODE_SENSORS_H_ */
