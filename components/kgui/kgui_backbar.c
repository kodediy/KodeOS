/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_backbar.c
 * @brief A page's title and its arrow back.
 */
#include "kgui_backbar.h"

#include <string.h>

#include "kbsp_board.h"
#include "kgui_lvgl.h"
#include "kui.h"

#define ARROW_W    12   /* The arrow's width. */
#define ARROW_H    22   /* And its height. */
#define ARROW_INK  4    /* Its stroke. */
#define ARROW_GAP  14   /* From the arrow to the words. */

lv_obj_t *kgui_backbar_make(lv_obj_t *parent, lv_event_cb_t on_tap)
{
    static const lv_point_precise_t s_arrow[] = {
        { ARROW_W, 0 }, { 0, ARROW_H / 2 }, { ARROW_W, ARROW_H },
    };
    lv_obj_t * const bar = lv_obj_create(parent);

    if ( NULL != bar )
    {
        lv_obj_t * const hit   = lv_obj_create(bar);
        lv_obj_t * const arrow = lv_line_create(bar);
        lv_obj_t * const words = lv_label_create(bar);
        int32_t const    line  = KGUI_BACKBAR_Y + KGUI_BACKBAR_H_PX;

        lv_obj_remove_style_all(bar);
        lv_obj_set_pos(bar, 0, 0);
        lv_obj_set_size(bar, (int32_t)KBSP_LCD_H_RES, KGUI_BACKBAR_FOOT);
        lv_obj_set_scrollable(bar, false);
        lv_obj_set_clickable(bar, false);
        /* Solid to the foot of its line, then out to nothing. */
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, KUI_COLOR_BG, 0);
        lv_obj_set_style_bg_grad_color(bar, KUI_COLOR_BG, 0);
        lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_main_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_grad_opa(bar, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_main_stop(bar, (line * 255) / KGUI_BACKBAR_FOOT, 0);
        lv_obj_set_style_bg_grad_stop(bar, 255, 0);
        /* Only its line takes a tap: what shows through the fade is the list's. */
        lv_obj_remove_style_all(hit);
        lv_obj_set_size(hit, (int32_t)KBSP_LCD_H_RES, line);
        lv_obj_set_scrollable(hit, false);
        lv_obj_set_clickable(hit, NULL != on_tap);
        if ( NULL != on_tap )
        {
            (void)lv_obj_add_event_cb(hit, on_tap, LV_EVENT_CLICKED, NULL);
        }
        lv_obj_set_name(arrow, "bar_arrow");
        lv_line_set_points(arrow, s_arrow, sizeof(s_arrow) / sizeof(s_arrow[0]));
        lv_obj_set_style_line_width(arrow, ARROW_INK, 0);
        lv_obj_set_style_line_rounded(arrow, true, 0);
        lv_obj_set_style_line_color(arrow, KUI_COLOR_TEXT_ON_BG, 0);
        lv_obj_set_clickable(arrow, false);
        lv_obj_set_y(arrow, KGUI_BACKBAR_Y + ((KGUI_BACKBAR_H_PX - ARROW_H) / 2));
        lv_obj_set_name(words, "bar_words");
        lv_obj_set_style_text_font(words, &kui_font_name, 0);
        lv_obj_set_style_text_color(words, KUI_COLOR_TEXT_ON_BG, 0);
        lv_label_set_text_static(words, "");
        lv_obj_set_clickable(words, false);
        lv_obj_set_y(words, KGUI_BACKBAR_Y
                            + ((KGUI_BACKBAR_H_PX - lv_font_get_line_height(&kui_font_name)) / 2));
    }
    return bar;
}

void kgui_backbar_set(const lv_obj_t *bar, const char *title)
{
    lv_obj_t * const words = (NULL != bar) ? lv_obj_find_by_name(bar, "bar_words") : NULL;
    lv_obj_t * const arrow = (NULL != bar) ? lv_obj_find_by_name(bar, "bar_arrow") : NULL;

    if ( (NULL != words) && (NULL != arrow) && (strcmp(lv_label_get_text(words), title) != 0) )
    {
        lv_point_t said;
        int32_t    x;

        lv_label_set_text(words, title);
        kgui_text_size(&said, title, &kui_font_name);
        x = ((int32_t)KBSP_LCD_H_RES - said.x) / 2;
        lv_obj_set_x(words, x);
        lv_obj_set_x(arrow, x - ARROW_GAP - ARROW_W);
    }
}
