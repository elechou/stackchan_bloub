#include <stdarg.h>
#include "hal.h"
#include "soc.h"

static uint32_t s_cpu_hz = 40000000u; /* what the ROM leaves us with: the crystal */
static int s_on_pll;

uint32_t hal_cpu_hz(void) { return s_cpu_hz; }
int hal_on_pll(void) { return s_on_pll; }
/* On the PLL the SPI master is fed 80 MHz; otherwise it runs off the 40 MHz crystal. */
uint32_t hal_spi_src_hz(void) { return s_on_pll ? 80000000u : 40000000u; }

/* -------------------------------------------------------------- watchdogs */

/*
 * The ROM arms watchdogs before it hands over, expecting a bootloader to feed or
 * disable them. Nothing here ever would, and the chip would reset every few
 * seconds. The stock bootloader does the same three things (bootloader_init,
 * bootloader_config_wdt).
 */
static void watchdogs_off(void)
{
    REG(RTC_CNTL_WDTWPROTECT_REG) = RTC_CNTL_WDT_WKEY_VALUE;
    REG(RTC_CNTL_WDTCONFIG0_REG) &= ~(RTC_CNTL_WDT_EN | RTC_CNTL_WDT_FLASHBOOT_MOD_EN);
    REG(RTC_CNTL_WDTWPROTECT_REG) = 0;

    REG(TIMG_WDTWPROTECT_REG(TIMG0_BASE)) = TIMG_WDT_WKEY_VALUE;
    REG(TIMG_WDTCONFIG0_REG(TIMG0_BASE)) = 0;
    REG(TIMG_WDTWPROTECT_REG(TIMG0_BASE)) = 0;
    REG(TIMG_WDTWPROTECT_REG(TIMG1_BASE)) = TIMG_WDT_WKEY_VALUE;
    REG(TIMG_WDTCONFIG0_REG(TIMG1_BASE)) = 0;
    REG(TIMG_WDTWPROTECT_REG(TIMG1_BASE)) = 0;

    /* The super watchdog cannot be stopped, only told to feed itself. */
    REG(RTC_CNTL_SWD_WPROTECT_REG) = RTC_CNTL_SWD_WKEY_VALUE;
    REG(RTC_CNTL_SWD_CONF_REG) |= RTC_CNTL_SWD_AUTO_FEED_EN;
    REG(RTC_CNTL_SWD_WPROTECT_REG) = 0;
}

/* ------------------------------------------------------------------ clock */

/*
 * Bring the 480 MHz PLL up from cold. Mirrors rtc_clk_bbpll_enable() and
 * rtc_clk_bbpll_configure() of ESP-IDF for a 40 MHz crystal.
 *
 * Normally this is not needed: the ROM already runs the PLL, because the USB
 * Serial/JTAG port takes its 48 MHz from it. It is here for the day the chip
 * is found in another state.
 */
static void pll_start_480(void)
{
    REG(RTC_CNTL_OPTIONS0_REG) &= ~(RTC_CNTL_BB_I2C_FORCE_PD | RTC_CNTL_BBPLL_FORCE_PD | RTC_CNTL_BBPLL_I2C_FORCE_PD);
    REG(SYSTEM_CPU_PER_CONF_REG) |= SYSTEM_PLL_FREQ_SEL;

    /* open the internal bus to the PLL block, and to nothing else */
    REG(ANA_CONFIG_REG) |= ANA_CONFIG_ALL_OFF;
    REG(ANA_CONFIG_REG) &= ~ANA_CONFIG_I2C_BBPLL;

    /* calibration start */
    REG(I2C_MST_ANA_CONF0_REG) &= ~I2C_MST_BBPLL_STOP_FORCE_HIGH;
    REG(I2C_MST_ANA_CONF0_REG) |= I2C_MST_BBPLL_STOP_FORCE_LOW;

    /* 480 MHz from 40 MHz: div_ref 0, div7_0 8, dr1 0, dr3 0, dchgp 5, dcur 3, dbias 3 */
    rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, 4, 0x6B);                /* MODE_HF */
    rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, 2, (5 << 4) | 0);        /* OC_REF_DIV | DCHGP */
    rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, 3, 8);                   /* OC_DIV_7_0 */
    rom_i2c_writeReg_Mask(I2C_BBPLL, I2C_BBPLL_HOSTID, 5, 2, 0, 0);        /* OC_DR1 */
    rom_i2c_writeReg_Mask(I2C_BBPLL, I2C_BBPLL_HOSTID, 5, 6, 4, 0);        /* OC_DR3 */
    rom_i2c_writeReg(I2C_BBPLL, I2C_BBPLL_HOSTID, 6, (1 << 6) | (3 << 4) | 3); /* DLREF | DHREF | DCUR */
    rom_i2c_writeReg_Mask(I2C_BBPLL, I2C_BBPLL_HOSTID, 9, 1, 0, 3);        /* OC_VCO_DBIAS */

    for (uint32_t i = 0; i < 400000u; i++) {
        if (REG(I2C_MST_ANA_CONF0_REG) & I2C_MST_BBPLL_CAL_DONE) {
            break;
        }
    }
    hal_delay_us(10);

    /* calibration stop */
    REG(I2C_MST_ANA_CONF0_REG) &= ~I2C_MST_BBPLL_STOP_FORCE_LOW;
    REG(I2C_MST_ANA_CONF0_REG) |= I2C_MST_BBPLL_STOP_FORCE_HIGH;
}

/*
 * 80 MHz from the PLL. This is the frequency the stock bootloader runs at,
 * before any of the core-voltage trimming an application does for 160 and
 * 240 MHz, so it needs nothing beyond flipping the clock mux. It is also
 * plenty: a frame costs a few milliseconds.
 */
static void clock_init(void)
{
    uint32_t sys = REG(SYSTEM_SYSCLK_CONF_REG);
    if (((sys & SYSTEM_SOC_CLK_SEL_M) >> SYSTEM_SOC_CLK_SEL_S) != 1) {
        uint32_t opt = REG(RTC_CNTL_OPTIONS0_REG);
        int powered = !(opt & (RTC_CNTL_BB_I2C_FORCE_PD | RTC_CNTL_BBPLL_FORCE_PD | RTC_CNTL_BBPLL_I2C_FORCE_PD));
        int at_480 = (REG(SYSTEM_CPU_PER_CONF_REG) & SYSTEM_PLL_FREQ_SEL) != 0;
        if (!(powered && at_480)) {
            pll_start_480();
        }
    }
    /* CPU = PLL / 6, no extra divider, then switch the source */
    REG(SYSTEM_CPU_PER_CONF_REG) &= ~SYSTEM_CPUPERIOD_SEL_M;
    REG(SYSTEM_SYSCLK_CONF_REG) &= ~SYSTEM_PRE_DIV_CNT_M;
    REG(SYSTEM_SYSCLK_CONF_REG) = (REG(SYSTEM_SYSCLK_CONF_REG) & ~SYSTEM_SOC_CLK_SEL_M) | (1u << SYSTEM_SOC_CLK_SEL_S);
    s_cpu_hz = 80000000u;
    s_on_pll = 1;
    ets_update_cpu_frequency(80); /* keeps the ROM's own delay loops honest */
}

/* ------------------------------------------------------------------- time */

void hal_delay_us(uint32_t us)
{
    uint32_t ticks = us * (s_cpu_hz / 1000000u);
    uint32_t t0 = hal_ccount();
    while ((uint32_t)(hal_ccount() - t0) < ticks) {
    }
}

void hal_delay_ms(uint32_t ms)
{
    while (ms--) {
        hal_delay_us(1000);
    }
}

uint32_t hal_random(void)
{
    /* The hardware generator is weak without the radio on. Stirring in the
     * cycle counter is enough to pick a different sequence on every boot. */
    uint32_t v = 0;
    for (int i = 0; i < 8; i++) {
        v = (v << 5) ^ (v >> 27) ^ REG(WDEV_RND_REG) ^ hal_ccount();
    }
    return v;
}

/* ---------------------------------------------------------------- console */

static int s_con_stalled;
static int s_con_flush_again;

static char s_boot_log[1536];
static int s_boot_log_len;
static int s_boot_log_open = 1;

void con_flush(void) { REG(USB_SERIAL_JTAG_EP1_CONF_REG) = USB_SERIAL_JTAG_WR_DONE; }

static void con_raw(char c);

void con_putc(char c)
{
    if (s_boot_log_open && s_boot_log_len < (int)sizeof s_boot_log) {
        s_boot_log[s_boot_log_len++] = c;
    }
    con_raw(c);
}

void con_boot_log_end(void) { s_boot_log_open = 0; }

void con_boot_log_replay(void)
{
    for (int i = 0; i < s_boot_log_len; i++) {
        con_raw(s_boot_log[i]);
    }
}

static void con_raw(char c)
{
    if (!(REG(USB_SERIAL_JTAG_EP1_CONF_REG) & USB_SERIAL_JTAG_SERIAL_IN_EP_DATA_FREE)) {
        /* The FIFO only empties when a program on the PC reads the port. Wait
         * once, briefly; after that drop output until the host shows up again. */
        if (s_con_stalled) {
            return;
        }
        uint32_t limit = s_cpu_hz / 50u; /* 20 ms */
        uint32_t t0 = hal_ccount();
        while (!(REG(USB_SERIAL_JTAG_EP1_CONF_REG) & USB_SERIAL_JTAG_SERIAL_IN_EP_DATA_FREE)) {
            if ((uint32_t)(hal_ccount() - t0) > limit) {
                s_con_stalled = 1;
                return;
            }
        }
    }
    s_con_stalled = 0;
    REG(USB_SERIAL_JTAG_EP1_REG) = (uint8_t)c;
    if (c == '\n') {
        con_flush();
        s_con_flush_again = 3;
    }
}

/*
 * A line longer than the 64-byte FIFO leaves in two packets, and the flush for
 * the second one is ignored while the first is still on the wire: the tail then
 * sits there until something else is printed. Asking again on the next few
 * frames gets it out.
 */
void con_tick(void)
{
    if (s_con_flush_again) {
        s_con_flush_again--;
        con_flush();
    }
}

void con_puts(const char *s)
{
    while (*s) {
        con_putc(*s++);
    }
}

int con_getc(void)
{
    if (REG(USB_SERIAL_JTAG_EP1_CONF_REG) & USB_SERIAL_JTAG_SERIAL_OUT_EP_DATA_AVAIL) {
        return (int)(REG(USB_SERIAL_JTAG_EP1_REG) & 0xFFu);
    }
    return -1;
}

static void put_num(uint32_t v, unsigned base, int width, char pad, int neg)
{
    char buf[12];
    int n = 0;
    do {
        unsigned d = v % base;
        buf[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= base;
    } while (v && n < (int)sizeof buf);
    if (neg) {
        if (pad == '0') {
            con_putc('-');
            width--;
        } else {
            buf[n++] = '-';
        }
    }
    while (width-- > n) {
        con_putc(pad);
    }
    while (n) {
        con_putc(buf[--n]);
    }
}

/* %s %c %d %u %x with an optional width and zero padding. No floats. */
void con_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            con_putc(*fmt);
            continue;
        }
        fmt++;
        char pad = ' ';
        int width = 0;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }
        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            con_puts(s ? s : "(null)");
            break;
        }
        case 'c':
            con_putc((char)va_arg(ap, int));
            break;
        case 'd': {
            int v = va_arg(ap, int);
            put_num(v < 0 ? (uint32_t)(-(v + 1)) + 1u : (uint32_t)v, 10, width, pad, v < 0);
            break;
        }
        case 'u':
            put_num(va_arg(ap, uint32_t), 10, width, pad, 0);
            break;
        case 'x':
            put_num(va_arg(ap, uint32_t), 16, width, pad, 0);
            break;
        case '%':
            con_putc('%');
            break;
        default:
            if (!*fmt) {
                va_end(ap);
                return;
            }
            con_putc(*fmt);
            break;
        }
    }
    va_end(ap);
}

/* ------------------------------------------------------------------- pins */

void gpio_write(int pin, int level)
{
    if (pin < 32) {
        REG(level ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG) = BIT(pin);
    } else {
        REG(level ? GPIO_OUT1_W1TS_REG : GPIO_OUT1_W1TC_REG) = BIT(pin - 32);
    }
}

int gpio_read(int pin)
{
    if (pin < 32) {
        return (REG(GPIO_IN_REG) >> pin) & 1u;
    }
    return (REG(GPIO_IN1_REG) >> (pin - 32)) & 1u;
}

void gpio_drive(int pin, int enable)
{
    if (pin < 32) {
        REG(enable ? GPIO_ENABLE_W1TS_REG : GPIO_ENABLE_W1TC_REG) = BIT(pin);
    } else {
        REG(enable ? GPIO_ENABLE1_W1TS_REG : GPIO_ENABLE1_W1TC_REG) = BIT(pin - 32);
    }
}

void gpio_route(int pin, int signal)
{
    /* The driver is always switched by GPIO_ENABLE, also for peripheral
     * signals: a clock or data line must never float between transfers. */
    REG(GPIO_FUNC_OUT_SEL_CFG_REG(pin)) = (uint32_t)signal | GPIO_FUNC_OEN_SEL;
}

static void pad_setup(int pin, int open_drain, int pull_up)
{
    /* pad function 1 = GPIO, drive strength 2, input buffer on */
    REG(IO_MUX_REG(pin)) = ((uint32_t)IO_MUX_FUNC_GPIO << IO_MUX_MCU_SEL_S) | (2u << IO_MUX_FUN_DRV_S)
                         | IO_MUX_FUN_IE | (pull_up ? IO_MUX_FUN_PU : 0u);
    REG(GPIO_PIN_REG(pin)) = open_drain ? GPIO_PIN_PAD_DRIVER : 0u;
}

void gpio_output(int pin, int level)
{
    pad_setup(pin, 0, 0);
    gpio_write(pin, level);
    gpio_route(pin, SIG_GPIO_OUT_IDX);
    gpio_drive(pin, 1);
}

void gpio_open_drain(int pin)
{
    pad_setup(pin, 1, 1);
    gpio_write(pin, 1); /* released */
    gpio_route(pin, SIG_GPIO_OUT_IDX);
    gpio_drive(pin, 1);
}

/* ------------------------------------------------------------------- init */

void hal_init(void)
{
    watchdogs_off();
    clock_init();
}
