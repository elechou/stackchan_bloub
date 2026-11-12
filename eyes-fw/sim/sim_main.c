/*
 * Desktop simulator: the same core as the firmware, drawing into memory.
 *
 *   sim dump            print the eye matrices of every expression (scale 100)
 *   sim sheet <dir>     one PGM per expression, settled, no drift
 *   sim run <s> <dir>   animate <s> seconds at 60 fps, check and measure
 *
 * `run` re-renders every frame from scratch and compares it with what the
 * dirty-rectangle path left on the "panel": any stale or missing pixel fails.
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

static int cmd_sheet(const char *dir)
{
    EyesLife life = { 0, 0, 0, 1.0f, 0, 0 };
    for (int i = 0; i < EYES_EXPR_COUNT; i++) {
        EyeShape s[2];
        Renderer r;
        renderer_init(&r);
        eyes_pose(&EYES_EXPR[i], &life, EYES_SCALE_PX, SCREEN_W / 2.0f, SCREEN_H / 2.0f, s);
        PixelSink sk = sink_of(&panel);
        renderer_full(&r, s, &sk);
        char path[512];
        snprintf(path, sizeof path, "%s/expr_%02d_%s.pgm", dir, i, EYES_EXPR_NAME[i]);
        write_pgm(path, &panel);
    }
    return 0;
}

static int cmd_run(float seconds, const char *dir, uint32_t seed, float scale, int every)
{
    EyesAnim a;
    Renderer r, rr;
    EyeShape s[2];
    PixelSink sk = sink_of(&panel);
    PixelSink skr = sink_of(&ref);
    PixelSink ske = sink_of(&exact);
    const float dt = 1.0f / 60.0f;
    int frames = (int)(seconds * 60.0f);
    int bad = 0, span_bad = 0, max_px = 0, shown = -1, changes = 0;
    long total_px = 0;
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    int clip = 0;
    float prev_c[2][2] = { { 0 } };
    float max_step = 0.0f, max_jerk = 0.0f, prev_v[2][2] = { { 0 } };
    char path[512];
    FILE *csv = NULL;

    eyes_init(&a, seed, scale, SCREEN_W / 2.0f, SCREEN_H / 2.0f);
    renderer_init(&r);
    if (dir) {
        snprintf(path, sizeof path, "%s/motion.csv", dir);
        csv = fopen(path, "w");
        if (csv) fprintf(csv, "frame,expr,x0,y0,x1,y1,k\n");
    }

    for (int f = 0; f < frames; f++) {
        eyes_step(&a, dt, s);
        renderer_update(&r, s, &sk);

        /* reference: the whole screen from scratch */
        renderer_init(&rr);
        renderer_full(&rr, s, &skr);
        if (memcmp(panel.px, ref.px, sizeof panel.px) != 0) {
            if (bad < 5) fprintf(stderr, "frame %d: incremental picture differs from full render\n", f);
            bad++;
        }

        /* and once more with every pixel through the exact edge maths */
        raster_spans_enabled = 0;
        renderer_init(&rr);
        renderer_full(&rr, s, &ske);
        raster_spans_enabled = 1;
        if (memcmp(ref.px, exact.px, sizeof ref.px) != 0) {
            if (span_bad < 5) fprintf(stderr, "frame %d: span shortcut differs from the exact maths\n", f);
            span_bad++;
        }

        if (f > 0 && r.last_pixels > max_px) max_px = r.last_pixels;
        total_px += r.last_pixels;
        if (a.expr_id != shown) {
            shown = a.expr_id;
            changes++;
        }

        for (int e = 0; e < 2; e++) {
            /* true extent of the eye, before clipping to the screen */
            EyeRaster er;
            raster_prepare(&s[e], &er);
            float core_x = er.vertical ? s[e].m01 : s[e].m00;
            float core_y = er.vertical ? s[e].m11 : s[e].m10;
            float ex = er.core * (core_x < 0 ? -core_x : core_x)
                     + er.r * sqrtf(s[e].m00 * s[e].m00 + s[e].m01 * s[e].m01);
            float ey = er.core * (core_y < 0 ? -core_y : core_y)
                     + er.r * sqrtf(s[e].m10 * s[e].m10 + s[e].m11 * s[e].m11);
            if (s[e].cx - ex < minx) minx = s[e].cx - ex;
            if (s[e].cx + ex > maxx) maxx = s[e].cx + ex;
            if (s[e].cy - ey < miny) miny = s[e].cy - ey;
            if (s[e].cy + ey > maxy) maxy = s[e].cy + ey;
            if (s[e].cx - ex < 0 || s[e].cx + ex > SCREEN_W || s[e].cy - ey < 0 || s[e].cy + ey > SCREEN_H) clip++;

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
            fprintf(csv, "%d,%s,%.3f,%.3f,%.3f,%.3f,%.4f\n", f, EYES_EXPR_NAME[a.expr_id], s[0].cx, s[0].cy,
                    s[1].cx, s[1].cy, a.morph_k);
        }
        if (dir && f % every == 0 && f < every * 400) {
            snprintf(path, sizeof path, "%s/f_%05d.pgm", dir, f);
            write_pgm(path, &panel);
        }
    }
    if (csv) fclose(csv);

    printf("frames            %d (%.0f s), %d expression changes\n", frames, seconds, changes);
    printf("picture mismatch  %d frames (partial repaint vs full), %d frames (span shortcut vs exact maths)\n", bad,
           span_bad);
    printf("eye extent        x %.1f..%.1f  y %.1f..%.1f  (screen %dx%d), frames touching the border: %d\n",
           minx, maxx, miny, maxy, SCREEN_W, SCREEN_H, clip);
    printf("repaint           avg %ld px/frame, worst %d px/frame\n", total_px / (frames ? frames : 1), max_px);
    printf("motion            fastest step %.2f px/frame, largest change of step %.2f px/frame^2\n", max_step,
           max_jerk);
    return bad || span_bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "dump")) return cmd_dump();
    if (argc >= 3 && !strcmp(argv[1], "sheet")) return cmd_sheet(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "run")) {
        uint32_t seed = argc >= 5 ? (uint32_t)strtoul(argv[4], NULL, 0) : 12345u;
        float scale = argc >= 6 ? (float)atof(argv[5]) : EYES_SCALE_PX;
        int every = argc >= 7 ? atoi(argv[6]) : 6;
        return cmd_run((float)atof(argv[2]), argc >= 4 && argv[3][0] ? argv[3] : NULL, seed, scale, every);
    }
    fprintf(stderr, "usage: sim dump | sheet <dir> | run <seconds> [dir] [seed] [scale] [dump-every-n-frames]\n");
    return 64;
}
