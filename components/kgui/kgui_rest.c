/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_rest.c
 * @brief The burn-in cycle of Rest (kgui_rest.h).
 */
#include "kgui_rest.h"

#include <stddef.h>

/** @brief One place of the cycle, in steps of KGUI_REST_SHIFT_PX. */
typedef struct {
    int8_t x;  /**< Steps across: -1 left, 0, 1 right. */
    int8_t y;  /**< Steps down: -1 up, 0, 1 down.      */
} rest_place_t;

kgui_rest_offset_t kgui_rest_offset(uint32_t step)
{
    /* The cycle: home, then the eight places round it, once each. */
    /* Neighbours are at most one step apart on each axis, so the content creeps and never jumps. */
    static const rest_place_t CYCLE[KGUI_REST_CYCLE_LEN] = {
        { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 },
        { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 },
    };
    rest_place_t const p = CYCLE[step % KGUI_REST_CYCLE_LEN];
    kgui_rest_offset_t out;

    out.x = (int32_t)p.x * KGUI_REST_SHIFT_PX;
    out.y = (int32_t)p.y * KGUI_REST_SHIFT_PX;
    return out;
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
