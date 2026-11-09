/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_tp_target.c
 * @brief The touch test's target: a ring with a cross through it.
 */
#include "kgui_screens.h"

/** The target's 40 x 40 box, invisible. */
static lv_style_t s_target;
/** The ring. */
static lv_style_t s_ring;
/** The two bars of the cross. */
static lv_style_t s_bar;
/** Set once the three styles are initialised. */
static bool s_ready = false;

/** @brief Initialise the target's styles, once. */
static void target_styles_init(void)
{
    lv_style_init(&s_target);
    lv_style_init(&s_ring);
    lv_style_init(&s_bar);
    lv_style_set_width(&s_target, 40);
    lv_style_set_height(&s_target, 40);
    lv_style_set_bg_opa(&s_target, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_target, 0);
    lv_style_set_pad_all(&s_target, 0);
    lv_style_set_radius(&s_target, 0);
    lv_style_set_bg_opa(&s_ring, LV_OPA_TRANSP);
    lv_style_set_border_color(&s_ring, KUI_COLOR_TEXT_ON_BG);
    lv_style_set_border_opa(&s_ring, LV_OPA_COVER);
    lv_style_set_border_width(&s_ring, 2);
    lv_style_set_radius(&s_ring, KUI_RADIUS_GLASS);
    lv_style_set_bg_color(&s_bar, KUI_COLOR_TEXT_ON_BG);
    lv_style_set_bg_opa(&s_bar, LV_OPA_COVER);
}

/**
 * @brief One centred part of the target.
 * @param target The target.
 * @param name   The part's name.
 * @param w      Its width.
 * @param h      Its height.
 * @param style  Its style.
 */
static void part(lv_obj_t *target, const char *name, int32_t w, int32_t h, lv_style_t *style)
{
    lv_obj_t *p = kui_flex_create(target, LV_FLEX_FLOW_COLUMN, 0, 0U,
                                  LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_name(p, name);
    lv_obj_set_width(p, w);
    lv_obj_set_height(p, h);
    lv_obj_set_align(p, LV_ALIGN_CENTER);
    lv_obj_add_style(p, style, 0);
}

lv_obj_t *kgui_tp_target_create(lv_obj_t *parent)
{
    if ( !s_ready )
    {
        target_styles_init();
        s_ready = true;
    }
    lv_obj_t *target = lv_obj_create(parent);
    lv_obj_set_name_static(target, "tp_target_#");
    lv_obj_remove_style_all(target);
    lv_obj_add_style(target, &s_target, 0);
    part(target, "ring", 40, 40, &s_ring);
    part(target, "bar_h", 40, 2, &s_bar);
    part(target, "bar_v", 2, 40, &s_bar);
    return target;
}
