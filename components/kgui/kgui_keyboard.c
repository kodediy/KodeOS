/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_keyboard.c
 * @brief The keypad card (kgui_keyboard.h): its objects, its layout and the field it draws.
 */
#include "kgui_keyboard.h"

#include <stdio.h>
#include <string.h>

#include "kbsp_board.h"
#include "kui.h"
#include "kgui_screens.h"

/* ---- The layout ------------------------------------------------------------- */

#define KBD_CARD_W        ((int32_t)KBSP_LCD_H_RES)  /**< The card is as wide as the glass. */
#define KBD_SIDE_PX       18                                           /**< The margin the field, the switch and the keys keep from the glass sides (kshell's 18). */
#define KBD_GAP_PX        10                                           /**< Between two keys across, and between the field and the pill. */
#define KBD_TITLE_Y       24                                           /**< Where the network's name starts: kshell's title row. */
#define KBD_TITLE_W       260                        /**< How wide the name may be before it is cut with dots: clear of the card's top corners at its row. */
#define KBD_FIELD_Y       44                                           /**< The field's top: the card's first row, no title over it (Luismi, 4-nov). */
#define KBD_FIELD_X       (KBD_SIDE_PX + KBD_FIELD_INSET)               /**< The field's left: inset so its focus ring stays inside the glass's top corners (review of 5-nov). */
#define KBD_FIELD_INSET   16                                           /**< How far in from the keys' sides the field stands, each side. */
#define KBD_FIELD_H       64                                           /**< The field's height, and the pill's: one capsule (kshell's 64). */
#define KBD_GO_W          200                                          /**< The yellow pill's width: centred under the keys, the primary action at the foot (ui-dot). */
#define KBD_GO_Y          418                                          /**< The yellow pill's top, under the keys and far from the field's Show (Luismi, 4-nov). */
#define KBD_FIELD_W       (KBD_ROW_W - (2 * KBD_FIELD_INSET))          /**< The field takes the row less its insets: the password has the room. */
#define KBD_SHOW_WORD_W   64                                           /**< The field's right end keeps this for its "Show" or "Hide". */
#define KBD_FIELD_PAD     20                         /**< Between the field's end and its first character. */
#define KBD_SHOW_Y        KBD_FIELD_Y                                  /**< Show is the field itself: a tap on it shows or hides the password. */
#define KBD_SHOW_H        KBD_FIELD_H                                  /**< ...as tall as the field. */
#define KBD_SHOW_PAD      14                         /**< Between the row's end and its word, and its switch. */
#define KBD_KEYS_Y        124                                          /**< The first row of keys' top. */
#define KBD_KEY_H         64                                           /**< A key's height. */
#define KBD_KEY_COLS      3                          /**< Keys across. */
#define KBD_KEY_ROWS      4                          /**< Rows of keys. */
#define KBD_KEY_W         ((KBD_ROW_W - ((KBD_KEY_COLS - 1) * KBD_GAP_PX)) / KBD_KEY_COLS) /**< A key's width: the row shared by three (kshell's 118). */
#define KBD_ROW_W         374                                          /**< The width of the field row and the keys: the glass less the side margin twice. */
#define KBD_KEY_GAP_Y     10                                           /**< Between two rows of keys. */
#define KBD_KEYS_BOTTOM   (KBD_KEYS_Y + (KBD_KEY_ROWS * KBD_KEY_H) + ((KBD_KEY_ROWS - 1) * KBD_KEY_GAP_Y)) /**< Where the last row of keys ends. */
#define KBD_FOOT_MIN      28                         /**< The least card left under the last row, for a capsule at the side margin to clear the glass's corner. */
#define KBD_LABEL_GAP     6  
#define KBD_KEY_INK_PAD   6                          /**< The room a key keeps clear of its edges: its letters are cut to fit what is left beside the digit. */
                        /**< Between a key's digit and its letters. */
#define KBD_ROW_SHOW      0U                         /**< The focus row of the field's Show, at the top. */
#define KBD_FIRST_KEY_ROW 1U                         /**< The focus row of the first row of keys. */
#define KBD_ROW_GO        5U                         /**< The focus row of the yellow pill, under the keys. */
#define KBD_FOCUS_ROWS    6U                         /**< Rows the focus walks: Show, four of keys and the pill. */
#define KBD_LAST_COL      2U                         /**< The last column of keys. */

/* ---- The field -------------------------------------------------------------- */

#define KBD_LETTER_SPACE  3                          /**< Between two plain characters of the field. */
#define KBD_DOT_D         10                         /**< A hidden character's dot: its diameter. */
#define KBD_DOT_PITCH     14                         /**< From one dot to the next. */
#define KBD_DOT_DY        1                          /**< How far under the field's middle a dot stands, to meet the middle of the lower-case letters. */
#define KBD_CARET_W       3                          /**< The caret's width. */
#define KBD_CARET_H       30                         /**< The caret's height. */
#define KBD_CARET_R       2                          /**< The caret's corner. */
#define KBD_CARET_GAP     3                          /**< Between the last character and the caret. */
#define KBD_PH_GAP        6                          /**< Between the caret and what the empty field asks for. */
#define KBD_ROOM_PX       (KBD_FIELD_W - (2 * KBD_FIELD_PAD) - KBD_CARET_W - KBD_CARET_GAP - KBD_SHOW_WORD_W) /**< The width the characters may take, with the caret after them and Show at the end. */
#define KBD_DOTS_MAX      16U                        /**< How many dots fit in the field with no plain character after them: the room over the pitch, rounded down. */
#define KBD_SHOWN_MAX     (KUI_MULTITAP_TEXT_MAX + 1U) /**< The plain characters the field shows at most, and the terminator. */

/* ---- The switch, the letters and the dimmed pill ---------------------------- */

#define KBD_SW_W          46                         /**< The Show password switch's width. */
#define KBD_SW_H          26                         /**< Its height. */
#define KBD_KNOB          20                         /**< Its knob's diameter. */
#define KBD_KNOB_INSET    3                          /**< Between the knob and the switch's end. */
#define KBD_LETTERS_PCT   70                         /**< How much of the ink a key's letters keep, as the kit's second lines do. */
#define KBD_DIM_PCT       40                         /**< How much of itself the yellow pill keeps while the keypad is held. */
#define KBD_PRESSED_PCT   35                         /**< How solid the Show password row's ground is while it is pressed. */
#define KBD_LETTERS_MAX   12U                        /**< Room for a key's letters and the terminator. */

_Static_assert((KBD_KEY_COLS * KBD_KEY_ROWS) == (int)KUI_MULTITAP_KEYS, "the keypad has the multitap's twelve keys");
_Static_assert(((KBD_KEY_COLS * KBD_KEY_W) + ((KBD_KEY_COLS - 1) * KBD_GAP_PX)) == KBD_ROW_W, "three keys and their gaps are one row");
_Static_assert((KBD_KEYS_BOTTOM + KBD_FOOT_MIN) <= (int32_t)KBSP_LCD_V_RES, "the last row of keys must clear the card's foot");
_Static_assert(KBD_KEYS_Y > (KBD_FIELD_Y + KBD_FIELD_H), "the keys stand under the field");
_Static_assert(KBD_GO_Y > KBD_KEYS_BOTTOM, "the pill stands under the keys");
_Static_assert((KBD_GO_Y + KBD_FIELD_H) <= (int32_t)KBSP_LCD_V_RES, "the pill stays on the glass");
_Static_assert(KBD_FIELD_Y > KGUI_KBD_HEAD_FOOT_Y, "the field stands under the header");
_Static_assert(KBD_ROOM_PX > 0, "the field has room for a character");
_Static_assert((((int)KBD_DOTS_MAX * KBD_DOT_PITCH) <= KBD_ROOM_PX) && ((((int)KBD_DOTS_MAX + 1) * KBD_DOT_PITCH) > KBD_ROOM_PX), "the dots are as many as the room holds");
_Static_assert(((unsigned int)KGUI_KBD_CTL_K1 + KUI_MULTITAP_KEYS) == (unsigned int)KGUI_KBD_CTL_COUNT, "the controls are the pill, the switch and the twelve keys");
_Static_assert((KBD_LAST_COL + 1U) == (unsigned int)KBD_KEY_COLS, "the last column is the third");
_Static_assert(KBD_FIRST_KEY_ROW == (KBD_ROW_SHOW + 1U), "the rows of keys follow Show's");
_Static_assert(KBD_ROW_GO == (KBD_FIRST_KEY_ROW + (unsigned int)KBD_KEY_ROWS), "the pill follows the rows of keys");
_Static_assert(KBD_FOCUS_ROWS == (KBD_ROW_GO + 1U), "the focus walks Show, the rows of keys and the pill");

/* ---- What the keys say ------------------------------------------------------ */

/** The word of each mode on the mode key, in the multitap's order. */
static const char *const MODE_WORD[KUI_MULTITAP_MODES] = { "abc", "ABC", "123", "#+=" };

/** The digit each character key wears large, indexed by the key. */
static const char *const DIGIT[KUI_MULTITAP_CHAR_KEYS] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };

/** The key each place of the keypad types, row by row: 1 to 9, then the mode key, 0 and Delete. */
static const kui_multitap_key_t KEY_OF[KUI_MULTITAP_KEYS] = {
    KUI_MULTITAP_KEY_1, KUI_MULTITAP_KEY_2, KUI_MULTITAP_KEY_3,
    KUI_MULTITAP_KEY_4, KUI_MULTITAP_KEY_5, KUI_MULTITAP_KEY_6,
    KUI_MULTITAP_KEY_7, KUI_MULTITAP_KEY_8, KUI_MULTITAP_KEY_9,
    KUI_MULTITAP_KEY_MODE, KUI_MULTITAP_KEY_0, KUI_MULTITAP_KEY_DELETE
};

/** Every control, indexed by its own value: what a loop over the controls reads them from. */
static const kgui_kbd_ctl_t CTL_ALL[KGUI_KBD_CTL_COUNT] = {
    KGUI_KBD_CTL_GO, KGUI_KBD_CTL_SHOW,
    KGUI_KBD_CTL_K1, KGUI_KBD_CTL_K2, KGUI_KBD_CTL_K3,
    KGUI_KBD_CTL_K4, KGUI_KBD_CTL_K5, KGUI_KBD_CTL_K6,
    KGUI_KBD_CTL_K7, KGUI_KBD_CTL_K8, KGUI_KBD_CTL_K9,
    KGUI_KBD_CTL_MODE, KGUI_KBD_CTL_K0, KGUI_KBD_CTL_DELETE
};

/* ---- The state --------------------------------------------------------------- */

/** What the keypad has made on the card; every pointer is NULL while it is not open. */
typedef struct {
    lv_obj_t *root;                         /**< The transparent object that covers the card. */
    lv_obj_t *title;                        /**< The network's name. */
    lv_obj_t *field;                        /**< The dark capsule. */
    lv_obj_t *ph;                           /**< What the empty field asks for, or the word of a hold. */
    lv_obj_t *txt;                          /**< The plain characters the field shows. */
    lv_obj_t *dot[KBD_DOTS_MAX];            /**< The dots of the hidden characters, in a row from the field's left. */
    lv_obj_t *caret;                        /**< The yellow caret. */
    lv_obj_t *go_txt;                       /**< The yellow pill's word. */
    lv_obj_t *sw_track;                     /**< The Show password switch's track. */
    lv_obj_t *sw_knob;                      /**< Its knob. */
    lv_obj_t *ctl[KGUI_KBD_CTL_COUNT];      /**< Each control, by its value. */
    lv_obj_t *main[KUI_MULTITAP_KEYS];      /**< Each key's large word: its digit, or the word of the mode key or Delete. */
    lv_obj_t *small[KUI_MULTITAP_KEYS];     /**< Each digit key's letters; NULL on the mode key and Delete. */
} kbd_ui_t;

/** What the keypad knows about the text and the focus. The text is a secret. */
typedef struct {
    kui_multitap_t mt;                      /**< The text and its rules. */
    char shown[KBD_SHOWN_MAX];              /**< The plain characters the field shows. */
    kgui_kbd_ctl_t focus;                   /**< The control with the ring. */
    uint8_t col;                            /**< The column of keys the focus last stood in. */
    bool secret;                            /**< The text is a password: hidden, with the switch to show it. */
    bool show;                              /**< Show password is on. */
    bool plain;                             /**< The last character was drawn plain at the last redraw. */
    uint8_t min;                            /**< Below this many characters the pill is grey. */
    const char *status;                     /**< The word of a hold, or NULL. */
    const char *placeholder;                /**< What the empty field asks for. */
    kgui_kbd_tap_fn tap;                    /**< What a finger on a control asks of the owner. */
} kbd_state_t;

static kbd_ui_t    s_ui;   /**< The objects. */
static lv_obj_t   *s_show_word;   /**< The field's "Show" or "Hide". */
static kbd_state_t s_st;   /**< The state. */

/* ---- Helpers ---------------------------------------------------------------- */

/**
 * @brief Zero a buffer in a way the compiler may not drop.
 *
 * @param buf The buffer.
 * @param len Its length.
 */
static void kbd_zero(char *buf, size_t len)
{
    volatile char *p = buf;

    for ( size_t i = 0U; i < len; i++ )
    {
        p[i] = '\0';
    }
}

/**
 * @brief The row of the focus a control stands in.
 *
 * @param ctl The control.
 * @return KBD_ROW_GO for the pill, KBD_ROW_SHOW for the switch, KBD_FIRST_KEY_ROW to the last for the four rows of keys.
 */
static uint8_t kbd_ctl_row(kgui_kbd_ctl_t ctl)
{
    uint8_t row = KBD_ROW_GO;

    if ( KGUI_KBD_CTL_SHOW == ctl )
    {
        row = KBD_ROW_SHOW;
    }
    else if ( KGUI_KBD_CTL_GO != ctl )
    {
        unsigned int const place = (unsigned int)ctl - (unsigned int)KGUI_KBD_CTL_K1;

        row = (uint8_t)(KBD_FIRST_KEY_ROW + (place / (unsigned int)KBD_KEY_COLS));
    }
    else
    {
        /* The pill is the first row. */
    }
    return row;
}

/**
 * @brief The column of keys a control stands in, or the one the focus last stood in when it is no key.
 *
 * @param ctl The control.
 * @return 0 to 2.
 */
static uint8_t kbd_ctl_col(kgui_kbd_ctl_t ctl)
{
    uint8_t col = s_st.col;

    if ( (KGUI_KBD_CTL_GO != ctl) && (KGUI_KBD_CTL_SHOW != ctl) )
    {
        unsigned int const place = (unsigned int)ctl - (unsigned int)KGUI_KBD_CTL_K1;

        col = (uint8_t)(place % (unsigned int)KBD_KEY_COLS);
    }
    return col;
}

/**
 * @brief The control at a row and a column of the focus.
 *
 * @param row KBD_ROW_GO to the last row of keys; the pill and the switch ignore the column.
 * @param col 0 to 2.
 * @return The control.
 */
static kgui_kbd_ctl_t kbd_row_ctl(uint8_t row, uint8_t col)
{
    kgui_kbd_ctl_t ctl = KGUI_KBD_CTL_GO;

    if ( KBD_ROW_SHOW == row )
    {
        ctl = KGUI_KBD_CTL_SHOW;
    }
    else if ( (KBD_FIRST_KEY_ROW <= row) && (KBD_ROW_GO > row) )
    {
        size_t place = (size_t)row - (size_t)KBD_FIRST_KEY_ROW;

        place = (place * (size_t)KBD_KEY_COLS) + (size_t)col;
        ctl = CTL_ALL[(size_t)KGUI_KBD_CTL_K1 + place];
    }
    else
    {
        /* The pill is the first row. */
    }
    return ctl;
}

/**
 * @brief The letters a key wears small in a mode, as the typing's own table has them.
 *
 * The table ends a key's cycle on its own digit, which the large digit says
 * already, so it is left off; a key with one character left says nothing
 * small, and the key 0's space says its word.
 *
 * @param key  The character key.
 * @param mode The mode.
 * @param out  Receives the words, terminated.
 * @param cap  Its size, KBD_LETTERS_MAX.
 */
static void kbd_letters(kui_multitap_key_t key, kui_multitap_mode_t mode, char *out, size_t cap)
{
    const char *const chars = kui_multitap_chars(key, mode);

    out[0] = '\0';
    if ( NULL != chars )
    {
        size_t n = strlen(chars);

        if ( (1U < n) && (chars[n - 1U] == DIGIT[(size_t)key][0]) )
        {
            n--;
        }
        if ( (1U == n) && (' ' == chars[0]) )
        {
            (void)snprintf(out, cap, "space");
        }
        else if ( 1U < n )
        {
            /* A key is 118 px wide: key 1's seven symbols would run past both
             * edges onto the black, where Soft Black ink is lost. Cut the
             * letters to the room beside the digit (the apocrifa shows ".@!#?"). */
            int32_t const room = KBD_KEY_W - (2 * KBD_KEY_INK_PAD) - KBD_LABEL_GAP
                                 - (int32_t)lv_font_get_glyph_width(&kui_font_digits_34, (uint32_t)'0', 0U);
            int32_t used = 0;
            size_t keep = 0U;

            while ( keep < n )
            {
                int32_t const w = (int32_t)lv_font_get_glyph_width(&kui_font_body, (uint32_t)(unsigned char)chars[keep], 0U);

                if ( (used + w) > room )
                {
                    break;
                }
                used += w;
                keep++;
            }
            if ( 0U == keep )
            {
                keep = 1U;
            }
            (void)snprintf(out, cap, "%.*s", (int)keep, chars);
        }
        else
        {
            /* One character: the large digit is the whole of the key. */
        }
    }
}

/* ---- The field --------------------------------------------------------------- */

/** What the field shows of a text: how many dots, then plain characters. */
typedef struct {
    size_t dots;     /**< How many hidden characters it draws as dots. */
    int32_t text_w;  /**< How wide the plain characters after them are, in pixels; 0 for none. */
} kbd_view_t;

/**
 * @brief Take the end of a text that fits a width, whole characters from the right.
 *
 * @param text The text.
 * @param len  How many characters it holds.
 * @param room The width they may take, in pixels.
 * @param used Receives the width they take.
 * @return The index of the first character taken; @p len when none fits.
 */
static size_t kbd_tail(const char *text, size_t len, int32_t room, int32_t *used)
{
    size_t first = len;
    int32_t sum = 0;
    uint32_t next = 0U;
    bool fits = true;

    while ( (0U < first) && fits )
    {
        uint32_t const cp = (uint32_t)(unsigned char)text[first - 1U];
        int32_t w = (int32_t)lv_font_get_glyph_width(&kui_font_name, cp, next);

        if ( 0 < sum )
        {
            w += KBD_LETTER_SPACE;
        }
        if ( (sum + w) > room )
        {
            fits = false;
        }
        else
        {
            sum += w;
            next = cp;
            first--;
        }
    }
    *used = sum;
    return first;
}

/**
 * @brief Say what the field shows: the dots and the plain characters after them.
 *
 * Hidden, the field draws a dot for every character but the last while that
 * one is plain (kui_multitap_show_plain()); shown, it draws the end of the
 * text plain, whole characters from the right, as many as fit. What does not
 * fit is the start of the text, which stays in the multitap.
 *
 * @param now_ms The clock now.
 * @return The dots and the width of the plain characters.
 */
static kbd_view_t kbd_compose(uint32_t now_ms)
{
    const char *const text = kui_multitap_text(&s_st.mt);
    size_t const len = kui_multitap_len(&s_st.mt);
    bool const last_plain = kui_multitap_show_plain(&s_st.mt, now_ms);
    bool const plain_all = s_st.show || !s_st.secret;
    kbd_view_t view = { 0U, 0 };
    size_t first = len;

    s_st.plain = last_plain;
    if ( plain_all )
    {
        first = kbd_tail(text, len, KBD_ROOM_PX, &view.text_w);
    }
    else if ( last_plain && (0U < len) )
    {
        first = kbd_tail(&text[len - 1U], 1U, KBD_ROOM_PX, &view.text_w) + (len - 1U);
        view.dots = len - 1U;
    }
    else
    {
        view.dots = len;
    }
    if ( 0U < view.dots )
    {
        int32_t const room = KBD_ROOM_PX - view.text_w;
        int32_t const fit  = room / KBD_DOT_PITCH;

        if ( view.dots > (size_t)fit )
        {
            view.dots = (size_t)fit;
        }
    }
    {
        size_t n = 0U;

        kbd_zero(s_st.shown, sizeof(s_st.shown));
        for ( size_t i = first; i < len; i++ )
        {
            s_st.shown[n] = text[i];
            n++;
        }
        s_st.shown[n] = '\0';
    }
    return view;
}

/**
 * @brief Draw the field: the dots, the plain characters and the caret; what it asks for while it is empty; or the word of a hold.
 *
 * @param now_ms The clock now.
 */
static void kbd_field_paint(uint32_t now_ms)
{
    if ( NULL != s_ui.root )
    {
        kbd_view_t const view = kbd_compose(now_ms);
        bool const empty = (kui_multitap_len(&s_st.mt) == 0U);
        bool const typed = (NULL == s_st.status) && !empty;
        int32_t const dots_w = (int32_t)view.dots * KBD_DOT_PITCH;

        for ( size_t i = 0U; (size_t)KBD_DOTS_MAX > i; i++ )
        {
            lv_obj_set_hidden(s_ui.dot[i], !(typed && (i < view.dots)));
        }
        if ( NULL != s_st.status )
        {
            lv_label_set_text_static(s_ui.ph, s_st.status);
            lv_obj_align(s_ui.ph, LV_ALIGN_LEFT_MID, KBD_FIELD_PAD, 0);
            lv_obj_set_hidden(s_ui.ph, false);
            lv_obj_set_hidden(s_ui.txt, true);
            lv_obj_set_hidden(s_ui.caret, true);
        }
        else if ( empty )
        {
            lv_label_set_text_static(s_ui.ph, s_st.placeholder);
            lv_obj_align(s_ui.ph, LV_ALIGN_LEFT_MID, KBD_FIELD_PAD + KBD_CARET_W + KBD_PH_GAP, 0);
            lv_obj_align(s_ui.caret, LV_ALIGN_LEFT_MID, KBD_FIELD_PAD, 0);
            lv_obj_set_hidden(s_ui.ph, false);
            lv_obj_set_hidden(s_ui.txt, true);
            lv_obj_set_hidden(s_ui.caret, false);
        }
        else
        {
            int32_t caret_x = KBD_FIELD_PAD + dots_w + view.text_w;

            if ( 0 < view.text_w )
            {
                caret_x += KBD_CARET_GAP;
            }
            /* Static: the label points at s_st.shown, so LVGL never holds a copy of the text. */
            lv_label_set_text_static(s_ui.txt, s_st.shown);
            lv_obj_align(s_ui.txt, LV_ALIGN_LEFT_MID, KBD_FIELD_PAD + dots_w, 0);
            lv_obj_align(s_ui.caret, LV_ALIGN_LEFT_MID, caret_x, 0);
            lv_obj_set_hidden(s_ui.ph, true);
            lv_obj_set_hidden(s_ui.txt, 0 == view.text_w);
            lv_obj_set_hidden(s_ui.caret, false);
        }
    }
}

/**
 * @brief Draw the Show password switch as the flag says.
 */
static void kbd_show_paint(void)
{
    if ( NULL != s_ui.root )
    {
        /* kd_interruptor: on, the track is the action yellow and the knob is
         * ink; off, a grey track and a light knob. Blue is Kody's and the
         * connections', never a switch. */
        if ( NULL != s_show_word )
        {
            lv_label_set_text_static(s_show_word, s_st.show ? "Hide" : "Show");
        }
        if ( s_st.show )
        {
            lv_obj_set_style_bg_color(s_ui.sw_track, KUI_COLOR_ACTION, 0);
            lv_obj_set_style_bg_color(s_ui.sw_knob, KUI_COLOR_TEXT, 0);
            lv_obj_align(s_ui.sw_knob, LV_ALIGN_RIGHT_MID, -KBD_KNOB_INSET, 0);
        }
        else
        {
            lv_obj_set_style_bg_color(s_ui.sw_track, KUI_COLOR_REST, 0);
            lv_obj_set_style_bg_color(s_ui.sw_knob, KUI_COLOR_TEXT_ON_BG, 0);
            lv_obj_align(s_ui.sw_knob, LV_ALIGN_LEFT_MID, KBD_KNOB_INSET, 0);
        }
    }
}

/**
 * @brief Dim the yellow pill while the keypad is held, and give it back after.
 */
static void kbd_go_paint(void)
{
    if ( NULL != s_ui.root )
    {
        lv_opa_t opa = LV_OPA_COVER;
        bool const short_text = (kui_multitap_len(&s_st.mt) < (size_t)s_st.min);

        if ( NULL != s_st.status )
        {
            opa = KUI_OPA_PCT(KBD_DIM_PCT);
        }
        lv_obj_set_style_opa(s_ui.ctl[KGUI_KBD_CTL_GO], opa, 0);
        /* Grey until the text is long enough to be worth sending (the
         * apocrifa's keypad): yellow means it can be pressed. */
        lv_obj_set_style_bg_color(s_ui.ctl[KGUI_KBD_CTL_GO],
                                  short_text ? KUI_COLOR_REST : lv_color_hex(KUI_PALETTE_BOLD_YELLOW), 0);
        if ( NULL != s_ui.go_txt )
        {
            lv_obj_set_style_text_color(s_ui.go_txt,
                                        short_text ? KUI_COLOR_TEXT_ON_BG : KUI_COLOR_ON_ACCENT, 0);
        }
    }
}

/**
 * @brief Delete held by a finger: the field empties (the apocrifa's).
 *
 * @param e The long press.
 */
static void kbd_delete_held(lv_event_t *e)
{
    (void)e;
    kgui_keyboard_clear();
}

/**
 * @brief How far a key's digit rises to stand on the line its letters stand on.
 *
 * The two faces are bottom-aligned in a row, and the letters' baseline sits
 * higher in their box than the digits' does in theirs: by the difference of
 * the two faces' own base lines.
 *
 * @return The rise, in pixels.
 */
static int32_t kbd_rise(void)
{
    return (int32_t)kui_font_body.base_line - (int32_t)kui_font_digits_28.base_line;
}

/**
 * @brief Write every key's words for the mode the text is in.
 */
static void kbd_keys_paint(void)
{
    kui_multitap_mode_t const mode = kui_multitap_mode(&s_st.mt);

    if ( NULL != s_ui.root )
    {
        for ( size_t g = 0U; (size_t)KUI_MULTITAP_KEYS > g; g++ )
        {
            kui_multitap_key_t const key = KEY_OF[g];

            if ( KUI_MULTITAP_KEY_MODE == key )
            {
                lv_label_set_text_static(s_ui.main[g], MODE_WORD[(size_t)mode]);
            }
            else if ( KUI_MULTITAP_KEY_DELETE != key )
            {
                char letters[KBD_LETTERS_MAX];

                bool has;
                int32_t rise = 0;

                kbd_letters(key, mode, letters, sizeof(letters));
                has = ('\0' != letters[0]);
                lv_label_set_text(s_ui.small[g], letters);
                lv_obj_set_hidden(s_ui.small[g], !has);
                if ( has )
                {
                    rise = -kbd_rise();
                }
                lv_obj_set_style_translate_y(s_ui.main[g], rise, 0);
            }
            else
            {
                /* Delete says its one word. */
            }
        }
    }
}

/**
 * @brief Put the ring on the control that has the focus, and on no other.
 */
static void kbd_focus_paint(void)
{
    if ( NULL != s_ui.root )
    {
        for ( size_t i = 0U; (size_t)KGUI_KBD_CTL_COUNT > i; i++ )
        {
            if ( CTL_ALL[i] == s_st.focus )
            {
                lv_obj_add_state(s_ui.ctl[i], LV_STATE_CHECKED);
            }
            else
            {
                lv_obj_remove_state(s_ui.ctl[i], LV_STATE_CHECKED);
            }
        }
    }
}

/* ---- Building --------------------------------------------------------------- */

/**
 * @brief A transparent object at a place, that takes no press and does not scroll.
 *
 * @param parent Where it stands.
 * @param x      Its left in the parent.
 * @param y      Its top.
 * @param w      Its width.
 * @param h      Its height.
 * @return The object, or NULL when LVGL had no room.
 */
static lv_obj_t *kbd_plain(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
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

/**
 * @brief What LVGL asks when a control is clicked: which control, and the owner's answer.
 *
 * @param e The event; its target is one of the controls.
 */
static void kbd_clicked(lv_event_t *e)
{
    lv_obj_t const *const target = lv_event_get_target_obj(e);

    if ( NULL != s_st.tap )
    {
        for ( size_t i = 0U; (size_t)KGUI_KBD_CTL_COUNT > i; i++ )
        {
            if ( s_ui.ctl[i] == target )
            {
                s_st.tap(CTL_ALL[i]);
            }
        }
    }
}

/**
 * @brief One control: a transparent object that takes a press, wears the focus ring and tells its click.
 *
 * @param ctl The control it is.
 * @param x   Its left in the card.
 * @param y   Its top.
 * @param w   Its width.
 * @param h   Its height.
 * @return The object, or NULL when LVGL had no room; recorded in s_ui.ctl.
 */
static lv_obj_t *kbd_control(kgui_kbd_ctl_t ctl, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *const obj = kbd_plain(s_ui.root, x, y, w, h);

    if ( NULL != obj )
    {
        lv_obj_set_clickable(obj, true);
        lv_obj_set_scroll_on_focus(obj, false);
        /* Yellow: the card is the black page, where Soft Black ink would vanish. The
         * pad-or-finger switch (gui_ring_visible()) hides it for a touch. */
        lv_obj_add_style(obj, &kui_style_ring, LV_STATE_CHECKED);
        (void)lv_obj_add_event_cb(obj, &kbd_clicked, LV_EVENT_CLICKED, NULL);
        if ( KGUI_KBD_CTL_DELETE == ctl )
        {
            (void)lv_obj_add_event_cb(obj, &kbd_delete_held, LV_EVENT_LONG_PRESSED, NULL);
        }
        s_ui.ctl[ctl] = obj;
    }
    return obj;
}

/**
 * @brief One label in a face and a colour.
 *
 * @param parent Where it stands.
 * @param font   Its face.
 * @param color  Its colour.
 * @return The label, or NULL when LVGL had no room.
 */
static lv_obj_t *kbd_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *const lbl = lv_label_create(parent);

    if ( NULL != lbl )
    {
        lv_obj_set_style_text_font(lbl, font, 0);
        lv_obj_set_style_text_color(lbl, color, 0);
        lv_label_set_text_static(lbl, "");
    }
    return lbl;
}

/**
 * @brief The network's name, centred under the handle.
 *
 * @return true when it stands.
 */
static bool kbd_build_title(void)
{
    s_ui.title = kbd_label(s_ui.root, &kui_font_name, KUI_COLOR_TEXT_ON_BG);
    if ( NULL != s_ui.title )
    {
        lv_obj_set_size(s_ui.title, KBD_TITLE_W, lv_font_get_line_height(&kui_font_name));
        lv_obj_align(s_ui.title, LV_ALIGN_TOP_MID, 0, KBD_TITLE_Y);
        lv_label_set_long_mode(s_ui.title, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(s_ui.title, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_ui.title, "");
        /* No title: the network is the one the person just picked (Luismi,
         * 4-nov). The label stays for the code that writes it. */
        lv_obj_set_hidden(s_ui.title, true);
    }
    return NULL != s_ui.title;
}

/**
 * @brief The field: a dark capsule, the characters, what it asks for and the caret.
 *
 * @return true when all of it stands.
 */
static bool kbd_build_field(void)
{
    s_ui.field = kbd_plain(s_ui.root, KBD_FIELD_X, KBD_FIELD_Y, KBD_FIELD_W, KBD_FIELD_H);
    if ( NULL != s_ui.field )
    {
        lv_obj_set_style_bg_color(s_ui.field, KUI_COLOR_SURFACE, 0);
        lv_obj_set_style_bg_opa(s_ui.field, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_ui.field, KUI_RADIUS_GLASS, 0);
        s_ui.ph  = kbd_label(s_ui.field, &kui_font_name, KUI_COLOR_TEXT_MUTED);
        s_ui.txt = kbd_label(s_ui.field, &kui_font_name, KUI_COLOR_TEXT_ON_BG);
        if ( NULL != s_ui.txt )
        {
            lv_obj_set_style_text_letter_space(s_ui.txt, KBD_LETTER_SPACE, 0);
        }
        for ( size_t i = 0U; (size_t)KBD_DOTS_MAX > i; i++ )
        {
            s_ui.dot[i] = kbd_plain(s_ui.field, 0, 0, KBD_DOT_D, KBD_DOT_D);
            if ( NULL != s_ui.dot[i] )
            {
                lv_obj_set_style_bg_color(s_ui.dot[i], KUI_COLOR_TEXT_ON_BG, 0);
                lv_obj_set_style_bg_opa(s_ui.dot[i], LV_OPA_COVER, 0);
                lv_obj_set_style_radius(s_ui.dot[i], KUI_RADIUS_GLASS, 0);
                lv_obj_align(s_ui.dot[i], LV_ALIGN_LEFT_MID, KBD_FIELD_PAD + ((int32_t)i * KBD_DOT_PITCH), KBD_DOT_DY);
                lv_obj_set_hidden(s_ui.dot[i], true);
            }
        }
        s_ui.caret = kbd_plain(s_ui.field, 0, 0, KBD_CARET_W, KBD_CARET_H);
        if ( NULL != s_ui.caret )
        {
            lv_obj_set_style_bg_color(s_ui.caret, lv_color_hex(KUI_PALETTE_BOLD_YELLOW), 0);
            lv_obj_set_style_bg_opa(s_ui.caret, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(s_ui.caret, KBD_CARET_R, 0);
        }
    }
    bool ok = (NULL != s_ui.field) && (NULL != s_ui.ph) && (NULL != s_ui.txt) && (NULL != s_ui.caret);

    for ( size_t i = 0U; (size_t)KBD_DOTS_MAX > i; i++ )
    {
        ok = ok && (NULL != s_ui.dot[i]);
    }
    return ok;
}

/**
 * @brief The yellow pill beside the field, wearing Soft Black.
 *
 * @return true when it stands.
 */
static bool kbd_build_go(void)
{
    lv_obj_t *const go = kbd_control(KGUI_KBD_CTL_GO, ((int32_t)KBSP_LCD_H_RES - KBD_GO_W) / 2,
                                     KBD_GO_Y, KBD_GO_W, KBD_FIELD_H);

    if ( NULL != go )
    {
        lv_obj_add_style(go, &kui_style_pill, 0);
        lv_obj_add_style(go, &kui_style_action_pressed, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(go, lv_color_hex(KUI_PALETTE_BOLD_YELLOW), 0);
        s_ui.go_txt = kbd_label(go, &kui_font_name, KUI_COLOR_ON_ACCENT);
        if ( NULL != s_ui.go_txt )
        {
            lv_obj_center(s_ui.go_txt);
        }
    }
    return (NULL != go) && (NULL != s_ui.go_txt);
}

/**
 * @brief The Show password row: its word and its switch.
 *
 * @return true when all of it stands.
 */
static bool kbd_build_show(void)
{
    lv_obj_t *const row = kbd_control(KGUI_KBD_CTL_SHOW, KBD_FIELD_X, KBD_SHOW_Y, KBD_FIELD_W, KBD_SHOW_H);
    lv_obj_t *word = NULL;

    if ( NULL != row )
    {
        /* Show is the field itself (Luismi, 4-nov): a tap on the password
         * shows or hides it, and its word stands at the field's right end,
         * far from Join at the card's foot. */
        lv_obj_set_style_radius(row, KUI_RADIUS_GLASS, 0);
        lv_obj_set_style_bg_color(row, KUI_COLOR_TRACK, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, KUI_OPA_PCT(KBD_PRESSED_PCT), LV_STATE_PRESSED);
        word = kbd_label(row, &kui_font_body, KUI_COLOR_TEXT_MUTED);
        if ( NULL != word )
        {
            lv_obj_set_style_text_opa(word, LV_OPA_COVER, 0);
            lv_label_set_text_static(word, "Show");
            lv_obj_align(word, LV_ALIGN_RIGHT_MID, -KBD_FIELD_PAD, 0);
            s_show_word = word;
        }
        s_ui.sw_track = kbd_plain(row, 0, 0, KBD_SW_W, KBD_SW_H);
        if ( NULL != s_ui.sw_track )
        {
            lv_obj_set_style_bg_opa(s_ui.sw_track, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(s_ui.sw_track, KUI_RADIUS_GLASS, 0);
            lv_obj_align(s_ui.sw_track, LV_ALIGN_RIGHT_MID, -KBD_SHOW_PAD, 0);
            s_ui.sw_knob = kbd_plain(s_ui.sw_track, 0, 0, KBD_KNOB, KBD_KNOB);
        }
        if ( NULL != s_ui.sw_knob )
        {
            lv_obj_set_style_bg_color(s_ui.sw_knob, KUI_COLOR_TEXT_ON_BG, 0);
            lv_obj_set_style_bg_opa(s_ui.sw_knob, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(s_ui.sw_knob, KUI_RADIUS_GLASS, 0);
        }
    }
    if ( NULL != s_ui.sw_track )
    {
        lv_obj_set_hidden(s_ui.sw_track, true);   /* the word says it now */
    }
    return (NULL != row) && (NULL != word) && (NULL != s_ui.sw_track) && (NULL != s_ui.sw_knob);
}

/**
 * @brief The words a key wears: a large digit and small letters, or one word.
 *
 * @param key The key's object.
 * @param g   The key's place, 0 to 11.
 * @return true when the words stand.
 */
static bool kbd_build_key_words(lv_obj_t *key, size_t g)
{
    bool ok = true;

    if ( (KEY_OF[g] == KUI_MULTITAP_KEY_MODE) || (KEY_OF[g] == KUI_MULTITAP_KEY_DELETE) )
    {
        s_ui.main[g] = kbd_label(key, &kui_font_name, KUI_COLOR_TEXT);
        ok = (NULL != s_ui.main[g]);
        if ( ok )
        {
            lv_obj_center(s_ui.main[g]);
            if ( KEY_OF[g] == KUI_MULTITAP_KEY_DELETE )
            {
                lv_label_set_text_static(s_ui.main[g], "Delete");
            }
        }
    }
    else
    {
        lv_obj_t *const row = kui_flex_create(key, LV_FLEX_FLOW_ROW, KBD_LABEL_GAP, 0U,
                                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                                              LV_FLEX_ALIGN_CENTER);

        ok = (NULL != row);
        if ( ok )
        {
            lv_obj_set_clickable(row, false);
            lv_obj_center(row);
            s_ui.main[g]  = kbd_label(row, &kui_font_digits_34, KUI_COLOR_TEXT);
            s_ui.small[g] = kbd_label(row, &kui_font_body, KUI_COLOR_TEXT);
            ok = (NULL != s_ui.main[g]) && (NULL != s_ui.small[g]);
        }
        if ( ok )
        {
            lv_obj_set_style_text_opa(s_ui.small[g], KUI_OPA_PCT(KBD_LETTERS_PCT), 0);
            lv_label_set_text_static(s_ui.main[g], DIGIT[(size_t)KEY_OF[g]]);
        }
    }
    return ok;
}

/**
 * @brief The twelve keys, four rows of three.
 *
 * @return true when all of them stand.
 */
static bool kbd_build_keys(void)
{
    bool ok = true;

    for ( size_t g = 0U; ((size_t)KUI_MULTITAP_KEYS > g) && ok; g++ )
    {
        size_t const col_n = g % (size_t)KBD_KEY_COLS;
        size_t const row_n = g / (size_t)KBD_KEY_COLS;
        int32_t const col  = (int32_t)col_n;
        int32_t const row  = (int32_t)row_n;
        lv_obj_t *const key = kbd_control(CTL_ALL[(size_t)KGUI_KBD_CTL_K1 + g],
                                          KBD_SIDE_PX + (col * (KBD_KEY_W + KBD_GAP_PX)),
                                          KBD_KEYS_Y + (row * (KBD_KEY_H + KBD_KEY_GAP_Y)),
                                          KBD_KEY_W, KBD_KEY_H);

        ok = (NULL != key);
        if ( ok )
        {
            lv_obj_add_style(key, &kui_style_pill, 0);
            /* kshell's keys: Medium Grey, and the two that are not letters -
             * the mode key and Delete - Light Grey. Pressed, each steps to the
             * other grey: the pill's resting grey would put Soft Black ink on
             * a dark ground. */
            if ( (KEY_OF[g] == KUI_MULTITAP_KEY_MODE) || (KEY_OF[g] == KUI_MULTITAP_KEY_DELETE) )
            {
                lv_obj_set_style_bg_color(key, KUI_COLOR_TEXT_ON_BG, 0);
                lv_obj_set_style_bg_color(key, KUI_COLOR_TEXT_MUTED, LV_STATE_PRESSED);
            }
            else
            {
                lv_obj_set_style_bg_color(key, KUI_COLOR_TEXT_MUTED, 0);
                lv_obj_set_style_bg_color(key, KUI_COLOR_TEXT_ON_BG, LV_STATE_PRESSED);
            }
            ok = kbd_build_key_words(key, g);
        }
    }
    return ok;
}

/**
 * @brief Forget every object: they are gone, or never were.
 */
static void kbd_forget_ui(void)
{
    (void)memset(&s_ui, 0, sizeof(s_ui));
    s_show_word = NULL;
}

/**
 * @brief Make everything the keypad is made of, on a card.
 *
 * @param card The card.
 * @return true when all of it stands; false leaves what was made for the caller to delete.
 */
static bool kbd_build(lv_obj_t *card)
{
    bool ok;

    s_ui.root = kbd_plain(card, 0, 0, KBD_CARD_W, (int32_t)KBSP_LCD_V_RES);
    ok = (NULL != s_ui.root);
    if ( ok )
    {
        ok = kbd_build_title();
    }
    if ( ok )
    {
        ok = kbd_build_field();
    }
    if ( ok )
    {
        ok = kbd_build_go();
    }
    if ( ok )
    {
        ok = kbd_build_show();
    }
    if ( ok )
    {
        ok = kbd_build_keys();
    }
    return ok;
}

/* ---- The module's calls ------------------------------------------------------- */

bool kgui_keyboard_open(lv_obj_t *card, kgui_kbd_tap_fn tap)
{
    bool ok = (NULL != s_ui.root);

    s_st.tap = tap;
    if ( (!ok) && (NULL != card) )
    {
        ok = kbd_build(card);
        if ( !ok )
        {
            if ( NULL != s_ui.root )
            {
                lv_obj_delete(s_ui.root);
            }
            kbd_forget_ui();
        }
    }
    return ok;
}

void kgui_keyboard_close(void)
{
    kgui_keyboard_wipe();
    if ( NULL != s_ui.root )
    {
        lv_obj_delete(s_ui.root);
        kbd_forget_ui();
    }
}

bool kgui_keyboard_is_open(void)
{
    return NULL != s_ui.root;
}

void kgui_keyboard_set(const kgui_kbd_spec_t *spec, uint32_t now_ms)
{
    if ( (NULL != s_ui.root) && (NULL != spec) )
    {
        kui_multitap_init(&s_st.mt, spec->cap);
        kbd_zero(s_st.shown, sizeof(s_st.shown));
        s_st.secret      = spec->secret;
        s_st.min         = spec->min;
        s_st.show        = false;
        s_st.plain       = false;
        s_st.status      = NULL;
        s_st.placeholder = spec->placeholder;
        s_st.focus       = KGUI_KBD_CTL_K5;
        s_st.col         = (uint8_t)(((unsigned int)KGUI_KBD_CTL_K5 - (unsigned int)KGUI_KBD_CTL_K1) % (unsigned int)KBD_KEY_COLS);
        lv_label_set_text(s_ui.title, spec->title);
        lv_label_set_text_static(s_ui.go_txt, spec->go);
        lv_obj_set_hidden(s_ui.ctl[KGUI_KBD_CTL_SHOW], !spec->secret);
        kbd_keys_paint();
        kbd_show_paint();
        kbd_go_paint();
        kbd_focus_paint();
        kbd_field_paint(now_ms);
    }
}

void kgui_keyboard_wipe(void)
{
    kui_multitap_clear(&s_st.mt);
    kbd_zero(s_st.shown, sizeof(s_st.shown));
    s_st.show   = false;
    s_st.plain  = false;
    s_st.status = NULL;
    if ( NULL != s_ui.root )
    {
        kbd_keys_paint();
        kbd_show_paint();
        kbd_go_paint();
        kbd_field_paint(0U);
    }
}

void kgui_keyboard_clear(void)
{
    if ( (NULL != s_ui.root) && (NULL == s_st.status) && (kui_multitap_len(&s_st.mt) > 0U) )
    {
        kui_multitap_clear(&s_st.mt);
        kbd_zero(s_st.shown, sizeof(s_st.shown));
        s_st.plain = false;
        kbd_keys_paint();
        kbd_go_paint();
        kbd_field_paint(0U);
    }
}

void kgui_keyboard_hold(const char *word, uint32_t now_ms)
{
    if ( NULL != s_ui.root )
    {
        s_st.status = word;
        kbd_go_paint();
        kbd_field_paint(now_ms);
    }
}

void kgui_keyboard_move(kgui_kbd_dir_t dir)
{
    uint8_t row = kbd_ctl_row(s_st.focus);
    uint8_t col = kbd_ctl_col(s_st.focus);

    switch ( dir )
    {
        case KGUI_KBD_UP: {
            if ( 0U == row )
            {
                row = (uint8_t)(KBD_FOCUS_ROWS - 1U);
            }
            else
            {
                row--;
            }
            break;
        }
        case KGUI_KBD_DOWN: {
            row++;
            if ( KBD_FOCUS_ROWS <= row )
            {
                row = 0U;
            }
            break;
        }
        case KGUI_KBD_LEFT: {
            if ( (KBD_FIRST_KEY_ROW <= row) && (KBD_ROW_GO > row) )
            {
                if ( 0U == col )
                {
                    col = (uint8_t)KBD_LAST_COL;
                }
                else
                {
                    col--;
                }
            }
            break;
        }
        case KGUI_KBD_RIGHT: {
            if ( (KBD_FIRST_KEY_ROW <= row) && (KBD_ROW_GO > row) )
            {
                if ( KBD_LAST_COL <= col )
                {
                    col = 0U;
                }
                else
                {
                    col++;
                }
            }
            break;
        }
        default: {
            break;
        }
    }
    if ( (KBD_ROW_SHOW == row) && !s_st.secret )
    {
        /* A name has no switch: the walk goes over its row. */
        if ( KGUI_KBD_UP == dir )
        {
            row = KBD_ROW_GO;
        }
        else
        {
            row = KBD_FIRST_KEY_ROW;
        }
    }
    s_st.col   = col;
    s_st.focus = kbd_row_ctl(row, col);
    kbd_focus_paint();
}

void kgui_keyboard_focus(kgui_kbd_ctl_t ctl)
{
    s_st.col   = kbd_ctl_col(ctl);
    s_st.focus = ctl;
    kbd_focus_paint();
}

kgui_kbd_ctl_t kgui_keyboard_focused(void)
{
    return s_st.focus;
}

/**
 * @brief Press one of the twelve keys through the multitap's rules.
 *
 * @param ctl    A key.
 * @param now_ms The clock now.
 * @return What the press did.
 */
static kgui_kbd_result_t kbd_press_key(kgui_kbd_ctl_t ctl, uint32_t now_ms)
{
    size_t const g = (size_t)((unsigned int)ctl - (unsigned int)KGUI_KBD_CTL_K1);
    kui_multitap_result_t const r = kui_multitap_press(&s_st.mt, KEY_OF[g], now_ms);
    kgui_kbd_result_t res = KGUI_KBD_EDITED;

    switch ( r )
    {
        case KUI_MULTITAP_STEPPED: {
            kbd_keys_paint();
            break;
        }
        case KUI_MULTITAP_FULL: {
            res = KGUI_KBD_FULL;
            break;
        }
        case KUI_MULTITAP_EMPTY:
        case KUI_MULTITAP_IGNORED: {
            res = KGUI_KBD_NONE;
            break;
        }
        default: {
            /* Typed, cycled or deleted: the field below says it. */
            break;
        }
    }
    if ( KGUI_KBD_NONE != res )
    {
        kbd_field_paint(now_ms);
        kbd_go_paint();
    }
    return res;
}

kgui_kbd_result_t kgui_keyboard_press(kgui_kbd_ctl_t ctl, uint32_t now_ms)
{
    kgui_kbd_result_t res = KGUI_KBD_NONE;

    if ( (NULL != s_ui.root) && (NULL == s_st.status) )
    {
        if ( KGUI_KBD_CTL_GO == ctl )
        {
            res = KGUI_KBD_GO;
        }
        else if ( KGUI_KBD_CTL_SHOW == ctl )
        {
            if ( s_st.secret )
            {
                s_st.show = !s_st.show;
                kbd_show_paint();
                kbd_field_paint(now_ms);
                res = KGUI_KBD_EDITED;
            }
        }
        else
        {
            res = kbd_press_key(ctl, now_ms);
        }
    }
    return res;
}

void kgui_keyboard_tick(uint32_t now_ms)
{
    if ( NULL != s_ui.root )
    {
        kui_multitap_tick(&s_st.mt, now_ms);
        if ( (NULL == s_st.status)
            && (kui_multitap_show_plain(&s_st.mt, now_ms) != s_st.plain) )
        {
            kbd_field_paint(now_ms);
        }
    }
}

const char *kgui_keyboard_text(void)
{
    return kui_multitap_text(&s_st.mt);
}

size_t kgui_keyboard_len(void)
{
    return kui_multitap_len(&s_st.mt);
}

const char *kgui_keyboard_mode_word(void)
{
    return MODE_WORD[(size_t)kui_multitap_mode(&s_st.mt)];
}

bool kgui_keyboard_shows_all(void)
{
    return s_st.show;
}
