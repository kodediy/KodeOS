/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_screen_face.c
 * @brief The face: time and date, the battery, Kody's scene and the red arc of the power-off hold.
 */
#include "kgui_screens.h"

#include <stdio.h>
#include <string.h>

#include "kgui_off.h"
#include "kgui_lvgl.h"
#include "kgui_rest.h"

lv_obj_t *kgui_screen_face = NULL;
lv_obj_t *kgui_screen_dark = NULL;

/** The time's labels: the hour, the colon and the minute. */
#define FACE_PARTS        3U
/** Which of them is the hour. */
#define FACE_HOUR         0U
/** Which of them is the colon. */
#define FACE_COLON        1U
/** Which of them is the minute. */
#define FACE_MIN          2U
/** The longest text of a label: two figures and the terminator. */
#define FACE_PART_LEN     3U
/** How opaque the time is while it is not known: --:-- is a place for the
 *  time, not a time. */
#define FACE_UNKNOWN_PCT  35
/** The digits in the strip: 0 to 9. */
#define STRIP_DIGITS      10U
/** The panel's memory window starts and ends on a multiple of this. */
#define STRIP_PAIR        2
_Static_assert((STRIP_DIGITS + 1U) == KGUI_FACE_STRIP_CELLS,
               "the strip's cells are the ten digits and the colon");
/** How many things the burn-in shift moves: the time's box, the date, the
 *  battery, Kody's box and the knock's row. */
#define FACE_SHIFTS       5U
#define FACE_SET_PILL_W   252   /**< "Set the time": the apocrifa's pill. */
#define FACE_SET_PILL_H   72
#define FACE_ZONE_PILL_H  56    /**< "Choose your time zone". */
#define FACE_ZONE_PILL_PAD 12   /**< Its words start where the date's do. */
/** Which of them is the time's box. */
#define FACE_SHIFT_TIME   0U
/** Which of them is the date. */
#define FACE_SHIFT_DATE   1U
/** Which of them is Kody's box. */
#define FACE_SHIFT_SCENE  2U
/** Which of them is the battery. */
#define FACE_SHIFT_PILE   3U
/** Which of them is the knock's row, in the date's place. */
#define FACE_SHIFT_KNOCK  4U

/** The battery's fill, at this width or more, is under the bolt: the bolt is
 *  then drawn black on it. Narrower, the bolt stands on the pile's own
 *  colour where the fill does not reach. */
#define PILE_BOLT_ON_FILL_PX  16
/** The fill's corners. */
#define PILE_FILL_RADIUS      2
/** The terminal's corners. */
#define PILE_TIP_RADIUS       1
/** A full charge, in percent. */
#define PILE_FULL_PCT         100

/** The arc's line, as the screen counts: signed pixels. */
#define FACE_ARC_W        ((int32_t)KGUI_OFF_ARC_W)
/** The arc's corner radius, as the screen counts. */
#define FACE_ARC_R        ((int32_t)KGUI_OFF_ARC_R)
/** The arc's distance from the glass, as the screen counts. */
#define FACE_ARC_EDGE     ((int32_t)KGUI_OFF_ARC_EDGE)
/** The straight's length, as the screen counts. */
#define FACE_ARC_STRAIGHT ((int32_t)KBSP_LCD_H_RES - (2 * (FACE_ARC_EDGE + FACE_ARC_R)))
/** The box one corner of the arc is drawn in: the circle of the corner's
 *  outer edge, the line's middle radius and half the line out. */
#define FACE_ARC_BOX      (2 * (FACE_ARC_R + (FACE_ARC_W / 2)))
/** A corner is a quarter of a circle. */
#define FACE_ARC_QUARTER  90
/** The top left corner starts at the circle's left (9 o'clock), in LVGL's degrees. */
#define FACE_ARC_LEFT_AT  180
/** The top right corner starts at the circle's top (12 o'clock). */
#define FACE_ARC_RIGHT_AT 270
/** The straight between the corners starts where the left one ends. */
#define FACE_ARC_LINE_X   (FACE_ARC_EDGE + FACE_ARC_R)
/** A rounded end is a half circle of the line's half width. */
#define FACE_ARC_CAP      (FACE_ARC_W / 2)

_Static_assert(KGUI_OFF_GLASS_W == KBSP_LCD_H_RES,
               "the arc's model spans the board's glass");

/** The bolt is cut into this many triangles. */
#define BOLT_TRIANGLES    4U
/** A triangle has this many corners. */
#define TRIANGLE_CORNERS  3U
/** The bolt is drawn on a grid of this many units a side. */
#define BOLT_UNITS        48

/** The time: the clock face, in the page's own type colour. Shared with
 *  the strip (kgui_screen_face_strip()). */
static lv_style_t s_time;

/** The time's labels; NULL until the face is built. */
static lv_obj_t *s_part[FACE_PARTS];
/** What each label says: a label is written only when its text changes. */
static char s_shown[FACE_PARTS][FACE_PART_LEN];
/** Where each label stands in the time's box. */
static int32_t s_x[FACE_PARTS];
/** Whether the time is dimmed for a time that is not known. */
static bool s_unknown;
/** Whether the colon wears Medium Grey for an approximate time. */
static bool s_approx;
/** The things the burn-in shift moves, in that order; NULL until the face is built. */
static lv_obj_t *s_shifted[FACE_SHIFTS];
static lv_obj_t *s_set_pill;     /**< Yellow "Set the time", in the hour's place. */
static lv_obj_t *s_zone_pill;    /**< Yellow "Choose your time zone", in the date's. */
static bool      s_prompt_set;   /**< The set pill is asked for: the hour steps aside. */
static bool      s_prompt_zone;  /**< The zone pill is asked for. */
static lv_obj_t *s_knock_last;   /**< The word after the knock's dot: twice, or again. */
/** The date's line; NULL until the face is built. */
static lv_obj_t *s_date_line;
/** The battery's outline. */
static lv_obj_t *s_pile_body;
/** The battery's fill. */
static lv_obj_t *s_pile_fill;
/** The terminal beside the body. */
static lv_obj_t *s_pile_tip;
/** The bolt on the battery. */
static lv_obj_t *s_pile_bolt;
/** Whether a cell is known, so the battery is meant to be seen. */
static bool s_pile_wanted;
static lv_obj_t *s_notice;    /**< The notice line, in the date's place. */
static bool s_notice_up;      /**< A notice is on the glass, or fading out. */
static int32_t s_colon_x = -1;   /**< The hour's colon's centre: the dot and the day stand under it. */
static bool s_date_wanted;   /**< The date is shown: a few seconds on waking (the apocrifa's), hidden otherwise. */
/** The arc's box and everything on it; NULL until the face is built. */
static lv_obj_t *s_arc_box;
/** The arc's top left corner. */
static lv_obj_t *s_arc_left;
/** The arc's top right corner. */
static lv_obj_t *s_arc_right;
/** The straight between them, over its track. */
static lv_obj_t *s_arc_line;
/** "Turn off", where the date stands while the arc is up. */
static lv_obj_t *s_arc_words;
/** Whether the screen is stripped to the scene (kgui_screen_face_bare()). */
static bool s_bare;
/** Whether the arc is up. */
static bool s_arc_up;
/** Whether the knock's "Press ● again" stands in the date's place. */
static bool s_knock_up;       /**< The knock's row is on the glass, or fading out. */
static lv_obj_t *s_knock_dot; /**< Its blue dot, which beats. */

/**
 * @brief The widest of the ten digits in @p font.
 * @param font The face the time is set in.
 * @return Its advance in pixels.
 */
static int32_t widest_digit(const lv_font_t *font)
{
    int32_t widest = 0;

    for ( uint32_t c = (uint32_t)'0'; (uint32_t)'9' >= c; c++ )
    {
        int32_t const w = (int32_t)lv_font_get_glyph_width(font, c, 0U);
        if ( w > widest )
        {
            widest = w;
        }
    }
    return widest;
}

/**
 * @brief How far @p c stands up from the label's line to sit on the digits'
 *        middle.
 *
 * DM Sans draws its colon and its hyphen for lower case: their middles stand
 * well below the middle of a figure (11 px and 6 px at 104), so a colon set
 * on the digits' line reads as sunk. A digit stands where the face puts it;
 * anything else is raised by the distance between its own box's middle and
 * the zero's, measured from the face.
 *
 * @param font The clock face.
 * @param c    The character.
 * @return The lift in pixels, 0 for a digit.
 */
static int32_t glyph_lift(const lv_font_t *font, char c)
{
    lv_font_glyph_dsc_t zero;
    lv_font_glyph_dsc_t g;
    int32_t lift = 0;

    if ( (('0' > c) || ('9' < c))
        && lv_font_get_glyph_dsc(font, &zero, (uint32_t)'0', 0U)
        && lv_font_get_glyph_dsc(font, &g, (uint32_t)(uint8_t)c, 0U) )
    {
        /* Twice each middle's height over the baseline, so the halves of an
         * odd box survive the integer arithmetic; rounded to the nearest
         * pixel. */
        int32_t const zero2 = (2 * (int32_t)zero.ofs_y) + (int32_t)zero.box_h;
        int32_t const c2    = (2 * (int32_t)g.ofs_y) + (int32_t)g.box_h;

        lift = ((zero2 - c2) + 1) / 2;
    }
    return lift;
}

/**
 * @brief Where the digits' baseline stands so that the zero's ink starts at
 *        KGUI_FACE_HOUR_Y.
 * @param font The clock face.
 * @return The baseline's row, in glass pixels.
 */
static int32_t hour_baseline(const lv_font_t *font)
{
    lv_font_glyph_dsc_t zero;
    int32_t rise = 0;

    if ( lv_font_get_glyph_dsc(font, &zero, (uint32_t)'0', 0U) )
    {
        rise = (int32_t)zero.box_h + (int32_t)zero.ofs_y;
    }
    return KGUI_FACE_HOUR_Y + rise;
}

/**
 * @brief How far a text runs: each character's advance and the space after it.
 * @param font The clock face.
 * @param text The text.
 * @return Its width in pixels.
 */
static int32_t text_span(const lv_font_t *font, const char *text)
{
    int32_t span = 0;

    for ( const char *c = text; '\0' != *c; c++ )
    {
        span += (int32_t)lv_font_get_glyph_width(font, (uint32_t)(uint8_t)*c, 0U)
                + KGUI_FACE_HOUR_SPACE;
    }
    return span;
}

/**
 * @brief Put label @p i at @p x saying @p text - each only if it changed.
 * @param font The clock face.
 * @param i    The label.
 * @param text What it says.
 * @param x    Where it starts in the time's box.
 */
static void part_set(const lv_font_t *font, uint32_t i, const char *text, int32_t x)
{
    if ( strncmp(text, s_shown[i], FACE_PART_LEN) != 0 )
    {
        lv_label_set_text(s_part[i], text);
        lv_obj_set_y(s_part[i], -glyph_lift(font, text[0]));
        (void)snprintf(s_shown[i], FACE_PART_LEN, "%s", text);
    }
    if ( x != s_x[i] )
    {
        lv_obj_set_x(s_part[i], x);
        s_x[i] = x;
    }
}

void kgui_screen_face_date_place(void)
{
    if ( (NULL != s_date_line) && (0 <= s_colon_x) )
    {
        const char * const text = lv_label_get_text(s_date_line);
        size_t             from = 0U;
        size_t             to;
        char               part[24];
        lv_point_t         said;
        int32_t            before = 0;

        while ( ('\0' != text[from]) && (('0' > text[from]) || ('9' < text[from])) )
        {
            ++from;
        }
        to = from;
        while ( ('0' <= text[to]) && ('9' >= text[to]) )
        {
            ++to;
        }
        if ( (to == from) || (sizeof(part) <= to) )
        {
            /* No day in it ("Time not set"): it stands where the hour starts. */
            lv_obj_set_x(s_date_line, KGUI_FACE_DATE_X);
            return;
        }
        /* The whole up to the day's end less the day: LVGL measures a line
         * without its last space, and the one before the day counts. */
        (void)memcpy(part, text, to);
        part[to] = '\0';
        kgui_text_size(&said, part, &kui_font_body);
        before = said.x;
        kgui_text_size(&said, &part[from], &kui_font_body);
        before -= said.x;
        lv_obj_set_x(s_date_line, s_colon_x - (before + (said.x / 2)));
    }
}

/**
 * @brief Stand "Press ● again" so its dot falls right under the hour's colon
 *        (Luismi, 5-nov). The hour's width changes with its figures.
 *
 * @param colon The colon's centre, in the face's columns.
 */
static void knock_under_colon(int32_t colon)
{
    lv_obj_t * const row = s_shifted[FACE_SHIFT_KNOCK];

    if ( colon != s_colon_x )
    {
        s_colon_x = colon;
        kgui_screen_face_date_place();
    }

    if ( NULL != row )
    {
        lv_point_t said;

        kgui_text_size(&said, KGUI_FACE_KNOCK_WORD, &kui_font_body);
        lv_obj_set_x(row, colon - (said.x + KGUI_FACE_KNOCK_GAP + (KGUI_FACE_KNOCK_DOT_PX / 2)));
    }
}

void kgui_screen_face_time(int32_t hour, int32_t minute, bool h12, bool approx)
{
    const lv_font_t * const clock = &kui_font_clock;
    char hh[FACE_PART_LEN] = { '-', '-', '\0' };
    char mm[FACE_PART_LEN] = { '-', '-', '\0' };
    bool const known = kgui_clock_known(hour, minute);
    bool const dim   = !known;
    int32_t x;

    if ( NULL == s_part[0] )
    {
        return;   /* the face is not built yet: nothing to set */
    }
    if ( known )
    {
        int32_t const h = kgui_clock_hour(hour, h12);

        hh[0] = (char)('0' + (h / 10));
        hh[1] = (char)('0' + (h % 10));
        mm[0] = (char)('0' + (minute / 10));
        mm[1] = (char)('0' + (minute % 10));
        if ( h12 && (10 > h) )
        {
            /* One figure for the hour, the way a 12 h clock is read. */
            hh[0] = hh[1];
            hh[1] = '\0';
        }
    }
    if ( dim != s_unknown )
    {
        lv_opa_t opa = LV_OPA_COVER;

        if ( dim )
        {
            opa = KUI_OPA_PCT(FACE_UNKNOWN_PCT);
        }
        for ( uint32_t i = 0U; FACE_PARTS > i; i++ )
        {
            lv_obj_set_style_text_opa(s_part[i], opa, 0);
        }
        s_unknown = dim;
    }
    if ( approx != s_approx )
    {
        lv_color_t ink = KUI_COLOR_TEXT_ON_BG;

        if ( approx )
        {
            ink = KUI_COLOR_TEXT_MUTED;
        }
        lv_obj_set_style_text_color(s_part[FACE_COLON], ink, 0);
        s_approx = approx;
    }
    x = 0;
    part_set(clock, FACE_HOUR, hh, x);
    x += text_span(clock, hh);
    part_set(clock, FACE_COLON, ":", x);
    /* The colon's own width: its span counts the (negative) space after it. */
    knock_under_colon(KGUI_FACE_HOUR_X + x + ((text_span(clock, ":") - KGUI_FACE_HOUR_SPACE + 1) / 2));
    x += text_span(clock, ":");
    part_set(clock, FACE_MIN, mm, x);
}

/** @brief Show each pill as what is asked for and what else stands there say. */
static void prompt_place(void)
{
    if ( NULL != s_set_pill )
    {
        lv_obj_set_hidden(s_set_pill, (!s_prompt_set) || s_bare);
    }
    if ( NULL != s_zone_pill )
    {
        lv_obj_set_hidden(s_zone_pill, (!s_prompt_zone) || s_bare || s_arc_up);
    }
}

void kgui_screen_face_prompt(bool set_time, bool zone)
{
    s_prompt_set  = set_time;
    s_prompt_zone = zone;
    if ( NULL != s_shifted[FACE_SHIFT_TIME] )
    {
        lv_obj_set_hidden(s_shifted[FACE_SHIFT_TIME], s_bare || s_prompt_set);
    }
    prompt_place();
}

void kgui_screen_face_bare(bool bare)
{
    s_bare = bare;
    prompt_place();
    if ( NULL != s_shifted[FACE_SHIFT_TIME] )
    {
        lv_obj_set_hidden(s_shifted[FACE_SHIFT_TIME], bare || s_prompt_set);
    }
    if ( NULL != s_shifted[FACE_SHIFT_DATE] )
    {
        lv_obj_set_hidden(s_shifted[FACE_SHIFT_DATE], bare || s_arc_up || s_knock_up || !s_date_wanted);
    }
    if ( NULL != s_shifted[FACE_SHIFT_KNOCK] )
    {
        lv_obj_set_hidden(s_shifted[FACE_SHIFT_KNOCK], bare || s_arc_up || !s_knock_up);
    }
    if ( NULL != s_shifted[FACE_SHIFT_PILE] )
    {
        lv_obj_set_hidden(s_shifted[FACE_SHIFT_PILE], bare || !s_pile_wanted);
    }
}

/**
 * @brief The object an animation runs on: LVGL keeps it as `void *`.
 *
 * @param var The animation's `var`, which lv_anim_set_var() took as an object.
 * @return That object.
 */
static lv_obj_t *face_anim_obj(void *var)
{
    /* cppcheck-suppress misra-c2012-11.5 ; lv_anim_set_var() took an lv_obj_t, which LVGL hands back as void * */
    return (lv_obj_t *)var;
}

/** @brief An lv_anim's setter: an object's opacity. */
static void face_opa(void *o, int32_t v)
{
    lv_obj_set_style_opa(face_anim_obj(o), (lv_opa_t)v, LV_PART_MAIN);
}

void kgui_screen_face_dim(bool dim)
{
    lv_opa_t opa = (lv_opa_t)LV_OPA_COVER;

    if ( dim )
    {
        opa = (lv_opa_t)KGUI_PEEK_DIM_OPA;
    }

    for ( uint32_t i = FACE_SHIFT_TIME; FACE_SHIFT_PILE >= i; i++ )
    {
        if ( (FACE_SHIFT_SCENE != i) && (FACE_SHIFT_KNOCK != i) && (NULL != s_shifted[i]) )
        {
            lv_anim_t a;

            (void)lv_anim_delete(s_shifted[i], face_opa);
            lv_anim_init(&a);
            lv_anim_set_var(&a, s_shifted[i]);
            lv_anim_set_exec_cb(&a, face_opa);
            lv_anim_set_values(&a, lv_obj_get_style_opa(s_shifted[i], LV_PART_MAIN), opa);
            lv_anim_set_duration(&a, KGUI_VEIL_MS);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
            (void)lv_anim_start(&a);
        }
    }
}

void kgui_screen_face_date(bool shown)
{
    s_date_wanted = shown;
    if ( NULL != s_date_line )
    {
        lv_obj_set_hidden(s_date_line, (!shown) || s_bare || s_arc_up || s_knock_up);
    }
}
/** @brief Where the content stood when it started home: kgui_screen_face_home(). */
static int32_t s_home_x;
static int32_t s_home_y;

/** @brief Put the face's content at (dx, dy) from its place. */
static void face_shift_write(int32_t dx, int32_t dy)
{
    for ( uint32_t i = 0U; FACE_SHIFTS > i; i++ )
    {
        if ( NULL != s_shifted[i] )
        {
            lv_obj_set_style_translate_x(s_shifted[i], dx, LV_PART_MAIN);
            lv_obj_set_style_translate_y(s_shifted[i], dy, LV_PART_MAIN);
        }
    }
}

void kgui_screen_face_shift(int32_t dx, int32_t dy)
{
    (void)lv_anim_delete(&s_home_x, NULL);
    face_shift_write(dx, dy);
}

/** @brief An lv_anim's setter: the way home left, in 1/256ths of it. */
static void face_home_step(void *var, int32_t left)
{
    (void)var;
    face_shift_write(lv_map(left, 0, 256, 0, s_home_x), lv_map(left, 0, 256, 0, s_home_y));
}

void kgui_screen_face_home(uint32_t delay_ms, uint32_t ms)
{
    lv_obj_t const *from = NULL;

    for ( uint32_t i = 0U; (FACE_SHIFTS > i) && (NULL == from); i++ )
    {
        from = s_shifted[i];
    }
    (void)lv_anim_delete(&s_home_x, NULL);
    if ( NULL == from )
    {
        return;
    }
    s_home_x = lv_obj_get_style_translate_x(from, LV_PART_MAIN);
    s_home_y = lv_obj_get_style_translate_y(from, LV_PART_MAIN);
    if ( (0 != s_home_x) || (0 != s_home_y) )
    {
        lv_anim_t a;

        lv_anim_init(&a);
        lv_anim_set_var(&a, &s_home_x);
        lv_anim_set_exec_cb(&a, face_home_step);
        lv_anim_set_values(&a, 256, 0);
        lv_anim_set_delay(&a, delay_ms);
        lv_anim_set_duration(&a, ms);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        (void)lv_anim_start(&a);
    }
}

/**
 * @brief Draw the bolt: four triangles in the colour the pile asks for.
 *
 * The colour is the object's background colour, which the object never draws
 * itself: changing it repaints the bolt. The shape is kshell's icon_bolt, a
 * 24 px drawing, in half pixels: the top, the left step, the foot of the left
 * step, the tip of the foot, the right step and its notch, cut into four
 * triangles.
 *
 * @param e The draw event of the bolt's object.
 */
static void bolt_draw(lv_event_t *e)
{
    static const int32_t  BOLT[6][2] = {
        { 26, 5 }, { 11, 27 }, { 23, 27 }, { 21, 43 }, { 37, 21 }, { 25, 21 },
    };
    static const uint8_t  TRI[BOLT_TRIANGLES][TRIANGLE_CORNERS] = {
        { 0, 1, 5 }, { 1, 2, 5 }, { 2, 4, 5 }, { 2, 3, 4 },
    };
    lv_layer_t * const    layer = lv_event_get_layer(e);
    const lv_obj_t * const obj  = lv_event_get_target_obj(e);
    lv_area_t             box;
    lv_draw_triangle_dsc_t dsc;

    lv_obj_get_coords(obj, &box);
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
    dsc.opa   = LV_OPA_COVER;
    for ( uint32_t t = 0U; BOLT_TRIANGLES > t; t++ )
    {
        for ( uint32_t k = 0U; TRIANGLE_CORNERS > k; k++ )
        {
            int32_t const * const at = BOLT[TRI[t][k]];

            dsc.p[k].x = (lv_value_precise_t)box.x1
                         + (((lv_value_precise_t)at[0] * (lv_value_precise_t)KGUI_FACE_BOLT_PX)
                            / (lv_value_precise_t)BOLT_UNITS);
            dsc.p[k].y = (lv_value_precise_t)box.y1
                         + (((lv_value_precise_t)at[1] * (lv_value_precise_t)KGUI_FACE_BOLT_PX)
                            / (lv_value_precise_t)BOLT_UNITS);
        }
        lv_draw_triangle(layer, &dsc);
    }
}

void kgui_screen_face_pile(bool shown, uint8_t pct, kgui_pile_t kind, bool bolt)
{
    lv_color_t ink = KUI_COLOR_TEXT_MUTED;
    int32_t    fill;

    if ( NULL == s_shifted[FACE_SHIFT_PILE] )
    {
        return;
    }
    if ( KGUI_PILE_CHARGE == kind )
    {
        ink = KUI_COLOR_CHARGE;
    }
    else if ( KGUI_PILE_LOW == kind )
    {
        ink = KUI_COLOR_WARN;
    }
    else if ( KGUI_PILE_ALERT == kind )
    {
        ink = KUI_COLOR_DANGER;
    }
    else
    {
        /* Medium Grey. */
    }
    s_pile_wanted = shown;
    lv_obj_set_hidden(s_shifted[FACE_SHIFT_PILE], s_bare || !shown);
    fill = ((int32_t)KGUI_FACE_PILE_FILL_W * (int32_t)pct) / PILE_FULL_PCT;
    if ( KGUI_FACE_PILE_FILL_MIN > fill )
    {
        fill = KGUI_FACE_PILE_FILL_MIN;
    }
    if ( KGUI_FACE_PILE_FILL_W < fill )
    {
        fill = KGUI_FACE_PILE_FILL_W;
    }
    lv_obj_set_style_border_color(s_pile_body, ink, 0);
    lv_obj_set_style_bg_color(s_pile_fill, ink, 0);
    lv_obj_set_style_bg_color(s_pile_tip, ink, 0);
    lv_obj_set_width(s_pile_fill, fill);
    lv_obj_set_hidden(s_pile_bolt, !bolt);
    /* Black on the fill, but in the pile's colour where the fill does not
     * reach the bolt (a low cell on the cable). */
    if ( PILE_BOLT_ON_FILL_PX > fill )
    {
        lv_obj_set_style_bg_color(s_pile_bolt, ink, LV_PART_MAIN);
    }
    else
    {
        lv_obj_set_style_bg_color(s_pile_bolt, KUI_COLOR_BG, LV_PART_MAIN);
    }
}

/**
 * @brief One corner of the arc: a quarter of a circle with the line over its
 *        track.
 *
 * Both ends are rounded: the one on the glass's side is the arc's start, the
 * one at the straight is its leading end while it fills and, once the
 * straight is drawn over it, a cap lost in the line. An arc of no length
 * draws nothing (lv_draw_arc()), so the right corner leaves no dot behind
 * until the straight is whole.
 *
 * @param parent   The arc's box.
 * @param x        Where its box starts across.
 * @param rotation Where the quarter starts, in LVGL's degrees.
 * @return The arc.
 */
static lv_obj_t *arc_corner(lv_obj_t *parent, int32_t x, int32_t rotation)
{
    lv_obj_t *arc = lv_arc_create(parent);

    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, FACE_ARC_BOX, FACE_ARC_BOX);
    lv_obj_set_pos(arc, x, 0);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_scrollable(arc, false);
    lv_arc_set_range(arc, 0, (int32_t)KGUI_OFF_FULL_PM);
    lv_arc_set_bg_angles(arc, 0, FACE_ARC_QUARTER);
    lv_arc_set_rotation(arc, rotation);
    lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
    lv_obj_set_style_arc_width(arc, FACE_ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(KGUI_FACE_ARC_REST_HEX), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, FACE_ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, KUI_COLOR_DANGER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_arc_set_value(arc, 0);
    return arc;
}

void kgui_screen_face_arc(uint16_t pm)
{
    kgui_off_parts_t parts;
    bool const       up = (0U < pm);

    if ( NULL == s_arc_box )
    {
        return;
    }
    kgui_off_split(pm, &parts);
    lv_arc_set_value(s_arc_left, (int32_t)parts.left_pm);
    lv_arc_set_value(s_arc_right, (int32_t)parts.right_pm);
    lv_obj_set_width(s_arc_line, (int32_t)parts.line_px + (2 * FACE_ARC_CAP));
    lv_obj_set_hidden(s_arc_line, 0U == parts.line_px);
    if ( up != s_arc_up )
    {
        s_arc_up = up;
        lv_obj_set_hidden(s_arc_box, !up);
        lv_obj_set_hidden(s_arc_words, !up);
        prompt_place();
        lv_obj_set_hidden(s_date_line, up || s_bare || s_knock_up || !s_date_wanted);
        if ( NULL != s_shifted[FACE_SHIFT_KNOCK] )
        {
            lv_obj_set_hidden(s_shifted[FACE_SHIFT_KNOCK], up || s_bare || !s_knock_up);
        }
    }
}

/** @brief An lv_anim's setter: the notice's opacity. */
static void notice_opa(void *o, int32_t v)
{
    lv_obj_set_style_opa(face_anim_obj(o), (lv_opa_t)v, LV_PART_MAIN);
}

/** @brief An lv_anim's setter: the notice's rise. */
static void notice_y(void *o, int32_t v)
{
    lv_obj_set_style_translate_y(face_anim_obj(o), v, LV_PART_MAIN);
}

/** @brief The notice has faded out: the date that waited comes back. */
static void notice_gone(lv_anim_t *a)
{
    lv_obj_set_hidden(face_anim_obj(a->var), true);
    s_notice_up = false;
    if ( NULL != s_date_line )
    {
        lv_obj_set_hidden(s_date_line, (!s_date_wanted) || s_bare || s_arc_up || s_knock_up);
    }
}

void kgui_screen_face_notice(const char *text)
{
    lv_anim_t a;

    if ( NULL == s_notice )
    {
        return;
    }
    if ( (NULL == text) && !s_notice_up )
    {
        return;
    }
    (void)lv_anim_delete(s_notice, NULL);
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_notice);
    if ( NULL != text )
    {
        s_notice_up = true;
        lv_label_set_text(s_notice, text);
        lv_obj_set_hidden(s_notice, false);
        lv_obj_set_hidden(s_date_line, true);   /* same place: the notice wins */
        lv_anim_set_duration(&a, KGUI_NOTICE_IN_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_exec_cb(&a, notice_opa);
        (void)lv_anim_start(&a);
        lv_anim_set_values(&a, -6, 0);
        lv_anim_set_exec_cb(&a, notice_y);
        (void)lv_anim_start(&a);
    }
    else
    {
        lv_anim_set_duration(&a, KGUI_NOTICE_OUT_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
        lv_anim_set_values(&a, 0, -6);
        lv_anim_set_exec_cb(&a, notice_y);
        (void)lv_anim_start(&a);
        lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_exec_cb(&a, notice_opa);
        lv_anim_set_completed_cb(&a, notice_gone);
        (void)lv_anim_start(&a);
    }
}

/** @brief An lv_anim's setter: the knock's blue dot. */
static void knock_dot_opa(void *o, int32_t v)
{
    lv_obj_set_style_bg_opa(face_anim_obj(o), (lv_opa_t)v, LV_PART_MAIN);
}

/** @brief The knock's row has faded out: the date that waited comes back. */
static void knock_gone(lv_anim_t *a)
{
    lv_obj_set_hidden(face_anim_obj(a->var), true);
    s_knock_up = false;
    if ( NULL != s_knock_dot )
    {
        (void)lv_anim_delete(s_knock_dot, knock_dot_opa);
    }
    lv_obj_set_hidden(s_date_line, (!s_date_wanted) || s_bare || s_arc_up || s_notice_up);
}

void kgui_screen_face_knock_say(const char *last)
{
    if ( NULL != s_knock_last )
    {
        lv_label_set_text(s_knock_last, last);
    }
}

void kgui_screen_face_knock(bool shown)
{
    static bool          s_knock_want;   /**< It was asked for. */
    lv_obj_t * const row = s_shifted[FACE_SHIFT_KNOCK];
    lv_anim_t        a;

    if ( (NULL == row) || (shown == s_knock_want) )
    {
        return;
    }
    s_knock_want = shown;
    (void)lv_anim_delete(row, face_opa);
    lv_anim_init(&a);
    lv_anim_set_var(&a, row);
    lv_anim_set_exec_cb(&a, face_opa);
    if ( shown )
    {
        /* It comes in softly and the dot beats: press me. */
        lv_anim_t beat;

        if ( !s_knock_up )
        {
            lv_obj_set_style_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        }
        s_knock_up = true;
        lv_obj_set_hidden(row, s_bare || s_arc_up);
        lv_obj_set_hidden(s_date_line, true);
        lv_anim_set_values(&a, lv_obj_get_style_opa(row, LV_PART_MAIN), LV_OPA_COVER);
        lv_anim_set_duration(&a, KGUI_VEIL_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        (void)lv_anim_start(&a);
        (void)lv_anim_delete(s_knock_dot, knock_dot_opa);
        lv_anim_init(&beat);
        lv_anim_set_var(&beat, s_knock_dot);
        lv_anim_set_exec_cb(&beat, knock_dot_opa);
        lv_anim_set_values(&beat, LV_OPA_COVER, KGUI_KNOCK_BEAT_LOW_OPA);
        lv_anim_set_duration(&beat, KGUI_KNOCK_BEAT_HALF_MS);
        lv_anim_set_reverse_duration(&beat, KGUI_KNOCK_BEAT_HALF_MS);
        /* cppcheck-suppress misra-c2012-7.2 ; LV_ANIM_REPEAT_INFINITE is LVGL's 0xFFFFFFFF, written without the suffix */
        lv_anim_set_repeat_count(&beat, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&beat, lv_anim_path_ease_in_out);
        (void)lv_anim_start(&beat);
    }
    else
    {
        lv_anim_set_values(&a, lv_obj_get_style_opa(row, LV_PART_MAIN), LV_OPA_TRANSP);
        lv_anim_set_duration(&a, KGUI_KNOCK_OUT_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
        lv_anim_set_completed_cb(&a, knock_gone);
        (void)lv_anim_start(&a);
    }
}

/**
 * @brief A box with no look of its own: nothing to press, nothing to scroll.
 * @param parent The parent object.
 * @param name   Its name, for AO_Gui to find it by.
 * @return The box.
 */
static lv_obj_t *plain_box(lv_obj_t *parent, const char *name)
{
    lv_obj_t *box = lv_obj_create(parent);

    lv_obj_set_name(box, name);
    lv_obj_remove_style_all(box);
    lv_obj_set_clickable(box, false);
    lv_obj_set_scrollable(box, false);
    return box;
}

/**
 * @brief Round a size up to the next even number.
 * @param n A size in pixels.
 * @return @p n, or one more when it is odd: the panel's memory window is even.
 *         @p n is not negative.
 */
static int32_t even_up(int32_t n)
{
    return n + (n % STRIP_PAIR);
}

/**
 * @brief Put one glyph in a cell of the strip's scratch row.
 * @param row  The row object the cells stand on.
 * @param font The clock face.
 * @param c    The character.
 * @param x    The cell's left edge in the row.
 * @param w    The cell's width.
 */
static void strip_cell(lv_obj_t *row, const lv_font_t *font, char c,
                       int32_t x, int32_t w, int32_t dy)
{
    char const text[2] = { c, '\0' };
    lv_obj_t *label = lv_label_create(row);

    lv_obj_add_style(label, &s_time, 0);
    /* At the cell's left: the figures stand where the face's own do. */
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_obj_set_scrollable(label, false);
    lv_obj_set_width(label, w);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, dy - glyph_lift(font, c));
}

bool kgui_screen_face_strip(kgui_face_strip_t *out)
{
    /* The strip's digits, in order. */
    static const char s_strip_digits[STRIP_DIGITS] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    const lv_font_t * const clock = &kui_font_clock;
    int32_t const cell_w  = even_up(widest_digit(clock));
    int32_t const colon_w = even_up((int32_t)lv_font_get_glyph_width(clock, (uint32_t)':', 0U));
    int32_t const h       = even_up(lv_font_get_line_height(clock) + (int32_t)STRIP_PAIR);
    int32_t const strip_w = ((int32_t)STRIP_DIGITS * cell_w) + colon_w;
    int32_t const line_h  = lv_font_get_line_height(clock);
    int32_t const top     = hour_baseline(clock) - (line_h - clock->base_line);
    bool ok = false;

    out->buf = NULL;
    if ( NULL != kgui_screen_dark )
    {
        lv_draw_buf_t * const buf = lv_draw_buf_create((uint32_t)strip_w, (uint32_t)h,
                                                       LV_COLOR_FORMAT_RGB565,
                                                       LV_STRIDE_AUTO);

        /* The rows must be packed: the panel's side reads the strip as
         * 10 * glyph_w + colon_w pixels a row, and nothing else. */
        if ( (NULL != buf) && (buf->header.stride == ((uint32_t)strip_w * sizeof(uint16_t))) )
        {
            lv_obj_t *row = plain_box(kgui_screen_dark, "strip_row");
            lv_result_t res;

            lv_obj_set_size(row, strip_w, h);
            lv_obj_set_pos(row, 0, 0);
            lv_obj_set_style_bg_color(row, KUI_COLOR_BG, 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            for ( uint32_t d = 0U; STRIP_DIGITS > d; d++ )
            {
                strip_cell(row, clock, s_strip_digits[d],
                           (int32_t)d * cell_w, cell_w, top % (int32_t)STRIP_PAIR);
                out->adv[d] = (uint8_t)text_span(clock, (char const[2]){ s_strip_digits[d], '\0' });
            }
            strip_cell(row, clock, ':', (int32_t)STRIP_DIGITS * cell_w, colon_w,
                       top % (int32_t)STRIP_PAIR);
            out->adv[STRIP_DIGITS] = (uint8_t)text_span(clock, ":");
            lv_obj_update_layout(row);
            res = lv_snapshot_take_to_draw_buf(row, LV_COLOR_FORMAT_RGB565, buf);
            lv_obj_delete(row);
            if ( LV_RESULT_OK == res )
            {
                out->buf     = buf;
                out->glyph_w = (uint16_t)cell_w;
                out->glyph_h = (uint16_t)h;
                out->colon_w = (uint16_t)colon_w;
                out->x       = (uint16_t)KGUI_FACE_HOUR_X;
                out->y       = (uint16_t)top;
                ok = true;
            }
            else
            {
                lv_draw_buf_destroy(buf);
            }
        }
        else if ( NULL != buf )
        {
            lv_draw_buf_destroy(buf);
        }
        else
        {
            /* LVGL's heap refused the strip: the glass sleeps without a time. */
        }
    }
    if ( ok )
    {
        /* The panel's window starts on an even column and row. */
        out->x = (uint16_t)(out->x - (out->x % (uint16_t)STRIP_PAIR));
        out->y = (uint16_t)(out->y - (out->y % (uint16_t)STRIP_PAIR));
    }
    return ok;
}

void kgui_screen_face_strip_free(kgui_face_strip_t *strip)
{
    if ( NULL != strip->buf )
    {
        lv_draw_buf_destroy(strip->buf);
        strip->buf = NULL;
    }
}

/**
 * @brief The battery's pile, hidden until AO_Gui reads a cell.
 * @param scr The face.
 */
static void pile_build(lv_obj_t *scr)
{
    lv_obj_t *pile = plain_box(scr, "face_pile");

    s_shifted[FACE_SHIFT_PILE] = pile;
    lv_obj_set_size(pile, KGUI_FACE_PILE_W + KGUI_FACE_PILE_TIP_W, KGUI_FACE_PILE_H);
    lv_obj_set_pos(pile, KGUI_FACE_PILE_X, KGUI_FACE_PILE_Y);

    s_pile_body = plain_box(pile, "pile_body");
    lv_obj_set_size(s_pile_body, KGUI_FACE_PILE_W, KGUI_FACE_PILE_H);
    lv_obj_set_pos(s_pile_body, 0, 0);
    lv_obj_set_style_radius(s_pile_body, KGUI_FACE_PILE_RADIUS, 0);
    lv_obj_set_style_border_width(s_pile_body, KGUI_FACE_PILE_BORDER, 0);
    lv_obj_set_style_border_opa(s_pile_body, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_pile_body, KUI_COLOR_TEXT_MUTED, 0);

    s_pile_fill = plain_box(s_pile_body, "pile_fill");
    lv_obj_set_pos(s_pile_fill, KGUI_FACE_PILE_GAP, KGUI_FACE_PILE_GAP);
    lv_obj_set_size(s_pile_fill, KGUI_FACE_PILE_FILL_W, KGUI_FACE_PILE_FILL_H);
    lv_obj_set_style_radius(s_pile_fill, PILE_FILL_RADIUS, 0);
    lv_obj_set_style_bg_opa(s_pile_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_pile_fill, KUI_COLOR_TEXT_MUTED, 0);

    /* A sibling of the body: a child would be clipped by it. */
    s_pile_tip = plain_box(pile, "pile_tip");
    lv_obj_set_size(s_pile_tip, KGUI_FACE_PILE_TIP_W, KGUI_FACE_PILE_TIP_H);
    lv_obj_set_pos(s_pile_tip, KGUI_FACE_PILE_W, KGUI_FACE_PILE_TIP_Y);
    lv_obj_set_style_radius(s_pile_tip, PILE_TIP_RADIUS, 0);
    lv_obj_set_style_bg_opa(s_pile_tip, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_pile_tip, KUI_COLOR_TEXT_MUTED, 0);

    s_pile_bolt = plain_box(pile, "pile_bolt");
    lv_obj_set_size(s_pile_bolt, KGUI_FACE_BOLT_PX, KGUI_FACE_BOLT_PX);
    lv_obj_set_pos(s_pile_bolt,
                   (KGUI_FACE_PILE_W - KGUI_FACE_BOLT_PX) / 2,
                   (KGUI_FACE_PILE_H - KGUI_FACE_BOLT_PX) / 2);
    (void)lv_obj_add_event_cb(s_pile_bolt, bolt_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_set_hidden(s_pile_bolt, true);

    /* No cell is known yet: hidden until AO_Gui reads one. */
    s_pile_wanted = false;
    lv_obj_set_hidden(pile, true);
}

/**
 * @brief One word of the knock's row.
 * @param row  The row.
 * @param text The word.
 */
static lv_obj_t *knock_word(lv_obj_t *row, const char *text)
{
    lv_obj_t * const word = lv_label_create(row);

    lv_label_set_text(word, text);
    lv_obj_set_style_text_font(word, &kui_font_body, 0);
    lv_obj_set_style_text_color(word, KUI_COLOR_TEXT_ON_BG, 0);
    lv_obj_set_scrollable(word, false);
    return word;
}

/**
 * @brief "Press ● again" in the date's place: two words in the body's face,
 *        Light Grey, and between them the blue dot of the top button, as
 *        tall as a capital and standing on the line's baseline, a gap from
 *        each word. Hidden until the knock asks for it.
 * @param scr The face.
 */
static void knock_build(lv_obj_t *scr)
{
    lv_obj_t * const row  = plain_box(scr, "face_knock");
    int32_t const    lh   = lv_font_get_line_height(&kui_font_body);
    lv_obj_t        *dot;

    s_shifted[FACE_SHIFT_KNOCK] = row;
    lv_obj_set_pos(row, KGUI_FACE_DATE_X, KGUI_FACE_DATE_Y);
    lv_obj_set_size(row, kgui_size_content(), lh);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    /* Its dot under the hour's colon (knock_under_colon()). */
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, KGUI_FACE_KNOCK_GAP, 0);
    (void)knock_word(row, KGUI_FACE_KNOCK_WORD);
    dot = plain_box(row, "knock_dot");
    s_knock_dot = dot;
    lv_obj_set_size(dot, KGUI_FACE_KNOCK_DOT_PX, KGUI_FACE_KNOCK_DOT_PX);
    /* The dot's foot on the baseline: a capital's height above it. */
    lv_obj_set_style_margin_top(dot, lh - kui_font_body.base_line - KGUI_FACE_KNOCK_DOT_PX, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    /* The blue of the top button, the one that wakes the Dot (Luismi, 5-nov). */
    lv_obj_set_style_bg_color(dot, KUI_COLOR_ACCENT, 0);
    s_knock_last = knock_word(row, "twice");
    lv_obj_set_hidden(row, true);
}

/**
 * @brief The apocrifa's yellow pills: the only yellow on the face, and the
 *        ones that ask for what the clock lacks (kshell_face.c).
 *
 * "Set the time", in the hour's place, and "Choose your time zone", over the
 * date's, its words starting where the date's do.
 *
 * @param scr The face.
 */
static void prompt_build(lv_obj_t *scr)
{
    lv_obj_t *words;

    s_set_pill = plain_box(scr, "face_set_time");
    lv_obj_set_pos(s_set_pill, KGUI_FACE_HOUR_X, KGUI_FACE_HOUR_Y + 2);
    lv_obj_set_size(s_set_pill, FACE_SET_PILL_W, FACE_SET_PILL_H);
    lv_obj_set_style_radius(s_set_pill, FACE_SET_PILL_H / 2, 0);
    lv_obj_set_style_bg_color(s_set_pill, lv_color_hex(KUI_PALETTE_BOLD_YELLOW), 0);
    lv_obj_set_style_bg_opa(s_set_pill, LV_OPA_COVER, 0);
    words = lv_label_create(s_set_pill);
    lv_obj_set_style_text_font(words, &kui_font_title, 0);
    lv_obj_set_style_text_color(words, KUI_COLOR_TEXT, 0);
    lv_label_set_text_static(words, "Set the time");
    lv_obj_center(words);
    lv_obj_set_hidden(s_set_pill, true);

    s_zone_pill = plain_box(scr, "face_set_zone");
    lv_obj_set_pos(s_zone_pill, KGUI_FACE_DATE_X - FACE_ZONE_PILL_PAD, KGUI_FACE_DATE_Y - 2);
    lv_obj_set_size(s_zone_pill, kgui_size_content(), FACE_ZONE_PILL_H);
    lv_obj_set_style_radius(s_zone_pill, FACE_ZONE_PILL_H / 2, 0);
    lv_obj_set_style_bg_color(s_zone_pill, lv_color_hex(KUI_PALETTE_BOLD_YELLOW), 0);
    lv_obj_set_style_bg_opa(s_zone_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_zone_pill, FACE_ZONE_PILL_PAD, 0);
    words = lv_label_create(s_zone_pill);
    lv_obj_set_style_text_font(words, &kui_font_name, 0);
    lv_obj_set_style_text_color(words, KUI_COLOR_TEXT, 0);
    lv_label_set_text_static(words, "Choose your time zone");
    lv_obj_align(words, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_hidden(s_zone_pill, true);
}

/**
 * @brief The red arc: two corners and the straight between them, with "Turn
 *        off" in the date's place; hidden until the hold asks for it.
 * @param scr The face.
 */
static void arc_build(lv_obj_t *scr)
{
    lv_obj_t *track;

    s_arc_box = plain_box(scr, "face_arc");
    lv_obj_set_pos(s_arc_box, 0, 0);
    /* A cap's room below the corners' ends: the round ends stand there, and
     * the box clips what is outside it. */
    lv_obj_set_size(s_arc_box, (int32_t)KBSP_LCD_H_RES, FACE_ARC_EDGE + FACE_ARC_R + FACE_ARC_CAP);

    /* The grey straight is the bottom of the pile: a corner's red cap, which
     * runs a cap's width past where the share ends, lies over it instead of
     * being cut square by it. */
    track = plain_box(s_arc_box, "arc_track");
    lv_obj_set_pos(track, FACE_ARC_LINE_X, 0);
    lv_obj_set_size(track, FACE_ARC_STRAIGHT, FACE_ARC_W);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(KGUI_FACE_ARC_REST_HEX), 0);

    s_arc_left  = arc_corner(s_arc_box, 0, FACE_ARC_LEFT_AT);
    s_arc_right = arc_corner(s_arc_box, (int32_t)KBSP_LCD_H_RES - FACE_ARC_BOX, FACE_ARC_RIGHT_AT);

    s_arc_line = plain_box(s_arc_box, "arc_line");
    /* The red straight is rounded at both ends, and so each end is a cap
     * of its own: it starts a cap's width inside the left corner and runs a
     * cap's width past where the share ends, so the half circles lie on
     * the corners' own band (where the line runs level) and the leading end
     * is round without a notch at either join. */
    lv_obj_set_pos(s_arc_line, FACE_ARC_LINE_X - FACE_ARC_CAP, 0);
    lv_obj_set_size(s_arc_line, 2 * FACE_ARC_CAP, FACE_ARC_W);
    lv_obj_set_style_radius(s_arc_line, FACE_ARC_CAP, 0);
    lv_obj_set_style_bg_opa(s_arc_line, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_arc_line, KUI_COLOR_DANGER, 0);
    lv_obj_set_hidden(s_arc_line, true);
    lv_obj_set_hidden(s_arc_box, true);

    s_arc_words = lv_label_create(scr);
    lv_obj_set_name(s_arc_words, "face_off_words");
    lv_obj_set_pos(s_arc_words, KGUI_FACE_DATE_X, KGUI_FACE_DATE_Y);
    lv_label_set_text(s_arc_words, "Turn off");
    lv_obj_set_style_text_font(s_arc_words, &kui_font_body, 0);
    lv_obj_set_style_text_color(s_arc_words, KUI_COLOR_TEXT_ON_BG, 0);
    lv_obj_set_scrollable(s_arc_words, false);
    lv_obj_set_hidden(s_arc_words, true);
}

void kgui_screen_face_build(void)
{
    /* The labels' names, left to right. */
    static const char *const s_face_part_names[FACE_PARTS] = {
        "face_h", "face_colon", "face_m",
    };
    /* The date: the body face, in the Medium Grey. */
    static lv_style_t s_date;
    const lv_font_t * const clock = &kui_font_clock;
    /* As wide as the widest time, 00:00: four of the widest digit, the
     * colon and the space after each of the five characters. */
    int32_t const time_w  = (4 * (widest_digit(clock) + KGUI_FACE_HOUR_SPACE))
                            + (int32_t)lv_font_get_glyph_width(clock, (uint32_t)':', 0U)
                            + KGUI_FACE_HOUR_SPACE;
    int32_t const line_h  = lv_font_get_line_height(clock);

    lv_style_init(&s_time);
    lv_style_set_text_font(&s_time, clock);
    lv_style_set_text_color(&s_time, KUI_COLOR_TEXT_ON_BG);
    lv_style_set_text_letter_space(&s_time, KGUI_FACE_HOUR_SPACE);

    lv_style_init(&s_date);
    lv_style_set_text_font(&s_date, &kui_font_body);
    lv_style_set_text_color(&s_date, KUI_COLOR_TEXT_MUTED);

    kgui_screen_face = lv_obj_create(NULL);
    lv_obj_t *scr = kgui_screen_face;
    lv_obj_set_name_static(scr, "screen_face_#");
    lv_obj_add_style(scr, &kui_style_screen, 0);
    lv_obj_set_scrollable(scr, false);

    /* First, so the time and the date draw over whatever Kody does. */
    lv_obj_t *scene = plain_box(scr, "face_scene");
    s_shifted[FACE_SHIFT_SCENE] = scene;
    lv_obj_set_size(scene, (int32_t)KBSP_LCD_H_RES, KGUI_FACE_SCENE_H);
    lv_obj_set_pos(scene, 0, (int32_t)KBSP_LCD_V_RES - KGUI_FACE_SCENE_H);

    /* The line box is placed so that its baseline - line_height less
     * base_line from its top - falls where the zero's ink starts at
     * KGUI_FACE_HOUR_Y, whatever the face measures. */
    lv_obj_t *time = plain_box(scr, "face_time");
    s_shifted[FACE_SHIFT_TIME] = time;
    lv_obj_set_size(time, time_w, line_h);
    lv_obj_set_pos(time, KGUI_FACE_HOUR_X,
                   hour_baseline(clock) - (line_h - clock->base_line));
    for ( uint32_t i = 0U; FACE_PARTS > i; i++ )
    {
        lv_obj_t *part = lv_label_create(time);

        lv_obj_set_name(part, s_face_part_names[i]);
        lv_obj_add_style(part, &s_time, 0);
        lv_obj_set_scrollable(part, false);
        s_part[i]     = part;
        s_shown[i][0] = '\0';
        s_x[i]        = -1;
    }
    s_unknown = false;
    s_approx  = false;
    kgui_screen_face_time(0, 0, false, false);

    lv_obj_t *date = lv_label_create(scr);
    s_shifted[FACE_SHIFT_DATE] = date;
    s_date_line = date;
    lv_obj_set_name(date, "face_date");
    lv_obj_set_width(date, KGUI_FACE_DATE_W);
    lv_obj_set_pos(date, KGUI_FACE_DATE_X, KGUI_FACE_DATE_Y);
    lv_label_set_long_mode(date, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(date, "Thu 1 Jan");
    lv_obj_add_style(date, &s_date, 0);
    lv_obj_set_height(date, lv_font_get_line_height(&kui_font_body));
    lv_obj_set_scrollable(date, false);

    s_notice = lv_label_create(scr);
    lv_obj_set_name(s_notice, "face_notice");
    lv_obj_set_width(s_notice, KGUI_FACE_NOTICE_W);
    lv_obj_set_pos(s_notice, KGUI_FACE_DATE_X, KGUI_FACE_DATE_Y);
    lv_label_set_long_mode(s_notice, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(s_notice, "");
    lv_obj_set_style_text_font(s_notice, &kui_font_body, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_notice, KUI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_scrollable(s_notice, false);
    lv_obj_set_hidden(s_notice, true);

    pile_build(scr);
    knock_build(scr);

    /* Last, so they are drawn over every scene Kody plays. */
    arc_build(scr);
    prompt_build(scr);

    /* The page the glass holds while the board sleeps: the kit's black, and
     * nothing on it. */
    kgui_screen_dark = lv_obj_create(NULL);
    lv_obj_set_name_static(kgui_screen_dark, "screen_dark_#");
    lv_obj_add_style(kgui_screen_dark, &kui_style_screen, 0);
    lv_obj_set_scrollable(kgui_screen_dark, false);
}
