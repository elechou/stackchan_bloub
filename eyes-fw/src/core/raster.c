#include "raster.h"
#include "fmath.h"

int raster_spans_enabled = 1;

/* ------------------------------------------------------------- rectangles */

static int rect_area(const Rect *r) { return (r->x1 - r->x0) * (r->y1 - r->y0); }

static Rect rect_union(const Rect *a, const Rect *b)
{
    Rect r;
    r.x0 = a->x0 < b->x0 ? a->x0 : b->x0;
    r.y0 = a->y0 < b->y0 ? a->y0 : b->y0;
    r.x1 = a->x1 > b->x1 ? a->x1 : b->x1;
    r.y1 = a->y1 > b->y1 ? a->y1 : b->y1;
    return r;
}

/* Opening a window costs a few commands: about as long as this many pixels. */
#define WINDOW_COST_PX 48

/*
 * Merge two rectangles whenever one window over both is no dearer than two
 * windows. An eye that moved a little overlaps its old place almost entirely,
 * so those two always merge. Two eyes side by side merge only if the gap
 * between them is small; set diagonally they stay apart, because their union
 * would be mostly empty corners that still have to be sent.
 */
static int rect_merge(Rect *list, int n)
{
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < n && !changed; i++) {
            for (int j = i + 1; j < n; j++) {
                Rect u = rect_union(&list[i], &list[j]);
                if (rect_area(&u) <= rect_area(&list[i]) + rect_area(&list[j]) + WINDOW_COST_PX) {
                    list[i] = u;
                    list[j] = list[n - 1];
                    n--;
                    changed = 1;
                    break;
                }
            }
        }
    }
    return n;
}

/* -------------------------------------------------------------- one eye */

static int floor_i(float v)
{
    int i = (int)v;
    return (float)i > v ? i - 1 : i;
}

static int ceil_i(float v)
{
    int i = (int)v;
    return (float)i < v ? i + 1 : i;
}

void raster_prepare(const EyeShape *s, EyeRaster *e)
{
    e->box.x0 = e->box.x1 = e->box.y0 = e->box.y1 = 0;
    e->alpha = s->alpha;
    e->row_y = -1;
    if (s->alpha <= 0.0f) {
        return;
    }
    float det = s->m00 * s->m11 - s->m01 * s->m10;
    if (f_abs(det) < 1e-6f) {
        e->alpha = 0.0f;
        return;
    }
    float inv = 1.0f / det;
    e->i00 = s->m11 * inv;
    e->i01 = -s->m01 * inv;
    e->i10 = -s->m10 * inv;
    e->i11 = s->m00 * inv;
    e->cx = s->cx;
    e->cy = s->cy;

    /* A capsule is a segment thickened by r. */
    e->vertical = s->hh >= s->hw;
    e->r = e->vertical ? s->hw : s->hh;
    e->core = (e->vertical ? s->hh : s->hw) - e->r;

    /*
     * Bounding box on screen. The matrix maps the core segment to another
     * segment and the disc to an ellipse whose half extents are r times the
     * norm of each ROW of M, so the box is exact, not a loose rectangle bound.
     */
    float sx = e->vertical ? s->m01 : s->m00; /* image of the core direction */
    float sy = e->vertical ? s->m11 : s->m10;
    float ex = e->core * f_abs(sx) + e->r * f_sqrt(s->m00 * s->m00 + s->m01 * s->m01);
    float ey = e->core * f_abs(sy) + e->r * f_sqrt(s->m10 * s->m10 + s->m11 * s->m11);
    /* one pixel of margin for the anti-aliased rim */
    int x0 = floor_i(s->cx - ex - 1.0f);
    int x1 = ceil_i(s->cx + ex + 1.0f);
    int y0 = floor_i(s->cy - ey - 1.0f);
    int y1 = ceil_i(s->cy + ey + 1.0f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    if (x0 >= x1 || y0 >= y1) {
        e->alpha = 0.0f;
        return;
    }
    e->box.x0 = x0;
    e->box.x1 = x1;
    e->box.y0 = y0;
    e->box.y1 = y1;

    /*
     * Half a pixel on screen is at most `band` in capsule space, where the
     * factor is the largest singular value of the inverse matrix. Anything
     * deeper than that inside (or further outside) needs no edge maths.
     */
    float sum = e->i00 * e->i00 + e->i01 * e->i01 + e->i10 * e->i10 + e->i11 * e->i11;
    float d = e->i00 * e->i11 - e->i01 * e->i10;
    float disc = sum * sum - 4.0f * d * d;
    float smax = f_sqrt(0.5f * (sum + f_sqrt(disc > 0.0f ? disc : 0.0f)));
    float band = 0.5f * smax + 1e-3f;
    e->rin = e->r - band;
    e->rout = e->r + band;
    e->rin2 = e->rin > 0.0f ? e->rin * e->rin : -1.0f;
    e->rout2 = e->rout * e->rout;

    /* What one pixel to the right means in the capsule's frame. */
    e->su = e->vertical ? e->i00 : e->i10;
    e->sv = e->vertical ? e->i10 : e->i00;
    e->inv_su = f_abs(e->su) > 1e-6f ? 1.0f / e->su : 0.0f;
    e->inv_sv = f_abs(e->sv) > 1e-6f ? 1.0f / e->sv : 0.0f;
    e->a = e->su * e->su + e->sv * e->sv;
    e->inv_a = 1.0f / e->a;
}

/* ---------------------------------------------- where a scanline crosses */

#define K_FAR 1.0e6f

/* Narrow [*k0, *k1] to the k for which |p0 + k * step| <= limit (inv_step = 1/step, or 0 for no step). */
static void slab(float p0, float inv_step, float limit, float *k0, float *k1)
{
    if (inv_step == 0.0f) {
        /* moving along x does not change this coordinate at all */
        if (f_abs(p0) > limit) {
            *k0 = K_FAR;
            *k1 = -K_FAR;
        }
        return;
    }
    float a = (-limit - p0) * inv_step;
    float b = (limit - p0) * inv_step;
    if (a > b) {
        float t = a;
        a = b;
        b = t;
    }
    if (a > *k0) *k0 = a;
    if (b < *k1) *k1 = b;
}

/*
 * Range of k over which the scanline is within `rho` of the core segment.
 * That region is convex (a capsule), so the answer is one interval: the hull
 * of where the line crosses the straight part and each of the two round ends.
 * Returns 0 when the line misses it.
 */
static int capsule_span(const EyeRaster *e, float u0, float v0, float rho, float *lo, float *hi)
{
    float l = K_FAR, h = -K_FAR;

    float k0 = -K_FAR, k1 = K_FAR;
    slab(u0, e->inv_su, rho, &k0, &k1);
    slab(v0, e->inv_sv, e->core, &k0, &k1);
    if (k0 <= k1) {
        l = k0;
        h = k1;
    }

    for (int end = 0; end < 2; end++) {
        float w0 = v0 - (end ? e->core : -e->core);
        float b = u0 * e->su + w0 * e->sv;
        float c = u0 * u0 + w0 * w0 - rho * rho;
        float d = b * b - e->a * c;
        if (d >= 0.0f) {
            float root = f_sqrt(d);
            float ka = (-b - root) * e->inv_a;
            float kb = (-b + root) * e->inv_a;
            if (ka < l) l = ka;
            if (kb > h) h = kb;
        }
    }
    *lo = l;
    *hi = h;
    return l <= h;
}

/*
 * Once per scanline and per eye: solve for the stretch that is plainly outside
 * and the stretch that is plainly inside. Only what lies between the two, a
 * few pixels at each edge, goes through the per-pixel edge maths. On a large
 * eye that is the difference between forty operations per pixel and a byte
 * store.
 *
 * Both spans are pulled in by a pixel on each side. That margin is not what
 * makes them correct: right at either boundary the exact maths already gives
 * "fully covered" or "nothing". It keeps the shortcut and the exact path from
 * ever disagreeing over rounding, which the simulator checks on every frame.
 */
static void row_begin(EyeRaster *e, int y)
{
    float dx = ((float)e->box.x0 + 0.5f) - e->cx;
    float dy = ((float)y + 0.5f) - e->cy;
    e->row_qx0 = e->i00 * dx + e->i01 * dy;
    e->row_qy0 = e->i10 * dx + e->i11 * dy;
    e->row_y = y;

    int last = e->box.x1 - e->box.x0 - 1;
    e->out_lo = 0;
    e->out_hi = last;
    e->in_lo = 1;
    e->in_hi = 0;
    if (!raster_spans_enabled) {
        return;
    }

    float u0 = e->vertical ? e->row_qx0 : e->row_qy0;
    float v0 = e->vertical ? e->row_qy0 : e->row_qx0;
    float lim = (float)last + 2.0f;
    float lo, hi;

    if (!capsule_span(e, u0, v0, e->rout, &lo, &hi)) {
        e->out_lo = 1;
        e->out_hi = 0; /* the eye does not reach this scanline */
        return;
    }
    int a = floor_i(f_clamp(lo, -2.0f, lim)) - 1;
    int b = ceil_i(f_clamp(hi, -2.0f, lim)) + 1;
    e->out_lo = a < 0 ? 0 : a;
    e->out_hi = b > last ? last : b;

    if (e->rin > 0.0f && capsule_span(e, u0, v0, e->rin, &lo, &hi)) {
        a = ceil_i(f_clamp(lo, -2.0f, lim)) + 1;
        b = floor_i(f_clamp(hi, -2.0f, lim)) - 1;
        e->in_lo = a < 0 ? 0 : a;
        e->in_hi = b > last ? last : b;
    }
}

/* The exact path: pixels k in [k0, k1) of the current scanline. */
static void rim(const EyeRaster *e, int k0, int k1, uint8_t *cov)
{
    const float qx0 = e->row_qx0;
    const float qy0 = e->row_qy0;
    const float sx = e->i00;
    const float sy = e->i10;
    const float core = e->core;
    const float rin2 = e->rin2;
    const float rout2 = e->rout2;
    const float full = e->alpha * 255.0f;
    const uint8_t full_u8 = (uint8_t)(full + 0.5f);
    float k = (float)k0;

    for (int i = k0; i < k1; i++, k += 1.0f) {
        /*
         * Capsule-space position of the pixel centre, as  q0 + k * step.
         * Deliberately not a running sum: a pixel must get the same value
         * whichever packet or dirty rectangle it is computed in, or a partial
         * repaint would not match a full one to the last bit.
         */
        float qx = qx0 + k * sx;
        float qy = qy0 + k * sy;
        /* vector from the nearest point of the core segment */
        float vx = qx;
        float vy = qy;
        if (e->vertical) {
            vy = qy > core ? qy - core : (qy < -core ? qy + core : 0.0f);
        } else {
            vx = qx > core ? qx - core : (qx < -core ? qx + core : 0.0f);
        }
        float d2 = vx * vx + vy * vy;
        if (d2 >= rout2) {
            continue;
        }
        uint8_t c;
        if (d2 <= rin2) {
            c = full_u8;
        } else {
            /*
             * On the rim. Signed distance in capsule space, divided by the
             * length of its gradient in SCREEN space, gives the distance to the
             * edge in pixels whatever the squash, the tilt or the perspective.
             */
            float nx, ny, dist;
            if (d2 > 1e-12f) {
                float inv = f_rsqrt(d2);
                dist = d2 * inv;
                nx = vx * inv;
                ny = vy * inv;
            } else {
                /* on the core itself: the edge is straight across */
                dist = 0.0f;
                nx = e->vertical ? 1.0f : 0.0f;
                ny = e->vertical ? 0.0f : 1.0f;
            }
            float gx = e->i00 * nx + e->i10 * ny;
            float gy = e->i01 * nx + e->i11 * ny;
            float a = 0.5f - (dist - e->r) * f_rsqrt(gx * gx + gy * gy);
            if (a <= 0.0f) {
                continue;
            }
            if (a > 1.0f) {
                a = 1.0f;
            }
            c = (uint8_t)(a * full + 0.5f);
        }
        if (c > cov[i]) {
            cov[i] = c;
        }
    }
}

void raster_row(EyeRaster *e, int y, int xa, int xb, uint8_t *cov)
{
    if (e->alpha <= 0.0f || y < e->box.y0 || y >= e->box.y1) {
        return;
    }
    if (xa < e->box.x0) xa = e->box.x0;
    if (xb > e->box.x1) xb = e->box.x1;
    if (xa >= xb) {
        return;
    }
    if (e->row_y != y) {
        row_begin(e, y);
    }

    /* from here on in k = x - box.x0, half-open [ka, kb) */
    int ka = xa - e->box.x0;
    int kb = xb - e->box.x0;
    if (ka < e->out_lo) ka = e->out_lo;
    if (kb > e->out_hi + 1) kb = e->out_hi + 1;
    if (ka >= kb) {
        return;
    }
    /* the fully covered stretch [s0, s1), clipped to this run; empty if none */
    int s0 = e->in_lo;
    int s1 = e->in_hi + 1;
    if (s1 <= s0) {
        s0 = s1 = kb;
    }
    if (s0 < ka) s0 = ka;
    if (s0 > kb) s0 = kb;
    if (s1 < s0) s1 = s0;
    if (s1 > kb) s1 = kb;

    uint8_t *row = cov + e->box.x0;
    rim(e, ka, s0, row);
    const uint8_t full_u8 = (uint8_t)(e->alpha * 255.0f + 0.5f);
    for (int k = s0; k < s1; k++) {
        if (row[k] < full_u8) {
            row[k] = full_u8;
        }
    }
    rim(e, s1, kb, row);
}

/* ------------------------------------------------------------- the frame */

void renderer_init(Renderer *r)
{
    r->prev[0].x0 = r->prev[0].x1 = r->prev[0].y0 = r->prev[0].y1 = 0;
    r->prev[1] = r->prev[0];
    r->primed = 0;
    r->last_rects = 0;
    r->last_pixels = 0;
}

static void compose(EyeRaster eyes[2], int y, int x0, int x1, uint16_t *out)
{
    static uint8_t cov[SCREEN_W];
    for (int x = x0; x < x1; x++) {
        cov[x] = 0;
    }
    raster_row(&eyes[0], y, x0, x1, cov);
    raster_row(&eyes[1], y, x0, x1, cov);
    for (int x = x0; x < x1; x++) {
        out[x - x0] = RASTER_LUT[cov[x]];
    }
}

void renderer_line(const EyeShape shapes[2], int y, int x0, int x1, uint16_t *out)
{
    EyeRaster eyes[2];
    raster_prepare(&shapes[0], &eyes[0]);
    raster_prepare(&shapes[1], &eyes[1]);
    compose(eyes, y, x0, x1, out);
}

/*
 * Pixels are computed in packets of RASTER_CHUNK, the size of one SPI transfer.
 * The sink starts a transfer and returns at once, so the next packet is being
 * computed while the previous one is still shifting out: the two costs overlap
 * instead of adding up. Computing a whole scanline first and then waiting for
 * each of its transfers in turn took twice as long on large eyes.
 */
static void paint(const Rect *area, EyeRaster eyes[2], const PixelSink *sink)
{
    uint16_t packet[RASTER_CHUNK];

    sink->begin(sink->ctx, area->x0, area->y0, area->x1, area->y1);
    for (int y = area->y0; y < area->y1; y++) {
        for (int x = area->x0; x < area->x1; x += RASTER_CHUNK) {
            int n = area->x1 - x < RASTER_CHUNK ? area->x1 - x : RASTER_CHUNK;
            compose(eyes, y, x, x + n, packet);
            sink->push(sink->ctx, packet, n);
        }
    }
    sink->end(sink->ctx);
}

void renderer_full(Renderer *r, const EyeShape shapes[2], const PixelSink *sink)
{
    EyeRaster eyes[2];
    raster_prepare(&shapes[0], &eyes[0]);
    raster_prepare(&shapes[1], &eyes[1]);
    Rect all = { 0, 0, SCREEN_W, SCREEN_H };
    paint(&all, eyes, sink);
    r->prev[0] = eyes[0].box;
    r->prev[1] = eyes[1].box;
    r->primed = 1;
    r->last_rects = 1;
    r->last_pixels = SCREEN_W * SCREEN_H;
}

void renderer_update(Renderer *r, const EyeShape shapes[2], const PixelSink *sink)
{
    if (!r->primed) {
        renderer_full(r, shapes, sink);
        return;
    }
    EyeRaster eyes[2];
    raster_prepare(&shapes[0], &eyes[0]);
    raster_prepare(&shapes[1], &eyes[1]);

    /*
     * What must be repainted: where each eye was (to erase it) and where it is
     * now. Every rectangle is painted with the full new picture inside it, both
     * eyes included, so the order does not matter and two rectangles that
     * overlap simply send the shared pixels twice, with the same values.
     */
    Rect dirty[4];
    int n = 0;
    for (int i = 0; i < 2; i++) {
        if (!rect_empty(&r->prev[i])) dirty[n++] = r->prev[i];
        if (!rect_empty(&eyes[i].box)) dirty[n++] = eyes[i].box;
    }
    n = rect_merge(dirty, n);

    int pixels = 0;
    for (int i = 0; i < n; i++) {
        paint(&dirty[i], eyes, sink);
        pixels += rect_area(&dirty[i]);
    }
    r->prev[0] = eyes[0].box;
    r->prev[1] = eyes[1].box;
    r->last_rects = n;
    r->last_pixels = pixels;
}
