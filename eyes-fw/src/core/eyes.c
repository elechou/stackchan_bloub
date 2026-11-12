#include "eyes.h"
#include "fmath.h"

/* ------------------------------------------------------------ expressions */

#define EYE(w, h, tilt, open) { (w), (h), (tilt), (open) }
/* Both eyes alike, tilts mirrored. Same helper as bloub's `pair`. */
#define PAIR(w, h, tilt, open) { EYE(w, h, tilt, open), EYE(w, h, -(tilt), open) }
#define EXPR(yaw, pitch, roll, split, ...) { .p = { (yaw), (pitch), (roll), (split), __VA_ARGS__ } }

/*
 * The 16 rest expressions of bloub (src/bot/expressions.ts), same order, same
 * numbers, plus the two face-only states of its catalogue (wink, wide) whose
 * eyes were measured off the reference video (src/bot/states.ts).
 */
const Expr EYES_EXPR[] = {
    /* neutre    */ EXPR(28.49f, 28.62f, -13.0f, 15.46f, PAIR(0.186f, 0.412f, 0.0f, 1.0f)),
    /* attentif  */ EXPR(4.0f, 5.0f, -4.0f, 16.0f, PAIR(0.21f, 0.44f, 0.0f, 1.0f)),
    /* surpris   */ EXPR(3.0f, -3.0f, 0.0f, 19.0f, PAIR(0.45f, 0.47f, 0.0f, 1.0f)),
    /* excite    */ EXPR(6.0f, -14.0f, 0.0f, 19.5f, PAIR(0.4f, 0.56f, -10.0f, 1.0f)),
    /* heureux   */ EXPR(5.0f, 9.0f, 0.0f, 17.0f, PAIR(0.27f, 0.17f, 14.0f, 1.0f)),
    /* hilare    */ EXPR(4.0f, 14.0f, 0.0f, 18.0f, PAIR(0.34f, 0.13f, 20.0f, 1.0f)),
    /* colere    */ EXPR(3.0f, 7.0f, 0.0f, 17.0f, PAIR(0.34f, 0.15f, 30.0f, 1.0f)),
    /* triste    */ EXPR(3.0f, -13.0f, 0.0f, 16.0f, PAIR(0.22f, 0.4f, -28.0f, 1.0f)),
    /* effraye   */ EXPR(2.0f, -20.0f, 0.0f, 20.5f, PAIR(0.4f, 0.6f, 0.0f, 1.0f)),
    /* mefiant   */ EXPR(12.0f, 6.0f, -6.0f, 16.0f, { EYE(0.21f, 0.4f, 0.0f, 1.0f), EYE(0.22f, 0.15f, 0.0f, 1.0f) }),
    /* confus    */ EXPR(-14.0f, 3.0f, 8.0f, 16.5f, { EYE(0.2f, 0.44f, -18.0f, 1.0f), EYE(0.28f, 0.17f, 14.0f, 1.0f) }),
    /* curieux   */ EXPR(16.0f, -9.0f, -15.0f, 16.5f, { EYE(0.24f, 0.46f, -8.0f, 1.0f), EYE(0.2f, 0.38f, -8.0f, 1.0f) }),
    /* fier      */ EXPR(5.0f, 17.0f, 0.0f, 17.0f, PAIR(0.3f, 0.15f, 18.0f, 1.0f)),
    /* timide    */ EXPR(-19.0f, -14.0f, -7.0f, 14.0f, PAIR(0.17f, 0.3f, 0.0f, 1.0f)),
    /* blase     */ EXPR(-22.0f, 2.0f, 0.0f, 16.0f, PAIR(0.3f, 0.12f, 0.0f, 1.0f)),
    /* somnolent */ EXPR(6.0f, -9.0f, -3.0f, 16.0f, PAIR(0.2f, 0.42f, 0.0f, 0.42f)),
    /* wink      */ EXPR(-5.37f, 4.55f, 6.7f, 16.25f, { EYE(0.236f, 0.464f, 0.0f, 1.0f), EYE(0.447f, 0.089f, 0.0f, 1.0f) }),
    /* wide      */ EXPR(6.92f, -21.96f, 11.6f, 18.43f, PAIR(0.356f, 0.875f, 0.0f, 1.0f)),
};

const char *const EYES_EXPR_NAME[] = {
    "neutre", "attentif", "surpris", "excite", "heureux", "hilare", "colere", "triste", "effraye",
    "mefiant", "confus", "curieux", "fier", "timide", "blase", "somnolent", "wink", "wide",
};

const int EYES_EXPR_COUNT = (int)(sizeof(EYES_EXPR) / sizeof(EYES_EXPR[0]));

/* ------------------------------------------------------------------ noise */

/*
 * Gaze drift, from bloub's `liveliness`: sums of three harmonics on periods that
 * are coprime, so the wander never visibly repeats. bloub evaluates them from an
 * absolute time; here each oscillator carries its own phase instead, advanced by
 * dt. A float clock loses its sub-millisecond bits after a few hours and the eyes
 * would start to stutter. Phases wrap, so this runs for ever.
 */
static const float NOISE_PERIOD[EYES_NOISE_COUNT] = { 11.3f, 3.7f, 9.1f, 4.3f, 13.7f, 7.9f, 5.3f };
static const float NOISE_SEED[EYES_NOISE_COUNT] = { 0.4f, 2.1f, 1.3f, 0.7f, 3.2f, 1.9f, 0.3f };

static float loop_noise(float phase, float seed)
{
    return 0.55f * f_sin_turns(phase + seed * F_RAD_TO_TURNS)
         + 0.30f * f_sin_turns(2.0f * phase + (seed * 1.7f + 1.1f) * F_RAD_TO_TURNS)
         + 0.15f * f_sin_turns(3.0f * phase + (seed * 2.3f + 2.4f) * F_RAD_TO_TURNS);
}

/* -------------------------------------------------------------------- rng */

static uint32_t rng_next(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static float rng_unit(uint32_t *s) { return (float)(rng_next(s) >> 8) * (1.0f / 16777216.0f); }

/* --------------------------------------------------------------- geometry */

typedef struct {
    float x, y, z;
} Vec3;

/* Rotate two vectors of an orthonormal frame within their common plane. */
static void spin(Vec3 *u, Vec3 *v, float degrees)
{
    float c = f_cos_turns(degrees * F_DEG_TO_TURNS);
    float s = f_sin_turns(degrees * F_DEG_TO_TURNS);
    Vec3 nu = { u->x * c + v->x * s, u->y * c + v->y * s, u->z * c + v->z * s };
    Vec3 nv = { v->x * c - u->x * s, v->y * c - u->y * s, v->z * c - u->z * s };
    *u = nu;
    *v = nv;
}

void eyes_pose(const Expr *e, const EyesLife *life, float scale, float center_x, float center_y,
               EyeShape out[2])
{
    /* Screen frame: x right, y down, z towards the viewer. */
    Vec3 f = { 0.0f, 0.0f, 1.0f };
    Vec3 right = { 1.0f, 0.0f, 0.0f };
    Vec3 down = { 0.0f, 1.0f, 0.0f };

    spin(&f, &right, e->p.yaw + life->d_yaw);    /* yaw: forward swings towards right */
    spin(&down, &f, e->p.pitch + life->d_pitch); /* pitch: forward swings up */
    spin(&right, &down, e->p.roll + life->d_roll); /* roll: the head tilts in its own plane */

    for (int i = 0; i < 2; i++) {
        /* index 0 is the eye on the left of the head, index 1 the one on the right */
        Vec3 ef = f;
        Vec3 er = right;
        spin(&ef, &er, e->p.split * (i == 0 ? -1.0f : 1.0f));

        const EyeCfg *cfg = &e->p.eye[i];
        EyeShape *o = &out[i];

        float depth = ef.z;
        if (depth <= 0.02f) {
            o->alpha = 0.0f;
            o->cx = o->cy = 0.0f;
            o->m00 = o->m11 = 1.0f;
            o->m01 = o->m10 = 0.0f;
            o->hw = o->hh = 0.0f;
            continue;
        }

        /* The eye's own tilt composes with the sphere's tangent frame. */
        float cp = f_cos_turns(cfg->tilt * F_DEG_TO_TURNS);
        float sp = f_sin_turns(cfg->tilt * F_DEG_TO_TURNS);
        float ax = er.x * cp + down.x * sp;
        float ay = er.y * cp + down.y * sp;
        float bx = -er.x * sp + down.x * cp;
        float by = -er.y * sp + down.y * cp;

        /* A blink is a VERTICAL squash on screen, applied after everything else,
         * not a shortening along the capsule's own axis. */
        float lid = f_min(life->lid, cfg->open);
        float k = 0.06f + 0.94f * f_clamp(lid, 0.0f, 1.0f);

        o->m00 = ax;
        o->m01 = bx;
        o->m10 = ay * k;
        o->m11 = by * k;
        o->cx = center_x + (ef.x + life->drift_x) * scale;
        o->cy = center_y + (ef.y + life->drift_y) * scale;
        o->hw = f_max(cfg->w * scale, 0.01f) * 0.5f;
        o->hh = f_max(cfg->h * scale, 0.01f) * 0.5f;
        o->alpha = f_clamp(depth / 0.12f, 0.0f, 1.0f);
    }
}

/* -------------------------------------------------------------- animation */

/*
 * Easing of the glide between two expressions.
 *
 * bloub morphs with easeOutQuint, 1 - (1-k)^5: an exponential tail, which is its
 * signature, but it leaves at full speed. With expressions changing at random
 * the eyes may have half the screen to cross, and that first frame reads as a
 * jump. Multiplying the tail by (1 + 5k) cancels the initial velocity and keeps
 * the same decay: the curve starts from rest, peaks at k = 1/5 and settles
 * exactly like the original. No overshoot, same as bloub.
 */
static float ease(float k)
{
    float u = 1.0f - k;
    float u2 = u * u;
    return 1.0f - u2 * u2 * u * (1.0f + 5.0f * k);
}

static void bag_refill(EyesAnim *a, int avoid_first)
{
    int n = EYES_EXPR_COUNT;
    for (int i = 0; i < n; i++) {
        a->bag[i] = (uint8_t)i;
    }
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(rng_next(&a->rng) % (uint32_t)(i + 1));
        uint8_t t = a->bag[i];
        a->bag[i] = a->bag[j];
        a->bag[j] = t;
    }
    /* never the same expression twice in a row across a refill */
    if (n > 1 && a->bag[0] == avoid_first) {
        int j = 1 + (int)(rng_next(&a->rng) % (uint32_t)(n - 1));
        uint8_t t = a->bag[0];
        a->bag[0] = a->bag[j];
        a->bag[j] = t;
    }
    a->bag_pos = 0;
}

static float hold_time(EyesAnim *a)
{
    return EYES_HOLD_MIN_S + rng_unit(&a->rng) * (EYES_HOLD_MAX_S - EYES_HOLD_MIN_S);
}

void eyes_init(EyesAnim *a, uint32_t seed, float scale, float center_x, float center_y)
{
    a->scale = scale;
    a->center_x = center_x;
    a->center_y = center_y;
    a->rng = seed ? seed : 0x5eedu;
    for (int i = 0; i < 8; i++) {
        rng_next(&a->rng);
    }

    /* Wake up on the rest pose, then start wandering. */
    a->expr_id = 0;
    a->cur = a->from = a->to = EYES_EXPR[0];
    a->morph_k = 1.0f;
    a->hold_s = 2.0f + rng_unit(&a->rng) * 1.5f;
    bag_refill(a, a->expr_id);

    a->blink_next_s = 1.4f;
    a->blink_t = -1.0f;
    a->blink_double = 0;

    for (int i = 0; i < EYES_NOISE_COUNT; i++) {
        a->phase[i] = rng_unit(&a->rng);
    }
}

void eyes_force(EyesAnim *a, int expr_id)
{
    a->expr_id = expr_id;
    a->cur = a->from = a->to = EYES_EXPR[expr_id];
    a->morph_k = 1.0f;
}

/* Measured on the reference video: a blink lasts 1 to 2 frames at 10 fps. */
#define BLINK_DUR_S 0.18f

void eyes_step(EyesAnim *a, float dt, EyeShape out[2])
{
    /* --- which expression ------------------------------------------------ */
    a->hold_s -= dt;
    if (a->hold_s <= 0.0f) {
        if (a->bag_pos >= EYES_EXPR_COUNT) {
            bag_refill(a, a->expr_id);
        }
        a->expr_id = a->bag[a->bag_pos++];
        /* Leave from what is on screen, not from the previous target: if a
         * change ever lands inside a glide, the eyes still do not jump. */
        a->from = a->cur;
        a->to = EYES_EXPR[a->expr_id];
        a->morph_k = 0.0f;
        a->hold_s = hold_time(a);
    }

    /* --- glide ----------------------------------------------------------- */
    if (a->morph_k < 1.0f) {
        a->morph_k += dt / EYES_MORPH_S;
        if (a->morph_k >= 1.0f) {
            a->morph_k = 1.0f;
            a->cur = a->to;
        } else {
            float g = ease(a->morph_k);
            for (int i = 0; i < EXPR_FLOATS; i++) {
                a->cur.v[i] = f_lerp(a->from.v[i], a->to.v[i], g);
            }
        }
    }

    /* --- blinks ---------------------------------------------------------- */
    a->blink_next_s -= dt;
    if (a->blink_next_s <= 0.0f) {
        a->blink_t = 0.0f;
        if (a->blink_double) {
            /* second beat of a double blink */
            a->blink_double = 0;
            a->blink_next_s = 0.24f;
        } else {
            /* 1.9 to 4.6 s between blinks, a double one now and then */
            a->blink_next_s = 1.9f + rng_unit(&a->rng) * 2.7f;
            a->blink_double = rng_unit(&a->rng) < 0.18f;
        }
    }
    float lid = 1.0f;
    if (a->blink_t >= 0.0f) {
        float k = a->blink_t / BLINK_DUR_S;
        if (k > 1.0f) {
            a->blink_t = -1.0f;
        } else {
            /* closes fast, reopens a little slower */
            lid = k < 0.45f ? 1.0f - k / 0.45f : (k - 0.45f) / 0.55f;
            a->blink_t += dt;
        }
    }

    /* --- life at rest ---------------------------------------------------- */
    for (int i = 0; i < EYES_NOISE_COUNT; i++) {
        float p = a->phase[i] + dt / NOISE_PERIOD[i];
        if (p >= 1.0f) {
            p -= 1.0f;
        }
        a->phase[i] = p;
    }
#define NOISE(i) loop_noise(a->phase[i], NOISE_SEED[i])
    EyesLife life;
    life.d_yaw = NOISE(0) * 5.5f + NOISE(1) * 1.6f;
    life.d_pitch = NOISE(2) * 4.2f + NOISE(3) * 1.3f;
    life.d_roll = NOISE(4) * 2.2f;
    life.drift_x = NOISE(5) * 0.006f;
    life.drift_y = NOISE(6) * 0.007f;
    life.lid = lid;
#undef NOISE

    eyes_pose(&a->cur, &life, a->scale, a->center_x, a->center_y, out);
}
