/*
 * The face: two capsules painted on an invisible sphere.
 *
 * This is a port of bloub's eye model (src/bot/face.ts, expressions.ts and the
 * eye part of engine.ts). The body is gone on purpose: the screen is black and
 * only the eyes are drawn, so everything that concerned the silhouette is left
 * out. What is kept, to the digit, is the geometry of the eyes and the numbers
 * of each expression.
 */
#ifndef EYES_H
#define EYES_H

#include <stdint.h>

/* ---- tunables ---------------------------------------------------------- */

/* Radius of the invisible sphere, in pixels. bloub uses 100 units for a ball;
 * here it only sets how large the eyes are and how far they travel. 136 is the
 * largest value that keeps every expression on a 320x240 panel, drift included. */
#define EYES_SCALE_PX 136.0f

/* How long one expression is held, morph included. */
#define EYES_HOLD_MIN_S 3.0f
#define EYES_HOLD_MAX_S 7.0f

/* Duration of the glide from one expression to the next. */
#define EYES_MORPH_S 0.70f

/* ---- model ------------------------------------------------------------- */

typedef struct {
    float w;    /* width (short axis at rest), in sphere radii */
    float h;    /* height, in sphere radii */
    float tilt; /* own tilt in degrees, positive = top leans right */
    float open; /* 1 = open, 0 = closed (vertical squash on screen) */
} EyeCfg;

#define EXPR_FLOATS 12
typedef union {
    struct {
        float yaw;   /* degrees, positive = looks right */
        float pitch; /* degrees, positive = looks up */
        float roll;  /* degrees, head tilt */
        float split; /* half the angle between the eyes on the sphere, degrees */
        EyeCfg eye[2];
    } p;
    float v[EXPR_FLOATS];
} Expr;

/* One eye, ready to rasterise: a capsule of half extents (hw, hh) centred on
 * the origin, mapped to the screen by  p = M * q + c  (pixels, y down). */
typedef struct {
    float cx, cy;
    float m00, m01, m10, m11;
    float hw, hh;
    float alpha; /* 0 = not drawn */
} EyeShape;

extern const Expr EYES_EXPR[];
extern const char *const EYES_EXPR_NAME[];
extern const int EYES_EXPR_COUNT;

#define EYES_NOISE_COUNT 7

typedef struct {
    float scale;
    float center_x, center_y;

    Expr from, to, cur;
    float morph_k;    /* 0..1, 1 = settled */
    float hold_s;     /* time left before the next change */
    int expr_id;

    uint8_t bag[32];  /* shuffle bag: every expression once before any repeat */
    int bag_pos;

    float blink_next_s; /* countdown to the next blink */
    float blink_t;      /* time into the current blink, < 0 when not blinking */
    int blink_double;   /* the next blink is followed by a second one */

    float phase[EYES_NOISE_COUNT]; /* gaze drift oscillators, in turns */
    uint32_t rng;
} EyesAnim;

void eyes_init(EyesAnim *a, uint32_t seed, float scale, float center_x, float center_y);

/* Advance by dt seconds and return the two eyes to draw. */
void eyes_step(EyesAnim *a, float dt, EyeShape out[2]);

/* Jump straight to an expression, no morph. For tests and contact sheets. */
void eyes_force(EyesAnim *a, int expr_id);

/* Pure geometry: expression + liveliness offsets -> two screen shapes. */
typedef struct {
    float d_yaw, d_pitch, d_roll; /* degrees, added to the gaze */
    float lid;                    /* 1 = open */
    float drift_x, drift_y;       /* in sphere radii */
} EyesLife;

void eyes_pose(const Expr *e, const EyesLife *life, float scale, float center_x, float center_y,
               EyeShape out[2]);

#endif
