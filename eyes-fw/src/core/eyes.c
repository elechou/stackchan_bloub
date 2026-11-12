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
 *
 * The last row is the only one that is not bloub's: the pet asleep. Its eyes
 * are the closed eye bloub measured for the wink, a dash wider than the open
 * eye, on both sides, with the head a little down. Their own tilt only undoes
 * what the sphere does to a dash set that low, so that the two lie parallel.
 */
const Expr EYES_EXPR[EYES_EXPR_COUNT] = {
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
    /* endormi   */ EXPR(3.0f, -12.0f, -3.0f, 18.0f, PAIR(0.447f, 0.089f, -4.0f, 1.0f)),
};

const char *const EYES_EXPR_NAME[EYES_EXPR_COUNT] = {
    "neutre", "attentif", "surpris", "excite", "heureux", "hilare", "colere", "triste", "effraye", "mefiant",
    "confus", "curieux", "fier", "timide", "blase", "somnolent", "wink", "wide", "endormi",
};

/* ------------------------------------------------------------------ moods */

/*
 * How the pet carries each expression. This table and the stories below are
 * the whole of its character: change a number here and it becomes livelier,
 * lazier, more or less of a daydreamer.
 */
enum {
    LIDS_NORMAL, /* blinks every 2 to 5 s, a double one now and then */
    LIDS_STARE,  /* half as often: something has its attention */
    LIDS_HEAVY,  /* slow, and they stay shut a moment: it is falling asleep */
    LIDS_STILL,  /* no blink at all */
};

typedef struct {
    float settle_s;               /* how long the face takes to get there */
    float stay_min_s, stay_max_s; /* how long it stays, once there */
    float glance_s;               /* mean time between two looks elsewhere; 0 = it does not look around */
    float wander;                 /* share of the idle gaze drift it keeps: 1 dreamy, 0 fixed */
    uint8_t lids;
    uint8_t gesture; /* left as fast as it was made, whatever comes next */
    float breath;    /* how far breathing lifts the eyes, in sphere radii */
} Mood;

static const Mood MOOD[EYES_EXPR_COUNT] = {
    /*                   settle   stay       glance wander lids */
    [EYES_NEUTRE]    = { 3.5f, 18.0f, 50.0f, 9.0f, 1.00f, LIDS_NORMAL, 0, 0.0f },
    [EYES_ATTENTIF]  = { 2.6f, 12.0f, 32.0f, 12.0f, 0.50f, LIDS_STARE, 0, 0.0f },
    [EYES_SURPRIS]   = { 0.9f, 2.0f, 4.0f, 0.0f, 0.35f, LIDS_STARE, 0, 0.0f },
    [EYES_EXCITE]    = { 1.4f, 3.0f, 6.0f, 0.0f, 1.00f, LIDS_NORMAL, 0, 0.0f },
    [EYES_HEUREUX]   = { 2.4f, 5.0f, 11.0f, 0.0f, 0.80f, LIDS_NORMAL, 0, 0.0f },
    [EYES_HILARE]    = { 1.5f, 2.5f, 5.0f, 0.0f, 0.80f, LIDS_NORMAL, 0, 0.0f },
    [EYES_COLERE]    = { 2.2f, 3.0f, 6.0f, 0.0f, 0.40f, LIDS_STARE, 0, 0.0f },
    [EYES_TRISTE]    = { 4.5f, 6.0f, 12.0f, 0.0f, 0.60f, LIDS_NORMAL, 0, 0.0f },
    [EYES_EFFRAYE]   = { 0.8f, 2.0f, 4.0f, 0.0f, 0.50f, LIDS_STARE, 0, 0.0f },
    [EYES_MEFIANT]   = { 2.2f, 4.0f, 8.0f, 2.8f, 0.50f, LIDS_STARE, 0, 0.0f },
    [EYES_CONFUS]    = { 2.4f, 4.0f, 8.0f, 3.2f, 0.90f, LIDS_NORMAL, 0, 0.0f },
    [EYES_CURIEUX]   = { 2.0f, 5.0f, 10.0f, 3.5f, 1.00f, LIDS_NORMAL, 0, 0.0f },
    [EYES_FIER]      = { 2.6f, 4.0f, 8.0f, 0.0f, 0.60f, LIDS_NORMAL, 0, 0.0f },
    [EYES_TIMIDE]    = { 2.6f, 4.0f, 8.0f, 0.0f, 0.70f, LIDS_NORMAL, 0, 0.0f },
    [EYES_BLASE]     = { 4.0f, 6.0f, 12.0f, 7.0f, 0.70f, LIDS_NORMAL, 0, 0.0f },
    [EYES_SOMNOLENT] = { 6.0f, 8.0f, 18.0f, 0.0f, 0.45f, LIDS_HEAVY, 0, 0.005f },
    [EYES_WINK]      = { 0.4f, 0.7f, 1.1f, 0.0f, 0.50f, LIDS_STILL, 1, 0.0f },
    [EYES_WIDE]      = { 0.8f, 1.6f, 2.6f, 0.0f, 0.35f, LIDS_STILL, 0, 0.0f },
    [EYES_ENDORMI]   = { 5.0f, 20.0f, 50.0f, 0.0f, 0.10f, LIDS_STILL, 0, 0.014f },
};

/* The two moods it rests in. Everything else is something that happens to it. */
int eyes_is_rest(int expr_id) { return expr_id == EYES_NEUTRE || expr_id == EYES_ATTENTIF; }

/*
 * What happens to it: little stories, each a few moods in a row, drawn by
 * weight when the previous one is over. A mood is never jumped into from
 * nowhere: anger comes through suspicion and fear through surprise, sleep
 * through calm and drowsiness, and every story ends at rest.
 *
 * The first two lines are simply "at rest", so their weight is what decides
 * how much of its time the pet spends doing nothing in particular.
 */
#define REST 0xFE /* whichever of the two resting moods, at random */
#define END 0xFF

static const uint8_t STORY[] = {
    /* weight, moods ..., END */
    22, EYES_NEUTRE, END,
    22, EYES_ATTENTIF, END,
    10, EYES_CURIEUX, EYES_ATTENTIF, END,
    8, EYES_HEUREUX, REST, END,
    6, EYES_NEUTRE, EYES_SOMNOLENT, EYES_NEUTRE, END,
    5, EYES_NEUTRE, EYES_SOMNOLENT, EYES_ENDORMI, EYES_SOMNOLENT, EYES_NEUTRE, END,
    5, EYES_NEUTRE, EYES_BLASE, EYES_NEUTRE, END,
    4, EYES_ATTENTIF, EYES_MEFIANT, EYES_ATTENTIF, END,
    3, EYES_HEUREUX, EYES_HILARE, EYES_HEUREUX, REST, END,
    3, EYES_FIER, EYES_HEUREUX, REST, END,
    3, EYES_CONFUS, EYES_CURIEUX, EYES_ATTENTIF, END,
    3, EYES_TIMIDE, EYES_NEUTRE, END,
    3, EYES_SURPRIS, EYES_ATTENTIF, END,
    2, EYES_SURPRIS, EYES_EXCITE, EYES_HEUREUX, REST, END,
    2, EYES_ATTENTIF, EYES_WINK, EYES_ATTENTIF, END,
    2, EYES_NEUTRE, EYES_TRISTE, EYES_NEUTRE, END,
    1, EYES_WIDE, EYES_SURPRIS, EYES_ATTENTIF, END,
    1, EYES_MEFIANT, EYES_COLERE, EYES_MEFIANT, EYES_NEUTRE, END,
    1, EYES_SURPRIS, EYES_EFFRAYE, EYES_TIMIDE, EYES_NEUTRE, END,
    0,
};

/* Of the two resting moods, how often REST means calm. */
#define REST_CALM 0.55f

/*
 * Looking elsewhere. A glance is a turn of the head, quick but not a snap:
 * DART_S for the shortest, plus DART_S_PER_DEG for every degree travelled.
 */
#define DART_S 0.30f
#define DART_S_PER_DEG 0.007f
/* How long a glance lasts, shortest and longest. Short ones are the rule. */
#define AWAY_MIN_S 0.7f
#define AWAY_MAX_S 3.0f
/* Chance that, from a glance, it looks at something else rather than back. */
#define AWAY_AGAIN 0.28f
/* Chance that a glance is at whoever is in front of it, when its mood looks away. */
#define LOOK_AT_YOU 0.30f
#define YOU_YAW 3.0f
#define YOU_PITCH 4.0f
/* The gaze never leaves the range the expressions themselves span. */
#define LOOK_YAW_MIN -26.0f
#define LOOK_YAW_MAX 30.0f
#define LOOK_PITCH_MIN -16.0f
#define LOOK_PITCH_MAX 30.0f

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

/* Amplitudes. loop_noise stays within 1, so the sums are exact bounds, and the
 * fit below relies on them. */
#define DRIFT_YAW_A 5.5f
#define DRIFT_YAW_B 1.6f
#define DRIFT_PITCH_A 4.2f
#define DRIFT_PITCH_B 1.3f
#define DRIFT_ROLL 2.2f
#define DRIFT_X 0.006f
#define DRIFT_Y 0.007f
#define BREATH_MAX 0.014f
#define BREATH_PERIOD_S 4.4f

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

static float rng_between(uint32_t *s, float lo, float hi) { return lo + rng_unit(s) * (hi - lo); }

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

/*
 * A capsule is a segment thickened by r. The matrix maps the segment to
 * another segment and the disc to an ellipse whose half extents are r times
 * the norm of each ROW of M, so this is the exact box, not a loose bound.
 */
void eyes_extent(const EyeShape *s, float *ex, float *ey)
{
    int vertical = s->hh >= s->hw;
    float r = vertical ? s->hw : s->hh;
    float core = (vertical ? s->hh : s->hw) - r;
    float sx = vertical ? s->m01 : s->m00; /* image of the core direction */
    float sy = vertical ? s->m11 : s->m10;
    *ex = core * f_abs(sx) + r * f_sqrt(s->m00 * s->m00 + s->m01 * s->m01);
    *ey = core * f_abs(sy) + r * f_sqrt(s->m10 * s->m10 + s->m11 * s->m11);
}

/* -------------------------------------------------------------------- fit */

/* Shift that brings [lo, hi] inside [a, b]; centred when it cannot fit. */
static float nudge(float lo, float hi, float a, float b)
{
    if (hi - lo >= b - a) {
        return 0.5f * ((a + b) - (lo + hi));
    }
    if (lo < a) {
        return a - lo;
    }
    if (hi > b) {
        return b - hi;
    }
    return 0.0f;
}

/*
 * bloub meets the same problem on its capsule body, which has no room at the
 * top: eyes placed on a sphere do not fit a shape that is not one. Its answer
 * (src/bot/eyefit.ts) is the one taken here, and for its reason.
 *
 * The shift is a translation common to both eyes, so their spacing, sizes and
 * tilts are untouched: the face is only set a little lower on a front that has
 * no room higher up. And it is worked out ONCE per pose, on the pose the eyes
 * are heading for, with the worst of the gaze drift already counted in; the
 * animation then glides between two such constants like between any two
 * numbers of an expression. Clamping the live picture frame by frame would be
 * shorter to write and would make the eyes slide along the border like along
 * a pane of glass, every time the drift pushed them there.
 *
 * bloub needs a search for this because its outlines are arbitrary. A screen
 * is a rectangle, so the two axes separate and each is a subtraction.
 */
void eyes_fit(const Expr *e, float scale, float screen_w, float screen_h, float *off_x, float *off_y)
{
    float lo_x = 1.0e9f, hi_x = -1.0e9f, lo_y = 1.0e9f, hi_y = -1.0e9f;
    EyesLife life = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };

    /* The corners of the drift bound everything in between. */
    for (int corner = 0; corner < 8; corner++) {
        life.d_yaw = (corner & 1) ? DRIFT_YAW_A + DRIFT_YAW_B : -(DRIFT_YAW_A + DRIFT_YAW_B);
        life.d_pitch = (corner & 2) ? DRIFT_PITCH_A + DRIFT_PITCH_B : -(DRIFT_PITCH_A + DRIFT_PITCH_B);
        life.d_roll = (corner & 4) ? DRIFT_ROLL : -DRIFT_ROLL;
        EyeShape s[2];
        eyes_pose(e, &life, scale, 0.5f * screen_w, 0.5f * screen_h, s);
        for (int i = 0; i < 2; i++) {
            if (s[i].alpha <= 0.0f) {
                continue;
            }
            float ex, ey;
            eyes_extent(&s[i], &ex, &ey);
            lo_x = f_min(lo_x, s[i].cx - ex);
            hi_x = f_max(hi_x, s[i].cx + ex);
            lo_y = f_min(lo_y, s[i].cy - ey);
            hi_y = f_max(hi_y, s[i].cy + ey);
        }
    }
    if (lo_x > hi_x) {
        *off_x = *off_y = 0.0f;
        return;
    }
    /* the small float of the centre, and breathing, come on top */
    float pad_x = DRIFT_X * scale;
    float pad_y = (DRIFT_Y + BREATH_MAX) * scale;
    *off_x = nudge(lo_x - pad_x, hi_x + pad_x, EYES_MARGIN_PX, screen_w - EYES_MARGIN_PX);
    *off_y = nudge(lo_y - pad_y, hi_y + pad_y, EYES_MARGIN_PX, screen_h - EYES_MARGIN_PX);
}

/* -------------------------------------------------------------- behaviour */

/*
 * The curve of a glide:  1 - (1-k)^n (1 + n k).  It leaves from rest, never
 * overshoots, and ends on an exponential tail, which is bloub's signature
 * (its own curve is the n = 5 tail without the factor that cancels the
 * initial speed).
 *
 * n sets where the speed peaks, at k = 1/n. A change of mood takes n = 3: the
 * face is a third of the way through its time before it moves fastest, and
 * spends the rest settling. A glance takes n = 5, a turn of the head that is
 * mostly over in its first third.
 */
static float ease(float k, int dart)
{
    float u = 1.0f - k;
    if (dart) {
        float u2 = u * u;
        return 1.0f - u2 * u2 * u * (1.0f + 5.0f * k);
    }
    return 1.0f - u * u * u * (1.0f + 3.0f * k);
}

/* Head for a pose, from whatever is on screen now. */
static void glide(EyesAnim *a, const Expr *target, float seconds, int dart)
{
    a->from = a->cur;
    a->to.p.e = *target;
    eyes_fit(target, a->scale, a->w, a->h, &a->to.p.off_x, &a->to.p.off_y);
    a->glide_k = 0.0f;
    a->glide_s = seconds;
    a->glide_dart = dart;
}

static void blink_start(EyesAnim *a)
{
    a->blink_t = 0.0f;
    if (MOOD[a->expr_id].lids == LIDS_HEAVY) {
        float u = rng_unit(&a->rng);
        a->blink_close_s = 0.20f;
        a->blink_shut_s = 0.05f + 0.75f * u * u;
        a->blink_open_s = 0.34f;
    } else {
        /* measured on bloub's reference video: 0.18 s, closing a little faster */
        a->blink_close_s = 0.081f;
        a->blink_shut_s = 0.0f;
        a->blink_open_s = 0.099f;
    }
}

/* A blink that goes with something the pet does, outside the schedule. */
static void blink_now(EyesAnim *a)
{
    if (MOOD[a->expr_id].lids == LIDS_STILL || a->blink_t >= 0.0f) {
        return;
    }
    blink_start(a);
    a->blink_double = 0;
    if (a->blink_next_s < 1.2f) {
        a->blink_next_s = 1.2f + rng_unit(&a->rng);
    }
}

/* Time until it next looks elsewhere. */
static float glance_wait(EyesAnim *a)
{
    float mean = MOOD[a->expr_id].glance_s;
    return mean > 0.0f ? mean * rng_between(&a->rng, 0.5f, 1.5f) : 1.0e9f;
}

/* Move into a mood. `settle_s` of 0 means at the mood's own pace. */
static void enter(EyesAnim *a, int mood, float settle_s)
{
    const Mood *m = &MOOD[mood];
    if (settle_s <= 0.0f) {
        settle_s = MOOD[a->expr_id].gesture ? MOOD[a->expr_id].settle_s : m->settle_s;
    }
    a->expr_id = mood;
    a->away = 0;
    a->stay_s = rng_between(&a->rng, m->stay_min_s, m->stay_max_s);
    glide(a, &EYES_EXPR[mood], settle_s, 0);
    a->hold_s = glance_wait(a);
    /* a change of mood often starts on a blink, as a thought does */
    if (rng_unit(&a->rng) < 0.5f) {
        blink_now(a);
    }
}

static int story_after(int i)
{
    i++;
    while (STORY[i] != END) {
        i++;
    }
    return i + 1;
}

/* Draw a story by weight. Returns the index of its first mood. */
static int story_pick(EyesAnim *a)
{
    uint32_t total = 0;
    for (int i = 0; STORY[i]; i = story_after(i)) {
        total += STORY[i];
    }
    int i = 0;
    for (int tries = 0; tries < 4; tries++) {
        uint32_t roll = rng_next(&a->rng) % total;
        i = 0;
        while (roll >= STORY[i]) {
            roll -= STORY[i];
            i = story_after(i);
        }
        /* the same thing does not happen to it twice in a row; resting may */
        if (i != a->trip_start || STORY[i + 2] == END) {
            break;
        }
    }
    a->trip_start = i;
    return i + 1;
}

static void next_mood(EyesAnim *a)
{
    if (a->trip < 0 || STORY[a->trip] == END) {
        a->trip = story_pick(a);
        /* already where the story begins: go straight to what happens next */
        if (STORY[a->trip] == a->expr_id && STORY[a->trip + 1] != END) {
            a->trip++;
        }
    }
    int mood = STORY[a->trip++];
    if (mood == REST) {
        mood = rng_unit(&a->rng) < REST_CALM ? EYES_NEUTRE : EYES_ATTENTIF;
    }
    if (mood == a->expr_id) {
        /* more of the same: nothing to glide to, it only stays longer */
        a->stay_s = rng_between(&a->rng, MOOD[mood].stay_min_s, MOOD[mood].stay_max_s);
        return;
    }
    enter(a, mood, 0.0f);
}

static float gaze_distance(const Expr *a, float yaw, float pitch)
{
    return f_max(f_abs(a->p.yaw - yaw), f_abs(a->p.pitch - pitch));
}

/* Turn the head somewhere else, keeping the mood's eyes. */
static void look_away(EyesAnim *a)
{
    Expr t = EYES_EXPR[a->expr_id];
    const Expr *now = &a->cur.p.e;
    float yaw, pitch;

    if (rng_unit(&a->rng) < LOOK_AT_YOU && gaze_distance(now, YOU_YAW, YOU_PITCH) > 12.0f) {
        yaw = YOU_YAW + rng_between(&a->rng, -4.0f, 4.0f);
        pitch = YOU_PITCH + rng_between(&a->rng, -3.0f, 3.0f);
    } else {
        /* off to one side of where the mood looks, and a little up or down */
        float turn = rng_between(&a->rng, 9.0f, 22.0f);
        if (rng_next(&a->rng) & 1u) {
            turn = -turn;
        }
        yaw = t.p.yaw + turn;
        if (yaw > LOOK_YAW_MAX || yaw < LOOK_YAW_MIN) {
            yaw = t.p.yaw - turn; /* no room on that side: the other one */
        }
        pitch = t.p.pitch + rng_between(&a->rng, -8.0f, 8.0f);
        /* when it is already away, do not pick the place it is looking at */
        if (gaze_distance(now, yaw, pitch) < 8.0f) {
            yaw = t.p.yaw - (yaw - t.p.yaw);
        }
    }
    t.p.yaw = f_clamp(yaw, LOOK_YAW_MIN, LOOK_YAW_MAX);
    t.p.pitch = f_clamp(pitch, LOOK_PITCH_MIN, LOOK_PITCH_MAX);

    float far = gaze_distance(now, t.p.yaw, t.p.pitch);
    glide(a, &t, DART_S + DART_S_PER_DEG * far, 1);
    a->away = 1;
    float u = rng_unit(&a->rng);
    a->hold_s = AWAY_MIN_S + (AWAY_MAX_S - AWAY_MIN_S) * u * u;
    /* eyes close on the way when the head turns far */
    if (far > 14.0f && rng_unit(&a->rng) < 0.6f) {
        blink_now(a);
    }
}

static void look_back(EyesAnim *a)
{
    const Expr *home = &EYES_EXPR[a->expr_id];
    float far = gaze_distance(&a->cur.p.e, home->p.yaw, home->p.pitch);
    /* a touch slower than it left: nothing is calling it back */
    glide(a, home, (DART_S + DART_S_PER_DEG * far) * 1.25f, 1);
    a->away = 0;
    a->hold_s = glance_wait(a);
    if (far > 14.0f && rng_unit(&a->rng) < 0.35f) {
        blink_now(a);
    }
}

void eyes_init(EyesAnim *a, uint32_t seed, float scale, float screen_w, float screen_h)
{
    a->scale = scale;
    a->w = screen_w;
    a->h = screen_h;
    a->rng = seed ? seed : 0x5eedu;
    for (int i = 0; i < 8; i++) {
        rng_next(&a->rng);
    }

    /* It wakes up calm, and stays so for a moment before anything happens. */
    a->expr_id = EYES_NEUTRE;
    a->cur.p.e = EYES_EXPR[EYES_NEUTRE];
    eyes_fit(&a->cur.p.e, scale, screen_w, screen_h, &a->cur.p.off_x, &a->cur.p.off_y);
    a->from = a->to = a->cur;
    a->glide_k = 1.0f;
    a->glide_s = 1.0f;
    a->glide_dart = 0;
    a->stay_s = rng_between(&a->rng, 8.0f, 14.0f);
    a->hold_s = rng_between(&a->rng, 3.0f, 5.0f);
    a->away = 0;
    a->trip = -1; /* no story under way */
    a->trip_start = -1;

    /* Eyes shut on the first picture, then they open: a slow half blink. */
    a->blink_close_s = 0.0f;
    a->blink_shut_s = 0.25f;
    a->blink_open_s = 0.60f;
    a->blink_t = 0.0f;
    a->blink_next_s = 2.6f;
    a->blink_double = 0;

    a->wander = MOOD[EYES_NEUTRE].wander;
    a->breath = 0.0f;
    a->breath_phase = 0.0f;
    for (int i = 0; i < EYES_NOISE_COUNT; i++) {
        a->phase[i] = rng_unit(&a->rng);
    }
}

void eyes_show(EyesAnim *a, int expr_id, float settle_s)
{
    if (expr_id < 0 || expr_id >= EYES_EXPR_COUNT) {
        return;
    }
    a->trip = -1; /* whatever story was under way is dropped */
    enter(a, expr_id, settle_s);
}

/* How fast `wander` and `breath` follow a change of mood. */
#define STILLNESS_S 2.0f

void eyes_step(EyesAnim *a, float dt, EyeShape out[2])
{
    /* --- glide, or decide what to do next -------------------------------- */
    if (a->glide_k < 1.0f) {
        a->glide_k += dt / a->glide_s;
        if (a->glide_k >= 1.0f) {
            a->glide_k = 1.0f;
            a->cur = a->to;
        } else {
            float g = ease(a->glide_k, a->glide_dart);
            for (int i = 0; i < PLACED_FLOATS; i++) {
                a->cur.v[i] = f_lerp(a->from.v[i], a->to.v[i], g);
            }
        }
    } else {
        /* Only a face that has arrived decides anything: every glide starts
         * from a pose that was declared, and ends on one. */
        a->stay_s -= dt;
        a->hold_s -= dt;
        if (a->stay_s <= 0.0f) {
            next_mood(a);
        } else if (a->hold_s <= 0.0f) {
            if (a->away && rng_unit(&a->rng) >= AWAY_AGAIN) {
                look_back(a);
            } else if (MOOD[a->expr_id].glance_s > 0.0f) {
                look_away(a);
            } else {
                a->hold_s = 1.0e9f;
            }
        }
    }
    const Mood *mood = &MOOD[a->expr_id];

    /* --- blinks ---------------------------------------------------------- */
    a->blink_next_s -= dt;
    if (a->blink_next_s <= 0.0f) {
        if (mood->lids == LIDS_STILL) {
            a->blink_next_s = rng_between(&a->rng, 0.5f, 1.5f);
            a->blink_double = 0;
        } else {
            blink_start(a);
            if (a->blink_double) {
                /* second beat of a double blink */
                a->blink_double = 0;
                a->blink_next_s = 0.24f;
            } else {
                /* 1.9 to 4.6 s between blinks, a double one now and then */
                float gap = rng_between(&a->rng, 1.9f, 4.6f);
                if (mood->lids == LIDS_STARE) {
                    gap *= 2.0f;
                } else if (mood->lids == LIDS_HEAVY) {
                    gap = 1.4f + 0.6f * gap;
                }
                a->blink_next_s = gap;
                a->blink_double = mood->lids == LIDS_NORMAL && rng_unit(&a->rng) < 0.18f;
            }
        }
    }
    float lid = 1.0f;
    if (a->blink_t >= 0.0f) {
        float t = a->blink_t;
        float shut_at = a->blink_close_s;
        float open_at = shut_at + a->blink_shut_s;
        if (t < shut_at) {
            lid = 1.0f - t / shut_at;
        } else if (t < open_at) {
            lid = 0.0f;
        } else if (t < open_at + a->blink_open_s) {
            lid = (t - open_at) / a->blink_open_s;
        } else {
            a->blink_t = -1.0f;
        }
        if (a->blink_t >= 0.0f) {
            a->blink_t += dt;
        }
    }

    /* --- life at rest ---------------------------------------------------- */
    float follow = f_min(dt / STILLNESS_S, 1.0f);
    a->wander += (mood->wander - a->wander) * follow;
    a->breath += (mood->breath - a->breath) * follow;
    a->breath_phase += dt / BREATH_PERIOD_S;
    if (a->breath_phase >= 1.0f) {
        a->breath_phase -= 1.0f;
    }
    for (int i = 0; i < EYES_NOISE_COUNT; i++) {
        float p = a->phase[i] + dt / NOISE_PERIOD[i];
        if (p >= 1.0f) {
            p -= 1.0f;
        }
        a->phase[i] = p;
    }
#define NOISE(i) loop_noise(a->phase[i], NOISE_SEED[i])
    EyesLife life;
    life.d_yaw = (NOISE(0) * DRIFT_YAW_A + NOISE(1) * DRIFT_YAW_B) * a->wander;
    life.d_pitch = (NOISE(2) * DRIFT_PITCH_A + NOISE(3) * DRIFT_PITCH_B) * a->wander;
    life.d_roll = NOISE(4) * DRIFT_ROLL * a->wander;
    life.drift_x = NOISE(5) * DRIFT_X;
    life.drift_y = NOISE(6) * DRIFT_Y + a->breath * f_sin_turns(a->breath_phase);
    life.lid = lid;
#undef NOISE

    eyes_pose(&a->cur.p.e, &life, a->scale, 0.5f * a->w + a->cur.p.off_x, 0.5f * a->h + a->cur.p.off_y, out);
}
