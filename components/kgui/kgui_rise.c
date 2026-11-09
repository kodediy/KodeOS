/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_rise.c
 * @brief Rise (kgui_rise.h).
 */
#include "kgui_rise.h"

#include <stddef.h>

#include "kbsp_board.h"
#include "kgui_curtain.h"
#include "kgui_grow.h"
#include "kgui_icon.h"
#include "kgui_motion.h"

#define RISE_OPA_FULL  255U  /**< A veil that covers, and a text that shows whole. */

int32_t kgui_rise_y(int32_t from_y, int32_t to_y, int32_t u)
{
    int32_t at = u;

    if ( 0 > at )
    {
        at = 0;
    }
    else if ( KGUI_GROW_UNITS < at )
    {
        at = KGUI_GROW_UNITS;
    }
    else
    {
        /* In range. */
    }
    return from_y + (((to_y - from_y) * at) / KGUI_GROW_UNITS);
}

bool kgui_rise_input_after(uint32_t since_ms)
{
    return KGUI_RISE_INPUT_MS <= since_ms;
}

bool kgui_rise_attach(kgui_rise_t *r, lv_obj_t *card)
{
    if ( (NULL != r) && (NULL != card) && (NULL == r->veil) )
    {
        r->card = card;
        r->veil = kgui_plain(card, 0, 0, (int32_t)KBSP_LCD_H_RES, (int32_t)KBSP_LCD_V_RES);
        if ( NULL != r->veil )
        {
            lv_obj_set_style_bg_color(r->veil, lv_obj_get_style_bg_color(card, LV_PART_MAIN), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(r->veil, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_radius(r->veil, lv_obj_get_style_radius(card, LV_PART_MAIN), LV_PART_MAIN);
            lv_obj_set_hidden(r->veil, true);
        }
        r->moving    = false;
        r->armed     = false;
        r->placed_ms = 0U;
    }
    return (NULL != r) && (NULL != r->veil);
}

/**
 * @brief Put the card and the veil where a progress says.
 *
 * @param r The rise.
 * @param u The progress, 0 to KGUI_GROW_UNITS.
 */
static void rise_place(kgui_rise_t *r, int32_t u)
{
    uint32_t const text = (uint32_t)kgui_grow_text_opa(u);
    uint32_t const veil = RISE_OPA_FULL - text;

    lv_obj_set_y(r->card, kgui_rise_y(r->from_y, r->to_y, u));
    if ( NULL != r->veil )
    {
        lv_obj_set_style_bg_opa(r->veil, (lv_opa_t)veil, LV_PART_MAIN);
    }
}

void kgui_rise_start(kgui_rise_t *r, int32_t from_y, int32_t to_y, uint32_t now_ms)
{
    if ( (NULL != r) && (NULL != r->card) )
    {
        r->from_y    = from_y;
        r->to_y      = to_y;
        r->moving    = true;
        r->armed     = true;
        r->t0_ms     = now_ms;
        r->placed_ms = now_ms;
        kui_spring_start(&r->sp, 0.0F, 0.0F, (float)KGUI_GROW_UNITS, KGUI_RISE_PERIOD_MS,
                         KUI_SPRING_CRITICAL, now_ms);
        if ( NULL != r->veil )
        {
            lv_obj_move_foreground(r->veil);
            lv_obj_set_hidden(r->veil, false);
        }
        rise_place(r, 0);
    }
}

void kgui_rise_gate(kgui_rise_t *r, uint32_t now_ms)
{
    if ( NULL != r )
    {
        r->armed = true;
        r->t0_ms = now_ms;
    }
}

bool kgui_rise_tick(kgui_rise_t *r, uint32_t now_ms)
{
    bool landed = false;

    if ( (NULL != r) && r->moving )
    {
        bool const rested = kui_spring_rested(&r->sp, now_ms, KGUI_REST_UNITS, KGUI_REST_UNITS_SPEED);

        if ( rested )
        {
            r->moving = false;
            landed    = true;
            rise_place(r, KGUI_GROW_UNITS);
            if ( NULL != r->veil )
            {
                lv_obj_set_hidden(r->veil, true);
            }
        }
        else if ( 0U == kgui_place_left(r->placed_ms, now_ms) )
        {
            float pos = 0.0F;

            kui_spring_at(&r->sp, now_ms, &pos, NULL);
            r->placed_ms = now_ms;
            rise_place(r, kgui_round_px(pos));
        }
        else
        {
            /* Not due: the pass draws what the last placement left. */
        }
    }
    return landed;
}

void kgui_rise_stop(kgui_rise_t *r)
{
    if ( NULL != r )
    {
        r->moving = false;
        r->armed  = false;
        if ( NULL != r->veil )
        {
            lv_obj_set_hidden(r->veil, true);
        }
    }
}

bool kgui_rise_input_open(const kgui_rise_t *r, uint32_t now_ms)
{
    bool open = true;

    if ( (NULL != r) && r->armed )
    {
        open = kgui_rise_input_after(now_ms - r->t0_ms);
    }
    return open;
}

uint32_t kgui_rise_input_left(const kgui_rise_t *r, uint32_t now_ms)
{
    uint32_t left = 0U;

    if ( (NULL != r) && r->armed && !kgui_rise_input_after(now_ms - r->t0_ms) )
    {
        left = KGUI_RISE_INPUT_MS - (now_ms - r->t0_ms);
    }
    return left;
}

uint32_t kgui_rise_wait(const kgui_rise_t *r, uint32_t now_ms, uint32_t next)
{
    uint32_t wait = next;

    if ( NULL != r )
    {
        if ( r->moving )
        {
            uint32_t const left = kgui_place_left(r->placed_ms, now_ms);

            if ( left < wait )
            {
                wait = left;
            }
        }
        if ( r->armed && !kgui_rise_input_after(now_ms - r->t0_ms) )
        {
            uint32_t const left = KGUI_RISE_INPUT_MS - (now_ms - r->t0_ms);

            if ( left < wait )
            {
                wait = left;
            }
        }
    }
    return wait;
}
