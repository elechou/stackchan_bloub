#include "lcd.h"
#include "hal.h"
#include "soc.h"

#define PIN_CS   3
#define PIN_DC   35
#define PIN_SCK  36
#define PIN_MOSI 37

/* 40 MHz is what the stock firmware and M5's own graphics library run this panel at. */
#define LCD_SPI_HZ 40000000u

/* ------------------------------------------------------------ SPI master */

static inline void spi_wait(void)
{
    while (REG(SPI_CMD_REG) & SPI_USR) {
    }
}

/* Start sending the first `bytes` (1..64) bytes of the data buffer. Returns at once. */
static void spi_go(int bytes)
{
    REG(SPI_MS_DLEN_REG) = (uint32_t)(bytes * 8 - 1);
    /* settings live in another clock domain: latch them, then fire */
    REG(SPI_CMD_REG) = SPI_UPDATE;
    while (REG(SPI_CMD_REG) & SPI_UPDATE) {
    }
    REG(SPI_CMD_REG) = SPI_USR;
}

static void spi_bytes(const uint8_t *p, int n)
{
    while (n > 0) {
        int chunk = n > 64 ? 64 : n;
        spi_wait();
        for (int i = 0; i < chunk; i += 4) {
            uint32_t w = p[i];
            if (i + 1 < chunk) w |= (uint32_t)p[i + 1] << 8;
            if (i + 2 < chunk) w |= (uint32_t)p[i + 2] << 16;
            if (i + 3 < chunk) w |= (uint32_t)p[i + 3] << 24;
            REG(SPI_W_REG(i >> 2)) = w;
        }
        spi_go(chunk);
        p += chunk;
        n -= chunk;
    }
}

void lcd_bus_init(void)
{
    gpio_output(PIN_CS, 1);
    gpio_output(PIN_DC, 1);
    gpio_output(PIN_SCK, 0);
    gpio_output(PIN_MOSI, 0);

    /* clock the peripheral and pulse its reset, so it starts from defaults */
    REG(SYSTEM_PERIP_CLK_EN0_REG) |= SYSTEM_SPI2_BIT;
    REG(SYSTEM_PERIP_RST_EN0_REG) |= SYSTEM_SPI2_BIT;
    REG(SYSTEM_PERIP_RST_EN0_REG) &= ~SYSTEM_SPI2_BIT;

    REG(SPI_CLK_GATE_REG) = SPI_CLK_EN | SPI_MST_CLK_ACTIVE | (hal_on_pll() ? SPI_MST_CLK_SEL : 0u);
    REG(SPI_SLAVE_REG) = 0;          /* master */
    REG(SPI_USER_REG) = SPI_USR_MOSI; /* write-only transfers, mode 0 */
    REG(SPI_USER1_REG) = 0;
    REG(SPI_USER2_REG) = 0;
    REG(SPI_CTRL_REG) = 0;           /* one data line, MSB first */
    REG(SPI_MISC_REG) = SPI_CS_ALL_DIS; /* clock idles low; chip select is a plain GPIO */
    REG(SPI_DIN_MODE_REG) = 0;
    REG(SPI_DIN_NUM_REG) = 0;
    REG(SPI_DOUT_MODE_REG) = 0;
    REG(SPI_DMA_CONF_REG) = SPI_BUF_AFIFO_RST | SPI_RX_AFIFO_RST;
    REG(SPI_DMA_CONF_REG) = 0;

    uint32_t n = hal_spi_src_hz() / LCD_SPI_HZ;
    if (n <= 1) {
        REG(SPI_CLOCK_REG) = SPI_CLK_EQU_SYSCLK;
    } else {
        uint32_t high = n / 2;
        REG(SPI_CLOCK_REG) = ((n - 1) << SPI_CLKCNT_N_S) | ((high - 1) << SPI_CLKCNT_H_S) | ((n - 1) << SPI_CLKCNT_L_S);
    }
    REG(SPI_CMD_REG) = SPI_UPDATE;
    while (REG(SPI_CMD_REG) & SPI_UPDATE) {
    }

    gpio_route(PIN_SCK, FSPICLK_OUT_IDX);
    gpio_route(PIN_MOSI, FSPID_OUT_IDX);
}

/* ------------------------------------------------------------- commands */

static void lcd_cmd(uint8_t cmd, const uint8_t *data, int n)
{
    spi_wait();
    gpio_write(PIN_CS, 0);
    gpio_write(PIN_DC, 0);
    spi_bytes(&cmd, 1);
    spi_wait();
    gpio_write(PIN_DC, 1);
    if (n > 0) {
        spi_bytes(data, n);
        spi_wait();
    }
    gpio_write(PIN_CS, 1);
}

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[15];
    uint8_t delay_ms;
} InitCmd;

static void run_list(const InitCmd *list, int count)
{
    for (int i = 0; i < count; i++) {
        lcd_cmd(list[i].cmd, list[i].data, list[i].len);
        if (list[i].delay_ms) {
            hal_delay_ms(list[i].delay_ms);
        }
    }
}

/*
 * The sequence the stock StackChan firmware sends, byte for byte: first the
 * generic ILI9341 list of Espressif's esp_lcd_ili9341 driver, then, on the E
 * variant only, the vendor list from the stock board file (stackchan.cc,
 * kIli9342eInitCommands). The panel on the bench displays correctly with it,
 * which no other list can claim.
 */
static const InitCmd INIT_COMMON[] = {
    { 0xCF, 3, { 0x00, 0xAA, 0xE0 }, 0 },
    { 0xED, 4, { 0x67, 0x03, 0x12, 0x81 }, 0 },
    { 0xE8, 3, { 0x8A, 0x01, 0x78 }, 0 },
    { 0xCB, 5, { 0x39, 0x2C, 0x00, 0x34, 0x02 }, 0 },
    { 0xF7, 1, { 0x20 }, 0 },
    { 0xF7, 1, { 0x20 }, 0 },
    { 0xEA, 2, { 0x00, 0x00 }, 0 },
    { 0xC0, 1, { 0x23 }, 0 },
    { 0xC1, 1, { 0x11 }, 0 },
    { 0xC5, 2, { 0x43, 0x4C }, 0 },
    { 0xC7, 1, { 0xA0 }, 0 },
    { 0xB1, 2, { 0x00, 0x1B }, 0 },
    { 0xF2, 1, { 0x00 }, 0 },
    { 0x26, 1, { 0x01 }, 0 },
    { 0xE0, 15, { 0x1F, 0x36, 0x36, 0x3A, 0x0C, 0x05, 0x4F, 0x87, 0x3C, 0x08, 0x11, 0x35, 0x19, 0x13, 0x00 }, 0 },
    { 0xE1, 15, { 0x00, 0x09, 0x09, 0x05, 0x13, 0x0A, 0x30, 0x78, 0x43, 0x07, 0x0E, 0x0A, 0x26, 0x2C, 0x1F }, 0 },
    { 0xB7, 1, { 0x07 }, 0 },
    { 0xB6, 3, { 0x08, 0x82, 0x27 }, 0 },
};

static const InitCmd INIT_ILI9342E[] = {
    { 0xDD, 1, { 0x01 }, 0 },
    { 0x3A, 1, { 0x55 }, 0 },
    { 0x21, 0, { 0 }, 0 },
    { 0x36, 1, { 0x08 }, 0 },
    { 0xD5, 1, { 0x00 }, 0 },
    { 0xB1, 1, { 0x22 }, 0 },
    { 0xC8, 1, { 0x38 }, 0 },
    { 0xCB, 1, { 0x1C }, 0 },
    { 0xC9, 1, { 0x1A }, 0 },
    { 0xCA, 1, { 0x1A }, 0 },
    { 0xB7, 4, { 0x5A, 0x41, 0x11, 0x19 }, 0 },
    { 0xE4, 15, { 0x04, 0x08, 0x11, 0x06, 0x12, 0x07, 0x3A, 0x76, 0x47, 0x07, 0x0F, 0x0A, 0x11, 0x19, 0x05 }, 0 },
    { 0xE5, 15, { 0x02, 0x03, 0x07, 0x06, 0x12, 0x07, 0x36, 0x5F, 0x48, 0x06, 0x10, 0x0C, 0x16, 0x14, 0x09 }, 0 },
    { 0x11, 0, { 0 }, 120 },
};

void lcd_wake(void)
{
    lcd_cmd(0x11, 0, 0); /* sleep out */
    hal_delay_ms(120);
}

void lcd_panel_init(int variant_e)
{
    static const uint8_t madctl = 0x08; /* BGR, landscape as mounted */
    static const uint8_t colmod = 0x55; /* 16 bits per pixel */

    lcd_cmd(0x36, &madctl, 1);
    lcd_cmd(0x3A, &colmod, 1);
    run_list(INIT_COMMON, (int)(sizeof INIT_COMMON / sizeof INIT_COMMON[0]));
    if (variant_e) {
        run_list(INIT_ILI9342E, (int)(sizeof INIT_ILI9342E / sizeof INIT_ILI9342E[0]));
    }
    lcd_cmd(0x21, 0, 0); /* inversion on: this IPS panel is wired that way */
    lcd_cmd(0x36, &madctl, 1);
    /* The display itself stays off until the first picture is in its memory. */
}

void lcd_display_on(void)
{
    lcd_cmd(0x29, 0, 0);
    hal_delay_ms(20);
}

/* ------------------------------------------------------------ pixel sink */

static void sink_begin(void *ctx, int x0, int y0, int x1, int y1)
{
    (void)ctx;
    uint8_t a[4];
    a[0] = (uint8_t)(x0 >> 8);
    a[1] = (uint8_t)x0;
    a[2] = (uint8_t)((x1 - 1) >> 8);
    a[3] = (uint8_t)(x1 - 1);
    lcd_cmd(0x2A, a, 4);
    a[0] = (uint8_t)(y0 >> 8);
    a[1] = (uint8_t)y0;
    a[2] = (uint8_t)((y1 - 1) >> 8);
    a[3] = (uint8_t)(y1 - 1);
    lcd_cmd(0x2B, a, 4);

    /* memory write: chip select stays low until the window is filled */
    uint8_t ramwr = 0x2C;
    gpio_write(PIN_CS, 0);
    gpio_write(PIN_DC, 0);
    spi_bytes(&ramwr, 1);
    spi_wait();
    gpio_write(PIN_DC, 1);
}

static void sink_push(void *ctx, const uint16_t *px, int n)
{
    (void)ctx;
    /* One transfer of up to 32 pixels (the peripheral's buffer is 64 bytes).
     * Wait for the previous one, load, fire, and return while it shifts out. */
    volatile uint32_t *w = (volatile uint32_t *)SPI_W_REG(0);
    spi_wait();
    int i = 0;
    for (; i + 1 < n; i += 2) {
        *w++ = (uint32_t)px[i] | ((uint32_t)px[i + 1] << 16);
    }
    if (i < n) {
        *w = px[i];
    }
    spi_go(n * 2);
}

static void sink_end(void *ctx)
{
    (void)ctx;
    spi_wait();
    gpio_write(PIN_CS, 1);
}

static const PixelSink SINK = { sink_begin, sink_push, sink_end, 0 };

const PixelSink *lcd_sink(void) { return &SINK; }

/* ------------------------------------------------------------- read back */

static void bb_delay(void) { hal_delay_us(1); }

static void bb_begin(uint8_t cmd)
{
    spi_wait();
    /* take the two bus pins away from the SPI peripheral */
    gpio_write(PIN_SCK, 0);
    gpio_write(PIN_MOSI, 0);
    gpio_route(PIN_SCK, SIG_GPIO_OUT_IDX);
    gpio_route(PIN_MOSI, SIG_GPIO_OUT_IDX);

    gpio_write(PIN_CS, 0);
    gpio_write(PIN_DC, 0);
    for (int i = 7; i >= 0; i--) {
        gpio_write(PIN_MOSI, (cmd >> i) & 1);
        bb_delay();
        gpio_write(PIN_SCK, 1);
        bb_delay();
        gpio_write(PIN_SCK, 0);
    }
    gpio_write(PIN_DC, 1);
    gpio_drive(PIN_MOSI, 0); /* the panel drives the data line from here on */
}

static uint32_t bb_bits(int bits)
{
    uint32_t v = 0;
    for (int i = 0; i < bits; i++) {
        bb_delay();
        gpio_write(PIN_SCK, 1);
        v = (v << 1) | (uint32_t)gpio_read(PIN_MOSI);
        bb_delay();
        gpio_write(PIN_SCK, 0);
    }
    return v;
}

static void bb_end(void)
{
    gpio_write(PIN_CS, 1);
    gpio_drive(PIN_MOSI, 1);
    gpio_route(PIN_SCK, FSPICLK_OUT_IDX);
    gpio_route(PIN_MOSI, FSPID_OUT_IDX);
}

uint32_t lcd_read(uint8_t cmd, int dummy_clocks, int bits)
{
    bb_begin(cmd);
    if (dummy_clocks) {
        bb_bits(dummy_clocks);
    }
    uint32_t v = bb_bits(bits);
    bb_end();
    return v;
}

void lcd_read_row(int y, uint8_t *out)
{
    uint8_t a[4];
    a[0] = 0;
    a[1] = 0;
    a[2] = (uint8_t)((SCREEN_W - 1) >> 8);
    a[3] = (uint8_t)(SCREEN_W - 1);
    lcd_cmd(0x2A, a, 4);
    a[0] = (uint8_t)(y >> 8);
    a[1] = (uint8_t)y;
    a[2] = a[0];
    a[3] = a[1];
    lcd_cmd(0x2B, a, 4);

    bb_begin(0x2E); /* memory read */
    bb_bits(8);     /* one dummy byte */
    for (int x = 0; x < SCREEN_W; x++) {
        /* each pixel comes back as three bytes, 6 significant bits each */
        uint32_t rgb = bb_bits(24);
        out[x] = (uint8_t)((rgb >> 10) & 0x3F);
    }
    bb_end();
}
