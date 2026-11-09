/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_screen_touch_test.c
 * @brief The guided touch test: a target and one line under it.
 */
#include "kgui_screens.h"

lv_obj_t *kgui_screen_touch_test = NULL;

void kgui_screen_touch_test_build(void)
{
    kgui_screen_touch_test = lv_obj_create(NULL);
    lv_obj_t *scr = kgui_screen_touch_test;
    lv_obj_set_name_static(scr, "screen_touch_test_#");
    lv_obj_add_style(scr, &kui_style_screen, 0);

    lv_obj_t *target = kgui_tp_target_create(scr);
    lv_obj_set_name(target, "target");
    lv_obj_set_x(target, 185);
    lv_obj_set_y(target, 231);

    lv_obj_t *label = lv_label_create(scr);
    lv_obj_set_name(label, "tp_label");
    (void)lv_label_bind_text(label, kgui_subject_tp_text, NULL);
    lv_obj_set_align(label, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(label, -150);
    lv_obj_set_style_text_font(label, &kui_font_caption, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}
