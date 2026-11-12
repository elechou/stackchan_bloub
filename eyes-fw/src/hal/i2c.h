/*
 * I2C master on two open-drain pins, bit-banged.
 *
 * The bus is only used to set up the power chip and the IO expander at boot,
 * a few dozen bytes in all. Driving the pins by hand needs no interrupt and no
 * peripheral state machine, and it can recover a bus that the previous
 * firmware left in the middle of a byte.
 */
#ifndef I2C_H
#define I2C_H

#include <stdint.h>

void i2c_init(int sda_pin, int scl_pin);

/* All return 0 on success, -1 when the device did not acknowledge. */
int i2c_probe(uint8_t addr);
int i2c_write(uint8_t addr, const uint8_t *data, int n);
int i2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, int n);
int i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value);
/* register value, or -1 */
int i2c_read_reg(uint8_t addr, uint8_t reg);

#endif
