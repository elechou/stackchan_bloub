/*
 * The chip, as far as this firmware needs it: clock, time, console, pins.
 * Polling only. Nothing here uses an interrupt.
 */
#ifndef HAL_H
#define HAL_H

#include <stdint.h>

void hal_init(void);

/* CPU clock in Hz, and where the SPI master takes its clock from. */
uint32_t hal_cpu_hz(void);
uint32_t hal_spi_src_hz(void);
int hal_on_pll(void);

/* Free-running CPU cycle counter. Wraps in under a minute at 80 MHz. */
static inline uint32_t hal_ccount(void)
{
    uint32_t v;
    __asm__ volatile("rsr.ccount %0" : "=a"(v));
    return v;
}

void hal_delay_us(uint32_t us);
void hal_delay_ms(uint32_t ms);
uint32_t hal_random(void);

/* console on the USB Serial/JTAG port; never blocks for long when nobody listens */
void con_putc(char c);
void con_puts(const char *s);
void con_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void con_flush(void);
void con_tick(void); /* call once per frame */
int con_getc(void); /* -1 when nothing was typed */
/* Everything printed during boot is kept and can be printed again: the PC
 * usually opens the port too late to see it the first time. */
void con_boot_log_end(void);
void con_boot_log_replay(void);

/* pins */
void gpio_output(int pin, int level);     /* push-pull output */
void gpio_open_drain(int pin);            /* open-drain output with input, released */
void gpio_write(int pin, int level);
int gpio_read(int pin);
void gpio_drive(int pin, int enable);     /* output driver on/off, pad stays an input */
void gpio_route(int pin, int signal);     /* which signal feeds the pad */

#endif
