/*
 * The face: two capsules painted on an invisible sphere.
 *
 * The geometry is a port of bloub's eye model (src/bot/face.ts, expressions.ts
 * and the eye part of engine.ts). The body is gone on purpose: the screen is
 * black and only the eyes are drawn. What is kept, to the digit, is the
 * geometry of the eyes and the numbers of each expression.
 *
 * What is not bloub's is the behaviour. bloub plays a montage; this is a pet
 * that sits on a desk for hours, so it has moods it settles into slowly, stays
 * in for a long while and leaves by a plausible way (see eyes.c).
 */
#ifndef EYES_H
#define EYES_H

#include <stdint.h>

/* ---- the face and the glass -------------------------------------------- */

/*
 * The panel is 320x240 px on about 40x30 mm, 8 px per mm, in the middle of a
 * front that is 50 mm square and black all over. With the eyes lit nobody
 * sees where the glass ends: the whole square reads as the face. So the ball
 * the eyes are painted on is sized to that square, not to the glass.
 *
 * bloub has two bodies that bracket this front. Its rounded square is 0.959
 * radius half-wide, which puts the radius at 208 px for a 400 px square. Its
 * capsule is 2.08 by 1.24 radii, which puts it at 192 px for the 400x240 band
 * of the front that the glass can light. The ball inscribed in the square,
 * 200 px, sits between the two. It is also about as large as the glass
 * allows: at 208 the widest-open expression no longer fits it with its gaze
 * drift.
 */
#define EYES_FACE_PX 400.0f
#define EYES_SCALE_PX (EYES_FACE_PX * 0.5f)

/* The eyes can only be shown on the glass. They are kept this far inside it,
 * so that a lit shape never ends on the straight line of the panel's border. */
#define EYES_MARGIN_PX 8.0f

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

/* An expression, and where on the glass the fit sets it down (pixels). */
#define PLACED_FLOATS (EXPR_FLOATS + 2)
typedef union {
    struct {
        Expr e;
        float off_x, off_y;
    } p;
    float v[PLACED_FLOATS];
} Placed;

/* One eye, ready to rasterise: a capsule of half extents (hw, hh) centred on
 * the origin, mapped to the screen by  p = M * q + c  (pixels, y down). */
typedef struct {
    float cx, cy;
    float m00, m01, m10, m11;
    float hw, hh;
    float alpha; /* 0 = not drawn */
} EyeShape;

/* The expressions double as the pet's moods: one row each, same order. */
enum {
    EYES_NEUTRE,
    EYES_ATTENTIF,
    EYES_SURPRIS,
    EYES_EXCITE,
    EYES_HEUREUX,
    EYES_HILARE,
    EYES_COLERE,
    EYES_TRISTE,
    EYES_EFFRAYE,
    EYES_MEFIANT,
    EYES_CONFUS,
    EYES_CURIEUX,
    EYES_FIER,
    EYES_TIMIDE,
    EYES_BLASE,
    EYES_SOMNOLENT,
    EYES_WINK,
    EYES_WIDE,
    EYES_ENDORMI,
    EYES_EXPR_COUNT
};

extern const Expr EYES_EXPR[EYES_EXPR_COUNT];
extern const char *const EYES_EXPR_NAME[EYES_EXPR_COUNT];

#define EYES_NOISE_COUNT 7

typedef struct {
    float scale;
    float w, h; /* the glass, in pixels */

    /* the glide in progress: from what was on screen to a declared pose */
    Placed from, to, cur;
    float glide_k; /* 0..1, 1 = arrived */
    float glide_s; /* how long this one takes */
    int glide_dart; /* a glance, not a change of mood: the sharper curve */

    /* what the pet is up to */
    int expr_id;    /* its mood: a row of EYES_EXPR */
    float stay_s;   /* time left in this mood, counted once it has settled */
    float hold_s;   /* time left before it next looks elsewhere, or back */
    int away;       /* it is looking somewhere else than its mood does */
    int trip;       /* where it is in the little story it is living */
    int trip_start; /* which story, so that it is not told twice in a row */

    /* lids */
    float blink_next_s; /* countdown to the next blink */
    float blink_t;      /* time into the current blink, < 0 when not blinking */
    float blink_close_s, blink_shut_s, blink_open_s; /* its three phases */
    int blink_double;   /* the next blink is followed by a second one */

    /* how still it is; both follow the mood slowly */
    float wander; /* share of the gaze drift let through */
    float breath; /* how far breathing lifts the eyes, in sphere radii */
    float breath_phase;

    float phase[EYES_NOISE_COUNT]; /* gaze drift oscillators, in turns */
    uint32_t rng;
} EyesAnim;

/* `screen_w` x `screen_h` is the glass; the face is centred on it. */
void eyes_init(EyesAnim *a, uint32_t seed, float scale, float screen_w, float screen_h);

/* Advance by dt seconds and return the two eyes to draw. */
void eyes_step(EyesAnim *a, float dt, EyeShape out[2]);

/* Put the pet in this mood now, reaching it in `settle_s` seconds (0 = the
 * mood's own pace). It goes on living from there. For the console and tests. */
void eyes_show(EyesAnim *a, int expr_id, float settle_s);

/* ---- pure geometry ----------------------------------------------------- */

typedef struct {
    float d_yaw, d_pitch, d_roll; /* degrees, added to the gaze */
    float lid;                    /* 1 = open */
    float drift_x, drift_y;       /* in sphere radii */
} EyesLife;

/* Expression + liveliness offsets -> two screen shapes. */
void eyes_pose(const Expr *e, const EyesLife *life, float scale, float center_x, float center_y,
               EyeShape out[2]);

/* Half width and half height of the box an eye covers on screen. Exact. */
void eyes_extent(const EyeShape *s, float *ex, float *ey);

/*
 * Where to set an expression down so that it stays on the glass: the smallest
 * shift, common to both eyes, that keeps the pair EYES_MARGIN_PX inside the
 * screen through the whole of its gaze drift. Zero for most expressions.
 */
void eyes_fit(const Expr *e, float scale, float screen_w, float screen_h, float *off_x, float *off_y);

/* For the simulator's statistics: is this mood one of the two it rests in. */
int eyes_is_rest(int expr_id);

#endif
