/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_sysscreen.c
 * @brief The system screen's frame (kgui_sysscreen.h); adapts kodediy/kode-dot-os, kshell_sysui.c.
 */
#include "kgui_sysscreen.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "kbsp_board.h"
#include "kgui_lvgl.h"
#include "kui.h"
#include "kui_glyphs.h"

LV_FONT_DECLARE(kgui_font_outfit_xb_48);

#define SYS_LINE_COUNT       3U    /**< Lines of detail a card has room for. */
#define SYS_PILL_X           (((int32_t)KBSP_LCD_H_RES - KGUI_SYS_PILL_ROW_W) / 2) /**< Where the pill's row stands across. */
#define SYS_GLYPH_FULL       256U  /**< LVGL's scale of an image as it is: 1/256ths. */
#define SYS_FIGURE_DIGITS    4U    /**< Room for the figure's digits and its terminator. */
#define SYS_NAME_DOTS_MAX    1U    /**< A name takes one line: the rest is dotted. */

/**
 * @brief A label of the frame: centred in the text column, hidden and empty.
 *
 * @param parent Where it stands.
 * @param font   Its face.
 * @param ink    Its colour.
 * @param y      Its top.
 * @return The label; NULL when LVGL had no room.
 */
static lv_obj_t *sys_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t ink, int32_t y)
{
    lv_obj_t * const l = lv_label_create(parent);

    if ( NULL != l )
    {
        lv_obj_set_pos(l, KGUI_SYS_TEXT_X, y);
        lv_obj_set_width(l, KGUI_SYS_TEXT_W);
        lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
        lv_obj_set_style_text_color(l, ink, LV_PART_MAIN);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_clickable(l, false);
        lv_label_set_text_static(l, "");
        lv_obj_set_hidden(l, true);
    }
    return l;
}

/**
 * @brief Write a label, or take it off the glass.
 *
 * @param l    The label.
 * @param text What it says; NULL or "" hides it.
 */
static void sys_say(lv_obj_t *l, const char *text)
{
    if ( NULL != l )
    {
        if ( (NULL == text) || ('\0' == text[0]) )
        {
            lv_label_set_text_static(l, "");
            lv_obj_set_hidden(l, true);
        }
        else
        {
            if ( 0 != strcmp(lv_label_get_text(l), text) )
            {
                lv_label_set_text(l, text);
            }
            lv_obj_set_hidden(l, false);
        }
    }
}

/**
 * @brief Take the protagonist off the glass.
 *
 * @param s The frame.
 */
static void sys_prot_off(kgui_sys_t *s)
{
    lv_obj_set_hidden(s->icon.disc, true);
    lv_obj_set_hidden(s->blank, true);
    lv_obj_set_hidden(s->glyph, true);
    lv_obj_set_hidden(s->word, true);
    lv_obj_set_hidden(s->pile, true);
    lv_obj_set_hidden(s->code, true);
}

/**
 * @brief Make the red battery: a body with a terminal and the sliver left.
 *
 * @param card Where it stands.
 * @return The body; NULL when LVGL had no room.
 */
static lv_obj_t *sys_pile_make(lv_obj_t *card)
{
    lv_obj_t * const pile = kgui_plain(card, KGUI_SYS_CENTRE_X - (KGUI_SYS_PILE_W / 2),
                                       KGUI_SYS_PROT_Y - (KGUI_SYS_PILE_H / 2),
                                       KGUI_SYS_PILE_W, KGUI_SYS_PILE_H);

    if ( NULL != pile )
    {
        lv_obj_t * const nub   = kgui_plain(pile, KGUI_SYS_PILE_W - KGUI_SYS_PILE_BORDER - KGUI_SYS_PILE_NUB_W
                                            - (KGUI_SYS_PILE_BORDER / 2),
                                            KGUI_SYS_PILE_NUB_Y, KGUI_SYS_PILE_NUB_W, KGUI_SYS_PILE_NUB_H);
        lv_obj_t * const level = kgui_plain(pile, KGUI_SYS_PILE_LEVEL_AT, KGUI_SYS_PILE_LEVEL_AT,
                                            KGUI_SYS_PILE_LEVEL_W, KGUI_SYS_PILE_LEVEL_H);

        lv_obj_set_style_radius(pile, KGUI_SYS_PILE_R, LV_PART_MAIN);
        lv_obj_set_style_border_width(pile, KGUI_SYS_PILE_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_color(pile, KUI_COLOR_DANGER, LV_PART_MAIN);
        lv_obj_set_style_border_opa(pile, LV_OPA_COVER, LV_PART_MAIN);
        if ( NULL != nub )
        {
            lv_obj_set_style_bg_color(nub, KUI_COLOR_DANGER, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, LV_PART_MAIN);
        }
        if ( NULL != level )
        {
            lv_obj_set_style_radius(level, KGUI_SYS_PILE_LEVEL_R, LV_PART_MAIN);
            lv_obj_set_style_bg_color(level, KUI_COLOR_DANGER, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(level, LV_OPA_COVER, LV_PART_MAIN);
        }
        lv_obj_set_hidden(pile, true);
    }
    return pile;
}

bool kgui_sys_create(kgui_sys_t *s, lv_obj_t *card)
{
    s->made  = false;
    s->shown = (uint8_t)KGUI_SYS_FIGURE_NONE;
    if ( (NULL != card) && kgui_icon_make(&s->icon, card, KGUI_SYS_DISC) )
    {
        lv_obj_set_pos(s->icon.disc, KGUI_SYS_CENTRE_X - (KGUI_SYS_DISC / 2),
                       KGUI_SYS_PROT_Y - (KGUI_SYS_DISC / 2));
        lv_obj_set_hidden(s->icon.disc, true);
        s->blank = kgui_plain(card, KGUI_SYS_CENTRE_X - (KGUI_SYS_DISC / 2),
                              KGUI_SYS_PROT_Y - (KGUI_SYS_DISC / 2), KGUI_SYS_DISC, KGUI_SYS_DISC);
        s->glyph = lv_image_create(card);
        s->pile  = sys_pile_make(card);
        s->word  = sys_label(card, &kui_font_display_40, KUI_COLOR_TEXT_ON_BG,
                             KGUI_SYS_PROT_Y - ((int32_t)lv_font_get_line_height(&kui_font_display_40) / 2));
        s->code   = sys_label(card, &kui_font_code_80, KUI_COLOR_TEXT_ON_BG, KGUI_SYS_CODE_Y);
        s->figure = sys_label(card, &kgui_font_figure_128, KUI_COLOR_TEXT_ON_BG, KGUI_SYS_FIGURE_Y);
        s->unit   = sys_label(card, &kui_font_display_40, KUI_COLOR_TEXT_MUTED, 0);
        s->name   = sys_label(card, &kui_font_title, KUI_COLOR_TEXT_ON_BG, KGUI_SYS_NAME_Y);
        s->second = sys_label(card, &kui_font_body, KUI_COLOR_TEXT_MUTED, KGUI_SYS_SECOND_Y);
        s->detail = sys_label(card, &kui_font_body, KUI_COLOR_TEXT_MUTED, KGUI_SYS_TEXT_Y);
        s->note   = sys_label(card, &kui_font_body, KUI_COLOR_TEXT_MUTED, KGUI_SYS_NOTE_Y);
        s->message = sys_label(card, &kgui_font_outfit_xb_48, KUI_COLOR_TEXT_ON_BG, 0);
        if ( NULL != s->message )
        {
            lv_label_set_long_mode(s->message, LV_LABEL_LONG_MODE_WRAP);
        }
        if ( (NULL != s->blank) && (NULL != s->glyph) && (NULL != s->pile) && (NULL != s->word)
             && (NULL != s->code) && (NULL != s->figure) && (NULL != s->unit) && (NULL != s->name)
             && (NULL != s->second) && (NULL != s->detail) && (NULL != s->note) )
        {
            s->blank_img = lv_image_create(s->blank);
            if ( NULL != s->blank_img )
            {
                lv_obj_set_style_radius(s->blank, LV_RADIUS_CIRCLE, LV_PART_MAIN);
                lv_obj_set_style_bg_color(s->blank, KUI_COLOR_TEXT_MUTED, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(s->blank, LV_OPA_COVER, LV_PART_MAIN);
                lv_image_set_src(s->blank_img, &kui_glyph_apps_64);
                lv_image_set_scale(s->blank_img, KGUI_SYS_BLANK_SCALE);
                lv_obj_set_style_image_recolor(s->blank_img, KUI_COLOR_TEXT, LV_PART_MAIN);
                lv_obj_set_style_image_recolor_opa(s->blank_img, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_align(s->blank_img, LV_ALIGN_CENTER);
                lv_label_set_text_static(s->unit, "%");
                lv_obj_set_width(s->unit, kgui_size_content());
                lv_obj_set_y(s->figure, KGUI_SYS_FIGURE_BASE
                             - ((int32_t)lv_font_get_line_height(&kgui_font_figure_128) - kgui_font_figure_128.base_line));
                lv_obj_set_y(s->unit, KGUI_SYS_FIGURE_BASE
                             - ((int32_t)lv_font_get_line_height(&kui_font_display_40) - kui_font_display_40.base_line));
                lv_label_set_long_mode(s->name, LV_LABEL_LONG_MODE_DOTS);
                lv_obj_set_height(s->name, (int32_t)SYS_NAME_DOTS_MAX * (int32_t)lv_font_get_line_height(&kui_font_title));
                lv_label_set_long_mode(s->second, LV_LABEL_LONG_MODE_DOTS);
                lv_obj_set_height(s->second, (int32_t)lv_font_get_line_height(&kui_font_body));
                lv_label_set_long_mode(s->detail, LV_LABEL_LONG_MODE_DOTS);
                lv_obj_set_height(s->detail, (int32_t)SYS_LINE_COUNT * (int32_t)lv_font_get_line_height(&kui_font_body));
                lv_obj_set_hidden(s->blank, true);
                lv_obj_set_hidden(s->glyph, true);
                s->made = true;
            }
        }
    }
    return s->made;
}

void kgui_sys_clear(kgui_sys_t *s)
{
    if ( s->made )
    {
        sys_prot_off(s);
        sys_say(s->figure, NULL);
        sys_say(s->name, NULL);
        sys_say(s->second, NULL);
        sys_say(s->detail, NULL);
        sys_say(s->note, NULL);
        sys_say(s->code, NULL);
        sys_say(s->message, NULL);
        lv_obj_set_hidden(s->unit, true);
        s->shown = (uint8_t)KGUI_SYS_FIGURE_NONE;
        kgui_sys_lift(s, 0);
    }
}

void kgui_sys_app(kgui_sys_t *s, const kgui_icon_spec_t *look)
{
    if ( s->made )
    {
        sys_prot_off(s);
        if ( NULL != look )
        {
            kgui_icon_dress(&s->icon, look, KGUI_SYS_DISC);
            lv_obj_set_hidden(s->icon.disc, false);
        }
        else
        {
            lv_obj_set_hidden(s->blank, false);
        }
    }
}

void kgui_sys_glyph(kgui_sys_t *s, const lv_image_dsc_t *img, lv_color_t ink)
{
    if ( s->made && (NULL != img) )
    {
        uint32_t w = img->header.w;

        if ( 0U == w )
        {
            w = (uint32_t)KGUI_SYS_GLYPH;
        }
        sys_prot_off(s);
        lv_image_set_src(s->glyph, img);
        lv_image_set_scale(s->glyph, (uint32_t)(((uint32_t)KGUI_SYS_GLYPH * SYS_GLYPH_FULL) / w));
        lv_obj_set_style_image_recolor(s->glyph, ink, LV_PART_MAIN);
        lv_obj_set_style_image_recolor_opa(s->glyph, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_pos(s->glyph, KGUI_SYS_CENTRE_X - ((int32_t)w / 2),
                       KGUI_SYS_PROT_Y - ((int32_t)img->header.h / 2));
        lv_obj_set_hidden(s->glyph, false);
    }
}

void kgui_sys_glyph_mid(kgui_sys_t *s, const lv_image_dsc_t *img, lv_color_t ink)
{
    if ( s->made && (NULL != img) )
    {
        sys_prot_off(s);
        lv_image_set_src(s->glyph, img);
        lv_image_set_scale(s->glyph, SYS_GLYPH_FULL);
        lv_obj_set_style_image_recolor(s->glyph, ink, LV_PART_MAIN);
        lv_obj_set_style_image_recolor_opa(s->glyph, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_pos(s->glyph, KGUI_SYS_CENTRE_X - ((int32_t)img->header.w / 2),
                       ((KGUI_SYS_GLASS_H - (int32_t)img->header.h) / 2) - KGUI_SYS_GLYPH_MID_RISE);
        lv_obj_set_hidden(s->glyph, false);
    }
}

void kgui_sys_lift(kgui_sys_t *s, int32_t dy)
{
    if ( s->made )
    {
        lv_obj_t * const parts[] = { s->icon.disc, s->blank, s->glyph, s->word, s->figure, s->unit,
                                   s->name, s->second };

        for ( size_t i = 0U; i < (sizeof(parts) / sizeof(parts[0])); ++i )
        {
            lv_obj_set_style_translate_y(parts[i], dy, LV_PART_MAIN);
        }
    }
}

void kgui_sys_message(kgui_sys_t *s, const char *text)
{
    if ( s->made && (NULL != s->message) )
    {
        sys_say(s->message, text);
        if ( (NULL != text) && ('\0' != text[0]) )
        {
            int32_t h;

            sys_prot_off(s);
            lv_obj_update_layout(s->message);
            h = lv_obj_get_height(s->message);
            /* In the room over the pill, well clear of it (Luismi, 5-nov). */
            lv_obj_set_y(s->message, (KGUI_SYS_PILL_Y - h) / 2);
        }
    }
}

void kgui_sys_word(kgui_sys_t *s, const char *text)
{
    if ( s->made )
    {
        sys_prot_off(s);
        sys_say(s->word, text);
    }
}

void kgui_sys_pile(kgui_sys_t *s)
{
    if ( s->made )
    {
        sys_prot_off(s);
        lv_obj_set_hidden(s->pile, false);
    }
}

void kgui_sys_code(kgui_sys_t *s, const char *text)
{
    if ( s->made )
    {
        sys_say(s->code, text);
    }
}

void kgui_sys_figure(kgui_sys_t *s, uint8_t pct)
{
    if ( s->made && (pct != s->shown) )
    {
        char digits[SYS_FIGURE_DIGITS];

        (void)snprintf(digits, sizeof(digits), "%u", (unsigned)pct);
        sys_say(s->figure, digits);
        /* The figure alone is centred, and the "%" stands beside it: with it
         * in the centring the number would sit off the middle (Luismi, 5-nov). */
        {
            lv_point_t said;

            kgui_text_size(&said, digits, &kgui_font_figure_128);
            lv_obj_set_x(s->unit, KGUI_SYS_CENTRE_X + (said.x / 2) + KGUI_SYS_UNIT_GAP);
        }
        lv_obj_set_hidden(s->unit, false);
        s->shown = pct;
    }
}

void kgui_sys_note(kgui_sys_t *s, const char *text)
{
    if ( s->made )
    {
        sys_say(s->note, text);
    }
}

void kgui_sys_hero(kgui_sys_t *s, const char *name, const char *second)
{
    if ( s->made )
    {
        lv_obj_set_y(s->name, KGUI_SYS_NAME_Y);
        /* Under a write's figure the name is larger; any other card's is put back
         * by kgui_sys_text(). */
        lv_obj_set_style_text_font(s->name, &kui_font_display_40, LV_PART_MAIN);
        lv_obj_set_height(s->name, (int32_t)lv_font_get_line_height(&kui_font_display_40));
        sys_say(s->name, name);
        lv_obj_set_style_text_color(s->second, KUI_COLOR_TEXT_MUTED, LV_PART_MAIN);
        if ( (NULL != name) && ('\0' != name[0]) )
        {
            lv_obj_set_y(s->second, KGUI_SYS_SECOND_Y);
        }
        else
        {
            lv_obj_set_y(s->second, KGUI_SYS_NAME_Y);
        }
        sys_say(s->second, second);
    }
}

/**
 * @brief How tall the card's words are, from the top of the first to the
 *        bottom of the last: each visible line's step to the next, and the
 *        last one's own height.
 *
 * @param name   The name, or NULL.
 * @param second The second line, or NULL.
 * @param detail The detail lines, or NULL.
 * @return The height in pixels; 0 when there is nothing to say.
 */
static int32_t sys_words_h(const char *name, const char *second, const char *detail)
{
    bool const has_name   = (NULL != name) && ('\0' != name[0]);
    bool const has_second = (NULL != second) && ('\0' != second[0]);
    bool const has_detail = (NULL != detail) && ('\0' != detail[0]);
    int32_t    h          = 0;

    if ( has_name )
    {
        h += KGUI_SYS_TEXT_STEP_NAME;
    }
    if ( has_second )
    {
        h += KGUI_SYS_TEXT_STEP_SECOND;
    }
    if ( has_detail )
    {
        lv_point_t said;
        int32_t const line = (int32_t)lv_font_get_line_height(&kui_font_body);

        lv_text_get_size(&said, detail, &kui_font_body, 0, 0, KGUI_SYS_TEXT_W, LV_TEXT_FLAG_NONE);
        if ( ((int32_t)SYS_LINE_COUNT * line) < said.y )
        {
            said.y = (int32_t)SYS_LINE_COUNT * line;
        }
        h += said.y;
    }
    else if ( has_second )
    {
        h += (int32_t)lv_font_get_line_height(&kui_font_body) - KGUI_SYS_TEXT_STEP_SECOND;
    }
    else if ( has_name )
    {
        h += (int32_t)lv_font_get_line_height(&kui_font_title) - KGUI_SYS_TEXT_STEP_NAME;
    }
    else
    {
        /* Nothing to say, nothing to measure. */
    }
    return h;
}

void kgui_sys_text(kgui_sys_t *s, int32_t top, int32_t bottom, const char *name,
                   const char *second, const char *detail)
{
    if ( s->made && (NULL != s->name) )
    {
        lv_obj_set_style_text_font(s->name, &kui_font_title, LV_PART_MAIN);
        lv_obj_set_height(s->name, (int32_t)SYS_NAME_DOTS_MAX * (int32_t)lv_font_get_line_height(&kui_font_title));
    }
    if ( s->made )
    {
        int32_t at = top;

        if ( bottom > top )
        {
            /* Centred in the room: the same air under the protagonist as
             * above the answer, whatever the card says. A block taller than
             * the room starts at the top and runs on. */
            int32_t const room = bottom - top;
            int32_t const h    = sys_words_h(name, second, detail);

            if ( h < room )
            {
                at = top + ((room - h) / 2);
            }
        }
        lv_obj_set_y(s->name, at);
        sys_say(s->name, name);
        if ( (NULL != name) && ('\0' != name[0]) )
        {
            at += KGUI_SYS_TEXT_STEP_NAME;
        }
        lv_obj_set_y(s->second, at);
        if ( (NULL != name) && ('\0' != name[0]) )
        {
            lv_obj_set_style_text_color(s->second, KUI_COLOR_TEXT_MUTED, LV_PART_MAIN);
        }
        else
        {
            lv_obj_set_style_text_color(s->second, KUI_COLOR_TEXT_ON_BG, LV_PART_MAIN);
        }
        sys_say(s->second, second);
        if ( (NULL != second) && ('\0' != second[0]) )
        {
            at += KGUI_SYS_TEXT_STEP_SECOND;
        }
        lv_obj_set_y(s->detail, at);
        sys_say(s->detail, detail);
    }
}

void kgui_sys_origin(int32_t *cx, int32_t *cy, int32_t *size)
{
    *cx   = KGUI_SYS_CENTRE_X;
    *cy   = KGUI_SYS_PROT_Y;
    *size = KGUI_SYS_DISC;
}

void kgui_sys_pill(const lv_obj_t *card, bool framed, const char *word)
{
    lv_obj_t * const row  = lv_obj_find_by_name(card, "actions");
    lv_obj_t * const btn  = lv_obj_find_by_name(card, "btn");
    lv_obj_t * const text = lv_obj_find_by_name(card, "btn_text");

    if ( (NULL != row) && (NULL != btn) )
    {
        if ( framed )
        {
            int32_t w = KGUI_SYS_PILL_W_MIN;

            if ( (NULL != text) && (NULL != word) )
            {
                lv_obj_set_style_text_font(text, &kui_font_title, LV_PART_MAIN);
                lv_label_set_text(text, word);
                lv_obj_update_layout(text);
                if ( (lv_obj_get_width(text) + KGUI_SYS_PILL_PAD) > w )
                {
                    w = lv_obj_get_width(text) + KGUI_SYS_PILL_PAD;
                }
            }
            /* The row is the pill's room, with its focus ring: the ring stands
             * outside the pill, and the row clips what stands outside it. */
            lv_obj_set_pos(row, SYS_PILL_X, KGUI_SYS_PILL_Y - KGUI_SYS_PILL_RING);
            lv_obj_set_size(row, KGUI_SYS_PILL_ROW_W, KGUI_SYS_PILL_H + (2 * KGUI_SYS_PILL_RING));
            lv_obj_set_style_pad_top(row, KGUI_SYS_PILL_RING, LV_PART_MAIN);
            lv_obj_set_scrollable(row, false);
            lv_obj_set_size(btn, w, KGUI_SYS_PILL_H);
        }
        else
        {
            lv_obj_set_pos(row, KUI_SCREEN_SIDE, KGUI_SYS_KIT_ROW_Y);
            lv_obj_set_style_pad_top(row, 0, LV_PART_MAIN);
            lv_obj_set_size(row, KUI_PILL_W, KUI_ACTION_H);
            lv_obj_set_size(btn, KUI_ACTION_W, KUI_ACTION_H);
            if ( NULL != text )
            {
                lv_obj_set_style_text_font(text, &kui_font_name, LV_PART_MAIN);
            }
        }
    }
}
