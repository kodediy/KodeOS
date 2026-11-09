/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_list.c
 * @brief The launcher's rail objects (kgui_list.h).
 */
#include "kgui_list.h"

#include <string.h>

#include "kbsp_board.h"
#include "kgui_icon.h"
#include "kgui_glyphs.h"
#include "kui_glyphs.h"

#define LIST_RING_Y        ((KGUI_ROW_H - KGUI_RAIL_ICON) / 2) /**< The icon disc's top from the row's, in pixels. */
#define LIST_GLYPH_PX      40    /**< The glyph in a disc, in pixels. */
#define LIST_GLYPH_SRC_APPS 64   /**< The apps glyph's own size, in pixels. */
#define LIST_MONO_PAIR_SPACE (-1) /**< Two letters of a folder stand this much closer, in pixels. */

/** @brief A group's glyph by the name kgui_group_glyph() gives it. */
typedef struct {
    const char           *name;   /**< The glyph's name. */
    const lv_image_dsc_t *glyph;  /**< Its picture, 40 x 40. */
} list_glyph_t;

/**
 * @brief Whether a string holds more than one UTF-8 character.
 *
 * @param s The string.
 * @return true from the second character's lead byte on.
 */
static bool list_two_chars(const char *s)
{
    uint8_t leads = 0U;

    for ( size_t i = 0U; ('\0' != s[i]) && (2U > leads); ++i )
    {
        if ( (((unsigned char)s[i]) & 0xC0U) != 0x80U )
        {
            ++leads;
        }
    }
    return 1U < leads;
}

/**
 * @brief The scale that draws a glyph of a size at LIST_GLYPH_PX.
 *
 * @param src The glyph's own side, in pixels.
 * @return The LVGL scale, 256 being its own size.
 */
static uint32_t list_scale_of(int32_t src)
{
    int32_t const scale = (LIST_GLYPH_PX * (int32_t)LV_SCALE_NONE) / src;

    return (uint32_t)scale;
}

bool kgui_list_build(kgui_list_t *list, lv_obj_t *viewport)
{
    bool ok = false;

    if ( (NULL != list) && (NULL != viewport) )
    {
        list->ring = NULL;
        list->box  = lv_obj_create(viewport);
        if ( NULL != list->box )
        {
            lv_obj_remove_style_all(list->box);
            lv_obj_set_pos(list->box, 0, 0);
            lv_obj_set_size(list->box, (int32_t)KBSP_LCD_H_RES, 0);
            lv_obj_set_scrollable(list->box, false);
            lv_obj_set_clickable(list->box, false);
            /* The ring is the box's first child, so the rows are drawn over it. */
            list->ring = lv_obj_create(list->box);
            if ( NULL != list->ring )
            {
                lv_obj_remove_style_all(list->ring);
                lv_obj_add_style(list->ring, &kui_style_ring, 0);
                lv_obj_set_style_radius(list->ring, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_size(list->ring, KGUI_RAIL_ICON, KGUI_RAIL_ICON);
                lv_obj_set_pos(list->ring, KGUI_RAIL_ICON_X, LIST_RING_Y);
                lv_obj_set_scrollable(list->ring, false);
                lv_obj_set_clickable(list->ring, false);
                lv_obj_set_hidden(list->ring, true);
                ok = true;
            }
        }
    }
    return ok;
}

void kgui_list_fit(kgui_list_t const *list, int32_t content_h)
{
    if ( (NULL != list) && (NULL != list->box) )
    {
        lv_obj_set_height(list->box, content_h);
    }
}

bool kgui_list_ring_place(kgui_list_t const *list, int32_t y, bool shown)
{
    bool moved = false;

    if ( (NULL != list) && (NULL != list->ring) )
    {
        int32_t const top = y + LIST_RING_Y;

        if ( lv_obj_is_hidden(list->ring) == shown )
        {
            lv_obj_set_hidden(list->ring, !shown);
        }
        if ( lv_obj_get_style_y(list->ring, LV_PART_MAIN) != top )
        {
            lv_obj_set_y(list->ring, top);
            moved = true;
        }
    }
    return moved;
}

lv_obj_t *kgui_list_disc_make(lv_obj_t *parent, int32_t x, int32_t y, int32_t side)
{
    lv_obj_t * const slot = kgui_plain(parent, x, y, side, side);

    if ( NULL != slot )
    {
        lv_obj_t * const img   = lv_image_create(slot);
        lv_obj_t * const glyph = lv_image_create(slot);
        lv_obj_t * const mono  = lv_label_create(slot);

        lv_obj_set_style_radius(slot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(slot, true, 0);
        lv_obj_set_style_bg_opa(slot, LV_OPA_COVER, 0);
        if ( NULL != img )
        {
            lv_obj_set_name(img, "slot_img");
            lv_obj_set_align(img, LV_ALIGN_CENTER);
            lv_obj_set_clickable(img, false);
            lv_obj_set_hidden(img, true);
        }
        if ( NULL != glyph )
        {
            lv_obj_set_name(glyph, "slot_glyph");
            lv_obj_set_align(glyph, LV_ALIGN_CENTER);
            lv_obj_set_clickable(glyph, false);
            lv_obj_set_style_image_recolor_opa(glyph, LV_OPA_COVER, 0);
            lv_obj_set_hidden(glyph, true);
        }
        if ( NULL != mono )
        {
            /* The launch's flying icon says the same letters in the same face
             * (kgui_icon_make()), so the disc that leaves is the one that flies. */
            lv_obj_set_name(mono, "slot_mono");
            lv_obj_set_style_text_font(mono, &kui_font_name, 0);
            lv_label_set_text_static(mono, "");
            lv_obj_set_align(mono, LV_ALIGN_CENTER);
            lv_obj_set_clickable(mono, false);
            lv_obj_set_hidden(mono, true);
        }
    }
    return slot;
}

void kgui_list_disc_dress(lv_obj_t *disc, int32_t side, lv_color_t ground,
                          const lv_image_dsc_t *pic, const char *mono,
                          const lv_font_t *mono_font,
                          const lv_image_dsc_t *glyph, uint32_t scale, lv_color_t ink)
{
    lv_obj_t * img     = NULL;
    lv_obj_t * mark    = NULL;
    lv_obj_t * letters = NULL;

    if ( NULL != disc )
    {
        img     = lv_obj_find_by_name(disc, "slot_img");
        mark    = lv_obj_find_by_name(disc, "slot_glyph");
        letters = lv_obj_find_by_name(disc, "slot_mono");
    }
    if ( (NULL == img) || (NULL == mark) || (NULL == letters) )
    {
        return;
    }
    lv_obj_set_hidden(letters, true);
    if ( NULL != pic )
    {
        /* The picture rides INSIDE the disc, as a glyph does: the disc keeps its
         * ground under it, so an icon with a clear margin sits in the same round
         * disc as an app with none, and the disc's corner clip cuts the picture
         * round (Luismi, 2026-11-03: two treatments in one list). */
        lv_obj_set_style_bg_color(disc, ground, 0);
        lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
        if ( (const void *)pic != lv_image_get_src(img) )
        {
            lv_image_set_src(img, pic);
        }
        int32_t const fit = (side * (int32_t)LV_SCALE_NONE) / (int32_t)KGUI_ROW_ICON_SRC;

        lv_image_set_scale(img, (uint32_t)fit);
        /* The same descriptor may hold a picture that has changed since. */
        (void)lv_obj_invalidate(img);
        lv_obj_set_hidden(img, false);
        lv_obj_set_hidden(mark, true);
    }
    else
    {
        lv_obj_set_style_bg_color(disc, ground, 0);
        lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
        lv_obj_set_hidden(img, true);
        if ( NULL != mono )
        {
            /* An app's two capitals in Outfit Bold 28; a folder's letters in
             * the face its caller gives, two of them a pixel closer. A row is
             * dressed again in place, so the face and the spacing are written
             * every time. */
            const lv_font_t *font  = &kui_font_name;
            int32_t          space = 0;

            if ( NULL != mono_font )
            {
                font = mono_font;
                if ( list_two_chars(mono) )
                {
                    space = LIST_MONO_PAIR_SPACE;
                }
            }
            lv_obj_set_style_text_font(letters, font, 0);
            lv_obj_set_style_text_letter_space(letters, space, 0);
            lv_label_set_text(letters, mono);
            lv_obj_set_style_text_color(letters, ink, 0);
            lv_obj_set_hidden(letters, false);
            lv_obj_set_hidden(mark, true);
        }
        else if ( NULL != glyph )
        {
            lv_image_set_src(mark, glyph);
            lv_image_set_scale(mark, scale);
            lv_obj_set_style_image_recolor(mark, ink, 0);
            /* A folder's art is drawn in its own colours (kgui_list_row_disc_art());
             * the row may wear a glyph next. */
            lv_obj_set_style_image_recolor_opa(mark, LV_OPA_COVER, 0);
            lv_obj_set_hidden(mark, false);
        }
        else
        {
            lv_obj_set_hidden(mark, true);
        }
    }
}

lv_obj_t *kgui_list_row_make(kgui_list_t const *list)
{
    lv_obj_t *row = NULL;

    if ( (NULL != list) && (NULL != list->box) )
    {
        row = kgui_plain(list->box, 0, 0, (int32_t)KBSP_LCD_H_RES, KGUI_ROW_H);
    }
    if ( NULL != row )
    {
        lv_obj_t * const slot = kgui_list_disc_make(row, KGUI_RAIL_ICON_X, LIST_RING_Y,
                                                    KGUI_RAIL_ICON);
        lv_obj_t * const name = lv_label_create(row);

        lv_obj_set_clickable(row, true);
        if ( NULL != slot )
        {
            lv_obj_set_name(slot, "slot");
        }
        if ( NULL != name )
        {
            lv_obj_set_name(name, "name");
            lv_obj_set_clickable(name, false);
            lv_obj_set_pos(name, KGUI_RAIL_NAME_X, 0);
            lv_obj_set_style_text_font(name, &kui_font_title, 0);
            lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        }
    }
    return row;
}

void kgui_list_row_disc(lv_obj_t const *row, lv_color_t ground, const lv_image_dsc_t *pic,
                        const char *mono, const lv_font_t *mono_font,
                        const lv_image_dsc_t *glyph, uint32_t scale, lv_color_t ink)
{
    lv_obj_t * slot = NULL;

    if ( NULL != row )
    {
        slot = lv_obj_find_by_name(row, "slot");
    }
    kgui_list_disc_dress(slot, KGUI_RAIL_ICON, ground, pic, mono, mono_font, glyph, scale, ink);
}

void kgui_list_row_disc_art(lv_obj_t const *row, lv_color_t ground, const lv_image_dsc_t *art)
{
    kgui_list_row_disc(row, ground, NULL, NULL, NULL, art, LV_SCALE_NONE, ground);
    if ( NULL != row )
    {
        const lv_obj_t * const slot = lv_obj_find_by_name(row, "slot");
        lv_obj_t * const mark = (NULL != slot) ? lv_obj_find_by_name(slot, "slot_glyph") : NULL;

        if ( NULL != mark )
        {
            lv_obj_set_style_image_recolor_opa(mark, LV_OPA_TRANSP, 0);
        }
    }
}

void kgui_list_row_name(lv_obj_t const *row, const char *text, bool focused, int32_t narrow)
{
    lv_obj_t * name = NULL;

    if ( NULL != row )
    {
        name = lv_obj_find_by_name(row, "name");
    }
    if ( NULL != name )
    {
        /* Pushed in by `narrow` at its left, the row's name is cut by as much
         * at its right too: the glass's corner is the same on both sides, and
         * a name as wide as the row is pushed would run out of the glass on
         * the right (the launcher design of 5-nov, «Fuera del encargo»). */
        int32_t const width = KGUI_RAIL_NAME_W - (2 * narrow);
        int32_t const line  = (int32_t)lv_font_get_line_height(&kui_font_title);

        if ( strcmp(lv_label_get_text(name), text) != 0 )
        {
            lv_label_set_text(name, text);
        }
        if ( focused )
        {
            lv_obj_set_style_text_color(name, KUI_COLOR_TEXT_ON_BG, 0);
        }
        else
        {
            lv_obj_set_style_text_color(name, KUI_COLOR_TEXT_MUTED, 0);
        }
        /* One line for the focused name too: the box is one line tall, which
         * is what makes the dotted mode cut the word with dots where a taller
         * box let "Communications" break at its last letter. */
        lv_obj_set_size(name, width, line);
        lv_obj_set_y(name, (KGUI_ROW_H - line) / 2);
    }
}

void kgui_list_row_edge(lv_obj_t *row, kgui_rail_t const *edge)
{
    if ( NULL == row )
    {
        return;
    }
    if ( lv_obj_is_hidden(row) != edge->gone )
    {
        lv_obj_set_hidden(row, edge->gone);
    }
    if ( lv_obj_get_style_x(row, LV_PART_MAIN) != edge->dx )
    {
        lv_obj_t * const name = lv_obj_find_by_name(row, "name");

        lv_obj_set_x(row, edge->dx);
        /* The name stays inside the glass on both sides: the push moves its
         * left in, and its right is cut by as much again, for the corner on
         * that side (kgui_list_row_name()). */
        if ( NULL != name )
        {
            lv_obj_set_width(name, KGUI_RAIL_NAME_W - (2 * edge->dx));
        }
    }
    /* A value LVGL already has is not written again: a write redraws the row. */
    if ( lv_obj_get_style_opa(row, LV_PART_MAIN) != (lv_opa_t)edge->opa )
    {
        lv_obj_set_style_opa(row, (lv_opa_t)edge->opa, LV_PART_MAIN);
    }
}

const lv_image_dsc_t *kgui_list_group_glyph(const char *name, uint32_t *scale)
{
    /* The folders' glyphs: the kit's seven, then kgui's own (kgui_glyphs.h). */
    static const list_glyph_t s_glyphs[] = {
        { "games",   &kui_glyph_games_40 },
        { "tool",    &kui_glyph_tool_40 },
        { "talk",    &kui_glyph_talk_40 },
        { "wifi",    &kui_glyph_wifi_40 },
        { "brush",   &kui_glyph_brush_40 },
        { "maker",   &kui_glyph_maker_40 },
        { "spark",   &kui_glyph_spark_40 },
        { "gauge",   &kgui_glyph_gauge_40 },
        { "antenna", &kgui_glyph_antenna_40 },
        { "shield",  &kgui_glyph_shield_40 },
        { "note",    &kgui_glyph_note_40 },
        { "camera",  &kgui_glyph_camera_40 },
        { "robot",   &kgui_glyph_robot_40 },
        { "home",    &kgui_glyph_home_40 },
        { "heart",   &kgui_glyph_heart_40 },
        { "pin",     &kgui_glyph_pin_40 },
        { "clock",   &kgui_glyph_clock_40 },
        { "list",    &kgui_glyph_list_40 },
        { "book",    &kgui_glyph_book_40 },
        { "code",    &kgui_glyph_code_40 },
    };

    /* The table's glyphs are drawn at their own 40; any other name, NULL
     * included, wears the apps glyph, scaled from its 64. */
    const lv_image_dsc_t *glyph = &kui_glyph_apps_64;

    *scale = list_scale_of(LIST_GLYPH_SRC_APPS);
    if ( NULL != name )
    {
        for ( size_t i = 0U; i < (sizeof(s_glyphs) / sizeof(s_glyphs[0])); ++i )
        {
            if ( 0 == strcmp(name, s_glyphs[i].name) )
            {
                glyph  = s_glyphs[i].glyph;
                *scale = LV_SCALE_NONE;
            }
        }
    }
    return glyph;
}
