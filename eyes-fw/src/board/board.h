/*
 * M5Stack StackChan (CoreS3 on the robot body): the chips around the ESP32-S3.
 *
 *   AXP2101  0x34  power: rails, charger, and the backlight (it is an LDO)
 *   AW9523B  0x58  IO expander: LCD reset, touch reset, 5 V bus switches
 *   FT6336   0x38  touch controller; only asked which panel it is glued to
 *   PY32     0x6F  the body's own expander: servo power and the RGB LEDs
 *
 * All on one I2C bus, SDA GPIO12 / SCL GPIO11.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

/*
 * Backlight, as the voltage of the AXP2101 rail that feeds it (DLDO1), in mV.
 * The stock firmware maps its 1..100 onto 2500..3300 mV. The panel is an LCD:
 * black pixels only block the backlight, so the dimmer it runs, the darker the
 * black around the eyes. 2700 is about a third of the stock range.
 */
#define BOARD_BACKLIGHT_MV_DEFAULT 2700
#define BOARD_BACKLIGHT_MV_MIN 500  /* the rail's lowest; the LEDs go dark well above it */
#define BOARD_BACKLIGHT_MV_MAX 3300 /* the stock firmware's full brightness */

/* Power rails and expander, exactly as the stock firmware sets them. The
 * backlight is left off. Returns 0 when both chips answered. */
int board_init(void);

/* Pulse the panel's reset line. */
void board_lcd_reset(void);

/* 1 when the panel is the ILI9342E variant, 0 for the ILI9342C. */
int board_panel_is_e(void);

/* Set the backlight rail, in 100 mV steps, clamped to MIN..MAX; 0 switches it
 * off. Returns the voltage actually set. */
int board_backlight_mv(int mv);

/*
 * Keep the body quiet: servo power off, LEDs dark. The body's controller may
 * take a second to boot, so this is polled from the main loop. Returns 1 once
 * it is done or has given up, 0 while it should be called again later.
 */
int board_body_quiet(void);

/* One line about the battery and the supply, for the console. */
void board_print_power(void);

#endif
