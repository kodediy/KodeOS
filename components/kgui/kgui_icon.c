/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_icon.c
 * @brief An app's icon as an object (kgui_icon.h).
 */
#include "kgui_icon.h"

#include <stddef.h>

#include "kui.h"
#include "kgui_list.h"

lv_obj_t *kgui_plain(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *const obj = lv_obj_create(parent);

    if ( NULL != obj )
    {
        lv_obj_remove_style_all(obj);
        lv_obj_set_pos(obj, x, y);
        lv_obj_set_size(obj, w, h);
        lv_obj_set_clickable(obj, false);
        lv_obj_set_scrollable(obj, false);
    }
    return obj;
}

bool kgui_icon_make(kgui_icon_t *ic, lv_obj_t *parent, int32_t size)
{
    ic->disc    = kgui_plain(parent, 0, 0, size, size);
    ic->img     = NULL;
    ic->mono    = NULL;
    ic->pic     = NULL;
    if ( NULL != ic->disc )
    {
        lv_obj_set_style_radius(ic->disc, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(ic->disc, true, 0);
        ic->img = lv_image_create(ic->disc);
        if ( NULL != ic->img )
        {
            lv_obj_set_align(ic->img, LV_ALIGN_CENTER);
            lv_obj_set_hidden(ic->img, true);
        }
        ic->mono = lv_label_create(ic->disc);
        if ( NULL != ic->mono )
        {
            lv_obj_set_style_text_font(ic->mono, &kui_font_name, 0);
            lv_obj_set_style_text_color(ic->mono, KUI_COLOR_ON_ACCENT, 0);
            lv_label_set_text_static(ic->mono, "");
            lv_obj_set_align(ic->mono, LV_ALIGN_CENTER);
            lv_obj_set_hidden(ic->mono, true);
        }
    }
    return (NULL != ic->disc) && (NULL != ic->img) && (NULL != ic->mono);
}

void kgui_icon_size(kgui_icon_t *ic, int32_t size)
{
    lv_obj_set_size(ic->disc, size, size);
    if ( NULL != ic->pic )
    {
        int32_t const scale = (size * (int32_t)LV_SCALE_NONE) / (int32_t)KGUI_ROW_ICON_SRC;

        /* A picture LVGL already has is not set again: setting it redraws it. */
        if ( (const void *)ic->pic != lv_image_get_src(ic->img) )
        {
            lv_image_set_src(ic->img, ic->pic);
        }
        lv_image_set_scale(ic->img, (uint32_t)scale);
    }
}

void kgui_icon_dress(kgui_icon_t *ic, const kgui_icon_spec_t *spec, int32_t size)
{
    ic->pic     = spec->image;
    if ( NULL != spec->image )
    {
        /* The picture rides inside the disc, on the ground its row wears
         * (kgui_list_disc_dress()), so the icon that flies is the one that left. */
        lv_obj_set_style_bg_color(ic->disc, spec->ground, 0);
        lv_obj_set_style_bg_opa(ic->disc, LV_OPA_COVER, 0);
        lv_obj_set_hidden(ic->img, false);
        lv_obj_set_hidden(ic->mono, true);
        /* The same descriptor may hold a picture that has changed since. */
        (void)lv_obj_invalidate(ic->img);
    }
    else
    {
        lv_obj_set_style_bg_color(ic->disc, spec->ground, 0);
        lv_obj_set_style_bg_opa(ic->disc, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(ic->mono, spec->ink, 0);
        lv_label_set_text(ic->mono, spec->mono);
        lv_obj_set_hidden(ic->mono, false);
        lv_obj_set_hidden(ic->img, true);
    }
    kgui_icon_size(ic, size);
}
