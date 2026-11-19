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
#include "kgui_dtext.h"
#include "kgui_icon.h"
#include "kgui_list.h"
#include "kgui_sysscreen.h"

/* ---- Layout, in the card's own rows ----------------------------------------- */

#define DT_W            ((int32_t)KBSP_LCD_H_RES)  /**< The card is as wide as the glass. */
#define DT_SIDE         48                         /**< Where the name and the words begin and end, from the glass's edge. */
#define DT_TEXT_W       (DT_W - (2 * DT_SIDE))     /**< The widest a line of the description may be: 314. */
#define DT_DISC_GAP     8                          /**< From the disc's foot to the name. */
#define DT_GAP          12                         /**< Between the header and the words. */
#define DT_TOP_MIN      24                         /**< Where the page may begin at the earliest. */
#define DT_ROOM_TOP     40                         /**< Where the room for the details' block begins. */
#define DT_ROOM_AIR     24                         /**< Air kept between the block and the pills. */
#define DT_DESC_LINES   3                          /**< Most lines the words may take; what does not fit is cut at a whole word with dots. */
#define DT_DESC_MAX     64U                        /**< Room for the description, a cut one and its dots. */
#define DT_DOTS         "..."                      /**< What a cut description ends with. */
#define DT_PILL_X_LEFT  ((DT_W - ((2 * KGUI_SYS_PAIR_W) + KGUI_SYS_PAIR_GAP)) / 2) /**< The left pill of a pair: 24. */
#define DT_PILL_X_RIGHT (DT_PILL_X_LEFT + KGUI_SYS_PAIR_W + KGUI_SYS_PAIR_GAP)     /**< ...and the right one: 213. */
#define DT_PILL_X_ALONE ((DT_W - KGUI_SYS_PAIR_W) / 2)                           /**< A pill that stands alone: centred. */
#define DT_WAIT_GLYPH_Y 126                        /**< The wait's glyph's top (drawn at DT_WAIT_PX, centred on the same middle). */
#define DT_WAIT_PX      96                         /**< The wait's glyph, in pixels. */
#define DT_WAIT_SRC_PX  64                         /**< ...and the apps glyph's own size. */
#define DT_WAIT_TITLE_Y 226                        /**< The wait's title. */
#define DT_WAIT_NAME_Y  280                        /**< The wait's name. */
#define DT_WORD_OPEN    "Open"                     /**< The details' left pill. */
#define DT_WORD_DELETE  "Delete"                   /**< The right pill of both pages. */
#define DT_WORD_KEEP    "Keep"                     /**< The question's left pill. */
#define DT_ASK_TITLE_A  "Delete "                  /**< The question's title before the name. */
#define DT_ASK_TITLE_B  "?"                        /**< ...and after it. */
#define DT_ASK_TITLE_MAX 64U                       /**< Room for the question's title: the words, a 31 character name and the mark. */
#define DT_WAIT_TITLE   "Removing"                 /**< The wait's title. */

/** What the pages are made of. */
typedef struct {
    bool        open;                 /**< Everything below is built. */
    bool        ring;                 /**< The focus ring shows: a button is the input in use. */
    bool        on_ask;               /**< The question's page is the one up (else the details'). */
    bool        has_left;             /**< The page up shows its left pill. */
    bool        has_right;            /**< ...and its right pill. */
    kgui_details_pill_t focus;        /**< The pill that holds the focus. */
    lv_obj_t   *card;                 /**< The sheet's card; not owned. */
    lv_obj_t   *handle;               /**< The kit's handle, hidden while the pages stand; not owned. */
    lv_obj_t   *kit_disc;             /**< The kit's header disc, hidden while the pages stand; not owned. */
    lv_obj_t   *info;                 /**< The details page: every object of it is its child. */
    lv_obj_t   *ask;                  /**< The question's page. */
    lv_obj_t   *wait;                 /**< The wait's page. */
    lv_obj_t   *disc;                 /**< The icon disc. */
    lv_obj_t   *name;                 /**< The app's name. */
    lv_obj_t   *meta;                 /**< The category, the version and the size, under the name. */
    lv_obj_t   *desc;                 /**< The app's own words. */
    lv_obj_t   *pill_open;            /**< The details' Open pill. */
    lv_obj_t   *pill;                 /**< The details' red Delete pill. */
    lv_obj_t   *ask_title;            /**< The question's title. */
    lv_obj_t   *ask_caption;          /**< ...and its caption. */
    lv_obj_t   *ask_keep;             /**< ...and its Keep pill. */
    lv_obj_t   *ask_pill;             /**< ...and its red Delete pill. */
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
 * @param e The event; its user data says which pill.
 */
static void dt_on_click(lv_event_t *e)
{
    if ( NULL != s_dt.tap )
    {
        s_dt.tap((kgui_details_pill_t)(uintptr_t)lv_event_get_user_data(e));
    }
}

/**
 * @brief A pill of a pair, with its word.
 *
 * @param parent Its page.
 * @param which  Which of the two it is.
 * @param word   Its word.
 * @param ground What it is filled with.
 * @param ink    The colour of its word.
 * @return The pill, or NULL.
 */
static lv_obj_t *dt_pill(lv_obj_t *parent, kgui_details_pill_t which, const char *word,
                         lv_color_t ground, lv_color_t ink)
{
    lv_obj_t *const pill = kgui_plain(parent, 0, KGUI_SYS_PILL_Y, KGUI_SYS_PAIR_W, KGUI_SYS_PILL_H);

    if ( NULL != pill )
    {
        lv_obj_t *const txt = lv_label_create(pill);

        lv_obj_set_clickable(pill, true);
        lv_obj_set_scroll_on_focus(pill, false);
        lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(pill, ground, 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(pill, KUI_COLOR_REST, LV_STATE_PRESSED);
        lv_obj_add_style(pill, &kui_style_ring, LV_STATE_CHECKED);
        (void)lv_obj_add_event_cb(pill, &dt_on_click, LV_EVENT_CLICKED, (void *)(uintptr_t)which);
        if ( NULL != txt )
        {
            lv_obj_set_style_text_font(txt, KGUI_SYS_PAIR_FONT, 0);
            lv_obj_set_style_text_color(txt, ink, 0);
            lv_label_set_text_static(txt, word);
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
    s_dt.info = dt_page();
    if ( NULL == s_dt.info )
    {
        return false;
    }
    s_dt.disc = kgui_list_disc_make(s_dt.info, (DT_W - KGUI_DETAILS_DISC_PX) / 2, DT_TOP_MIN,
                                    KGUI_DETAILS_DISC_PX);
    s_dt.name = dt_label(s_dt.info, &kui_font_title, KUI_COLOR_TEXT_ON_BG,
                         LV_TEXT_ALIGN_CENTER, DT_SIDE, DT_TEXT_W);
    /* The body's face: nothing on the glass is under 28 px (the apocrifa's). */
    s_dt.meta = dt_label(s_dt.info, &kui_font_body, KUI_COLOR_TEXT_MUTED,
                         LV_TEXT_ALIGN_CENTER, KGUI_SYS_TEXT_X, KGUI_SYS_TEXT_W);
    s_dt.desc = dt_label(s_dt.info, &kui_font_body, KUI_COLOR_TEXT_ON_BG,
                         LV_TEXT_ALIGN_CENTER, DT_SIDE, DT_TEXT_W);
    s_dt.pill_open = dt_pill(s_dt.info, KGUI_DETAILS_PILL_LEFT, DT_WORD_OPEN, KUI_COLOR_ACTION,
                             KUI_COLOR_TEXT);
    s_dt.pill = dt_pill(s_dt.info, KGUI_DETAILS_PILL_RIGHT, DT_WORD_DELETE, KUI_COLOR_DANGER,
                        KUI_COLOR_TEXT_ON_BG);
    return (NULL != s_dt.disc) && (NULL != s_dt.name) && (NULL != s_dt.meta)
           && (NULL != s_dt.desc) && (NULL != s_dt.pill_open) && (NULL != s_dt.pill);
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
                              LV_TEXT_ALIGN_CENTER, KGUI_SYS_ASK_X, KGUI_SYS_ASK_W);
    s_dt.ask_caption = dt_label(s_dt.ask, &kui_font_body, KUI_COLOR_TEXT_MUTED,
                                LV_TEXT_ALIGN_CENTER, KGUI_SYS_ASK_X, KGUI_SYS_ASK_W);
    s_dt.ask_keep = dt_pill(s_dt.ask, KGUI_DETAILS_PILL_LEFT, DT_WORD_KEEP, KUI_COLOR_TEXT_ON_BG,
                            KUI_COLOR_TEXT);
    s_dt.ask_pill = dt_pill(s_dt.ask, KGUI_DETAILS_PILL_RIGHT, DT_WORD_DELETE, KUI_COLOR_DANGER,
                            KUI_COLOR_TEXT_ON_BG);
    s_dt.wait_title = dt_label(s_dt.wait, &kui_font_display_40, KUI_COLOR_TEXT_ON_BG,
                               LV_TEXT_ALIGN_CENTER, 0, DT_W);
    s_dt.wait_name = dt_label(s_dt.wait, &kui_font_body, KUI_COLOR_TEXT_MUTED,
                              LV_TEXT_ALIGN_CENTER, DT_SIDE, DT_TEXT_W);
    s_dt.wait_glyph = lv_image_create(s_dt.wait);
    bool const ok = (NULL != s_dt.ask_title) && (NULL != s_dt.ask_caption)
                    && (NULL != s_dt.ask_keep) && (NULL != s_dt.ask_pill)
                    && (NULL != s_dt.wait_title) && (NULL != s_dt.wait_name)
                    && (NULL != s_dt.wait_glyph);

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
    if ( (NULL != s_dt.ask_keep) && (NULL != s_dt.ask_pill) )
    {
        lv_obj_set_x(s_dt.ask_keep, DT_PILL_X_LEFT);
        lv_obj_set_x(s_dt.ask_pill, DT_PILL_X_RIGHT);
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
 * @brief Put the pills of the page that is up where they stand: both of a
 *        pair, or the one there is, centred.
 *
 * The focus goes to the left pill, or to the only one.
 *
 * @param left  The left pill.
 * @param right The right pill.
 * @param show_left  Whether the left pill shows.
 * @param show_right Whether the right pill shows.
 */
static void dt_pills_place(lv_obj_t *left, lv_obj_t *right, bool show_left, bool show_right)
{
    if ( show_left && show_right )
    {
        lv_obj_set_x(left, DT_PILL_X_LEFT);
        lv_obj_set_x(right, DT_PILL_X_RIGHT);
    }
    else
    {
        lv_obj_set_x(left, DT_PILL_X_ALONE);
        lv_obj_set_x(right, DT_PILL_X_ALONE);
    }
    lv_obj_set_hidden(left, !show_left);
    lv_obj_set_hidden(right, !show_right);
    s_dt.has_left  = show_left;
    s_dt.has_right = show_right;
    s_dt.focus     = KGUI_DETAILS_PILL_LEFT;
    if ( !show_left )
    {
        s_dt.focus = KGUI_DETAILS_PILL_RIGHT;
    }
    kgui_details_ring(s_dt.ring);
}

/**
 * @brief The description as it is shown: whole when it fits in DT_DESC_LINES
 *        lines, else cut at the last whole word that leaves room for the dots.
 *
 * @param text What the app says of itself.
 * @param out  Receives the words, terminated.
 * @param cap  Its size.
 * @return The number of lines the words take.
 */
static int32_t dt_words_fit(const char *text, char *out, size_t cap)
{
    int32_t const body = (int32_t)lv_font_get_line_height(&kui_font_body);
    lv_point_t    said;
    size_t        end;

    (void)snprintf(out, cap, "%s", text);
    lv_text_get_size(&said, out, &kui_font_body, 0, 0, DT_TEXT_W, LV_TEXT_FLAG_NONE);
    end = strlen(out);
    while ( ((DT_DESC_LINES * body) < (said.y + (body / 2))) && (0U < end) )
    {
        /* Back to the end of the word before the last one. */
        while ( (0U < end) && (' ' != out[end - 1U]) )
        {
            --end;
        }
        while ( (0U < end) && (' ' == out[end - 1U]) )
        {
            --end;
        }
        out[end] = '\0';
        while ( (0U < end) && ((',' == out[end - 1U]) || (';' == out[end - 1U])
                               || (':' == out[end - 1U])) )
        {
            --end;
            out[end] = '\0';
        }
        (void)snprintf(&out[end], cap - end, DT_DOTS);
        lv_text_get_size(&said, out, &kui_font_body, 0, 0, DT_TEXT_W, LV_TEXT_FLAG_NONE);
        if ( (DT_DESC_LINES * body) < (said.y + (body / 2)) )
        {
            out[end] = '\0';
        }
    }
    return (said.y + (body / 2)) / body;
}

void kgui_details_info(const kgui_details_spec_t *spec)
{
    if ( (!s_dt.open) || (NULL == spec) )
    {
        return;
    }
    dt_hide_all();
    s_dt.on_ask = false;
    kgui_list_disc_dress(s_dt.disc, KGUI_DETAILS_DISC_PX, spec->ground, spec->pic, spec->mono,
                         NULL, spec->glyph, spec->glyph_scale, spec->ink);

    /* The block (the disc, the name, one line of facts and the words) is
     * centred in the room over the pills, so a page with no description has
     * no hole under it. */
    int32_t const body     = (int32_t)lv_font_get_line_height(&kui_font_body);
    int32_t const title    = (int32_t)lv_font_get_line_height(&kui_font_title);
    char          words[DT_DESC_MAX];
    int32_t       lines    = 0;
    int32_t       block    = KGUI_DETAILS_DISC_PX + DT_DISC_GAP + title;
    int32_t       y;

    if ( dt_known(spec->meta) )
    {
        block += body;
    }
    if ( dt_known(spec->description) )
    {
        lines = dt_words_fit(spec->description, words, sizeof(words));
        block += DT_GAP + (lines * body);
    }
    y = DT_ROOM_TOP + (((KGUI_SYS_PILL_Y - DT_ROOM_AIR - DT_ROOM_TOP) - block) / 2);
    if ( DT_TOP_MIN > y )
    {
        y = DT_TOP_MIN;
    }
    lv_obj_set_y(s_dt.disc, y);
    y += KGUI_DETAILS_DISC_PX + DT_DISC_GAP;
    lv_label_set_text(s_dt.name, dt_text(spec->name));
    lv_obj_set_y(s_dt.name, y);
    lv_obj_set_hidden(s_dt.name, false);
    y += title;
    if ( dt_known(spec->meta) )
    {
        lv_label_set_text(s_dt.meta, spec->meta);
        lv_obj_set_y(s_dt.meta, y);
        y += body;
    }
    lv_obj_set_hidden(s_dt.meta, !dt_known(spec->meta));
    if ( dt_known(spec->description) )
    {
        y += DT_GAP;
        lv_label_set_text(s_dt.desc, words);
        lv_obj_set_size(s_dt.desc, DT_TEXT_W, lines * body);
        lv_obj_set_y(s_dt.desc, y);
    }
    lv_obj_set_hidden(s_dt.desc, !dt_known(spec->description));
    dt_pills_place(s_dt.pill_open, s_dt.pill, spec->openable, spec->removable);
    lv_obj_set_hidden(s_dt.info, false);
}

void kgui_details_ask(const char *name)
{
    if ( !s_dt.open )
    {
        return;
    }
    dt_hide_all();
    s_dt.on_ask = true;
    char title[DT_ASK_TITLE_MAX];

    (void)snprintf(title, sizeof(title), DT_ASK_TITLE_A "%s" DT_ASK_TITLE_B, dt_text(name));
    {
        int32_t const line = (int32_t)lv_font_get_line_height(&kui_font_display_40);
        lv_point_t    size;
        lv_point_t    said;
        int32_t       rows;
        int32_t       top = KGUI_SYS_ASK_Y;

        lv_text_get_size(&size, title, &kui_font_display_40, 0, 0, KGUI_SYS_ASK_W, LV_TEXT_FLAG_NONE);
        rows = (size.y + (line / 2)) / line;
        if ( KGUI_SYS_ASK_TITLE_LINES < rows )
        {
            rows = KGUI_SYS_ASK_TITLE_LINES;
        }
        if ( 1 > rows )
        {
            rows = 1;
        }
        lv_text_get_size(&said, KGUI_DTEXT_DELETE_BODY, &kui_font_body, 0, 0, KGUI_SYS_ASK_W,
                         LV_TEXT_FLAG_NONE);
        lv_label_set_text(s_dt.ask_title, title);
        lv_obj_set_height(s_dt.ask_title, rows * line);
        lv_obj_set_y(s_dt.ask_title, top);
        lv_label_set_text_static(s_dt.ask_caption, KGUI_DTEXT_DELETE_BODY);
        lv_obj_set_size(s_dt.ask_caption, KGUI_SYS_ASK_W, said.y);
        lv_obj_set_y(s_dt.ask_caption, top + (rows * line) + KGUI_SYS_ASK_GAP);
    }
    dt_pills_place(s_dt.ask_keep, s_dt.ask_pill, true, true);
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

bool kgui_details_origin(int32_t *cx, int32_t *cy, int32_t *size)
{
    bool ok = false;

    if ( s_dt.open && (NULL != s_dt.disc) && (!s_dt.on_ask) )
    {
        lv_area_t a;

        lv_obj_update_layout(s_dt.disc);
        lv_obj_get_coords(s_dt.disc, &a);
        *size = lv_area_get_width(&a);
        *cx   = a.x1 + (*size / 2);
        *cy   = a.y1 + (lv_area_get_height(&a) / 2);
        ok    = (0 < *size);
    }
    return ok;
}

void kgui_details_ring(bool shown)
{
    lv_obj_t *left  = s_dt.pill_open;
    lv_obj_t *right = s_dt.pill;

    s_dt.ring = shown;
    if ( s_dt.on_ask )
    {
        left  = s_dt.ask_keep;
        right = s_dt.ask_pill;
    }
    lv_obj_t *const pills[] = { s_dt.pill_open, s_dt.pill, s_dt.ask_keep, s_dt.ask_pill };

    for ( size_t i = 0U; (sizeof(pills) / sizeof(pills[0])) > i; ++i )
    {
        if ( NULL != pills[i] )
        {
            bool const mine = (pills[i] == left) || (pills[i] == right);
            bool const here = ((pills[i] == left) && (KGUI_DETAILS_PILL_LEFT == s_dt.focus))
                              || ((pills[i] == right) && (KGUI_DETAILS_PILL_RIGHT == s_dt.focus));

            if ( shown && mine && here )
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

kgui_details_pill_t kgui_details_focus(void)
{
    return s_dt.focus;
}

bool kgui_details_focus_set(kgui_details_pill_t pill)
{
    bool const there = ((KGUI_DETAILS_PILL_LEFT == pill) && s_dt.has_left)
                       || ((KGUI_DETAILS_PILL_RIGHT == pill) && s_dt.has_right);
    bool const moved = there && (pill != s_dt.focus);

    if ( moved )
    {
        s_dt.focus = pill;
        kgui_details_ring(s_dt.ring);
    }
    return moved;
}
