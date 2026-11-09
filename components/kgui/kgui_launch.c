/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_launch.c
 * @brief The launch of an installed app (kgui_launch.h).
 */
#include "kgui_launch.h"

#include <stddef.h>

#include "kbsp_board.h"
#include "kui.h"
#include "kgui_curtain.h"
#include "kgui_ring.h"

#define LAUNCH_OPA_FULL     255   /**< A layer that is wholly there. */
#define LAUNCH_ONE_K        1000  /**< The flight's progress, in thousandths. */
#define LAUNCH_MID_X        ((int32_t)KBSP_LCD_H_RES / 2)  /**< The glass's middle, across. */
#define LAUNCH_MID_Y        231   /**< And down: the apocrifa's 231, 20 px over the middle, so the icon of 160 and its Ring sit in the glass's optical centre. */

#define LAUNCH_SPIN_AT      KGUI_LAUNCH_FLIGHT_MS                        /**< The Ring begins to turn. */
#define LAUNCH_CLOSE_AT     (LAUNCH_SPIN_AT + KGUI_LAUNCH_SPIN_MS)       /**< The Ring begins to close. */
#define LAUNCH_FADE_AT      (LAUNCH_CLOSE_AT + KGUI_POP_TOTAL_MS + KGUI_LAUNCH_HOLD_MS)        /**< The glass begins to go black. */
#define LAUNCH_DONE_AT      (LAUNCH_FADE_AT + (KGUI_LAUNCH_FADE_STEPS * KGUI_LAUNCH_FADE_STEP_MS)) /**< The run may be asked for. */

_Static_assert(0U == (KGUI_LAUNCH_SPIN_MS % KGUI_RING_SPIN_STEP_MS), "the Ring turns a whole number of positions");
_Static_assert(KGUI_LAUNCH_RING_PX > KGUI_LAUNCH_ICON_PX, "the Ring stands round the icon");

/** @brief The scene's objects and where it is. */
typedef struct {
    lv_obj_t           *root;      /**< Covers the glass and takes every touch. */
    lv_obj_t           *dim;       /**< Black under the icon. */
    lv_obj_t           *fade;      /**< Black over everything. */
    kgui_icon_t         icon;      /**< The flying icon. */
    kgui_ring_t         ring;      /**< The Ring round it. */
    kgui_grow_icon_t    from;      /**< Where the icon began. */
    kgui_launch_stage_t stage;     /**< The stage whose entry was last done. */
    uint32_t            t0_ms;     /**< When it began. */
    uint32_t            placed_ms; /**< When the icon was last placed. */
    uint8_t             dim_opa;   /**< The dim as last drawn. */
    uint8_t             fade_opa;  /**< The fade as last drawn. */
    uint8_t             ring_pct;  /**< The Ring's share as last drawn while it closes. */
} launch_t;

static launch_t s_ln;   /**< The scene. */

/* ---- The model ---------------------------------------------------------------- */

kgui_launch_stage_t kgui_launch_stage_at(uint32_t t_ms)
{
    kgui_launch_stage_t stage = KGUI_LAUNCH_DONE;

    if ( LAUNCH_SPIN_AT > t_ms )
    {
        stage = KGUI_LAUNCH_FLIGHT;
    }
    else if ( LAUNCH_CLOSE_AT > t_ms )
    {
        stage = KGUI_LAUNCH_SPIN;
    }
    else if ( LAUNCH_FADE_AT > t_ms )
    {
        stage = KGUI_LAUNCH_CLOSE;
    }
    else if ( LAUNCH_DONE_AT > t_ms )
    {
        stage = KGUI_LAUNCH_FADE;
    }
    else
    {
        /* Black. */
    }
    return stage;
}

uint32_t kgui_launch_stage_start(kgui_launch_stage_t stage)
{
    uint32_t at = 0U;

    if ( KGUI_LAUNCH_SPIN == stage )
    {
        at = LAUNCH_SPIN_AT;
    }
    else if ( KGUI_LAUNCH_CLOSE == stage )
    {
        at = LAUNCH_CLOSE_AT;
    }
    else if ( KGUI_LAUNCH_FADE == stage )
    {
        at = LAUNCH_FADE_AT;
    }
    else if ( KGUI_LAUNCH_DONE == stage )
    {
        at = LAUNCH_DONE_AT;
    }
    else
    {
        /* The flight begins with the scene. */
    }
    return at;
}

int32_t kgui_launch_flight_u(uint32_t t_ms)
{
    int32_t u = LAUNCH_ONE_K;

    if ( KGUI_LAUNCH_FLIGHT_MS > t_ms )
    {
        uint32_t const share = (t_ms * (uint32_t)LAUNCH_ONE_K) / KGUI_LAUNCH_FLIGHT_MS;
        int32_t const  x     = (int32_t)share;
        int32_t const  left  = LAUNCH_ONE_K - x;

        u = LAUNCH_ONE_K - ((left * left * left) / (LAUNCH_ONE_K * LAUNCH_ONE_K));
    }
    return u;
}

kgui_grow_icon_t kgui_launch_icon_at(const kgui_grow_icon_t *from, uint32_t t_ms)
{
    int32_t const    u = kgui_launch_flight_u(t_ms);
    kgui_grow_icon_t at;

    at.cx   = from->cx + (((LAUNCH_MID_X - from->cx) * u) / LAUNCH_ONE_K);
    at.cy   = from->cy + (((LAUNCH_MID_Y - from->cy) * u) / LAUNCH_ONE_K);
    at.size = from->size + (((KGUI_LAUNCH_ICON_PX - from->size) * u) / LAUNCH_ONE_K);
    return at;
}

uint8_t kgui_launch_dim_opa(uint32_t t_ms)
{
    int32_t const u = kgui_launch_flight_u(t_ms);
    int32_t const n = ((u * (int32_t)KGUI_LAUNCH_DIM_STEPS) + (LAUNCH_ONE_K - 1)) / LAUNCH_ONE_K;

    int32_t const opa = (n * LAUNCH_OPA_FULL) / (int32_t)KGUI_LAUNCH_DIM_STEPS;

    return (uint8_t)opa;
}

uint8_t kgui_launch_fade_opa(uint32_t t_ms)
{
    uint32_t n = 0U;

    if ( LAUNCH_FADE_AT <= t_ms )
    {
        n = ((t_ms - LAUNCH_FADE_AT) / KGUI_LAUNCH_FADE_STEP_MS) + 1U;
        if ( KGUI_LAUNCH_FADE_STEPS < n )
        {
            n = KGUI_LAUNCH_FADE_STEPS;
        }
    }
    uint32_t const opa = (n * (uint32_t)LAUNCH_OPA_FULL) / KGUI_LAUNCH_FADE_STEPS;

    return (uint8_t)opa;
}

uint8_t kgui_launch_close_pct(uint32_t t_ms)
{
    uint32_t pct = KGUI_RING_SPIN_ARC_PCT;

    if ( LAUNCH_FADE_AT <= t_ms )
    {
        pct = KGUI_RING_FULL_PCT;
    }
    else if ( LAUNCH_CLOSE_AT < t_ms )
    {
        uint32_t into = t_ms - LAUNCH_CLOSE_AT;

        /* Whole by the end of the pop; it stands so through the hold. */
        if ( KGUI_POP_TOTAL_MS < into )
        {
            into = KGUI_POP_TOTAL_MS;
        }
        pct += ((KGUI_RING_FULL_PCT - KGUI_RING_SPIN_ARC_PCT) * into) / KGUI_POP_TOTAL_MS;
    }
    else
    {
        /* Not closing yet. */
    }
    return (uint8_t)pct;
}

/* ---- The scene ---------------------------------------------------------------- */

/**
 * @brief A full-glass black layer, clear.
 *
 * @param parent Its parent.
 * @return The layer, or NULL.
 */
static lv_obj_t *launch_black(lv_obj_t *parent)
{
    lv_obj_t *const obj = kgui_plain(parent, 0, 0, (int32_t)KBSP_LCD_H_RES, (int32_t)KBSP_LCD_V_RES);

    if ( NULL != obj )
    {
        lv_obj_set_style_bg_color(obj, KUI_COLOR_BG, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    }
    return obj;
}

/**
 * @brief Put the icon where the flight says.
 *
 * @param at The icon.
 */
static void launch_icon_place(const kgui_grow_icon_t *at)
{
    kgui_icon_size(&s_ln.icon, at->size);
    lv_obj_set_pos(s_ln.icon.disc, at->cx - (at->size / 2), at->cy - (at->size / 2));
    lv_obj_set_style_transform_pivot_x(s_ln.icon.disc, at->size / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(s_ln.icon.disc, at->size / 2, LV_PART_MAIN);
}

/**
 * @brief Do what a stage begins with.
 *
 * @param stage The stage entered.
 * @param now_ms The clock.
 */
static void launch_enter(kgui_launch_stage_t stage, uint32_t now_ms)
{
    if ( KGUI_LAUNCH_SPIN == stage )
    {
        kgui_grow_icon_t const mid = { LAUNCH_MID_X, LAUNCH_MID_Y, KGUI_LAUNCH_ICON_PX };

        launch_icon_place(&mid);
        lv_obj_set_style_bg_opa(s_ln.dim, (lv_opa_t)LAUNCH_OPA_FULL, LV_PART_MAIN);
        kgui_ring_show(&s_ln.ring, true);
        kgui_ring_spin(&s_ln.ring, now_ms);
    }
    else if ( KGUI_LAUNCH_CLOSE == stage )
    {
        kgui_ring_set_color(&s_ln.ring, KUI_COLOR_OK);
        s_ln.ring_pct = 0U;
    }
    else if ( KGUI_LAUNCH_FADE == stage )
    {
        lv_obj_set_style_transform_scale(s_ln.icon.disc, (int32_t)KGUI_POP_ONE, LV_PART_MAIN);
        kgui_ring_progress(&s_ln.ring, (uint8_t)KGUI_RING_FULL_PCT);
    }
    else if ( KGUI_LAUNCH_DONE == stage )
    {
        s_ln.fade_opa = (uint8_t)LAUNCH_OPA_FULL;
        lv_obj_set_style_bg_opa(s_ln.fade, (lv_opa_t)LAUNCH_OPA_FULL, LV_PART_MAIN);
    }
    else
    {
        /* The flight needs nothing at its start. */
    }
}

/**
 * @brief Place what moves inside a stage.
 *
 * @param stage The stage.
 * @param t_ms  Milliseconds since the scene began.
 * @param now_ms The clock.
 */
static void launch_place(kgui_launch_stage_t stage, uint32_t t_ms, uint32_t now_ms)
{
    bool const due = (0U == kgui_place_left(s_ln.placed_ms, now_ms));

    if ( KGUI_LAUNCH_FLIGHT == stage )
    {
        if ( due )
        {
            kgui_grow_icon_t const at = kgui_launch_icon_at(&s_ln.from, t_ms);
            uint8_t const          dim = kgui_launch_dim_opa(t_ms);

            launch_icon_place(&at);
            if ( dim != s_ln.dim_opa )
            {
                s_ln.dim_opa = dim;
                lv_obj_set_style_bg_opa(s_ln.dim, (lv_opa_t)dim, LV_PART_MAIN);
            }
            s_ln.placed_ms = now_ms;
        }
    }
    else if ( KGUI_LAUNCH_SPIN == stage )
    {
        kgui_ring_tick(&s_ln.ring, now_ms);
    }
    else if ( KGUI_LAUNCH_CLOSE == stage )
    {
        uint8_t const pct = kgui_launch_close_pct(t_ms);

        if ( pct != s_ln.ring_pct )
        {
            s_ln.ring_pct = pct;
            kgui_ring_progress(&s_ln.ring, pct);
        }
        if ( due )
        {
            lv_obj_set_style_transform_scale(s_ln.icon.disc,
                                             (int32_t)kgui_pop_scale(t_ms - LAUNCH_CLOSE_AT), LV_PART_MAIN);
            s_ln.placed_ms = now_ms;
        }
    }
    else if ( KGUI_LAUNCH_FADE == stage )
    {
        uint8_t const fade = kgui_launch_fade_opa(t_ms);

        if ( fade != s_ln.fade_opa )
        {
            s_ln.fade_opa = fade;
            lv_obj_set_style_bg_opa(s_ln.fade, (lv_opa_t)fade, LV_PART_MAIN);
        }
    }
    else
    {
        /* Black: nothing moves. */
    }
}

bool kgui_launch_begin(lv_obj_t *layer, const kgui_icon_spec_t *look,
                       const kgui_grow_icon_t *from, uint32_t now_ms)
{
    bool ok = false;

    kgui_launch_end();
    s_ln.root = kgui_plain(layer, 0, 0, (int32_t)KBSP_LCD_H_RES, (int32_t)KBSP_LCD_V_RES);
    if ( NULL != s_ln.root )
    {
        lv_obj_set_clickable(s_ln.root, true);
        s_ln.dim  = launch_black(s_ln.root);
        ok        = (NULL != s_ln.dim)
                    && kgui_ring_create(&s_ln.ring, s_ln.root, KGUI_LAUNCH_RING_PX, KGUI_LAUNCH_RING_W,
                                        KUI_COLOR_TEXT_ON_BG, KUI_COLOR_OFF)
                    && kgui_icon_make(&s_ln.icon, s_ln.root, from->size);
    }
    if ( ok )
    {
        s_ln.fade = launch_black(s_ln.root);
        ok        = (NULL != s_ln.fade);
    }
    if ( ok )
    {
        kgui_ring_place(&s_ln.ring, LAUNCH_MID_X - (KGUI_LAUNCH_RING_PX / 2),
                        LAUNCH_MID_Y - (KGUI_LAUNCH_RING_PX / 2));
        kgui_ring_show(&s_ln.ring, false);
        kgui_icon_dress(&s_ln.icon, look, from->size);
        s_ln.from      = *from;
        s_ln.stage     = KGUI_LAUNCH_FLIGHT;
        s_ln.t0_ms     = now_ms;
        s_ln.placed_ms = now_ms;
        s_ln.dim_opa   = 0U;
        s_ln.fade_opa  = 0U;
        s_ln.ring_pct  = 0U;
        launch_icon_place(from);
    }
    else
    {
        kgui_launch_end();
    }
    return ok;
}

uint32_t kgui_launch_tick(uint32_t now_ms)
{
    uint32_t ev = 0U;

    if ( (NULL != s_ln.root) && (KGUI_LAUNCH_DONE != s_ln.stage) )
    {
        uint32_t const            t     = now_ms - s_ln.t0_ms;
        kgui_launch_stage_t const stage = kgui_launch_stage_at(t);

        while ( s_ln.stage < stage )
        {
            uint32_t const next = (uint32_t)s_ln.stage + 1U;

            s_ln.stage = (kgui_launch_stage_t)next;
            launch_enter(s_ln.stage, now_ms);
        }
        launch_place(s_ln.stage, t, now_ms);
        if ( KGUI_LAUNCH_DONE == s_ln.stage )
        {
            ev = KGUI_LAUNCH_EV_RUN;
        }
    }
    return ev;
}

uint32_t kgui_launch_wait(uint32_t now_ms, uint32_t next)
{
    uint32_t wait = next;

    if ( (NULL != s_ln.root) && (KGUI_LAUNCH_DONE != s_ln.stage) )
    {
        uint32_t const            t     = now_ms - s_ln.t0_ms;
        kgui_launch_stage_t const after = kgui_launch_stage_at(t);
        uint32_t                  left  = kgui_place_left(s_ln.placed_ms, now_ms);

        if ( KGUI_LAUNCH_SPIN == after )
        {
            left = kgui_ring_wait(&s_ln.ring, now_ms, wait);
        }
        else if ( KGUI_LAUNCH_FADE == after )
        {
            left = KGUI_LAUNCH_FADE_STEP_MS - ((t - LAUNCH_FADE_AT) % KGUI_LAUNCH_FADE_STEP_MS);
        }
        else
        {
            /* Flight and close are placed on the frame rate. */
        }
        if ( left < wait )
        {
            wait = left;
        }
        if ( KGUI_LAUNCH_DONE != after )
        {
            uint32_t const following = (uint32_t)after + 1U;
            uint32_t const edge      = kgui_launch_stage_start((kgui_launch_stage_t)following) - t;

            if ( edge < wait )
            {
                wait = edge;
            }
        }
    }
    return wait;
}

void kgui_launch_end(void)
{
    if ( NULL != s_ln.root )
    {
        lv_obj_delete(s_ln.root);
    }
    s_ln.root      = NULL;
    s_ln.dim       = NULL;
    s_ln.fade      = NULL;
    s_ln.icon.disc = NULL;
    s_ln.icon.img  = NULL;
    s_ln.icon.mono = NULL;
    s_ln.ring.arc  = NULL;
    s_ln.stage     = KGUI_LAUNCH_IDLE;
}

bool kgui_launch_active(void)
{
    return NULL != s_ln.root;
}

bool kgui_launch_stoppable(void)
{
    return (NULL != s_ln.root) && (KGUI_LAUNCH_DONE != s_ln.stage);
}

const char *kgui_launch_word(void)
{
    static const char *const WORDS[] = { "idle", "flight", "ring", "close", "fade", "done" };
    const char *word = WORDS[KGUI_LAUNCH_IDLE];

    if ( (uint32_t)s_ln.stage < (uint32_t)(sizeof(WORDS) / sizeof(WORDS[0])) )
    {
        word = WORDS[s_ln.stage];
    }
    return word;
}
