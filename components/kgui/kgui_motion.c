/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_motion.c
 * @brief The launcher's rail model: its rows' places and its motions (kgui_motion.h).
 */
#include "kgui_motion.h"

#include <math.h>

#define MOTION_HALF    0.5F  /**< Added to a float before it is cut, to round it to the nearest integer; also half of a length. */
#define MOTION_OPA_MAX 255   /**< A row that shows whole. */
#define MOTION_CORNER  ((int32_t)KBSP_LCD_CORNER_R_PX) /**< The glass's corner radius, in pixels. */
#define MOTION_HEIGHT  ((int32_t)KBSP_LCD_V_RES)       /**< The glass's height, in pixels. */

int32_t kgui_round_px(float v)
{
    int32_t px;

    if ( 0.0F > v )
    {
        float const dn = MOTION_HALF - v;

        px = -(int32_t)dn;
    }
    else
    {
        float const up = v + MOTION_HALF;

        px = (int32_t)up;
    }
    return px;
}

float kgui_rubber(float past)
{
    float shown = 0.0F;

    if ( 0.0F < past )
    {
        shown = (past * KGUI_RUBBER_LIMIT_PX * KGUI_RUBBER_GAIN)
                / (KGUI_RUBBER_LIMIT_PX + (KGUI_RUBBER_GAIN * past));
    }
    return shown;
}

float kgui_rubber_pull(float shown)
{
    float const limit = KGUI_RUBBER_LIMIT_PX - KGUI_REST_PX;
    float       at    = shown;
    float       past  = 0.0F;

    if ( at > limit )
    {
        at = limit;
    }
    if ( 0.0F < at )
    {
        past = (at * KGUI_RUBBER_LIMIT_PX)
               / (KGUI_RUBBER_GAIN * (KGUI_RUBBER_LIMIT_PX - at));
    }
    return past;
}

float kgui_rubber_shown(float raw, float lo, float hi)
{
    float shown = raw;

    if ( raw < lo )
    {
        shown = lo - kgui_rubber(lo - raw);
    }
    else if ( raw > hi )
    {
        shown = hi + kgui_rubber(raw - hi);
    }
    else
    {
        /* Within the rows: as it is. */
    }
    return shown;
}

float kgui_rubber_raw(float shown, float lo, float hi)
{
    float raw = shown;

    if ( shown < lo )
    {
        raw = lo - kgui_rubber_pull(lo - shown);
    }
    else if ( shown > hi )
    {
        raw = hi + kgui_rubber_pull(shown - hi);
    }
    else
    {
        /* Within the rows: as it is. */
    }
    return raw;
}

/**
 * @brief KGUI_ROW_PITCH as a float.
 *
 * @return The pitch, in pixels.
 */
static float rows_pitch(void)
{
    int32_t const pitch = KGUI_ROW_PITCH;

    return (float)pitch;
}

/**
 * @brief KGUI_ROW_MID as a float.
 *
 * @return The first row's middle from the content's top, in pixels.
 */
static float rows_mid(void)
{
    int32_t const mid = KGUI_ROW_MID;

    return (float)mid;
}

int32_t kgui_rows_content(uint32_t n)
{
    int32_t height = 0;

    if ( 0U < n )
    {
        height = (int32_t)n * KGUI_ROW_PITCH;
    }
    return height;
}

float kgui_rows_rest(uint32_t row)
{
    return (float)row * rows_pitch();
}

float kgui_rows_span(uint32_t n)
{
    float span = 0.0F;

    if ( 1U < n )
    {
        span = kgui_rows_rest(n - 1U);
    }
    return span;
}

uint32_t kgui_rows_nearest(uint32_t n, float u)
{
    uint32_t row = 0U;

    if ( (1U < n) && (0.0F < u) )
    {
        float const    at   = (u / rows_pitch()) + MOTION_HALF;
        uint32_t const last = n - 1U;

        if ( (float)last <= at )
        {
            row = last;
        }
        else
        {
            row = (uint32_t)at;
        }
    }
    return row;
}

float kgui_rows_scroll(float u, uint32_t n)
{
    float const focus = (float)KGUI_RAIL_FOCUS_Y;
    float       s     = (u + rows_mid()) - focus;

    if ( KGUI_RAIL_BLOCK_MAX >= n )
    {
        /* A block: its middle on the focus line, whichever row the focus is on. */
        float const rows = (float)kgui_rows_content(n);

        s = ((rows * MOTION_HALF)) - focus;
    }
    return s;
}

bool kgui_rows_fill(uint32_t n)
{
    return kgui_rows_content(n) >= MOTION_HEIGHT;
}

float kgui_rows_top_scroll_at(float v, float first_y)
{
    float const start = rows_mid() - first_y;
    float const walk  = (float)KGUI_RAIL_FOCUS_Y - first_y;
    float       s     = start;

    if ( 0.0F > v )
    {
        s = start + v;
    }
    else if ( walk < v )
    {
        s = start + (v - walk);
    }
    else
    {
        /* The list stands still and the ring walks down its rows. */
    }
    return s;
}

/**
 * @brief The u that gives an offset, for a list whose first row's middle starts at @p first_y.
 *
 * @param v       The content's offset, in pixels.
 * @param first_y Where the first row's middle starts on the glass.
 * @return u, never inside the stretch where the content stands still.
 */
float kgui_rows_top_u_at(float v, float first_y)
{
    float const start = rows_mid() - first_y;
    float       u     = v - start;

    if ( 0.0F <= u )
    {
        u += (float)KGUI_RAIL_FOCUS_Y - first_y;
    }
    return u;
}

/* cppcheck-suppress misra-c2012-8.7 ; public API of the motion module, taken by its host test */
float kgui_rows_top_scroll(float u)
{
    return kgui_rows_top_scroll_at(u, (float)KGUI_LIST_FIRST_Y);
}

/* cppcheck-suppress misra-c2012-8.7 ; public API of the motion module, taken by its host test */
float kgui_rows_top_u(float s)
{
    return kgui_rows_top_u_at(s, (float)KGUI_LIST_FIRST_Y);
}

float kgui_rows_focus_y(float u, uint32_t n)
{
    return (rows_mid() + u) - kgui_rows_scroll(u, n);
}

float kgui_rows_light_y(float u, uint32_t n)
{
    float const span   = kgui_rows_span(n);
    float       within = u;

    if ( 0.0F > within )
    {
        within = 0.0F;
    }
    if ( within > span )
    {
        within = span;
    }
    return within;
}

uint32_t kgui_glide_row(uint32_t n, float u, float vel)
{
    uint32_t const from = kgui_rows_nearest(n, u);
    uint32_t       to   = kgui_rows_nearest(n, u + (vel * KGUI_GLIDE_REACH_MS));

    if ( (to > from) && ((to - from) > KGUI_GLIDE_MAX_ROWS) )
    {
        to = from + KGUI_GLIDE_MAX_ROWS;
    }
    else if ( (from > to) && ((from - to) > KGUI_GLIDE_MAX_ROWS) )
    {
        to = from - KGUI_GLIDE_MAX_ROWS;
    }
    else
    {
        /* Within reach. */
    }
    return to;
}

/**
 * @brief How far the glass's corner cuts in at a distance from the top or the
 *        bottom edge.
 *
 * @param d The distance, in pixels; under 0 is taken as 0.
 * @return The inset, in pixels: 0 from the corner radius on.
 */
static int32_t rail_inset(int32_t d)
{
    int32_t inset = 0;

    if ( MOTION_CORNER > d )
    {
        int32_t edge = d;
        float const r = (float)MOTION_CORNER;

        if ( 0 > edge )
        {
            edge = 0;
        }
        float const k = r - (float)edge;

        inset = kgui_round_px(r - sqrtf((r * r) - (k * k)));
    }
    return inset;
}

void kgui_rail_edge(int32_t cy, kgui_rail_t *out)
{
    int32_t d = cy;
    bool const pushed = ((cy - (KGUI_RAIL_ICON / 2)) < KGUI_RAIL_TOP)
                        || ((cy + (KGUI_RAIL_ICON / 2)) > KGUI_RAIL_BOTTOM);

    if ( (MOTION_HEIGHT - cy) < d )
    {
        d = MOTION_HEIGHT - cy;
    }
    out->gone = ((cy + KGUI_RAIL_ICON) < 0) || ((cy - KGUI_RAIL_ICON) > MOTION_HEIGHT);
    out->dx   = 0;
    out->outside = pushed;
    if ( pushed )
    {
        out->dx = rail_inset(d);
    }
    if ( KGUI_RAIL_EDGE_FULL <= d )
    {
        out->opa = (uint8_t)MOTION_OPA_MAX;
    }
    else if ( KGUI_RAIL_EDGE_GONE >= d )
    {
        out->opa = 0U;
    }
    else
    {
        int32_t const share = (MOTION_OPA_MAX * (d - KGUI_RAIL_EDGE_GONE))
                              / (KGUI_RAIL_EDGE_FULL - KGUI_RAIL_EDGE_GONE);

        out->opa = (uint8_t)share;
    }
}
