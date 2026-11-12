/*
 * Desktop simulator: the same core as the firmware, drawing into memory.
 *
 *   sim dump            print the eye matrices of every expression (scale 100)
 *   sim sheet <dir>     one PGM per expression, settled, no drift
 *   sim run <s> <dir>   animate <s> seconds at 60 fps, check and measure
 *   sim life <s>        live <s> seconds without drawing: what the pet did
 *
 * `run` re-renders every frame from scratch and compares it with what the
 * partial repaint left on the "panel": any stale or missing pixel fails.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/core/eyes.h"
#include "../src/core/raster.h"

typedef struct {
    uint16_t px[SCREEN_W * SCREEN_H];
    int x0, y0, x1, y1; /* the open window */
    int x, y;           /* where the next pixel lands, like the panel's own cursor */
    long pushed;
} Panel;

static void p_begin(void *ctx, int x0, int y0, int x1, int y1)
{
    Panel *p = ctx;
    if (x0 < 0 || y0 < 0 || x1 > SCREEN_W || y1 > SCREEN_H || x0 >= x1 || y0 >= y1) {
        fprintf(stderr, "sink misuse: window %d,%d..%d,%d\n", x0, y0, x1, y1);
        exit(2);
    }
    p->x0 = x0; p->y0 = y0; p->x1 = x1; p->y1 = y1;
    p->x = x0; p->y = y0;
}

static void p_push(void *ctx, const uint16_t *px, int n)
{
    Panel *p = ctx;
    if (n < 1 || n > RASTER_CHUNK) {
        fprintf(stderr, "sink misuse: %d pixels in one push\n", n);
        exit(2);
    }
    for (int i = 0; i < n; i++) {
        if (p->y >= p->y1) {
            fprintf(stderr, "sink misuse: more pixels than the window holds\n");
            exit(2);
        }
        p->px[p->y * SCREEN_W + p->x] = px[i];
        if (++p->x == p->x1) {
            p->x = p->x0;
            p->y++;
        }
    }
    p->pushed += n;
}

static void p_end(void *ctx)
{
    Panel *p = ctx;
    if (p->y != p->y1 || p->x != p->x0) {
        fprintf(stderr, "short window\n");
        exit(2);
    }
}

static PixelSink sink_of(Panel *p)
{
    PixelSink s = { p_begin, p_push, p_end, p };
    return s;
}

static int grey_of(uint16_t wire)
{
    uint16_t rgb = (uint16_t)((wire >> 8) | (wire << 8));
    return ((rgb >> 11) & 31) * 255 / 31;
}

static void write_pgm(const char *path, const Panel *p)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P5\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        fputc(grey_of(p->px[i]), f);
    }
    fclose(f);
}

static Panel panel, ref, exact;
static Renderer live, scratch;

static int cmd_dump(void)
{
    EyesLife life = { 0, 0, 0, 1.0f, 0, 0 };
    for (int i = 0; i < EYES_EXPR_COUNT; i++) {
        EyeShape s[2];
        eyes_pose(&EYES_EXPR[i], &life, 100.0f, 0.0f, 0.0f, s);
        for (int e = 0; e < 2; e++) {
            printf("%s %d %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.3f\n", EYES_EXPR_NAME[i], e,
                   s[e].m00, s[e].m10, s[e].m01, s[e].m11, s[e].cx, s[e].cy,
                   s[e].hw * 2, s[e].hh * 2, s[e].alpha);
        }
    }
    return 0;
}

/* Every expression where the firmware sets it down, and by how much it had to move. */
static int cmd_sheet(const char *dir, float scale)
{
    EyesLife life = { 0, 0, 0, 1.0f, 0, 0 };
    for (int i = 0; i < EYES_EXPR_COUNT; i++) {
        EyeShape s[2];
        float ox, oy;
        eyes_fit(&EYES_EXPR[i], scale, SCREEN_W, SCREEN_H, &ox, &oy);
        eyes_pose(&EYES_EXPR[i], &life, scale, SCREEN_W / 2.0f + ox, SCREEN_H / 2.0f + oy, s);
        renderer_init(&scratch);
        PixelSink sk = sink_of(&panel);
        renderer_full(&scratch, s, &sk);
        char path[512];
        snprintf(path, sizeof path, "%s/expr_%02d_%s.pgm", dir, i, EYES_EXPR_NAME[i]);
        write_pgm(path, &panel);
        printf("%-10s set down at %+6.1f %+6.1f px\n", EYES_EXPR_NAME[i], ox, oy);
    }
    return 0;
}

/* How far the lit part of both eyes stays from the border of the glass; negative = cut off. */
static float clearance(const EyeShape s[2])
{
    float m = 1e9f;
    for (int e = 0; e < 2; e++) {
        if (s[e].alpha <= 0.0f) continue;
        float ex, ey;
        eyes_extent(&s[e], &ex, &ey);
        float d = s[e].cx - ex;
        if (d < m) m = d;
        d = SCREEN_W - (s[e].cx + ex);
        if (d < m) m = d;
        d = s[e].cy - ey;
        if (d < m) m = d;
        d = SCREEN_H - (s[e].cy + ey);
        if (d < m) m = d;
    }
    return m;
}

static int cmd_run(float seconds, const char *dir, uint32_t seed, float scale, int every, float poke_s)
{
    EyesAnim a;
    EyeShape s[2];
    PixelSink sk = sink_of(&panel);
    PixelSink skr = sink_of(&ref);
    PixelSink ske = sink_of(&exact);
    const float dt = 1.0f / 60.0f;
    int frames = (int)(seconds * 60.0f);
    int bad = 0, span_bad = 0, max_px = 0, max_rects = 0, max_cost = 0, shown = -1, changes = 0;
    long total_px = 0, total_rects = 0;
    float min_clear = 1e9f, poke_t = 0.0f;
    float prev_c[2][2] = { { 0 } };
    float max_step = 0.0f, max_jerk = 0.0f, prev_v[2][2] = { { 0 } };
    char path[512];
    FILE *csv = NULL;

    eyes_init(&a, seed, scale, SCREEN_W, SCREEN_H);
    renderer_init(&live);
    if (dir) {
        snprintf(path, sizeof path, "%s/motion.csv", dir);
        csv = fopen(path, "w");
        if (csv) fprintf(csv, "frame,expr,away,x0,y0,x1,y1,k\n");
    }

    for (int f = 0; f < frames; f++) {
        /* like tools/stress.py: the next expression every `poke_s`, reached in 0.7 s */
        if (poke_s > 0.0f) {
            poke_t += dt;
            if (poke_t >= poke_s) {
                poke_t = 0.0f;
                eyes_show(&a, (a.expr_id + 1) % EYES_EXPR_COUNT, 0.7f);
            }
        }
        eyes_step(&a, dt, s);
        renderer_update(&live, s, &sk);

        /* reference: the whole screen from scratch */
        renderer_init(&scratch);
        renderer_full(&scratch, s, &skr);
        if (memcmp(panel.px, ref.px, sizeof panel.px) != 0) {
            if (bad < 5) fprintf(stderr, "frame %d: partial repaint differs from full render\n", f);
            bad++;
        }

        /* and once more with every pixel through the exact edge maths */
        raster_spans_enabled = 0;
        renderer_init(&scratch);
        renderer_full(&scratch, s, &ske);
        raster_spans_enabled = 1;
        if (memcmp(ref.px, exact.px, sizeof ref.px) != 0) {
            if (span_bad < 5) fprintf(stderr, "frame %d: span shortcut differs from the exact maths\n", f);
            span_bad++;
        }

        if (f > 0) {
            /* what a frame costs the panel: its pixels, plus a few commands per window */
            int cost = live.last_pixels + 34 * live.last_rects;
            if (live.last_pixels > max_px) max_px = live.last_pixels;
            if (live.last_rects > max_rects) max_rects = live.last_rects;
            if (cost > max_cost) max_cost = cost;
            total_px += live.last_pixels;
            total_rects += live.last_rects;
        }
        if (a.expr_id != shown) {
            shown = a.expr_id;
            changes++;
        }
        float c = clearance(s);
        if (c < min_clear) min_clear = c;

        for (int e = 0; e < 2; e++) {
            if (f > 0) {
                float vx = s[e].cx - prev_c[e][0];
                float vy = s[e].cy - prev_c[e][1];
                float step = sqrtf(vx * vx + vy * vy);
                if (step > max_step) max_step = step;
                if (f > 1) {
                    float jx = vx - prev_v[e][0], jy = vy - prev_v[e][1];
                    float jerk = sqrtf(jx * jx + jy * jy);
                    if (jerk > max_jerk) max_jerk = jerk;
                }
                prev_v[e][0] = vx;
                prev_v[e][1] = vy;
            }
            prev_c[e][0] = s[e].cx;
            prev_c[e][1] = s[e].cy;
        }
        if (csv) {
            fprintf(csv, "%d,%s,%d,%.3f,%.3f,%.3f,%.3f,%.4f\n", f, EYES_EXPR_NAME[a.expr_id], a.away, s[0].cx,
                    s[0].cy, s[1].cx, s[1].cy, a.glide_k);
        }
        if (dir && f % every == 0 && f < every * 600) {
            snprintf(path, sizeof path, "%s/f_%05d.pgm", dir, f);
            write_pgm(path, &panel);
        }
    }
    if (csv) fclose(csv);

    int n = frames > 1 ? frames - 1 : 1;
    printf("frames            %d (%.0f s), %d changes of mood\n", frames, seconds, changes);
    printf("picture mismatch  %d frames (partial repaint vs full), %d frames (span shortcut vs exact maths)\n", bad,
           span_bad);
    printf("clearance         the eyes stay %.1f px or more inside the glass\n", min_clear);
    printf("repaint           avg %ld px in %.1f windows per frame; worst %d px, %d windows, cost %d px-equivalents\n",
           total_px / n, (double)total_rects / n, max_px, max_rects, max_cost);
    printf("motion            fastest step %.2f px/frame, largest change of step %.2f px/frame^2\n", max_step,
           max_jerk);
    return bad || span_bad ? 1 : 0;
}

/* What the pet did with its time. No drawing, so hours go by in a second. */
static int cmd_life(float seconds, uint32_t seed, float scale, int story)
{
    EyesAnim a;
    EyeShape s[2];
    const float dt = 1.0f / 60.0f;
    long frames = (long)(seconds * 60.0f);
    long in_mood[EYES_EXPR_COUNT] = { 0 };
    long settling = 0, away = 0, shut = 0;
    int visits[EYES_EXPR_COUNT] = { 0 };
    int glances = 0, changes = 0, shown = -1, was_away = 0;
    float min_clear = 1e9f, max_step = 0.0f, prev[2] = { 0, 0 };
    long since = 0, longest_rest = 0, rest_run = 0;

    eyes_init(&a, seed, scale, SCREEN_W, SCREEN_H);
    for (long f = 0; f < frames; f++) {
        eyes_step(&a, dt, s);
        in_mood[a.expr_id]++;
        if (a.expr_id != shown) {
            if (story && f < 60L * 60 * 20) {
                if (shown >= 0) printf("  %5.1f s  %-10s for %5.1f s\n", (f - since) / 60.0, EYES_EXPR_NAME[shown], since / 60.0);
            }
            shown = a.expr_id;
            visits[shown]++;
            changes++;
            since = 0;
        }
        since++;
        if (eyes_is_rest(a.expr_id)) {
            rest_run++;
            if (rest_run > longest_rest) longest_rest = rest_run;
        } else {
            rest_run = 0;
        }
        if (a.glide_k < 1.0f && !a.glide_dart) settling++;
        if (a.away) away++;
        if (a.away && !was_away) glances++;
        was_away = a.away;
        if (a.blink_t >= 0.0f) shut++;

        float c = clearance(s);
        if (c < min_clear) min_clear = c;
        if (f > 0) {
            float step = hypotf(s[0].cx - prev[0], s[0].cy - prev[1]);
            if (step > max_step) max_step = step;
        }
        prev[0] = s[0].cx;
        prev[1] = s[0].cy;
    }

    long rest = 0;
    printf("%.0f s (%.1f h), seed %u, scale %.0f px\n\n", seconds, seconds / 3600.0, (unsigned)seed, scale);
    printf("mood        share   visits  mean stay\n");
    for (int i = 0; i < EYES_EXPR_COUNT; i++) {
        if (eyes_is_rest(i)) rest += in_mood[i];
        printf("%-10s %5.1f %%  %6d  %7.1f s\n", EYES_EXPR_NAME[i], 100.0 * in_mood[i] / frames, visits[i],
               visits[i] ? in_mood[i] / 60.0 / visits[i] : 0.0);
    }
    printf("\nat rest (calm or attentive)   %.1f %% of the time, %.0f s at most in one go\n", 100.0 * rest / frames,
           longest_rest / 60.0);
    printf("changes of mood               %d, one every %.1f s\n", changes, seconds / (changes ? changes : 1));
    printf("in the middle of one          %.1f %% of the time\n", 100.0 * settling / frames);
    printf("glances                       %d, one every %.1f s; looking away %.1f %% of the time\n", glances,
           seconds / (glances ? glances : 1), 100.0 * away / frames);
    printf("blinking                      %.1f %% of the time\n", 100.0 * shut / frames);
    printf("clearance                     the eyes stay %.1f px or more inside the glass\n", min_clear);
    printf("fastest move                  %.2f px/frame\n", max_step);
    return min_clear < 0.0f ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "dump")) return cmd_dump();
    if (argc >= 3 && !strcmp(argv[1], "sheet")) {
        return cmd_sheet(argv[2], argc >= 4 ? (float)atof(argv[3]) : EYES_SCALE_PX);
    }
    if (argc >= 3 && !strcmp(argv[1], "run")) {
        uint32_t seed = argc >= 5 ? (uint32_t)strtoul(argv[4], NULL, 0) : 12345u;
        float scale = argc >= 6 ? (float)atof(argv[5]) : EYES_SCALE_PX;
        int every = argc >= 7 ? atoi(argv[6]) : 6;
        float poke = argc >= 8 ? (float)atof(argv[7]) : 0.0f;
        return cmd_run((float)atof(argv[2]), argc >= 4 && argv[3][0] ? argv[3] : NULL, seed, scale, every, poke);
    }
    if (argc >= 3 && !strcmp(argv[1], "life")) {
        uint32_t seed = argc >= 4 ? (uint32_t)strtoul(argv[3], NULL, 0) : 12345u;
        float scale = argc >= 5 ? (float)atof(argv[4]) : EYES_SCALE_PX;
        return cmd_life((float)atof(argv[2]), seed, scale, argc >= 6 && !strcmp(argv[5], "story"));
    }
    fprintf(stderr, "usage: sim dump\n"
                    "       sim sheet <dir> [scale]\n"
                    "       sim run <seconds> [dir] [seed] [scale] [dump-every-n-frames] [next-expression-every-s]\n"
                    "       sim life <seconds> [seed] [scale] [story]\n");
    return 64;
}
