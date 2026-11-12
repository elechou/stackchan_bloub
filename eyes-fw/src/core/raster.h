/*
 * Anti-aliased rasteriser for the eyes, and the frame update.
 *
 * There is no framebuffer. Each frame only what changed is recomputed, a
 * packet of pixels at a time, and streamed straight to the panel. The panel
 * keeps the rest. And what changes, when a white shape moves a little on a
 * black screen, is its rim: the inside was white and still is. So the cost of
 * a frame follows the outline of the eyes, not their area, and that is what
 * makes 60 fps possible over SPI with eyes this large.
 *
 * The "sink" is an interface: the firmware plugs the LCD in, the simulator
 * plugs an in-memory image in, and both run the very same code.
 */
#ifndef RASTER_H
#define RASTER_H

#include <stdint.h>
#include "eyes.h"

#define SCREEN_W 320
#define SCREEN_H 240

typedef struct {
    int x0, y0, x1, y1; /* half-open; empty when x0 >= x1 */
} Rect;

static inline int rect_empty(const Rect *r) { return r->x0 >= r->x1 || r->y0 >= r->y1; }

/*
 * One scanline of one eye, in screen x. Outside [out_lo, out_hi] the eye draws
 * nothing; inside [in_lo, in_hi] every pixel is fully covered. Either stretch
 * is empty when its lo is above its hi.
 */
typedef struct {
    int16_t out_lo, out_hi;
    int16_t in_lo, in_hi;
} RowSpan;

typedef struct {
    Rect box;                 /* clipped to the screen, AA margin included */
    float cx, cy;
    float i00, i01, i10, i11; /* inverse of the eye matrix: screen -> capsule */
    float core;               /* half length of the capsule's core segment */
    float r;                  /* its radius */
    int vertical;             /* core runs along the capsule's y axis */
    float rin, rout;          /* radii of the sure-inside / sure-outside capsules */
    float rin2, rout2;
    float alpha;

    /* one step along x, in the capsule's own frame: u across it, v along its core */
    float su, sv, inv_su, inv_sv;
    float a, inv_a; /* su^2 + sv^2 */

    /* every scanline already solved, indexed by y; or 0, to solve them as they come */
    const RowSpan *rows;

    /* the scanline being drawn; x is counted as k = x - box.x0 */
    int row_y;
    float row_qx0, row_qy0; /* capsule-space position of pixel k = 0 */
    int out_lo, out_hi;     /* outside [out_lo, out_hi] nothing is drawn */
    int in_lo, in_hi;       /* inside [in_lo, in_hi] every pixel is fully covered */
} EyeRaster;

void raster_prepare(const EyeShape *shape, EyeRaster *out);

/* Coverage of one eye on scanline y, for x in [xa, xb). `cov` is indexed by
 * absolute x and holds 0..255; the eye is max-blended into it. */
void raster_row(EyeRaster *e, int y, int xa, int xb, uint8_t *cov);

/* For the simulator: 0 forces every pixel through the exact edge maths, with
 * no span shortcut. The pictures must come out identical either way. */
extern int raster_spans_enabled;

/*
 * Where the pixels go. Pixels are RGB565, already byte-swapped for the wire.
 * After begin(), push() receives the window's pixels in scan order, a few at a
 * time (RASTER_CHUNK at most); a scanline arrives in several calls.
 */
#define RASTER_CHUNK 32

typedef struct {
    void (*begin)(void *ctx, int x0, int y0, int x1, int y1);
    void (*push)(void *ctx, const uint16_t *px, int n);
    void (*end)(void *ctx);
    void *ctx;
} PixelSink;

typedef struct {
    /* Every scanline of each eye, for the picture on the panel and for the one
     * being drawn: rows[flip] is the panel's, rows[flip ^ 1] the next. */
    RowSpan rows[2][2][SCREEN_H];
    int flip;
    Rect prev[2];    /* the box each eye covered on the previous frame */
    uint8_t fill[2]; /* and how white its inside was */
    int primed;
    /* statistics of the last frame */
    int last_rects;
    int last_pixels;
} Renderer;

void renderer_init(Renderer *r);

/* Paint the whole screen once (black background + eyes). */
void renderer_full(Renderer *r, const EyeShape shapes[2], const PixelSink *sink);

/* Repaint only what changed since the previous call. */
void renderer_update(Renderer *r, const EyeShape shapes[2], const PixelSink *sink);

/* One finished scanline of the current picture, x in [x0, x1), as it goes on
 * the wire. Used by the painter, and by the firmware's self-check. */
void renderer_line(const EyeShape shapes[2], int y, int x0, int x1, uint16_t *out);

/* 0..255 coverage -> wire-order RGB565, gamma corrected. */
extern const uint16_t RASTER_LUT[256];

#endif
