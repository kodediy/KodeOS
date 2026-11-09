/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_fluid.c
 * @brief The battery's liquid - see kgui_fluid.h.
 */
#include "kgui_fluid.h"

#include <math.h>
#include <stddef.h>

#define FLUID_PI        3.14159265F
#define FLUID_DT        ((float)KGUI_FLUID_STEP_MS / 1000.0F)
#define SWING_K         11.0F    /* The pendulum's pull: slow, a thick liquid. */
#define SWING_C         3.6F     /* Its damping: it passes level once, a little, and comes back. */
#define RIPPLE_SPEED    0.10F    /* The wave equation's c^2, per step: under 0.5, or it blows up. */
#define RIPPLE_BACK     0.012F   /* What brings a point back to the mean by itself. */
#define RIPPLE_DAMP     0.07F   /* What the ripples lose a step. */
#define RIPPLE_SWING    1.7F     /* How much of the swing's acceleration reaches them. */
#define RIPPLE_JOLT     3.8F     /* And of a knock. */
#define RIPPLE_VISC     0.22F    /* Viscosity: each point drags on its neighbours, so no sharp ripple lasts. */
#define RIPPLE_MAX_R    0.30F    /* The tallest ripple, as a share of the radius. */
#define LEVEL_RISE      0.12F    /* The share of the way to a new level, a step. */
#define MOVED_ANGLE     0.006F   /* Under these nothing has moved: the panel rests. */
#define MOVED_SPIN      0.012F
#define MOVED_V         0.012F
#define MOVED_LEVEL     0.04F

/** @brief An angle's way round to the nearest turn: -pi to pi. */
static float fluid_wrap(float a)
{
    float w = fmodf(a + FLUID_PI, 2.0F * FLUID_PI);

    if ( 0.0F > w )
    {
        w += 2.0F * FLUID_PI;
    }
    return w - FLUID_PI;
}

void kgui_fluid_init(kgui_fluid_t *f, int32_t side)
{
    f->r        = (float)side / 2.0F;
    f->level    = -f->r;
    f->level_to = -f->r;
    kgui_fluid_rest(f, 0.0F);
}

void kgui_fluid_rest(kgui_fluid_t *f, float angle)
{
    f->angle = angle;
    f->spin  = 0.0F;
    for ( size_t i = 0U; KGUI_FLUID_COLS > i; ++i )
    {
        f->h[i] = 0.0F;
        f->v[i] = 0.0F;
    }
}

void kgui_fluid_fill(kgui_fluid_t *f, float fill, bool now)
{
    float lo = -1.0F;
    float hi = 1.0F;

    /* The share of a disc under a chord at d radii over its centre. */
    for ( int n = 0; 24 > n; ++n )
    {
        float const d    = (lo + hi) / 2.0F;
        float const area = (FLUID_PI - acosf(d) + (d * sqrtf(1.0F - (d * d)))) / FLUID_PI;

        if ( area < fill )
        {
            lo = d;
        }
        else
        {
            hi = d;
        }
    }
    f->level_to = ((lo + hi) / 2.0F) * f->r;
    if ( now )
    {
        f->level = f->level_to;
    }
}

bool kgui_fluid_step(kgui_fluid_t *f, float angle, float jolt)
{
    float const  off       = fluid_wrap(angle - f->angle);
    float const  swing_acc = (SWING_K * off) - (SWING_C * f->spin);
    size_t const last      = KGUI_FLUID_COLS - 1U;
    float const  n1        = (float)last;
    float const  top       = RIPPLE_MAX_R * f->r;
    float        next[KGUI_FLUID_COLS];
    float        mean      = 0.0F;
    float        fast      = 0.0F;

    f->spin += swing_acc * FLUID_DT;
    {
        float was = f->v[0];

        for ( size_t i = 0U; KGUI_FLUID_COLS > i; ++i )
        {
            float const here = f->v[i];
            float const next_v = (i == (KGUI_FLUID_COLS - 1U)) ? here : f->v[i + 1U];

            f->v[i] = here + (RIPPLE_VISC * (was + next_v - (2.0F * here)));
            was = here;
        }
    }
    f->angle = fluid_wrap(f->angle + (f->spin * FLUID_DT));

    for ( size_t i = 0U; KGUI_FLUID_COLS > i; ++i )
    {
        float const  u = ((2.0F * (float)i) / n1) - 1.0F;
        size_t const a = (0U == i) ? 0U : (i - 1U);
        size_t const b = (i == (KGUI_FLUID_COLS - 1U)) ? i : (i + 1U);
        float const  lap = f->h[a] + f->h[b] - (2.0F * f->h[i]);

        f->v[i] += (RIPPLE_SPEED * lap) - (RIPPLE_BACK * f->h[i]) - (RIPPLE_DAMP * f->v[i]);
        /* A swing sloshes it from side to side; a knock shakes it. */
        f->v[i] -= swing_acc * FLUID_DT * RIPPLE_SWING * u;
        f->v[i] += jolt * RIPPLE_JOLT * FLUID_DT * sinf(3.0F * FLUID_PI * u);
        if ( fabsf(f->v[i]) > fast )
        {
            fast = fabsf(f->v[i]);
        }
    }
    for ( size_t i = 0U; KGUI_FLUID_COLS > i; ++i )
    {
        next[i] = f->h[i] + f->v[i];
        if ( next[i] > top )
        {
            next[i] = top;
            f->v[i] = 0.0F;
        }
        if ( next[i] < -top )
        {
            next[i] = -top;
            f->v[i] = 0.0F;
        }
        mean += next[i];
    }
    /* The ripples keep a mean of zero, so they never add or take liquid. */
    mean /= (float)KGUI_FLUID_COLS;
    for ( size_t i = 0U; KGUI_FLUID_COLS > i; ++i )
    {
        f->h[i] = next[i] - mean;
    }
    f->level += (f->level_to - f->level) * LEVEL_RISE;

    return (fabsf(off) > MOVED_ANGLE) || (fabsf(f->spin) > MOVED_SPIN) || (MOVED_V < fast)
           || (fabsf(f->level_to - f->level) > MOVED_LEVEL);
}

void kgui_fluid_draw(kgui_fluid_t const *f, uint8_t *a8, uint32_t stride, int32_t side)
{
    float const  c    = ((float)side - 1.0F) / 2.0F;
    float const  sn   = sinf(f->angle);
    float const  cs   = cosf(f->angle);
    size_t const last = KGUI_FLUID_COLS - 1U;
    float const  n1   = (float)last;

    for ( int32_t y = 0; y < side; ++y )
    {
        uint8_t * const row = &a8[(uint32_t)y * stride];
        float const     dy  = (float)y - c;

        for ( int32_t x = 0; x < side; ++x )
        {
            float const dx = (float)x - c;
            /* u along the surface, v up from the centre, y growing downwards. */
            float const u  = (dx * cs) + (dy * sn);
            float const v  = (dx * sn) - (dy * cs);
            float       at = ((u / f->r) + 1.0F) * 0.5F * n1;

            if ( 0.0F > at )
            {
                at = 0.0F;
            }
            if ( at > n1 )
            {
                at = n1;
            }
            size_t const i  = (size_t)at;
            size_t const j  = (i < (KGUI_FLUID_COLS - 1U)) ? (i + 1U) : i;
            float const  t  = at - (float)i;
            float const  d  = (f->level + (f->h[i] * (1.0F - t)) + (f->h[j] * t)) - v;

            if ( 0.5F <= d )
            {
                row[x] = 255U;
            }
            else if ( d <= -0.5F )
            {
                row[x] = 0U;
            }
            else
            {
                float const shade = (d + 0.5F) * 255.0F;

                row[x] = (uint8_t)shade;
            }
        }
    }
}
