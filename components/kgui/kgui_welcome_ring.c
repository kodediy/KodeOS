/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_welcome_ring.c
 * @brief The landing's ring (kgui_welcome.h): kshell_edge.c's SEGS form at 10 px, from kode-dot-os.
 */
#include <stdbool.h>
#include <stddef.h>

#include "kbsp_board.h"
#include "kgui_icon.h"
#include "kgui_welcome.h"
#include "kui.h"
#include "lvgl.h"

#define RING_STEP_PX   24.0F /**< One chord of the stroke: kshell's. */
#define RING_DRAW_PX   8.0F  /**< One chord as drawn: at kshell's 24 the corners show their sides on the Dot. */
#define RING_PAD_PX    4     /**< Round a repainted stretch, past the stroke: kshell's. */

_Static_assert((410U == KBSP_LCD_H_RES) && (502U == KBSP_LCD_V_RES) && (114U == KBSP_LCD_CORNER_R_PX),
               "kgui_welcome.c's path is this glass's");

/** @brief The one ring. It is the anims' `var`, and outlives the object. */
static struct {
    lv_obj_t *obj;     /**< NULL when it is not on the glass. */
    int32_t   drawn;   /**< The last eighth, 0 to KGUI_WELCOME_FULL_PM. */
    int32_t   w;       /**< The stroke now: the pop thickens it. */
    lv_opa_t  opa;     /**< How much of its colour is left: the fade. */
    bool      closed;  /**< Whole: `ok`. */
} s_welcome_ring;

/**
 * @brief Draw the stroke along the path as far as it is filled, in chords of RING_DRAW_PX.
 *
 * @param ev The ring's LV_EVENT_DRAW_MAIN.
 */
static void ring_on_draw(lv_event_t *ev)
{
    lv_layer_t * const layer = lv_event_get_layer(ev);
    float const        end   = kgui_welcome_fill_len(s_welcome_ring.drawn);
    lv_area_t          c;
    lv_color_t         col   = KUI_COLOR_TEXT_ON_BG;
    lv_draw_line_dsc_t dsc;
    float              d     = 0.0F;

    lv_obj_get_coords(s_welcome_ring.obj, &c);
    if ( s_welcome_ring.closed )
    {
        col = KUI_COLOR_OK;
    }
    /* The fade mixes the colour into the page, not opacity: overlapping round caps would bead. */
    if ( (lv_opa_t)LV_OPA_COVER != s_welcome_ring.opa )
    {
        col = lv_color_mix(col, KUI_COLOR_BG, s_welcome_ring.opa);
    }
    lv_draw_line_dsc_init(&dsc);
    dsc.color       = col;
    dsc.width       = s_welcome_ring.w;
    dsc.round_start = 1;
    dsc.round_end   = 1;
    while ( d < end )
    {
        float const e = ((d + RING_DRAW_PX) < end) ? (d + RING_DRAW_PX) : end;
        float       ax;
        float       ay;
        float       bx;
        float       by;

        kgui_welcome_path_at(d, &ax, &ay);
        kgui_welcome_path_at(e, &bx, &by);
        dsc.p1.x = (lv_value_precise_t)(ax + (float)c.x1);
        dsc.p1.y = (lv_value_precise_t)(ay + (float)c.y1);
        dsc.p2.x = (lv_value_precise_t)(bx + (float)c.x1);
        dsc.p2.y = (lv_value_precise_t)(by + (float)c.y1);
        lv_draw_line(layer, &dsc);
        d = e;
    }
}

/**
 * @brief Repaint the glass the stroke touches between two lengths of the path.
 *
 * @param d0 The nearer length.
 * @param d1 The further one.
 */
static void ring_inval_span(float d0, float d1)
{
    float     x0 = 1.0e6F;
    float     y0 = 1.0e6F;
    float     x1 = -1.0e6F;
    float     y1 = -1.0e6F;
    float     d  = d0;
    bool      last = false;
    int32_t   pad;
    lv_area_t c;
    lv_area_t a;

    while ( !last )
    {
        float px;
        float py;

        if ( d >= d1 )
        {
            d    = d1;
            last = true;
        }
        kgui_welcome_path_at(d, &px, &py);
        x0 = (px < x0) ? px : x0;
        y0 = (py < y0) ? py : y0;
        x1 = (px > x1) ? px : x1;
        y1 = (py > y1) ? py : y1;
        d += RING_STEP_PX;
    }
    pad = (s_welcome_ring.w / 2) + RING_PAD_PX;
    lv_obj_get_coords(s_welcome_ring.obj, &c);
    a.x1 = c.x1 + (int32_t)x0 - pad;
    a.y1 = c.y1 + (int32_t)y0 - pad;
    a.x2 = c.x1 + (int32_t)x1 + pad;
    a.y2 = c.y1 + (int32_t)y1 + pad;
    (void)lv_obj_invalidate_area(s_welcome_ring.obj, &a);
}

/** @brief The fill's step: the ring drawn to @p v, and the stretch it adds repainted. */
static void ring_fill_exec(void *var, int32_t v)
{
    int32_t const old = s_welcome_ring.drawn;

    (void)var;
    s_welcome_ring.drawn = v;
    ring_inval_span(kgui_welcome_fill_len(old) - 2.0F, kgui_welcome_fill_len(v) + 2.0F);
}

/** @brief The pop's step: the stroke @p v px thicker than KGUI_WELCOME_RING_W. */
static void ring_pop_exec(void *var, int32_t v)
{
    (void)var;
    s_welcome_ring.w = KGUI_WELCOME_RING_W + v;
    (void)lv_obj_invalidate(s_welcome_ring.obj);
}

/** @brief The fade's step: @p v of the ring's colour left. */
static void ring_fade_exec(void *var, int32_t v)
{
    (void)var;
    s_welcome_ring.opa = (lv_opa_t)v;
    (void)lv_obj_invalidate(s_welcome_ring.obj);
}

/** @brief The fade is over: the ring leaves the glass. */
static void ring_gone(lv_anim_t *a)
{
    (void)a;
    if ( NULL != s_welcome_ring.obj )
    {
        lv_obj_delete(s_welcome_ring.obj);
        s_welcome_ring.obj = NULL;
    }
}

/** @brief The pop is over: the ring stays, and fades. */
static void ring_pop_back(lv_anim_t *a)
{
    lv_anim_t an;

    (void)a;
    s_welcome_ring.w = KGUI_WELCOME_RING_W;
    (void)lv_obj_invalidate(s_welcome_ring.obj);
    lv_anim_init(&an);
    lv_anim_set_var(&an, &s_welcome_ring);
    lv_anim_set_exec_cb(&an, ring_fade_exec);
    lv_anim_set_values(&an, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_delay(&an, KGUI_WELCOME_HOLD_MS);
    lv_anim_set_duration(&an, KGUI_WELCOME_FADE_MS);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&an, ring_gone);
    (void)lv_anim_start(&an);
}

/** @brief The top of the pop: back down. */
static void ring_pop_out(lv_anim_t *a)
{
    lv_anim_t an;

    (void)a;
    lv_anim_init(&an);
    lv_anim_set_var(&an, &s_welcome_ring);
    lv_anim_set_exec_cb(&an, ring_pop_exec);
    lv_anim_set_values(&an, KGUI_WELCOME_POP_PX, 0);
    lv_anim_set_duration(&an, KGUI_WELCOME_POP_MS);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&an, ring_pop_back);
    (void)lv_anim_start(&an);
}

/** @brief The eighth is in: the whole ring turns `ok` with the pop. */
static void ring_filled(lv_anim_t *a)
{
    lv_anim_t an;

    (void)a;
    s_welcome_ring.drawn  = KGUI_WELCOME_FULL_PM;
    s_welcome_ring.closed = true;
    (void)lv_obj_invalidate(s_welcome_ring.obj);
    lv_anim_init(&an);
    lv_anim_set_var(&an, &s_welcome_ring);
    lv_anim_set_exec_cb(&an, ring_pop_exec);
    lv_anim_set_values(&an, 0, KGUI_WELCOME_POP_PX);
    lv_anim_set_duration(&an, KGUI_WELCOME_POP_MS);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&an, ring_pop_out);
    (void)lv_anim_start(&an);
}

bool kgui_welcome_ring_start(void)
{
    lv_anim_t an;

    kgui_welcome_ring_stop();
    s_welcome_ring.obj = kgui_plain(lv_layer_top(), 0, 0, (int32_t)KBSP_LCD_H_RES, (int32_t)KBSP_LCD_V_RES);
    if ( NULL == s_welcome_ring.obj )
    {
        return false;
    }
    s_welcome_ring.w      = KGUI_WELCOME_RING_W;
    s_welcome_ring.opa    = LV_OPA_COVER;
    s_welcome_ring.closed = false;
    s_welcome_ring.drawn  = 0;
    (void)lv_obj_add_event_cb(s_welcome_ring.obj, ring_on_draw, LV_EVENT_DRAW_MAIN, NULL);
    (void)lv_obj_invalidate(s_welcome_ring.obj);
    lv_anim_init(&an);
    lv_anim_set_var(&an, &s_welcome_ring);
    lv_anim_set_exec_cb(&an, ring_fill_exec);
    lv_anim_set_values(&an, 0, KGUI_WELCOME_FULL_PM);
    lv_anim_set_duration(&an, KGUI_WELCOME_FILL_MS);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&an, ring_filled);
    (void)lv_anim_start(&an);
    return true;
}

void kgui_welcome_ring_stop(void)
{
    if ( NULL != s_welcome_ring.obj )
    {
        (void)lv_anim_delete(&s_welcome_ring, NULL);
        lv_obj_delete(s_welcome_ring.obj);
        s_welcome_ring.obj = NULL;
    }
}
