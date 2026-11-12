#include "fmath.h"

float f_sin_turns(float x)
{
    /* reduce to [-0.5, 0.5) */
    int i = (int)x;
    x -= (float)i;
    if (x >= 0.5f) {
        x -= 1.0f;
    } else if (x < -0.5f) {
        x += 1.0f;
    }
    /* fold onto [-0.25, 0.25], where the series converges fast */
    if (x > 0.25f) {
        x = 0.5f - x;
    } else if (x < -0.25f) {
        x = -0.5f - x;
    }
    float y = x * 6.283185307f;
    float y2 = y * y;
    /* Taylor to y^9: worst error 3.6e-6 at the fold */
    return y * (1.0f + y2 * (-1.0f / 6.0f + y2 * (1.0f / 120.0f + y2 * (-1.0f / 5040.0f + y2 * (1.0f / 362880.0f)))));
}
