/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_hint.c
 * @brief The hint (kgui_hint.h).
 */
#include "kgui_hint.h"

#include "kbsp_board.h"
#include "kode_log.h"
#include "kui.h"
#include "kgui_screens.h"

/** @brief Log tag: kgui's own. */
#define TAG "kgui"

#define HINT_CENTER_DY_PX  ((KGUI_TOP_BAND_PX / 2) - ((int32_t)KBSP_LCD_V_RES / 2)) /**< The hint's centre from the top layer's centre: the middle of the top band's rows. */
#define HINT_PAD_X_PX  16            /**< The hint's side padding: it reads as a capsule. */
#define HINT_PAD_Y_PX  6             /**< The hint's top and bottom padding. */
#define HINT_BG_PCT    90            /**< How solid the hint's black is: pills and Kody show faintly through it. */
#define HINT_MAX_W_PX  KUI_PILL_W    /**< The widest box a label that wears the hint's look takes: the width of a pill (kgui_hint_dress()). */
#define HINT_LINES_ONE  1            /**< A hint of one line: how wide its words may be is measured for it first (hint_fit()). */

static lv_obj_t   *s_hint;           /**< The hint, over the top band's rows, on the top layer. NULL until built. */
static lv_timer_t *s_hint_timer;     /**< Takes the hint away; paused while none is up or one is held. */
static const char *s_hint_held;      /**< The words of the hint held until it is cleared (kgui_hint_hold()), or NULL. */
static bool        s_hint_timed;     /**< A hint is up that ends by itself. */
static uint32_t    s_hint_from_ms;   /**< lv_tick_get() when it began. */
static uint32_t    s_hint_for_ms;    /**< How long it lasts. */

/**
 * @brief Take the hint away and stop its clock; a held hint is let go.
 */
static void hint_take_away(void)
{
    s_hint_held  = NULL;
    s_hint_timed = false;
    if ( NULL != s_hint_timer )
    {
        lv_timer_pause(s_hint_timer);
    }
    if ( NULL != s_hint )
    {
        lv_obj_set_hidden(s_hint, true);
    }
}

/**
 * @brief How wide a line of the hint may be so that its words stay on the
 *        visible glass.
 *
 * The lines stand centred in the top band's rows, so a hint of @p lines lines
 * occupies the rows from the middle less half their height, and the glass is
 * narrowest at the first of them (kui_glass_inset()).
 *
 * @param lines How many lines the hint takes.
 * @return Pixels from side to side of the glass at the first row of the text.
 */
static int32_t hint_line_w(int32_t lines)
{
    kui_glass_t const glass = kui_glass_kode_dot();
    int32_t const     text  = lines * (int32_t)lv_font_get_line_height(&kui_font_body);
    int32_t const     first = (KGUI_TOP_BAND_PX - text) / 2;

    return glass.w - (2 * kui_glass_inset(&glass, first));
}

/**
 * @brief Size the hint's box to its words: one line as wide as the words when
 *        they fit the glass on one line, else the lines they wrap into.
 *
 * LVGL measures a label's height with the width the label has, so a box left
 * to size itself to the words cuts a second line off. The width is set here
 * from the words, once they are in the label, and the label holds at most
 * KGUI_HINT_LINES_MAX lines (kgui_hint_create()). A one-line hint is the box
 * it was: as wide as its words and the padding.
 *
 * @param words The line, as put in the label.
 */
static void hint_fit(const char *words)
{
    lv_point_t size;
    int32_t    line_w = hint_line_w(HINT_LINES_ONE);

    lv_text_get_size(&size, words, &kui_font_body, 0, 0, line_w,
                     LV_TEXT_FLAG_NONE);
    if ( size.y > lv_font_get_line_height(&kui_font_body) )
    {
        /* The words wrapped at one line's width: they take the lines of
         * KGUI_HINT_LINES_MAX, which stand higher up the glass and so on a
         * narrower stretch of it. */
        line_w = hint_line_w(KGUI_HINT_LINES_MAX);
        lv_text_get_size(&size, words, &kui_font_body, 0, 0, line_w,
                         LV_TEXT_FLAG_NONE);
    }
    lv_obj_set_width(s_hint, size.x + (2 * HINT_PAD_X_PX));
}

/**
 * @brief Put the words over the top band's rows: the hint is shown.
 *
 * The caller starts or stops the clock.
 *
 * @param words The line.
 */
static void hint_put(const char *words)
{
    lv_label_set_text(s_hint, words);
    hint_fit(words);
    lv_obj_set_hidden(s_hint, false);
}

/**
 * @brief The hint's time is up.
 *
 * @param timer The hint's timer.
 */
static void hint_expired(lv_timer_t *timer)
{
    (void)timer;
    hint_take_away();
}

bool kgui_hint_create(lv_obj_t *parent)
{
    if ( NULL == s_hint )
    {
        s_hint       = lv_label_create(parent);
        s_hint_timer = lv_timer_create(hint_expired, KGUI_HINT_MS, NULL);
        if ( (NULL != s_hint) && (NULL != s_hint_timer) )
        {
            kgui_hint_dress(s_hint);
            lv_label_set_max_lines(s_hint, KGUI_HINT_LINES_MAX);
            lv_obj_set_clickable(s_hint, false);
            lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, HINT_CENTER_DY_PX);
            lv_obj_set_hidden(s_hint, true);
            lv_timer_pause(s_hint_timer);
        }
        else
        {
            KLOG_E(TAG, "no room in LVGL's pool for the hint");
            if ( NULL != s_hint_timer )
            {
                lv_timer_delete(s_hint_timer);
            }
            if ( NULL != s_hint )
            {
                lv_obj_delete(s_hint);
            }
            s_hint_timer = NULL;
            s_hint       = NULL;
        }
    }
    return NULL != s_hint;
}

void kgui_hint_dress(lv_obj_t *label)
{
    static lv_style_t style;       /* The hint's look: a dark capsule round small cream words. */
    static bool       style_ready; /* style has been initialised. */

    if ( !style_ready )
    {
        lv_style_init(&style);
        lv_style_set_max_width(&style, HINT_MAX_W_PX);
        lv_style_set_text_font(&style, &kui_font_body);
        lv_style_set_text_color(&style, KUI_COLOR_TEXT_ON_BG);
        lv_style_set_text_align(&style, LV_TEXT_ALIGN_CENTER);
        lv_style_set_bg_color(&style, KUI_COLOR_BG);
        lv_style_set_bg_opa(&style, KUI_OPA_PCT(HINT_BG_PCT));
        lv_style_set_radius(&style, KUI_RADIUS_GLASS);
        lv_style_set_pad_hor(&style, HINT_PAD_X_PX);
        lv_style_set_pad_ver(&style, HINT_PAD_Y_PX);
        style_ready = true;
    }
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(label, &style, LV_PART_MAIN);
}

void kgui_hint_for(const char *words, uint32_t ms)
{
    if ( NULL != words )
    {
        KLOG_I(TAG, "hint: %s", words);
        s_hint_held = NULL;
        if ( NULL != s_hint )
        {
            hint_put(words);
            lv_timer_set_period(s_hint_timer, ms);
            lv_timer_reset(s_hint_timer);
            lv_timer_resume(s_hint_timer);
            s_hint_timed   = true;
            s_hint_from_ms = lv_tick_get();
            s_hint_for_ms  = ms;
        }
    }
}

void kgui_hint(const char *words)
{
    kgui_hint_for(words, KGUI_HINT_MS);
}

uint32_t kgui_hint_left_ms(void)
{
    uint32_t left = KGUI_HINT_NONE;

    if ( s_hint_timed )
    {
        uint32_t const gone = lv_tick_elaps(s_hint_from_ms);

        left = 0U;
        if ( gone < s_hint_for_ms )
        {
            left = s_hint_for_ms - gone;
        }
    }
    return left;
}

void kgui_hint_dismiss(void)
{
    if ( (NULL != s_hint) && !lv_obj_is_hidden(s_hint) )
    {
        hint_take_away();
    }
}

void kgui_hint_hold(const char *words)
{
    if ( (NULL != words) && (s_hint_held != words) )
    {
        KLOG_I(TAG, "hint: %s", words);
        if ( NULL != s_hint )
        {
            hint_put(words);
            lv_timer_pause(s_hint_timer);
            s_hint_held  = words;
            s_hint_timed = false;
        }
    }
}

void kgui_hint_clear(void)
{
    if ( NULL != s_hint_held )
    {
        hint_take_away();
    }
}
