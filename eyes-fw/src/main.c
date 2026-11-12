/*
 * StackChan eyes: a bare-metal standby face.
 *
 * One loop, sixty times a second: advance the animation, repaint what moved,
 * wait for the next tick. No OS, no tasks, no interrupts, no heap.
 */
#include "core/eyes.h"
#include "core/raster.h"
#include "hal/hal.h"
#include "hal/lcd.h"
#include "board/board.h"

#define FRAME_HZ 60u

extern uint32_t _bss_start, _bss_end;

static EyesAnim s_anim;
static Renderer s_renderer;
static EyeShape s_shapes[2]; /* what was last sent to the panel */
static int s_backlight_mv = BOARD_BACKLIGHT_MV_DEFAULT;

/* What the panel answers about itself. Proves the bus without a human looking. */
static void print_panel_status(void)
{
    uint32_t id = lcd_read(0x04, 1, 24);
    uint32_t pm = lcd_read(0x0A, 0, 8);
    uint32_t mad = lcd_read(0x0B, 0, 8);
    uint32_t fmt = lcd_read(0x0C, 0, 8);
    con_printf("panel: id 0x%06x, power mode 0x%02x (%s%s%s), madctl 0x%02x, pixel format 0x%02x\n",
               (unsigned)id, (unsigned)pm, (pm & 0x10) ? "awake" : "ASLEEP", (pm & 0x04) ? ", display on" : ", DISPLAY OFF",
               (pm & 0x80) ? ", booster on" : "", (unsigned)mad, (unsigned)fmt);
}

/*
 * 's' on the console: read the panel's memory back and print it, run-length
 * encoded, one line per row. tools/fw.py turns it into a PNG. This is what the
 * glass is showing, not what the firmware believes it drew.
 */
static void screenshot(void)
{
    static uint8_t row[SCREEN_W];
    con_puts("SHOT BEGIN 320 240\n");
    for (int y = 0; y < SCREEN_H; y++) {
        lcd_read_row(y, row);
        con_printf("R%d:", y);
        int x = 0;
        while (x < SCREEN_W) {
            int n = 1;
            while (x + n < SCREEN_W && row[x + n] == row[x]) {
                n++;
            }
            con_printf(" %d*%d", n, row[x]);
            x += n;
        }
        con_puts("\n");
    }
    con_puts("SHOT END\n");
}

/*
 * 'v' on the console: read the whole panel back and compare it, pixel by
 * pixel, with the picture the firmware has just sent. A single flipped bit on
 * the 40 MHz bus would show up here.
 */
static void verify(void)
{
    static uint8_t got[SCREEN_W];
    static uint16_t want[SCREEN_W];
    int bad = 0, lit = 0, first_x = -1, first_y = -1;
    for (int y = 0; y < SCREEN_H; y++) {
        lcd_read_row(y, got);
        renderer_line(s_shapes, y, 0, SCREEN_W, want);
        for (int x = 0; x < SCREEN_W; x++) {
            uint16_t rgb = (uint16_t)((want[x] >> 8) | (want[x] << 8));
            int green = (rgb >> 5) & 0x3F;
            if (green) {
                lit++;
            }
            if (green != got[x]) {
                if (!bad) {
                    first_x = x;
                    first_y = y;
                }
                bad++;
            }
        }
    }
    con_printf("verify: %d of %d pixels differ (%d lit)", bad, SCREEN_W * SCREEN_H, lit);
    if (bad) {
        con_printf(", first at %d,%d", first_x, first_y);
    }
    con_puts("\n");
}

/*
 * 't' on the console: what the panel charges for opening a window, and for a
 * pixel on the wire. The renderer weighs one against the other when it cuts a
 * frame into windows (WINDOW_COST_PX in raster.c), so the figure is worth
 * being able to measure. It rewrites the top scanline with what is already
 * there, so nothing changes on the glass.
 */
static void timing(void)
{
    static uint16_t line[SCREEN_W];
    const PixelSink *sink = lcd_sink();
    const uint32_t per_us = hal_cpu_hz() / 1000000u;
    enum { WINDOWS = 256, PASSES = 64 };

    renderer_line(s_shapes, 0, 0, SCREEN_W, line);

    uint32_t t0 = hal_ccount();
    for (int x = 0; x < WINDOWS; x++) {
        sink->begin(sink->ctx, x, 0, x + 1, 1);
        sink->push(sink->ctx, &line[x], 1);
        sink->end(sink->ctx);
    }
    uint32_t window_ns = (hal_ccount() - t0) / WINDOWS * 1000u / per_us;

    t0 = hal_ccount();
    for (int pass = 0; pass < PASSES; pass++) {
        sink->begin(sink->ctx, 0, 0, SCREEN_W, 1);
        for (int x = 0; x < SCREEN_W; x += RASTER_CHUNK) {
            sink->push(sink->ctx, &line[x], RASTER_CHUNK);
        }
        sink->end(sink->ctx);
    }
    uint32_t pass_ns = (hal_ccount() - t0) / PASSES * 1000u / per_us;
    uint32_t pixel_ns = pass_ns > window_ns ? (pass_ns - window_ns) / SCREEN_W : 0;

    con_printf("timing: a window costs %u ns, a pixel on the wire %u ns: one window = %u pixels sent\n",
               (unsigned)window_ns, (unsigned)pixel_ns, (unsigned)(pixel_ns ? window_ns / pixel_ns : 0));
}

static void set_backlight(int mv)
{
    s_backlight_mv = board_backlight_mv(mv);
    if (s_backlight_mv) {
        con_printf("backlight %d mV\n", s_backlight_mv);
    } else {
        con_puts("backlight off\n");
    }
}

static void console_key(int c)
{
    switch (c) {
    case 's':
        screenshot();
        break;
    case 'v':
        verify();
        break;
    case 'b':
        con_boot_log_replay();
        break;
    case 'i':
        print_panel_status();
        board_print_power();
        con_printf("mood %s for %d s more%s, %u px repainted last frame in %d window(s)\n",
                   EYES_EXPR_NAME[s_anim.expr_id], (int)s_anim.stay_s, s_anim.away ? ", looking elsewhere" : "",
                   (unsigned)s_renderer.last_pixels, s_renderer.last_rects);
        break;
    case 'n':
        /* the next expression of the table, now and briskly: to see them all, and to load the renderer */
        eyes_show(&s_anim, (s_anim.expr_id + 1) % EYES_EXPR_COUNT, 0.7f);
        break;
    case 't':
        timing();
        break;
    case '+':
    case '-': {
        /* from off, '+' comes back at the stock firmware's dimmest */
        int mv = (s_backlight_mv ? s_backlight_mv : 2400) + (c == '+' ? 100 : -100);
        set_backlight(mv < BOARD_BACKLIGHT_MV_MIN ? BOARD_BACKLIGHT_MV_MIN : mv);
        break;
    }
    default:
        break;
    }
}

/*
 * Single keys, plus one command with an argument: 'L' and a voltage in mV,
 * ended by a newline ("L2700\n"; "L0\n" switches the backlight off). Up to a
 * few bytes a frame, so a slider dragged in the web tool keeps up.
 */
static void console_poll(void)
{
    static int number = -1; /* >= 0 while an 'L' command is being typed */
    for (int i = 0; i < 16; i++) {
        int c = con_getc();
        if (c < 0) {
            return;
        }
        if (number >= 0) {
            if (c >= '0' && c <= '9') {
                if (number < 100000) number = number * 10 + (c - '0');
                continue;
            }
            set_backlight(number);
            number = -1;
            if (c == '\r' || c == '\n') {
                continue;
            }
        }
        if (c == 'L') {
            number = 0;
        } else {
            console_key(c);
        }
    }
}

void hal_main(void)
{
    for (uint32_t *p = &_bss_start; p < &_bss_end; p++) {
        *p = 0;
    }

    hal_init();
    con_printf("\nstackchan-eyes: bare metal, cpu %u MHz, spi source %u MHz\n", (unsigned)(hal_cpu_hz() / 1000000u),
               (unsigned)(hal_spi_src_hz() / 1000000u));

    int board_ok = board_init() == 0;

    /* Reset and wake the panel first: by the time it is awake, the touch
     * controller that tells which panel this is has finished booting too. */
    lcd_bus_init();
    board_lcd_reset();
    lcd_wake();
    lcd_panel_init(board_panel_is_e());

    /* First picture into the panel's memory, then light it up: no flash of garbage. */
    EyeShape *shapes = s_shapes;
    eyes_init(&s_anim, hal_random(), EYES_SCALE_PX, SCREEN_W, SCREEN_H);
    renderer_init(&s_renderer);
    eyes_step(&s_anim, 0.0f, shapes);
    renderer_full(&s_renderer, shapes, lcd_sink());
    lcd_display_on();
    board_backlight_mv(s_backlight_mv);

    print_panel_status();
    board_print_power();
    con_printf("running%s. keys: i info, b boot log, n next expression, +/- backlight 0.1 V, L<mV> backlight, s screenshot, v verify, t timing\n",
               board_ok ? "" : " (power chips did not answer!)");

    const uint32_t cpu_hz = hal_cpu_hz();
    const uint32_t period = cpu_hz / FRAME_HZ;
    const float tick_s = 1.0f / (float)cpu_hz;
    uint32_t last = hal_ccount();
    uint32_t next = last + period;

    int shown = s_anim.expr_id;
    int body_done = 0;
    uint32_t stat_frames = 0, stat_late = 0;
    uint32_t work_max = 0, work_sum = 0;
    uint32_t px_sum = 0, window_sum = 0;
    float body_timer = 0.3f, stat_timer = 0.0f;

    uint32_t up_s = 0, up_ticks = 0; /* time since boot, kept in integers so it never drifts */

    for (;;) {
        uint32_t now = hal_ccount();
        uint32_t elapsed = now - last;
        last = now;
        up_ticks += elapsed;
        while (up_ticks >= cpu_hz) {
            up_ticks -= cpu_hz;
            up_s++;
        }
        float dt = (float)elapsed * tick_s;
        if (dt > 0.05f) {
            dt = 0.05f; /* after a stall, resume; do not leap */
        }

        eyes_step(&s_anim, dt, shapes);
        renderer_update(&s_renderer, shapes, lcd_sink());

        uint32_t work = hal_ccount() - now;
        work_sum += work / 64u;
        if (work > work_max) {
            work_max = work;
        }
        stat_frames++;
        px_sum += (uint32_t)s_renderer.last_pixels;
        window_sum += (uint32_t)s_renderer.last_rects;

        if (s_anim.expr_id != shown) {
            shown = s_anim.expr_id;
            con_printf("expr %s\n", EYES_EXPR_NAME[shown]);
        }

        if (!body_done) {
            body_timer -= dt;
            if (body_timer <= 0.0f) {
                body_timer = 0.25f;
                body_done = board_body_quiet();
                if (body_done) {
                    con_boot_log_end();
                }
            }
        }

        stat_timer += dt;
        if (stat_timer >= 10.0f) {
            uint32_t per_us = cpu_hz / 1000000u;
            con_printf("stat: up %u.%03u s, %u frames in 10 s, work avg %u us, max %u us, late %u, %u px in %u windows a frame\n",
                       (unsigned)up_s, (unsigned)(up_ticks / (cpu_hz / 1000u)), (unsigned)stat_frames,
                       (unsigned)((work_sum / stat_frames) * 64u / per_us), (unsigned)(work_max / per_us),
                       (unsigned)stat_late, (unsigned)(px_sum / stat_frames), (unsigned)(window_sum / stat_frames));
            stat_timer = 0.0f;
            stat_frames = 0;
            stat_late = 0;
            work_sum = 0;
            work_max = 0;
            px_sum = 0;
            window_sum = 0;
        }

        console_poll();
        con_tick();

        /* wait for the tick; if more than a frame behind, start counting afresh */
        int32_t slack = (int32_t)(next - hal_ccount());
        if (slack < -(int32_t)period) {
            stat_late++;
            next = hal_ccount();
        } else {
            while ((int32_t)(next - hal_ccount()) > 0) {
            }
        }
        next += period;
    }
}
