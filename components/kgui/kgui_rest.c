/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_rest.c
 * @brief The burn-in cycle of Rest (kgui_rest.h).
 */
#include "kgui_rest.h"

#include <stddef.h>

/** @brief How far the content goes across from home, in pixels. */
#define REST_REACH_X  ((uint32_t)KGUI_REST_SHIFT_PX)

/** @brief How far it goes down: one less than across, so the two axes drift apart. */
#define REST_REACH_Y  (REST_REACH_X - 1U)

/** @brief A way out and back has four legs: out, back, out the other way, back. */
#define REST_LEGS     4U

_Static_assert(((KGUI_REST_CYCLE_LEN % (REST_LEGS * REST_REACH_X)) == 0U)
               && ((KGUI_REST_CYCLE_LEN % (REST_LEGS * REST_REACH_Y)) == 0U),
               "the cycle ends where both axes are home again");

/**
 * @brief One axis of the cycle: out to @p reach, back through home to the
 *        other side and home again, a pixel a step.
 *
 * @param step  The place along the cycle.
 * @param reach How far from home it turns back, in pixels; above zero.
 * @return The distance from home, -reach to reach.
 */
static int32_t rest_bounce(uint32_t step, uint32_t reach)
{
    uint32_t const at  = step % (REST_LEGS * reach);
    uint32_t       far = at;       /* how far from home */
    bool           neg = false;    /* on the other side of it */

    if ( at > (3U * reach) )
    {
        far = (REST_LEGS * reach) - at;
        neg = true;
    }
    else if ( at > (2U * reach) )
    {
        far = at - (2U * reach);
        neg = true;
    }
    else if ( at > reach )
    {
        far = (2U * reach) - at;
    }
    else
    {
        /* On its way out: as far as it has gone. */
    }
    return neg ? -(int32_t)far : (int32_t)far;
}

kgui_rest_offset_t kgui_rest_offset(uint32_t step)
{
    kgui_rest_offset_t out;

    out.x = rest_bounce(step, REST_REACH_X);
    out.y = rest_bounce(step, REST_REACH_Y);
    return out;
}

uint32_t kgui_rest_even(uint32_t step)
{
    uint32_t const at = step % KGUI_REST_CYCLE_LEN;

    return (at + (at % 2U)) % KGUI_REST_CYCLE_LEN;
}

uint32_t kgui_rest_start(uint32_t step)
{
    return kgui_rest_even((step % KGUI_REST_CYCLE_LEN) + KGUI_REST_NEXT_STEPS);
}

bool kgui_rest_sleep_due(kgui_rest_sleep_t const *s)
{
    return s->settled && (!s->cable) && (KGUI_REST_SLEEP_CALM_MS <= s->calm_ms)
           && ((!s->asked) || (KGUI_REST_SLEEP_AGAIN_MS <= s->asked_ms));
}

uint32_t kgui_rest_left_ms(uint32_t gone_ms, uint32_t after_ms)
{
    uint32_t left = 0U;

    if ( after_ms > gone_ms )
    {
        left = after_ms - gone_ms;
    }
    return left;
}

bool kgui_rest_strip_at(int32_t home_x, int32_t home_y, int32_t w, int32_t h,
                        kgui_rest_offset_t at, int32_t glass_w, int32_t glass_h,
                        uint16_t *x, uint16_t *y)
{
    int32_t left = home_x + at.x;
    int32_t top  = home_y + at.y;
    bool    fits = false;

    /* Off the glass is off it, and down to even from there. */
    if ( (0 <= left) && (0 <= top) )
    {
        left -= (left % 2);
        top  -= (top % 2);
    }
    if ( (0 <= left) && (0 <= top) && (0 < w) && (0 < h)
        && ((left + w) <= glass_w) && ((top + h) <= glass_h) )
    {
        *x   = (uint16_t)left;
        *y   = (uint16_t)top;
        fits = true;
    }
    return fits;
}
