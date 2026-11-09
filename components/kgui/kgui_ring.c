/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_ring.c
 * @brief The Ring (kgui_ring.h); its edge adapts kodediy/kode-dot-os, kshell_edge.c.
 */
#include "kgui_ring.h"

#include <math.h>
#include <stddef.h>

#include "kbsp_board.h"
#include "kgui_icon.h"
#include "kui.h"

#define RING_SPIN_DEG       (KGUI_RING_TURN_DEG / (int32_t)KGUI_RING_SPIN_STEPS) /**< How far the turning ring moves in one step. */

#define RING_GLASS_ARC_STEPS 12U  /**< Chords a corner of the glass ring is drawn in. */
#define RING_GLASS_CORNERS   4U   /**< The glass has four corners. */
#define RING_GLASS_VERTS     (2U + (RING_GLASS_CORNERS * (RING_GLASS_ARC_STEPS + 1U))) /**< Points of the path: the top middle at both ends and every corner's chords. */
#define RING_GLASS_PTS_MAX   (RING_GLASS_VERTS + 2U) /**< Most points of a span: the path's own and the two ends. */
#define RING_GLASS_LAPS      2U   /**< A span that passes the top middle is walked over the path twice. */
#define RING_GLASS_EPS       0.01F /**< Closer than this along the path, in pixels, two points are one. */
#define RING_QUARTER_DEG     90.0F /**< A corner turns a quarter of a circle. */
#define RING_HALF_TURN_DEG   180.0F /**< Half a turn, in degrees. */
#define RING_PI              3.14159265F /**< Pi. */

#define RING_GLASS_MARGIN_EDGE 0  /**< The edge's outer side is flush with the glass. */
#define RING_DOT_HALF       (KGUI_EDGE_DOT_D / 2) /**< From the cut's dot to its middle, in pixels. */

#define RING_PM_PER_PCT     (KGUI_RING_FULL_PM / KGUI_RING_FULL_PCT) /**< Thousandths of the ring in one percent. */

_Static_assert((KGUI_RING_FULL_PCT % KGUI_RING_STEP_PCT) == 0U, "the ring's steps divide a whole");
_Static_assert(RING_GLASS_MARGIN_EDGE == (KGUI_EDGE_LINE_IN - (KGUI_EDGE_W / 2)),
               "the edge's line runs where kgui_edge.h says, the face's arc with it");
_Static_assert((int32_t)KGUI_EDGE_R == ((int32_t)KBSP_LCD_CORNER_R_PX - KGUI_EDGE_LINE_IN),
               "the edge's corners follow the glass's, the face's arc with them");

/** @brief One point of the glass path, and how far along it the point is. */
typedef struct {
    float x;  /**< Across, in the parent's pixels. */
    float y;  /**< Down. */
    float s;  /**< Distance from the top middle along the path, in pixels. */
} ring_vtx_t;

/** @brief A glass ring's objects and its path. */
struct kgui_ring_glass {
    lv_obj_t          *root;                    /**< Covers the glass; holds the others. */
    lv_obj_t          *line;                    /**< The span that is filled. */
    lv_obj_t          *dot;                     /**< The red dot where a cut ring stopped. */
    lv_color_t         col;                     /**< What the ring fills in. */
    int32_t            width;                   /**< The line's width at rest. */
    uint16_t           close_from;              /**< Where a close began, in thousandths. */
    uint8_t            spin_steps;              /**< Positions of the turning arc in one lap. */
    bool               cut;                     /**< The ring stopped short: resting grey and a dot. */
    float              total;                   /**< The path's length. */
    ring_vtx_t         v[RING_GLASS_VERTS];     /**< The path, clockwise from the top middle. */
    lv_point_precise_t pts[RING_GLASS_PTS_MAX]; /**< The span the line is drawn along. */
};

/**
 * @brief Work out the path: the glass's edge, inset, clockwise from the top
 *        middle.
 *
 * Four corners of RING_GLASS_ARC_STEPS chords each, joined by the straight
 * sides, which are their own chords. The distances are those of the path as it
 * is drawn.
 *
 * @param g     The ring's glass.
 * @param inset Where the line's middle runs, in from the glass's edge.
 */
static void ring_glass_build(struct kgui_ring_glass *g, float inset)
{
    float const w      = (float)KBSP_LCD_H_RES;
    float const h      = (float)KBSP_LCD_V_RES;
    float const r      = (float)KBSP_LCD_CORNER_R_PX;
    float const rc     = r - inset;
    float const cx[RING_GLASS_CORNERS] = { w - r, w - r, r, r };
    float const cy[RING_GLASS_CORNERS] = { r, h - r, h - r, r };
    float const from[RING_GLASS_CORNERS] = { -RING_QUARTER_DEG, 0.0F, RING_QUARTER_DEG, RING_HALF_TURN_DEG };
    uint32_t    k      = 0U;

    g->v[0].x = w / 2.0F;
    g->v[0].y = inset;
    g->v[0].s = 0.0F;
    k         = 1U;
    for ( uint32_t c = 0U; RING_GLASS_CORNERS > c; c++ )
    {
        for ( uint32_t i = 0U; RING_GLASS_ARC_STEPS >= i; i++ )
        {
            float const deg = from[c] + ((RING_QUARTER_DEG * (float)i) / (float)RING_GLASS_ARC_STEPS);
            float const rad = (deg * RING_PI) / RING_HALF_TURN_DEG;

            g->v[k].x = cx[c] + (rc * cosf(rad));
            g->v[k].y = cy[c] + (rc * sinf(rad));
            k++;
        }
    }
    g->v[k].x = w / 2.0F;
    g->v[k].y = inset;
    for ( uint32_t i = 1U; RING_GLASS_VERTS > i; i++ )
    {
        float const dx = g->v[i].x - g->v[i - 1U].x;
        float const dy = g->v[i].y - g->v[i - 1U].y;

        g->v[i].s = g->v[i - 1U].s + sqrtf((dx * dx) + (dy * dy));
    }
    g->total = g->v[RING_GLASS_VERTS - 1U].s;
}

/**
 * @brief The point a distance along the path.
 *
 * @param g   The ring's glass.
 * @param s   The distance; one past the path's length is a lap on.
 * @param out Receives the point.
 */
static void ring_glass_at(const struct kgui_ring_glass *g, float s, lv_point_precise_t *out)
{
    float const at = fmodf(s, g->total);

    for ( uint32_t k = 1U; RING_GLASS_VERTS > k; k++ )
    {
        if ( at <= g->v[k].s )
        {
            float const span = g->v[k].s - g->v[k - 1U].s;
            float       t    = 0.0F;

            if ( RING_GLASS_EPS < span )
            {
                t = (at - g->v[k - 1U].s) / span;
            }
            out->x = g->v[k - 1U].x + (t * (g->v[k].x - g->v[k - 1U].x));
            out->y = g->v[k - 1U].y + (t * (g->v[k].y - g->v[k - 1U].y));
            break;
        }
    }
}

/**
 * @brief Put the cut's dot on the path, a distance along it.
 *
 * @param g The ring's glass.
 * @param s The distance from the top middle, in pixels.
 */
static void ring_dot_place(const struct kgui_ring_glass *g, float s)
{
    lv_point_precise_t p;

    ring_glass_at(g, s, &p);
    lv_obj_set_pos(g->dot, (int32_t)p.x - RING_DOT_HALF, (int32_t)p.y - RING_DOT_HALF);
    lv_obj_set_hidden(g->dot, false);
}

/**
 * @brief Draw the share of the path the ring's form stands for.
 *
 * A progress ring fills from the top middle; a turning one is a quarter
 * of the path that starts one position on at every step. A span that has no
 * length takes the line off the glass.
 *
 * @param r The ring.
 */
static void ring_glass_draw(kgui_ring_t *r)
{
    struct kgui_ring_glass * const g = r->glass;
    float const len = (g->total * (float)r->pm) / (float)KGUI_RING_FULL_PM;
    float       a   = 0.0F;
    float       last;
    uint32_t    n   = 1U;

    if ( KGUI_RING_SPIN == r->mode )
    {
        a = (g->total * (float)r->spin) / (float)g->spin_steps;
    }
    if ( g->cut )
    {
        ring_dot_place(g, len);
    }
    if ( RING_GLASS_EPS > len )
    {
        lv_obj_set_hidden(g->line, true);
        return;
    }
    ring_glass_at(g, a, &g->pts[0]);
    last = a;
    for ( uint32_t lap = 0U; RING_GLASS_LAPS > lap; lap++ )
    {
        for ( uint32_t k = 0U; RING_GLASS_VERTS > k; k++ )
        {
            float const sv = g->v[k].s + ((float)lap * g->total);

            if ( ((last + RING_GLASS_EPS) < sv) && (((a + len) - RING_GLASS_EPS) > sv)
                 && ((RING_GLASS_PTS_MAX - 1U) > n) )
            {
                g->pts[n].x = g->v[k].x;
                g->pts[n].y = g->v[k].y;
                n++;
                last = sv;
            }
        }
    }
    ring_glass_at(g, a + len, &g->pts[n]);
    n++;
    lv_line_set_points(g->line, g->pts, n);
    lv_obj_set_hidden(g->line, false);
}

/**
 * @brief Draw a share of the ring.
 *
 * @param r   The ring.
 * @param pct The share.
 */
static void ring_draw_pct(kgui_ring_t *r, uint8_t pct)
{
    r->pct = pct;
    r->pm  = (uint16_t)((uint32_t)pct * RING_PM_PER_PCT);
    if ( NULL != r->arc )
    {
        lv_arc_set_value(r->arc, (int32_t)pct);
    }
    else if ( NULL != r->glass )
    {
        ring_glass_draw(r);
    }
    else
    {
        /* Not made. */
    }
}

/**
 * @brief Draw the turning ring at a position.
 *
 * @param r    The ring.
 * @param spin The position.
 */
static void ring_draw_spin(kgui_ring_t *r, uint8_t spin)
{
    r->spin = spin;
    if ( NULL != r->arc )
    {
        lv_arc_set_rotation(r->arc, KGUI_RING_TOP_DEG + ((int32_t)spin * RING_SPIN_DEG));
    }
    else if ( NULL != r->glass )
    {
        ring_glass_draw(r);
    }
    else
    {
        /* Not made. */
    }
}

/**
 * @brief Put the ring back where a form begins: at the top, empty.
 *
 * @param r The ring.
 */
static void ring_reset(kgui_ring_t *r)
{
    r->spin = 0U;
    if ( NULL != r->arc )
    {
        lv_arc_set_rotation(r->arc, KGUI_RING_TOP_DEG);
    }
    if ( NULL != r->glass )
    {
        struct kgui_ring_glass * const g = r->glass;

        g->cut = false;
        lv_obj_set_hidden(g->dot, true);
        lv_obj_set_style_line_color(g->line, g->col, LV_PART_MAIN);
        lv_obj_set_style_line_width(g->line, g->width, LV_PART_MAIN);
    }
    ring_draw_pct(r, 0U);
}

/**
 * @brief A share cut to the step the ring is drawn in.
 *
 * @param pct 0 to KGUI_RING_FULL_PCT.
 * @return The share in whole steps.
 */
static uint8_t ring_stepped(uint8_t pct)
{
    return (uint8_t)(((uint32_t)pct / KGUI_RING_STEP_PCT) * KGUI_RING_STEP_PCT);
}

uint8_t kgui_ring_spin_at(uint32_t turned_ms)
{
    return (uint8_t)((turned_ms / KGUI_RING_SPIN_STEP_MS) % KGUI_RING_SPIN_STEPS);
}

/**
 * @brief The position of a ring's turning arc a time after it began.
 *
 * @param r        The ring.
 * @param turned_ms How long it has turned.
 * @return 0 to the ring's own steps less one.
 */
static uint8_t ring_spin_pos(const kgui_ring_t *r, uint32_t turned_ms)
{
    uint8_t pos = kgui_ring_spin_at(turned_ms);

    if ( NULL != r->glass )
    {
        pos = (uint8_t)((turned_ms / KGUI_RING_SPIN_STEP_MS) % r->glass->spin_steps);
    }
    return pos;
}

bool kgui_ring_create(kgui_ring_t *r, lv_obj_t *parent, int32_t size,
                      int32_t width, lv_color_t col, lv_color_t track)
{
    bool ok = false;

    if ( (NULL != r) && (NULL != parent) )
    {
        r->arc   = lv_arc_create(parent);
        r->glass = NULL;
        r->mode  = KGUI_RING_IDLE;
        r->t0_ms = 0U;
        r->pct   = 0U;
        r->pm    = 0U;
        r->spin  = 0U;
        if ( NULL != r->arc )
        {
            lv_obj_t * const arc = r->arc;

            lv_obj_remove_style_all(arc);
            lv_obj_set_size(arc, size, size);
            lv_obj_set_clickable(arc, false);
            lv_obj_set_scrollable(arc, false);
            lv_arc_set_range(arc, 0, (int32_t)KGUI_RING_FULL_PCT);
            lv_arc_set_bg_angles(arc, 0, KGUI_RING_TURN_DEG);
            lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
            lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
            lv_obj_set_style_arc_color(arc, track, LV_PART_MAIN);
            lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
            lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(arc, col, LV_PART_INDICATOR);
            lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
            lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
            ring_reset(r);
            ok = true;
        }
    }
    return ok;
}

/**
 * @brief Make a ring along the glass, whatever its line and its place.
 *
 * @param r     Receives the ring.
 * @param parent Where it stands.
 * @param width The line's width.
 * @param margin Between the glass's edge and the line's outer edge.
 * @param steps Positions of the turning arc in one lap.
 * @param col   The colour it fills in.
 * @param track The colour of the path under it.
 * @return true when LVGL had room for it.
 */
static bool ring_make_glass(kgui_ring_t *r, lv_obj_t *parent, int32_t width, int32_t margin,
                            uint8_t steps, lv_color_t col, lv_color_t track)
{
    bool ok = false;

    if ( (NULL != r) && (NULL != parent) )
    {
        /* cppcheck-suppress misra-c2012-11.5 ; lv_malloc() answers void *: LVGL's heap hands back a block aligned for any type */
        struct kgui_ring_glass * const g = lv_malloc(sizeof(struct kgui_ring_glass));

        r->arc   = NULL;
        r->glass = NULL;
        r->mode  = KGUI_RING_IDLE;
        r->t0_ms = 0U;
        r->pct   = 0U;
        r->pm    = 0U;
        r->spin  = 0U;
        if ( NULL != g )
        {
            g->root       = kgui_plain(parent, 0, 0, (int32_t)KBSP_LCD_H_RES, (int32_t)KBSP_LCD_V_RES);
            g->line       = NULL;
            g->dot        = NULL;
            g->col        = col;
            g->width      = width;
            g->close_from = 0U;
            g->spin_steps = steps;
            g->cut        = false;
            if ( NULL != g->root )
            {
                lv_obj_t * const edge = kgui_plain(g->root, margin, margin,
                                                   (int32_t)KBSP_LCD_H_RES - (2 * margin),
                                                   (int32_t)KBSP_LCD_V_RES - (2 * margin));

                g->line = lv_line_create(g->root);
                g->dot  = kgui_plain(g->root, 0, 0, KGUI_EDGE_DOT_D, KGUI_EDGE_DOT_D);
                if ( (NULL != edge) && (NULL != g->line) && (NULL != g->dot) )
                {
                    lv_obj_set_style_radius(edge, (int32_t)KBSP_LCD_CORNER_R_PX - margin, LV_PART_MAIN);
                    lv_obj_set_style_border_width(edge, width, LV_PART_MAIN);
                    lv_obj_set_style_border_color(edge, track, LV_PART_MAIN);
                    lv_obj_set_style_border_opa(edge, LV_OPA_COVER, LV_PART_MAIN);
                    lv_obj_set_style_line_width(g->line, width, LV_PART_MAIN);
                    lv_obj_set_style_line_color(g->line, col, LV_PART_MAIN);
                    lv_obj_set_style_line_rounded(g->line, true, LV_PART_MAIN);
                    lv_obj_set_pos(g->line, 0, 0);
                    lv_obj_set_hidden(g->line, true);
                    lv_obj_set_style_radius(g->dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
                    lv_obj_set_style_bg_color(g->dot, KUI_COLOR_DANGER, LV_PART_MAIN);
                    lv_obj_set_style_bg_opa(g->dot, LV_OPA_COVER, LV_PART_MAIN);
                    lv_obj_set_hidden(g->dot, true);
                    ring_glass_build(g, (float)margin + ((float)width / 2.0F));
                    r->glass = g;
                    ring_reset(r);
                    ok = true;
                }
            }
            if ( !ok )
            {
                if ( NULL != g->root )
                {
                    lv_obj_delete(g->root);
                }
                lv_free(g);
            }
        }
    }
    return ok;
}

bool kgui_ring_create_edge(kgui_ring_t *r, lv_obj_t *parent, lv_color_t col)
{
    return ring_make_glass(r, parent, KGUI_EDGE_W, RING_GLASS_MARGIN_EDGE, (uint8_t)KGUI_EDGE_SPIN_STEPS,
                           col, lv_color_hex(KGUI_EDGE_TRACK_HEX));
}

/* cppcheck-suppress misra-c2012-8.7 ; public API of the Ring module */
void kgui_ring_set_color(kgui_ring_t *r, lv_color_t col)
{
    if ( (NULL != r) && (NULL != r->arc) )
    {
        lv_obj_set_style_arc_color(r->arc, col, LV_PART_INDICATOR);
    }
    else if ( (NULL != r) && (NULL != r->glass) )
    {
        r->glass->col = col;
        if ( !r->glass->cut )
        {
            lv_obj_set_style_line_color(r->glass->line, col, LV_PART_MAIN);
        }
    }
    else
    {
        /* Not made. */
    }
}

void kgui_ring_place(kgui_ring_t *r, int32_t x, int32_t y)
{
    if ( (NULL != r) && (NULL != r->arc) )
    {
        lv_obj_set_pos(r->arc, x, y);
    }
}

void kgui_ring_show(kgui_ring_t *r, bool show)
{
    if ( (NULL != r) && (NULL != r->arc) )
    {
        lv_obj_set_hidden(r->arc, !show);
    }
    else if ( (NULL != r) && (NULL != r->glass) )
    {
        lv_obj_set_hidden(r->glass->root, !show);
    }
    else
    {
        /* Not made. */
    }
}

void kgui_ring_cancel(kgui_ring_t *r)
{
    if ( NULL != r )
    {
        r->mode = KGUI_RING_IDLE;
        ring_reset(r);
    }
}

void kgui_ring_done(kgui_ring_t *r, uint32_t now_ms)
{
    if ( (NULL != r) && (KGUI_RING_CLOSE != r->mode) && (KGUI_RING_DONE != r->mode) )
    {
        if ( NULL != r->glass )
        {
            r->glass->close_from = r->pm;
            r->mode              = KGUI_RING_CLOSE;
            r->t0_ms             = now_ms;
        }
        else
        {
            ring_draw_pct(r, (uint8_t)KGUI_RING_FULL_PCT);
            r->pm   = (uint16_t)KGUI_RING_FULL_PM;
            r->mode = KGUI_RING_DONE;
        }
    }
}

void kgui_ring_cut(kgui_ring_t *r)
{
    if ( (NULL != r) && (NULL != r->glass) )
    {
        r->glass->cut = true;
        lv_obj_set_style_line_color(r->glass->line, KUI_COLOR_REST, LV_PART_MAIN);
        ring_glass_draw(r);
    }
}

/* cppcheck-suppress misra-c2012-8.7 ; public API of the Ring module */
void kgui_ring_progress(kgui_ring_t *r, uint8_t pct)
{
    if ( NULL != r )
    {
        uint8_t shown = pct;

        if ( KGUI_RING_FULL_PCT < shown )
        {
            shown = (uint8_t)KGUI_RING_FULL_PCT;
        }
        shown = ring_stepped(shown);
        if ( KGUI_RING_PROGRESS != r->mode )
        {
            ring_reset(r);
            r->mode = KGUI_RING_PROGRESS;
        }
        if ( shown != r->pct )
        {
            ring_draw_pct(r, shown);
        }
    }
}

/* cppcheck-suppress misra-c2012-8.7 ; public API of the Ring module */
void kgui_ring_spin(kgui_ring_t *r, uint32_t now_ms)
{
    if ( NULL != r )
    {
        ring_reset(r);
        r->mode  = KGUI_RING_SPIN;
        r->t0_ms = now_ms;
        ring_draw_pct(r, (uint8_t)KGUI_RING_SPIN_ARC_PCT);
    }
}

/**
 * @brief One step of a closing edge: the line runs on, turns `ok` and pops.
 *
 * @param r     The ring, in the CLOSE mode.
 * @param since How long the close has run, in milliseconds.
 */
static void ring_close_step(kgui_ring_t *r, uint32_t since)
{
    struct kgui_ring_glass * const g = r->glass;

    if ( KGUI_EDGE_CLOSE_MS > since )
    {
        uint16_t const pm = kgui_ring_close_pm(g->close_from, since);

        if ( pm != r->pm )
        {
            r->pm  = pm;
            r->pct = (uint8_t)(pm / RING_PM_PER_PCT);
            ring_glass_draw(r);
        }
    }
    else
    {
        lv_obj_set_style_line_color(g->line, KUI_COLOR_OK, LV_PART_MAIN);
        if ( KGUI_RING_FULL_PM != r->pm )
        {
            r->pm  = (uint16_t)KGUI_RING_FULL_PM;
            r->pct = (uint8_t)KGUI_RING_FULL_PCT;
            ring_glass_draw(r);
        }
        lv_obj_set_style_line_width(g->line,
                                    g->width + kgui_ring_pop_px(since - KGUI_EDGE_CLOSE_MS),
                                    LV_PART_MAIN);
        if ( KGUI_EDGE_DONE_MS <= since )
        {
            lv_obj_set_style_line_width(g->line, g->width, LV_PART_MAIN);
            r->mode = KGUI_RING_DONE;
        }
    }
}

void kgui_ring_tick(kgui_ring_t *r, uint32_t now_ms)
{
    if ( NULL != r )
    {
        uint32_t const since = now_ms - r->t0_ms;

        if ( KGUI_RING_SPIN == r->mode )
        {
            uint8_t const spin = ring_spin_pos(r, since);

            if ( spin != r->spin )
            {
                ring_draw_spin(r, spin);
            }
        }
        else if ( KGUI_RING_CLOSE == r->mode )
        {
            ring_close_step(r, since);
        }
        else
        {
            /* Nothing moves by the clock. */
        }
    }
}

uint32_t kgui_ring_wait(const kgui_ring_t *r, uint32_t now_ms, uint32_t next)
{
    uint32_t wait = next;

    if ( NULL != r )
    {
        uint32_t period = 0U;

        if ( KGUI_RING_SPIN == r->mode )
        {
            period = KGUI_RING_SPIN_STEP_MS;
        }
        else if ( KGUI_RING_CLOSE == r->mode )
        {
            period = KGUI_EDGE_STEP_MS;
        }
        else
        {
            /* No clock runs. */
        }
        if ( 0U != period )
        {
            uint32_t const left = period - ((now_ms - r->t0_ms) % period);

            if ( left < wait )
            {
                wait = left;
            }
        }
    }
    return wait;
}

kgui_ring_mode_t kgui_ring_mode(const kgui_ring_t *r)
{
    kgui_ring_mode_t mode = KGUI_RING_IDLE;

    if ( NULL != r )
    {
        mode = r->mode;
    }
    return mode;
}

uint8_t kgui_ring_pct(const kgui_ring_t *r)
{
    uint8_t pct = 0U;

    if ( NULL != r )
    {
        pct = r->pct;
    }
    return pct;
}

uint16_t kgui_ring_pm(const kgui_ring_t *r)
{
    uint16_t pm = 0U;

    if ( NULL != r )
    {
        pm = r->pm;
    }
    return pm;
}

const char *kgui_ring_word(const kgui_ring_t *r)
{
    static const char *const WORDS[] = { "idle", "progress", "spin", "done", "closing" };
    const char *word = WORDS[KGUI_RING_IDLE];

    if ( (NULL != r) && ((uint32_t)r->mode < (uint32_t)(sizeof(WORDS) / sizeof(WORDS[0]))) )
    {
        word = WORDS[r->mode];
    }
    return word;
}
