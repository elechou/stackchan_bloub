#include "raster.h"
#include "fmath.h"

int raster_spans_enabled = 1;

static int rect_area(const Rect *r) { return (r->x1 - r->x0) * (r->y1 - r->y0); }

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
    e->rows = 0;
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

    /* Bounding box on screen: exact, plus one pixel of margin for the
     * anti-aliased rim. */
    float ex, ey;
    eyes_extent(s, &ex, &ey);
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

    if (e->rows) {
        /* solved already for this frame; an empty stretch stays empty when shifted */
        const RowSpan *r = &e->rows[y];
        e->out_lo = r->out_lo - e->box.x0;
        e->out_hi = r->out_hi - e->box.x0;
        e->in_lo = r->in_lo - e->box.x0;
        e->in_hi = r->in_hi - e->box.x0;
        return;
    }

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

static uint8_t fill_of(const EyeRaster *e) { return e->alpha > 0.0f ? (uint8_t)(e->alpha * 255.0f + 0.5f) : 0; }

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
    const uint8_t full_u8 = fill_of(e);
    for (int k = s0; k < s1; k++) {
        if (row[k] < full_u8) {
            row[k] = full_u8;
        }
    }
    rim(e, s1, kb, row);
}

/*
 * Solve every scanline of an eye once, into `rows`, in screen coordinates.
 * The painter then reads them from there, and the next frame compares its own
 * against them to find what changed.
 */
static void solve_rows(EyeRaster *e, RowSpan *rows)
{
    static const RowSpan none = { 1, 0, 1, 0 };
    for (int y = 0; y < SCREEN_H; y++) {
        rows[y] = none;
    }
    if (e->alpha > 0.0f) {
        for (int y = e->box.y0; y < e->box.y1; y++) {
            row_begin(e, y);
            RowSpan *r = &rows[y];
            if (e->out_lo <= e->out_hi) {
                r->out_lo = (int16_t)(e->box.x0 + e->out_lo);
                r->out_hi = (int16_t)(e->box.x0 + e->out_hi);
            }
            if (e->in_lo <= e->in_hi) {
                r->in_lo = (int16_t)(e->box.x0 + e->in_lo);
                r->in_hi = (int16_t)(e->box.x0 + e->in_hi);
            }
        }
    }
    e->rows = rows;
    e->row_y = -1;
}

/* ------------------------------------------------------------- the frame */

void renderer_init(Renderer *r)
{
    r->prev[0].x0 = r->prev[0].x1 = r->prev[0].y0 = r->prev[0].y1 = 0;
    r->prev[1] = r->prev[0];
    r->fill[0] = r->fill[1] = 0;
    r->flip = 0;
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
    r->flip = 0;
    for (int i = 0; i < 2; i++) {
        raster_prepare(&shapes[i], &eyes[i]);
        solve_rows(&eyes[i], r->rows[r->flip][i]);
    }
    Rect all = { 0, 0, SCREEN_W, SCREEN_H };
    paint(&all, eyes, sink);
    for (int i = 0; i < 2; i++) {
        r->prev[i] = eyes[i].box;
        r->fill[i] = fill_of(&eyes[i]);
    }
    r->primed = 1;
    r->last_rects = 1;
    r->last_pixels = SCREEN_W * SCREEN_H;
}

/*
 * Opening a window costs five short transfers. Measured on the robot with the
 * console's `t`: 16.9 us, against 0.49 us for a pixel sent, so a window is
 * worth 34 pixels repainted for nothing.
 */
#define WINDOW_COST_PX 34

/* A solid stretch narrower than this is repainted with the rest of its row:
 * leaving it out would split the row in two for next to nothing. */
#define KEEP_MIN_PX 8

/*
 * Where one scanline of an eye may differ between the picture on the panel
 * and the new one: from the leftmost pixel either of them lights to the
 * rightmost, minus the stretch in the middle that was solid white and still
 * is. That leaves [a0, a1) on the left and [b0, b1) on the right, or only the
 * first when there is no such middle.
 */
static void row_changes(const RowSpan *was, const RowSpan *now, int same_fill, int *a0, int *a1, int *b0, int *b1)
{
    *a0 = *a1 = *b0 = *b1 = 0;
    int w = was->out_lo <= was->out_hi;
    int n = now->out_lo <= now->out_hi;
    if (!w && !n) {
        return;
    }
    int lo = !w ? now->out_lo : (!n || was->out_lo < now->out_lo) ? was->out_lo : now->out_lo;
    int hi = !w ? now->out_hi : (!n || was->out_hi > now->out_hi) ? was->out_hi : now->out_hi;
    /* an empty stretch is (1, 0), so an intersection with one comes out empty too */
    int in_lo = was->in_lo > now->in_lo ? was->in_lo : now->in_lo;
    int in_hi = was->in_hi < now->in_hi ? was->in_hi : now->in_hi;
    *a0 = lo;
    if (same_fill && w && n && in_hi - in_lo + 1 >= KEEP_MIN_PX) {
        *a1 = in_lo;
        *b0 = in_hi + 1;
        *b1 = hi + 1;
    } else {
        *a1 = hi + 1;
    }
}

/*
 * The changed stretches of consecutive scanlines are gathered into rectangles,
 * because the panel is written a window at a time. A band grows downwards for
 * as long as the pixels it repaints for nothing, those inside its box but
 * outside the stretches, cost less than opening another window would. Along a
 * straight vertical edge that makes one tall thin window; round an end cap, a
 * staircase of a few short ones.
 */
typedef struct {
    int x0, x1, y0, rows;
    int needed; /* pixels the stretches themselves add up to */
} Band;

static void band_flush(Renderer *r, Band *b, EyeRaster eyes[2], const PixelSink *sink)
{
    if (!b->rows) {
        return;
    }
    Rect area = { b->x0, b->y0, b->x1, b->y0 + b->rows };
    paint(&area, eyes, sink);
    r->last_rects++;
    r->last_pixels += rect_area(&area);
    b->rows = 0;
}

static void band_row(Renderer *r, Band *b, int y, int x0, int x1, EyeRaster eyes[2], const PixelSink *sink)
{
    if (x0 >= x1) {
        band_flush(r, b, eyes, sink);
        return;
    }
    if (b->rows) {
        int nx0 = x0 < b->x0 ? x0 : b->x0;
        int nx1 = x1 > b->x1 ? x1 : b->x1;
        int needed = b->needed + (x1 - x0);
        if ((nx1 - nx0) * (b->rows + 1) - needed <= WINDOW_COST_PX) {
            b->x0 = nx0;
            b->x1 = nx1;
            b->rows++;
            b->needed = needed;
            return;
        }
        band_flush(r, b, eyes, sink);
    }
    b->x0 = x0;
    b->x1 = x1;
    b->y0 = y;
    b->rows = 1;
    b->needed = x1 - x0;
}

void renderer_update(Renderer *r, const EyeShape shapes[2], const PixelSink *sink)
{
    if (!r->primed) {
        renderer_full(r, shapes, sink);
        return;
    }
    EyeRaster eyes[2];
    int was = r->flip;
    int now = was ^ 1;
    for (int i = 0; i < 2; i++) {
        raster_prepare(&shapes[i], &eyes[i]);
        solve_rows(&eyes[i], r->rows[now][i]);
    }

    /*
     * Each band is painted with the full new picture inside it, both eyes
     * included, so the order does not matter, and where the changes of the two
     * eyes overlap the shared pixels are simply sent twice with the same
     * values. Nothing is missed: a pixel changes only if one eye's coverage of
     * it does, and that eye's stretches contain it.
     */
    r->last_rects = 0;
    r->last_pixels = 0;
    for (int i = 0; i < 2; i++) {
        const RowSpan *p = r->rows[was][i];
        const RowSpan *c = r->rows[now][i];
        uint8_t fill = fill_of(&eyes[i]);
        int same_fill = fill == r->fill[i];
        int y0 = SCREEN_H, y1 = 0;
        if (!rect_empty(&r->prev[i])) {
            y0 = r->prev[i].y0;
            y1 = r->prev[i].y1;
        }
        if (!rect_empty(&eyes[i].box)) {
            if (eyes[i].box.y0 < y0) y0 = eyes[i].box.y0;
            if (eyes[i].box.y1 > y1) y1 = eyes[i].box.y1;
        }
        Band left = { 0, 0, 0, 0, 0 };
        Band right = { 0, 0, 0, 0, 0 };
        for (int y = y0; y < y1; y++) {
            int a0, a1, b0, b1;
            row_changes(&p[y], &c[y], same_fill, &a0, &a1, &b0, &b1);
            band_row(r, &left, y, a0, a1, eyes, sink);
            band_row(r, &right, y, b0, b1, eyes, sink);
        }
        band_flush(r, &left, eyes, sink);
        band_flush(r, &right, eyes, sink);
        r->prev[i] = eyes[i].box;
        r->fill[i] = fill;
    }
    r->flip = now;
}
