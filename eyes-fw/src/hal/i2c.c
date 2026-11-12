#include "i2c.h"
#include "hal.h"

static int s_sda, s_scl;

/* Quarter period. 3 us gives roughly 80 kHz once the pin accesses are counted. */
static void tick(void) { hal_delay_us(3); }

static void sda(int level) { gpio_write(s_sda, level); }

static void scl_low(void) { gpio_write(s_scl, 0); }

/* Release the clock and wait for it to rise: a slave may hold it low. */
static void scl_high(void)
{
    gpio_write(s_scl, 1);
    for (int i = 0; i < 2000 && !gpio_read(s_scl); i++) {
        hal_delay_us(1);
    }
}

static void start(void)
{
    sda(1);
    scl_high();
    tick();
    sda(0);
    tick();
    scl_low();
    tick();
}

static void stop(void)
{
    sda(0);
    tick();
    scl_high();
    tick();
    sda(1);
    tick();
}

/* returns 1 when acknowledged */
static int put(uint8_t byte)
{
    for (int i = 7; i >= 0; i--) {
        sda((byte >> i) & 1);
        tick();
        scl_high();
        tick();
        scl_low();
    }
    sda(1);
    tick();
    scl_high();
    int ack = !gpio_read(s_sda);
    tick();
    scl_low();
    tick();
    return ack;
}

static uint8_t get(int ack)
{
    uint8_t byte = 0;
    sda(1);
    for (int i = 0; i < 8; i++) {
        tick();
        scl_high();
        byte = (uint8_t)((byte << 1) | gpio_read(s_sda));
        tick();
        scl_low();
    }
    sda(ack ? 0 : 1);
    tick();
    scl_high();
    tick();
    scl_low();
    sda(1);
    tick();
    return byte;
}

void i2c_init(int sda_pin, int scl_pin)
{
    s_sda = sda_pin;
    s_scl = scl_pin;
    gpio_open_drain(s_sda);
    gpio_open_drain(s_scl);
    hal_delay_us(50);
    /* A slave caught mid-byte by the reset keeps SDA low. Clock it out. */
    for (int i = 0; i < 9 && !gpio_read(s_sda); i++) {
        scl_low();
        tick();
        scl_high();
        tick();
    }
    stop();
}

int i2c_probe(uint8_t addr)
{
    start();
    int ok = put((uint8_t)(addr << 1));
    stop();
    return ok ? 0 : -1;
}

int i2c_write(uint8_t addr, const uint8_t *data, int n)
{
    start();
    int ok = put((uint8_t)(addr << 1));
    for (int i = 0; ok && i < n; i++) {
        ok = put(data[i]);
    }
    stop();
    return ok ? 0 : -1;
}

int i2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, int n)
{
    start();
    int ok = put((uint8_t)(addr << 1)) && put(reg);
    if (ok) {
        start(); /* repeated start */
        ok = put((uint8_t)((addr << 1) | 1));
    }
    if (ok) {
        for (int i = 0; i < n; i++) {
            buf[i] = get(i < n - 1);
        }
    }
    stop();
    return ok ? 0 : -1;
}

int i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t d[2] = { reg, value };
    return i2c_write(addr, d, 2);
}

int i2c_read_reg(uint8_t addr, uint8_t reg)
{
    uint8_t v;
    return i2c_read(addr, reg, &v, 1) == 0 ? (int)v : -1;
}
