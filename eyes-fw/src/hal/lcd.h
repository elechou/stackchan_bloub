/*
 * The panel: an ILI9342 (C or E variant), 320x240, on SPI.
 *
 *   SCK  GPIO36     MOSI GPIO37 (the panel answers reads on the same wire)
 *   CS   GPIO3      D/C  GPIO35
 *
 * Reset comes from the IO expander and the backlight from the power chip, so
 * those two live in board.c.
 */
#ifndef LCD_H
#define LCD_H

#include <stdint.h>
#include "../core/raster.h"

void lcd_bus_init(void);

/* Leave sleep mode. Takes 120 ms, during which nothing else may be sent. */
void lcd_wake(void);
/* Send the init sequence. `variant_e` selects the ILI9342E command set. */
void lcd_panel_init(int variant_e);
void lcd_display_on(void);

/* The sink the renderer streams pixels into. */
const PixelSink *lcd_sink(void);

/*
 * Diagnostics. The panel is read back by bit-banging the bus, slowly, which
 * has nothing in common with the write path: if what comes back is what was
 * sent, the write path works.
 */
uint32_t lcd_read(uint8_t cmd, int dummy_clocks, int bits);
/* One row of the panel's memory, as 6-bit green values. */
void lcd_read_row(int y, uint8_t *out);

#endif
