#include "board.h"
#include "../hal/hal.h"
#include "../hal/i2c.h"

#define PIN_SDA 12
#define PIN_SCL 11

#define AXP2101 0x34
#define AW9523  0x58
#define FT6336  0x38
#define PY32    0x6F

/* ------------------------------------------------------------------ power */

/*
 * Register for register what the stock firmware writes (Pmic in stackchan.cc).
 * These rails feed chips that share the I2C bus and the board was designed
 * around these voltages, so nothing here is "tidied up": same values, same
 * order.
 */
static int axp_init(void)
{
    int v = i2c_read_reg(AXP2101, 0x90);
    if (v < 0) {
        return -1;
    }
    i2c_write_reg(AXP2101, 0x90, (uint8_t)(v | 0xB4)); /* LDO enables */
    i2c_write_reg(AXP2101, 0x97, 28);   /* BLDO2 3.3 V */
    i2c_write_reg(AXP2101, 0x69, 0x35); /* charge LED */
    i2c_write_reg(AXP2101, 0x30, 0x3F); /* ADC channels on */
    i2c_write_reg(AXP2101, 0x90, 0xBF); /* ALDO1-4, BLDO1-2, DLDO1 */
    i2c_write_reg(AXP2101, 0x94, 28);   /* ALDO3 3.3 V */
    i2c_write_reg(AXP2101, 0x95, 28);   /* ALDO4 3.3 V */
    i2c_write_reg(AXP2101, 0x27, 0x00); /* power key: 1 s on, 4 s off */

    v = i2c_read_reg(AXP2101, 0x62);    /* charge current: 700 mA, as stock */
    if (v >= 0) {
        i2c_write_reg(AXP2101, 0x62, (uint8_t)((v & 0xE0) | 13));
    }

    board_backlight(0);
    return 0;
}

void board_backlight(int percent)
{
    int v = i2c_read_reg(AXP2101, 0x90);
    if (v < 0) {
        return;
    }
    if (percent <= 0) {
        i2c_write_reg(AXP2101, 0x90, (uint8_t)(v & 0x7F)); /* DLDO1 off */
        return;
    }
    if (percent > 100) {
        percent = 100;
    }
    /* DLDO1 is the backlight supply: 2.5 V (dim) to 3.3 V (full), 0.1 V steps */
    i2c_write_reg(AXP2101, 0x99, (uint8_t)(20 + percent * 8 / 100));
    if (!(v & 0x80)) {
        i2c_write_reg(AXP2101, 0x90, (uint8_t)(v | 0x80));
    }
}

void board_print_power(void)
{
    int st = i2c_read_reg(AXP2101, 0x00);
    int chg = i2c_read_reg(AXP2101, 0x01);
    int pct = i2c_read_reg(AXP2101, 0xA4);
    int ldo = i2c_read_reg(AXP2101, 0x90);
    int bl = i2c_read_reg(AXP2101, 0x99);
    static const char *const dir[] = { "standby", "charging", "discharging", "?" };
    con_printf("power: usb %s, battery %s %d%%, %s, ldo 0x%02x, backlight reg %d\n",
               (st >= 0 && (st & 0x20)) ? "present" : "absent",
               (st >= 0 && (st & 0x08)) ? "present" : "absent", pct,
               chg >= 0 ? dir[(chg >> 5) & 3] : "?", (unsigned)ldo, bl);
}

/* --------------------------------------------------------------- expander */

/* Same bytes as the stock Aw9523 class. P1 bit 1 is the LCD reset line. */
static int aw_init(void)
{
    if (i2c_write_reg(AW9523, 0x02, 0x07) < 0) { /* P0 outputs */
        return -1;
    }
    i2c_write_reg(AW9523, 0x03, 0x8F); /* P1 outputs */
    i2c_write_reg(AW9523, 0x04, 0x18); /* P0 direction */
    i2c_write_reg(AW9523, 0x05, 0x0C); /* P1 direction */
    i2c_write_reg(AW9523, 0x11, 0x10); /* P0 push-pull */
    i2c_write_reg(AW9523, 0x12, 0xFF); /* all pins GPIO, not LED drivers */
    i2c_write_reg(AW9523, 0x13, 0xFF);
    hal_delay_ms(50);
    return 0;
}

void board_lcd_reset(void)
{
    i2c_write_reg(AW9523, 0x03, 0x81);
    hal_delay_ms(20);
    i2c_write_reg(AW9523, 0x03, 0x83);
    hal_delay_ms(120);
}

/*
 * Two panels ship on this board and they want different init commands. The
 * stock firmware tells them apart by the firmware of the touch controller
 * bonded to the glass, which is cheaper than talking to the panel itself.
 */
int board_panel_is_e(void)
{
    /* After a cold start the controller needs about 300 ms out of reset before
     * it answers. Guessing "C" too early would send an E panel the wrong
     * commands, so wait for it: up to 0.6 s on top of the panel's own wake-up. */
    for (int attempt = 0; attempt < 30; attempt++) {
        static const uint8_t work_mode[2] = { 0x00, 0x00 };
        uint8_t info[6] = { 0 };
        if (i2c_write(FT6336, work_mode, 2) == 0 && i2c_read(FT6336, 0xA3, info, 6) == 0
            && info[0] != 0 && info[5] != 0) {
            int is_e = info[3] == 0x12 && info[5] == 0x11;
            con_printf("touch: cipher 0x%02x firmware 0x%02x vendor 0x%02x -> panel ILI9342%c\n",
                       info[0], info[3], info[5], is_e ? 'E' : 'C');
            return is_e;
        }
        hal_delay_ms(20);
    }
    con_puts("touch: no version, assuming panel ILI9342C\n");
    return 0;
}

int board_init(void)
{
    i2c_init(PIN_SDA, PIN_SCL);

    con_puts("i2c:");
    for (int a = 0x08; a < 0x78; a++) {
        if (i2c_probe((uint8_t)a) == 0) {
            con_printf(" 0x%02x", a);
        }
    }
    con_puts("\n");

    int axp = axp_init();
    int aw = aw_init();
    if (axp < 0) con_puts("AXP2101 did not answer\n");
    if (aw < 0) con_puts("AW9523 did not answer\n");
    return (axp < 0 || aw < 0) ? -1 : 0;
}

/* ------------------------------------------------------------------- body */

/* PY32 expander registers (PY32IOExpander_Class in the stock firmware). */
#define PY_VERSION 0x02
#define PY_MODE_L  0x03 /* 1 = output */
#define PY_MODE_H  0x04
#define PY_OUT_L   0x05
#define PY_PU_L    0x09
#define PY_PU_H    0x0A
#define PY_PD_L    0x0B
#define PY_PD_H    0x0C
#define PY_DRV_H   0x14 /* 1 = open drain */
#define PY_LED_CFG 0x24 /* [5:0] count, bit 6 = refresh */
#define PY_LED_RAM 0x30 /* RGB565 per LED */

#define PY_PIN_SERVO_POWER 0  /* VM_EN */
#define PY_PIN_LED_DATA    13
#define PY_LED_COUNT       12

static int py_bit(uint8_t reg, int bit, int on)
{
    int v = i2c_read_reg(PY32, reg);
    if (v < 0) {
        return -1;
    }
    int nv = on ? (v | (1 << bit)) : (v & ~(1 << bit));
    return nv == v ? 0 : i2c_write_reg(PY32, reg, (uint8_t)nv);
}

int board_body_quiet(void)
{
    static int tries, stage;

    if (stage == 0) {
        if (++tries > 8) {
            con_puts("body: no answer from the body controller (CoreS3 alone?)\n");
            return 1;
        }
        int ver = i2c_read_reg(PY32, PY_VERSION);
        if (ver <= 0 || ver == 0xFF) {
            return 0; /* still booting, ask again later */
        }

        /* Servo supply: set the enable line low BEFORE making it an output. */
        py_bit(PY_OUT_L, PY_PIN_SERVO_POWER, 0);
        py_bit(PY_MODE_L, PY_PIN_SERVO_POWER, 1);

        /* LED strip: same pin setup as stock. */
        py_bit(PY_MODE_H, PY_PIN_LED_DATA - 8, 1);
        py_bit(PY_PD_H, PY_PIN_LED_DATA - 8, 0);
        py_bit(PY_PU_H, PY_PIN_LED_DATA - 8, 1);
        py_bit(PY_DRV_H, PY_PIN_LED_DATA - 8, 0);
        i2c_write_reg(PY32, PY_LED_CFG, PY_LED_COUNT);
        con_printf("body: controller v0x%02x, servo power off\n", ver);
        stage = 1;
        return 0;
    }

    /* Later calls: twelve black pixels, sent twice like the stock firmware
     * does, each a poll period after the strip was configured. */
    uint8_t frame[1 + PY_LED_COUNT * 2] = { PY_LED_RAM };
    i2c_write(PY32, frame, sizeof frame);
    int cfg = i2c_read_reg(PY32, PY_LED_CFG);
    if (cfg >= 0) {
        i2c_write_reg(PY32, PY_LED_CFG, (uint8_t)(cfg | 0x40));
    }
    if (++stage < 3) {
        return 0;
    }
    con_puts("body: LEDs off\n");
    return 1;
}
