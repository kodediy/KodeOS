/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_details.c
 * @brief An app's details, and the two cards that remove it (kgui_details.h).
 */
#include "kgui_details.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "kbsp_board.h"
#include "kui.h"
#include "kgui_icon.h"
#include "kgui_list.h"

/* ---- Layout, in the card's own rows ----------------------------------------- */

#define DT_W            ((int32_t)KBSP_LCD_H_RES)  /**< The card is as wide as the glass. */
#define DT_SIDE         48                         /**< Where the name, the version and the words begin and end, from the glass's edge. */
#define DT_TEXT_W       (DT_W - (2 * DT_SIDE))     /**< The widest a line on the details may be: 314. */
#define DT_DISC_Y       24                         /**< The icon disc's top. */
#define DT_NAME_GAP     8                          /**< From the disc's foot to the name. */
#define DT_GAP          12                         /**< Between the header, the words and the panel. */
#define DT_DESC_LINES   3                          /**< Most lines the words may take (52 characters fill three); what does not fit is cut with dots. */
#define DT_PANEL_W      KUI_PILL_W                 /**< The panel of rows: as wide as a full pill, 346. */
#define DT_PANEL_X      ((DT_W - DT_PANEL_W) / 2)  /**< ...and its left, 32. */
#define DT_PANEL_R      24                         /**< ...and its corners. */
#define DT_PANEL_PAD    20                         /**< From the panel's edge to a row's words. */
#define DT_ROW_LABEL_W  100                        /**< The width of a row's label ("Size", "Built"); its value takes the rest of the panel. */
#define DT_ROW_H        44                         /**< A row's height, and the pitch between two. */
#define DT_ROWS_MAX     2U                         /**< Rows: Size and Built. */
#define DT_SEP_PCT      12U                        /**< How much of the ink the rule between two rows keeps. */
#define DT_FOOT         440                        /**< Where the page must end when there is no pill, clear of the glass's corners. */
#define DT_PILL_H       72                         /**< The question's pill's height. */
#define DT_INFO_PILL_W  160                        /**< The details' Remove pill: a sheet's one answer button. */
#define DT_INFO_PILL_H  60                         /**< ...and its height. */
#define DT_INFO_PILL_Y  412                        /**< ...and its top, held to the foot whatever stands above it. */
#define DT_ASK_PILL_W   374                        /**< The question's pill. */
#define DT_ASK_PILL_X   18                         /**< ...and its left. */
#define DT_ASK_PILL_Y   370                        /**< ...and its top. */
#define DT_ASK_TITLE_Y  226                        /**< The question's title, when it takes one line. */
#define DT_ASK_TITLE_W  KUI_PILL_W                 /**< ...and the width it wraps in. */
#define DT_ASK_GAP      14                         /**< From the title's foot to the caption. */
#define DT_ASK_LINES    2                          /**< Lines the title may take. */
#define DT_WAIT_GLYPH_Y 126                        /**< The wait's glyph's top (drawn at DT_WAIT_PX, centred on the same middle). */
#define DT_WAIT_PX      96                         /**< The wait's glyph, in pixels. */
#define DT_WAIT_SRC_PX  64                         /**< ...and the apps glyph's own size. */
#define DT_WAIT_TITLE_Y 226                        /**< The wait's title. */
#define DT_WAIT_NAME_Y  280                        /**< The wait's name. */
#define DT_PILL_WORD    "Remove"                   /**< The pill's word. */
#define DT_ASK_TITLE_A  "Remove "                  /**< The question's title before the name. */
#define DT_ASK_TITLE_B  "?"                        /**< ...and after it. */
#define DT_ASK_TITLE_MAX 64U                       /**< Room for the question's title: the words, a 31 character name and the mark. */
#define DT_ASK_LEAVES   "It leaves the Dot."      /**< The caption's first line. */
#define DT_ASK_COPY     "\nThe SD card copy stays."   /**< ...and its second, when a copy stays. */
#define DT_ASK_CAPTION_MAX 64U                     /**< Room for the caption. */
#define DT_WAIT_TITLE   "Removing"                 /**< The wait's title. */

/** What the pages are made of. */
typedef struct {
    bool        open;                 /**< Everything below is built. */
    lv_obj_t   *card;                 /**< The sheet's card; not owned. */
    lv_obj_t   *handle;               /**< The kit's handle, hidden while the pages stand; not owned. */
    lv_obj_t   *kit_disc;             /**< The kit's header disc, hidden while the pages stand; not owned. */
    lv_obj_t   *info;                 /**< The details page: every object of it is its child. */
    lv_obj_t   *ask;                  /**< The question's page. */
    lv_obj_t   *wait;                 /**< The wait's page. */
    lv_obj_t   *disc;                 /**< The icon disc. */
    lv_obj_t   *name;                 /**< The app's name. */
    lv_obj_t   *meta;                 /**< The version and the category, under the name. */
    lv_obj_t   *panel;                /**< The dark panel the rows stand on. */
    lv_obj_t   *row_label[DT_ROWS_MAX]; /**< The rows' labels. */
    lv_obj_t   *row_value[DT_ROWS_MAX]; /**< ...and their values. */
    lv_obj_t   *row_rule[DT_ROWS_MAX];  /**< ...and the rules under them. */
    lv_obj_t   *desc;                 /**< The app's own words. */
    lv_obj_t   *pill;                 /**< The details' red pill. */
    lv_obj_t   *ask_title;            /**< The question's title. */
    lv_obj_t   *ask_caption;          /**< ...and its caption. */
    lv_obj_t   *ask_pill;             /**< ...and its red pill. */
    lv_obj_t   *wait_glyph;           /**< The wait's glyph. */
    lv_obj_t   *wait_title;           /**< ...its title. */
    lv_obj_t   *wait_name;            /**< ...and the app's name. */
    kgui_details_tap_fn tap;          /**< What a click on a pill asks. */
} dt_ui_t;

static dt_ui_t s_dt;  /**< The pages. */

/* ---- Building --------------------------------------------------------------- */

/**
 * @brief A label in a face and a colour, empty, on one line cut with dots.
 *
 * @param parent Its parent.
 * @param font   Its face.
 * @param color  Its colour.
 * @param align  Its text alignment.
 * @param x      Its left.
 * @param width  Its width.
 * @return The label, or NULL.
 */
static lv_obj_t *dt_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                          lv_text_align_t align, int32_t x, int32_t width)
{
    lv_obj_t *const lbl = lv_label_create(parent);

    if ( NULL != lbl )
    {
        lv_obj_set_style_text_font(lbl, font, 0);
        lv_obj_set_style_text_color(lbl, color, 0);
        lv_obj_set_style_text_align(lbl, align, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text_static(lbl, "");
        lv_obj_set_pos(lbl, x, 0);
        lv_obj_set_size(lbl, width, (int32_t)lv_font_get_line_height(font));
        lv_obj_set_hidden(lbl, true);
    }
    return lbl;
}

/**
 * @brief A page: a plain object as big as the card, off the glass, that holds
 *        the objects of one of the three pages.
 *
 * @return The page, or NULL.
 */
static lv_obj_t *dt_page(void)
{
    lv_obj_t *const page = kgui_plain(s_dt.card, 0, 0, DT_W, (int32_t)KBSP_LCD_V_RES);

    if ( NULL != page )
    {
        lv_obj_set_hidden(page, true);
    }
    return page;
}

/**
 * @brief A click on a pill: the owner decides what it is worth.
 *
 * @param e The event.
 */
static void dt_on_click(lv_event_t *e)
{
    (void)e;
    if ( NULL != s_dt.tap )
    {
        s_dt.tap();
    }
}

/**
 * @brief A red pill with its word.
 *
 * @param parent Its page.
 * @param x Its left.
 * @param y Its top.
 * @param w Its width.
 * @param h Its height.
 * @return The pill, or NULL.
 */
static lv_obj_t *dt_pill(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *const pill = kgui_plain(parent, x, y, w, h);

    if ( NULL != pill )
    {
        lv_obj_t *const txt = lv_label_create(pill);

        lv_obj_set_clickable(pill, true);
        lv_obj_set_scroll_on_focus(pill, false);
        lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(pill, KUI_COLOR_DANGER, 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(pill, KUI_COLOR_REST, LV_STATE_PRESSED);
        lv_obj_add_style(pill, &kui_style_ring, LV_STATE_CHECKED);
        (void)lv_obj_add_event_cb(pill, &dt_on_click, LV_EVENT_CLICKED, NULL);
        if ( NULL != txt )
        {
            lv_obj_set_style_text_font(txt, &kui_font_name, 0);
            lv_obj_set_style_text_color(txt, KUI_COLOR_TEXT_ON_BG, 0);
            lv_label_set_text_static(txt, DT_PILL_WORD);
            lv_obj_center(txt);
            lv_obj_set_clickable(txt, false);
        }
    }
    return pill;
}

/**
 * @brief Build the details page's objects.
 *
 * @return true when they all stand.
 */
static bool dt_build_info(void)
{
    int32_t const value_x = DT_PANEL_PAD + DT_ROW_LABEL_W;
    int32_t const value_w = DT_PANEL_W - DT_PANEL_PAD - value_x;

    s_dt.info = dt_page();
    if ( NULL == s_dt.info )
    {
        return false;
    }
    s_dt.disc = kgui_list_disc_make(s_dt.info, (DT_W - KGUI_DETAILS_DISC_PX) / 2, DT_DISC_Y,
                                    KGUI_DETAILS_DISC_PX);
    s_dt.name = dt_label(s_dt.info, &kui_font_title, KUI_COLOR_TEXT_ON_BG,
                         LV_TEXT_ALIGN_CENTER, DT_SIDE, DT_TEXT_W);
    /* The body's face: nothing on the glass is under 28 px (the apocrifa's). */
    s_dt.meta = dt_label(s_dt.info, &kui_font_body, KUI_COLOR_TEXT_MUTED,
                         LV_TEXT_ALIGN_CENTER, DT_SIDE, DT_TEXT_W);
    s_dt.desc = dt_label(s_dt.info, &kui_font_body, KUI_COLOR_TEXT_ON_BG,
                         LV_TEXT_ALIGN_CENTER, DT_SIDE, DT_TEXT_W);
    s_dt.panel = kgui_plain(s_dt.info, DT_PANEL_X, 0, DT_PANEL_W, DT_ROW_H);
    s_dt.pill = dt_pill(s_dt.info, (DT_W - DT_INFO_PILL_W) / 2, DT_INFO_PILL_Y,
                        DT_INFO_PILL_W, DT_INFO_PILL_H);
    bool ok = (NULL != s_dt.disc) && (NULL != s_dt.name) && (NULL != s_dt.meta)
              && (NULL != s_dt.desc) && (NULL != s_dt.panel) && (NULL != s_dt.pill);

    if ( NULL == s_dt.panel )
    {
        return false;
    }
    lv_obj_set_style_radius(s_dt.panel, DT_PANEL_R, 0);
    lv_obj_set_style_bg_color(s_dt.panel, KUI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(s_dt.panel, LV_OPA_COVER, 0);
    lv_obj_set_hidden(s_dt.panel, true);
    for ( uint32_t i = 0U; DT_ROWS_MAX > i; ++i )
    {
        s_dt.row_label[i] = dt_label(s_dt.panel, &kui_font_body, KUI_COLOR_TEXT_MUTED,
                                     LV_TEXT_ALIGN_LEFT, DT_PANEL_PAD, DT_ROW_LABEL_W);
        s_dt.row_value[i] = dt_label(s_dt.panel, &kui_font_name, KUI_COLOR_TEXT_ON_BG,
                                     LV_TEXT_ALIGN_RIGHT, value_x, value_w);
        s_dt.row_rule[i]  = kgui_plain(s_dt.panel, DT_PANEL_PAD, 0,
                                       DT_PANEL_W - (2 * DT_PANEL_PAD), 1);
        ok = ok && (NULL != s_dt.row_label[i]) && (NULL != s_dt.row_value[i])
             && (NULL != s_dt.row_rule[i]);
        if ( NULL != s_dt.row_rule[i] )
        {
            lv_obj_set_style_bg_color(s_dt.row_rule[i], KUI_COLOR_TEXT_ON_BG, 0);
            lv_obj_set_style_bg_opa(s_dt.row_rule[i], KUI_OPA_PCT(DT_SEP_PCT), 0);
            lv_obj_set_hidden(s_dt.row_rule[i], true);
        }
    }
    return ok;
}

/**
 * @brief Build the question's and the wait's objects.
 *
 * @return true when they all stand.
 */
static bool dt_build_ask(void)
{
    s_dt.ask  = dt_page();
    s_dt.wait = dt_page();
    if ( (NULL == s_dt.ask) || (NULL == s_dt.wait) )
    {
        return false;
    }
    s_dt.ask_title = dt_label(s_dt.ask, &kui_font_display_40, KUI_COLOR_TEXT_ON_BG,
                              LV_TEXT_ALIGN_CENTER, (DT_W - DT_ASK_TITLE_W) / 2, DT_ASK_TITLE_W);
    s_dt.ask_caption = dt_label(s_dt.ask, &kui_font_body, KUI_COLOR_TEXT_MUTED,
                                LV_TEXT_ALIGN_CENTER, (DT_W - DT_ASK_TITLE_W) / 2, DT_ASK_TITLE_W);
    s_dt.ask_pill = dt_pill(s_dt.ask, DT_ASK_PILL_X, DT_ASK_PILL_Y, DT_ASK_PILL_W, DT_PILL_H);
    s_dt.wait_title = dt_label(s_dt.wait, &kui_font_display_40, KUI_COLOR_TEXT_ON_BG,
                               LV_TEXT_ALIGN_CENTER, 0, DT_W);
    s_dt.wait_name = dt_label(s_dt.wait, &kui_font_body, KUI_COLOR_TEXT_MUTED,
                              LV_TEXT_ALIGN_CENTER, DT_SIDE, DT_TEXT_W);
    s_dt.wait_glyph = lv_image_create(s_dt.wait);
    bool const ok = (NULL != s_dt.ask_title) && (NULL != s_dt.ask_caption)
                    && (NULL != s_dt.ask_pill) && (NULL != s_dt.wait_title)
                    && (NULL != s_dt.wait_name) && (NULL != s_dt.wait_glyph);

    if ( NULL != s_dt.ask_title )
    {
        lv_label_set_long_mode(s_dt.ask_title, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_hidden(s_dt.ask_title, false);
    }
    if ( NULL != s_dt.ask_caption )
    {
        lv_label_set_long_mode(s_dt.ask_caption, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_hidden(s_dt.ask_caption, false);
    }
    if ( NULL != s_dt.ask_pill )
    {
        lv_obj_set_hidden(s_dt.ask_pill, false);
    }
    if ( NULL != s_dt.wait_title )
    {
        lv_label_set_text_static(s_dt.wait_title, DT_WAIT_TITLE);
        lv_obj_set_y(s_dt.wait_title, DT_WAIT_TITLE_Y);
        lv_obj_set_hidden(s_dt.wait_title, false);
    }
    if ( NULL != s_dt.wait_name )
    {
        lv_obj_set_y(s_dt.wait_name, DT_WAIT_NAME_Y);
        lv_obj_set_hidden(s_dt.wait_name, false);
    }
    if ( NULL != s_dt.wait_glyph )
    {
        uint32_t const scale = ((uint32_t)DT_WAIT_PX * (uint32_t)LV_SCALE_NONE) / (uint32_t)DT_WAIT_SRC_PX;

        lv_image_set_src(s_dt.wait_glyph, &kui_glyph_apps_64);
        lv_image_set_scale(s_dt.wait_glyph, scale);
        lv_obj_set_style_image_recolor(s_dt.wait_glyph, KUI_COLOR_TEXT_ON_BG, 0);
        lv_obj_set_style_image_recolor_opa(s_dt.wait_glyph, LV_OPA_COVER, 0);
        lv_obj_set_pos(s_dt.wait_glyph, (DT_W - DT_WAIT_SRC_PX) / 2, DT_WAIT_GLYPH_Y);
        lv_obj_set_clickable(s_dt.wait_glyph, false);
    }
    return ok;
}

/** @brief Hide every page. */
static void dt_hide_all(void)
{
    lv_obj_set_hidden(s_dt.info, true);
    lv_obj_set_hidden(s_dt.ask, true);
    lv_obj_set_hidden(s_dt.wait, true);
}

bool kgui_details_open(lv_obj_t *card, kgui_details_tap_fn tap)
{
    bool ok = s_dt.open;

    s_dt.tap = tap;
    if ( (!ok) && (NULL != card) )
    {
        (void)memset(&s_dt, 0, sizeof(s_dt));
        s_dt.tap      = tap;
        s_dt.card     = card;
        s_dt.handle   = lv_obj_find_by_name(card, "handle");
        s_dt.kit_disc = lv_obj_find_by_name(card, "hdr_disc");
        ok = dt_build_info() && dt_build_ask();
        if ( ok )
        {
            s_dt.open = true;
        }
        else
        {
            kgui_details_close();
        }
    }
    if ( ok )
    {
        /* The kit gives its header disc back every time it clears the card, so
         * they are taken off the glass again with every call. */
        if ( NULL != s_dt.handle )
        {
            lv_obj_set_hidden(s_dt.handle, true);
        }
        if ( NULL != s_dt.kit_disc )
        {
            lv_obj_set_hidden(s_dt.kit_disc, true);
        }
    }
    return ok;
}

void kgui_details_close(void)
{
    if ( NULL != s_dt.card )
    {
        /* The pages hold every object of the module: they go, and the rest with them. */
        if ( NULL != s_dt.info )
        {
            lv_obj_delete(s_dt.info);
        }
        if ( NULL != s_dt.ask )
        {
            lv_obj_delete(s_dt.ask);
        }
        if ( NULL != s_dt.wait )
        {
            lv_obj_delete(s_dt.wait);
        }
        /* The kit's parts go back as it made them. */
        if ( NULL != s_dt.handle )
        {
            lv_obj_set_hidden(s_dt.handle, false);
        }
        if ( NULL != s_dt.kit_disc )
        {
            lv_obj_set_hidden(s_dt.kit_disc, false);
        }
    }
    (void)memset(&s_dt, 0, sizeof(s_dt));
}

bool kgui_details_is_open(void)
{
    return s_dt.open;
}

/* ---- Saying ------------------------------------------------------------------ */

/**
 * @brief Whether a string says something.
 *
 * @param text A string; may be NULL.
 * @return true when it has at least one character.
 */
static bool dt_known(const char *text)
{
    return (NULL != text) && ('\0' != text[0]);
}

/**
 * @brief A string, or "" when it is NULL.
 *
 * @param text A string; may be NULL.
 * @return @p text, or an empty string.
 */
static const char *dt_text(const char *text)
{
    const char *out = "";

    if ( NULL != text )
    {
        out = text;
    }
    return out;
}

/**
 * @brief Put one row at its place on the panel, or leave it out.
 *
 * The rule is drawn on the row's top, so the first row shown has none.
 *
 * @param slot  Which place, 0-based, counting the rows that show.
 * @param which The row's own index.
 * @param label Its label.
 * @param value Its value; "" or NULL leaves the row out.
 * @return true when the row shows.
 */
static bool dt_row(uint32_t slot, uint32_t which, const char *label, const char *value)
{
    bool const shows = dt_known(value);

    if ( shows )
    {
        int32_t const y = (int32_t)slot * DT_ROW_H;
        int32_t const h = (int32_t)lv_font_get_line_height(&kui_font_body);

        lv_label_set_text(s_dt.row_label[which], label);
        lv_label_set_text(s_dt.row_value[which], value);
        lv_obj_set_y(s_dt.row_label[which], y + ((DT_ROW_H - h) / 2));
        lv_obj_set_y(s_dt.row_value[which], y + ((DT_ROW_H - h) / 2));
        lv_obj_set_y(s_dt.row_rule[which], y);
    }
    lv_obj_set_hidden(s_dt.row_label[which], !shows);
    lv_obj_set_hidden(s_dt.row_value[which], !shows);
    lv_obj_set_hidden(s_dt.row_rule[which], (!shows) || (0U == slot));
    return shows;
}

void kgui_details_info(const kgui_details_spec_t *spec)
{
    if ( (!s_dt.open) || (NULL == spec) )
    {
        return;
    }
    dt_hide_all();
    kgui_list_disc_dress(s_dt.disc, KGUI_DETAILS_DISC_PX, spec->ground, spec->pic, spec->mono,
                         NULL, spec->glyph, spec->glyph_scale, spec->ink);

    /* Who: the name, and the version and category under it when known. */
    int32_t const body = (int32_t)lv_font_get_line_height(&kui_font_body);
    int32_t       y    = DT_DISC_Y + KGUI_DETAILS_DISC_PX + DT_NAME_GAP;

    lv_label_set_text(s_dt.name, dt_text(spec->name));
    lv_obj_set_y(s_dt.name, y);
    lv_obj_set_hidden(s_dt.name, false);
    y += (int32_t)lv_font_get_line_height(&kui_font_title);
    if ( dt_known(spec->meta) )
    {
        lv_label_set_text(s_dt.meta, spec->meta);
        lv_obj_set_y(s_dt.meta, y);
        y += body;
    }
    lv_obj_set_hidden(s_dt.meta, !dt_known(spec->meta));
    y += DT_GAP;

    /* The facts, counted first: the words take what room the panel leaves. */
    uint32_t shown = 0U;

    if ( dt_row(shown, 0U, "Size", spec->size) )
    {
        ++shown;
    }
    if ( dt_row(shown, 1U, "Built", spec->built) )
    {
        ++shown;
    }
    int32_t const panel_h = (int32_t)shown * DT_ROW_H;
    int32_t       foot    = DT_FOOT;

    if ( spec->removable )
    {
        foot = DT_INFO_PILL_Y - DT_GAP;
    }
    if ( 0U < shown )
    {
        foot -= panel_h + DT_GAP;
    }

    /* What it says of itself: as many lines as the room holds, one at least
     * and DT_DESC_LINES at most. */
    if ( dt_known(spec->description) )
    {
        int32_t lines = (foot - y) / body;

        if ( DT_DESC_LINES < lines )
        {
            lines = DT_DESC_LINES;
        }
        if ( 1 > lines )
        {
            lines = 1;
        }
        lv_point_t said;
        int32_t    said_lines;

        /* The words take the lines they need, not the most they may. */
        lv_text_get_size(&said, spec->description, &kui_font_body, 0, 0, DT_TEXT_W,
                         LV_TEXT_FLAG_NONE);
        said_lines = (said.y + (body / 2)) / body;
        if ( said_lines < lines )
        {
            lines = said_lines;
        }
        if ( 1 > lines )
        {
            lines = 1;
        }
        lv_label_set_text(s_dt.desc, spec->description);
        lv_obj_set_size(s_dt.desc, DT_TEXT_W, lines * body);
        lv_obj_set_y(s_dt.desc, y);
        y += (lines * body) + DT_GAP;
    }
    lv_obj_set_hidden(s_dt.desc, !dt_known(spec->description));

    /* The facts, under whatever stands above them. */
    if ( 0U < shown )
    {
        lv_obj_set_y(s_dt.panel, y);
        lv_obj_set_height(s_dt.panel, panel_h);
    }
    lv_obj_set_hidden(s_dt.panel, 0U == shown);
    lv_obj_set_hidden(s_dt.pill, !spec->removable);
    lv_obj_set_hidden(s_dt.info, false);
}

void kgui_details_ask(const char *name, bool copy)
{
    if ( !s_dt.open )
    {
        return;
    }
    dt_hide_all();
    char title[DT_ASK_TITLE_MAX];
    char caption[DT_ASK_CAPTION_MAX];

    (void)snprintf(title, sizeof(title), DT_ASK_TITLE_A "%s" DT_ASK_TITLE_B, dt_text(name));
    (void)snprintf(caption, sizeof(caption), DT_ASK_LEAVES);
    if ( copy )
    {
        (void)snprintf(caption, sizeof(caption), DT_ASK_LEAVES DT_ASK_COPY);
    }
    {
        int32_t const line = (int32_t)lv_font_get_line_height(&kui_font_display_40);
        lv_point_t    size;
        lv_point_t    said;
        int32_t       rows;
        int32_t       top;

        lv_text_get_size(&size, title, &kui_font_display_40, 0, 0, DT_ASK_TITLE_W, LV_TEXT_FLAG_NONE);
        rows = (size.y + (line / 2)) / line;
        if ( DT_ASK_LINES < rows )
        {
            rows = DT_ASK_LINES;
        }
        if ( 1 > rows )
        {
            rows = 1;
        }
        top = DT_ASK_TITLE_Y - (((rows - 1) * line) / 2);
        lv_label_set_text(s_dt.ask_title, title);
        lv_obj_set_height(s_dt.ask_title, rows * line);
        lv_obj_set_y(s_dt.ask_title, top);
        lv_text_get_size(&said, caption, &kui_font_body, 0, 0, DT_ASK_TITLE_W, LV_TEXT_FLAG_NONE);
        lv_label_set_text(s_dt.ask_caption, caption);
        lv_obj_set_size(s_dt.ask_caption, DT_ASK_TITLE_W, said.y);
        lv_obj_set_y(s_dt.ask_caption, top + (rows * line) + DT_ASK_GAP);
    }
    lv_obj_set_hidden(s_dt.ask, false);
}

void kgui_details_wait(const char *name)
{
    if ( !s_dt.open )
    {
        return;
    }
    dt_hide_all();
    lv_label_set_text(s_dt.wait_name, dt_text(name));
    lv_obj_set_hidden(s_dt.wait, false);
}

void kgui_details_ring(bool shown)
{
    lv_obj_t *const pills[] = { s_dt.pill, s_dt.ask_pill };

    for ( size_t i = 0U; (sizeof(pills) / sizeof(pills[0])) > i; ++i )
    {
        if ( NULL != pills[i] )
        {
            if ( shown )
            {
                lv_obj_add_state(pills[i], LV_STATE_CHECKED);
            }
            else
            {
                lv_obj_remove_state(pills[i], LV_STATE_CHECKED);
            }
        }
    }
}
