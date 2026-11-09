/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_test_dot.c
 * @brief The E5 test's canvas on kodeOS's glass (kgui_test_dot.h).
 */
#include "kgui_test_dot.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "kode_log.h"
#include "kui_test_dot.h"

#define TAG "kgui"

_Static_assert(KUI_TD_GRID_CELLS <= TEST_DOT_SCREEN_ARG_MAX,
               "an auto screen's cells fit in a request's arg");

static lv_obj_t *s_obj;                      /**< The canvas' parent the last request built, or NULL. */
static uint8_t   s_kind;                     /**< Its kind.                                           */
static uint8_t   s_grid[KUI_TD_GRID_CELLS];  /**< An auto screen's cells as shown.                    */

void kgui_td_copy(kgui_td_req_t *out, TestDotScreenEvt const *e)
{
    if ( (NULL != out) && (NULL != e) )
    {
        out->kind = e->kind;
        out->seq  = e->seq;
        out->len  = e->len;
        if ( (uint8_t)TEST_DOT_SCREEN_ARG_MAX < out->len )
        {
            out->len = (uint8_t)TEST_DOT_SCREEN_ARG_MAX;
        }
        (void)memcpy(out->arg, e->arg, sizeof(out->arg));
    }
}

void kgui_td_clear(void)
{
    if ( NULL != s_obj )
    {
        lv_obj_delete(s_obj);
        s_obj = NULL;
    }
}

/**
 * @brief Change the cells of the auto screen on the glass that @p r changes.
 * @param r The request, an auto screen.
 */
static void grid_update(kgui_td_req_t const *r)
{
    for ( uint8_t i = 0U; ((uint8_t)KUI_TD_GRID_CELLS > i) && (i < r->len); i++ )
    {
        if ( s_grid[i] != r->arg[i] )
        {
            kui_td_grid_set(s_obj, i, r->arg[i]);
            s_grid[i] = r->arg[i];
        }
    }
}

/**
 * @brief Build what @p r names on a new parent on @p page.
 * @param page The page.
 * @param r    The request, not the end.
 */
static void build(lv_obj_t *page, kgui_td_req_t const *r)
{
    s_obj = lv_obj_create(page);
    if ( NULL == s_obj )
    {
        KLOG_E(TAG, "no memory for E5 screen 0x%02x", (unsigned)r->kind);
    }
    else
    {
        lv_obj_remove_style_all(s_obj);
        lv_obj_set_size(s_obj, KUI_TD_W_PX, KUI_TD_H_PX);
        lv_obj_set_scrollable(s_obj, false);
        if ( 0U != (r->kind & TEST_DOT_PAT_FLAG) )
        {
            uint8_t const pat = r->kind & (uint8_t)~TEST_DOT_PAT_FLAG;
            uint8_t       arg = 0U;

            if ( 0U < r->len )
            {
                arg = r->arg[0];
            }
            kui_td_pattern_build(s_obj, (kui_td_pattern_t)pat, arg);
        }
        else
        {
            kui_td_screen_build(s_obj, (kui_td_screen_t)r->kind, r->arg, r->len);
        }
        s_kind = r->kind;
        (void)memset(s_grid, (int)KUI_TD_ST_PENDING, sizeof(s_grid));
        if ( TEST_DOT_SCR_AUTO == r->kind )
        {
            for ( uint8_t i = 0U; ((uint8_t)KUI_TD_GRID_CELLS > i) && (i < r->len); i++ )
            {
                s_grid[i] = r->arg[i];
            }
        }
    }
}

void kgui_td_show(lv_obj_t *page, kgui_td_req_t const *r)
{
    if ( (NULL == page) || (NULL == r) )
    {
        /* Nothing to build on, or nothing asked. */
    }
    else if ( (NULL != s_obj) && (TEST_DOT_SCR_AUTO == r->kind) && (TEST_DOT_SCR_AUTO == s_kind) )
    {
        grid_update(r);
    }
    else
    {
        kgui_td_clear();
        if ( TEST_DOT_SCR_END != r->kind )
        {
            build(page, r);
        }
    }
}
