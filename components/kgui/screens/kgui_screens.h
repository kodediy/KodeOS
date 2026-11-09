/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_screens.h
 * @brief kodeOS's own screens and the four subjects they bind.
 */
#ifndef KGUI_SCREENS_H
#define KGUI_SCREENS_H

#include "kui.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The top band's height: the rows kgui reserves for the hint
 *  (kgui_hint.h). The screens' own content may stand in them, and a tap there
 *  is the content's tap. */
#define KGUI_TOP_BAND_PX   60
/** The launcher's window: the whole glass. */
#define KGUI_WINDOW_W        ((int32_t)KBSP_LCD_H_RES)
/** The launcher's window height. */
#define KGUI_WINDOW_H        ((int32_t)KBSP_LCD_V_RES)
/** The empty state's card the code stands on: Light Grey, square. */
#define KGUI_EMPTY_CARD    198
/** The card's corner radius. */
#define KGUI_EMPTY_CARD_R  22
/** The quiet zone round the code: four modules on each side of the card. */
#define KGUI_EMPTY_QUIET   24
/** The code inside the card: the card less its quiet zone on both sides. */
#define KGUI_EMPTY_QR      (KGUI_EMPTY_CARD - (2 * KGUI_EMPTY_QUIET))
/** Where the empty state's title stands, from the glass's top (kshell's). */
#define KGUI_EMPTY_TITLE_Y 41
/** Where the card stands, from the glass's top. */
#define KGUI_EMPTY_CARD_Y  116
/** Where the address stands, from the glass's top. */
#define KGUI_EMPTY_URL_Y   330
/** Where "Looking for apps" stands, from the glass's top: a Medium caption, not the title. */
#define KGUI_EMPTY_WAIT_Y  236
/** Between the address and the hint under it ("Tap Kody to find apps"); the gap the screen had before the rail, kept. */
#define KGUI_EMPTY_HINT_GAP 6
/** What the title says while the lists have not both answered. */
#define KGUI_EMPTY_WAIT_TEXT "Looking for apps"
/** A control-centre circle's diameter: larger than kshell's 88 (Luismi, 5-nov). */
#define KGUI_CC_CIRCLE       100
/** The three columns of circles: the left edge of each. */
#define KGUI_CC_X0           35
#define KGUI_CC_X1           155   /**< The middle column's left edge. */
#define KGUI_CC_X2           275   /**< The right column's left edge. */
/**
 * The first row of circles: Wi-Fi, Bluetooth and More. The block is centred on
 * the glass: its rows 16 px apart, 44 px over it and 42 under it, and the circles
 * 20 px apart (Luismi, 5-nov).
 */
#define KGUI_CC_Y0           44
/** The second row: the battery's liquid under Wi-Fi, Turn off under More. */
#define KGUI_CC_Y1           160
/** A circle's glyph, at the size the kit draws it. */
#define KGUI_CC_GLYPH_PX     40
/** The grab bar at the foot of the settings tab: subtle, and it says the tab can be taken from below. */
#define KGUI_CC_HANDLE_W     56
#define KGUI_CC_HANDLE_H     5
#define KGUI_CC_HANDLE_Y     478
/** The level's fill settling on its step when the finger leaves, or on a step of the pad. */
#define KGUI_LEVEL_SNAP_MS   132U
/** The red ring of the Turn off circle (kshell's 4 px). */
#define KGUI_CC_OFF_RING     4
/** The brightness level: its left edge, its top, its width and its height. */
#define KGUI_CC_LEVEL_X      18
#define KGUI_CC_LEVEL_Y      276   /**< The brightness level's top. */
#define KGUI_VOL_VIBRATE_PCT 5   /**< The volume's stop that vibrates and does not sound. */
#define KGUI_CC_VOLUME_Y     376   /**< The volume level's top, under the brightness. */
#define KGUI_CC_LEVEL_W      374   /**< The level's width. */
#define KGUI_CC_LEVEL_H      84    /**< The level's height; its fill never under this wide. */
/** What the level's fill grows by from 0 to 100 %: the track less the cap. */
#define KGUI_CC_LEVEL_SPAN   (KGUI_CC_LEVEL_W - KGUI_CC_LEVEL_H)
/** The least brightness the level sets (kshell's BRIGHT_MIN), in percent. */
#define KGUI_CC_LEVEL_MIN_PCT 10U
/** The glyph's distance from the level's left edge. */
#define KGUI_CC_LEVEL_GLYPH_PX 52   /**< A level's glyph (Luismi, 5-nov: larger). */
#define KGUI_CC_LEVEL_GLYPH_X 16
/** The fill is wide enough for the figure inside it from this width. */
#define KGUI_CC_LEVEL_INSIDE  160
/** The figure's left edge inside the fill, and after the fill's edge outside. */
#define KGUI_CC_FIG_IN_X      84
#define KGUI_CC_FIG_OUT_GAP   16   /**< The gap between the fill's edge and the figure. */
/** The brightness glyph has this many steps (sun0 to sun3); the speaker's (spk0 to spk3) too. */
#define KGUI_CC_SUN_STEPS     4
/** The share of the disc's area a full cell fills: its surface always shows, or there is nothing to lean. */
#define KGUI_CC_LIQUID_FULL   0.97F
/** The liquid's opacity over the circle's off grey: Ok green this far in reads as a deep, quiet green (Luismi, 4-nov). */
#define KGUI_CC_LIQUID_OPA    LV_OPA_30
/** Whether the battery's circle draws a liquid at all: yes, a quiet one behind the figure, leaning with the Dot (Luismi, 5-nov). */
#define KGUI_CC_LIQUID_SHOWN  1
/** The level in percent: all of it, and one more than that for the sun's step. */
#define KGUI_CC_LEVEL_FULL    100
/** A rail page's body: the clip's top and its height. A page has no title line: the first row starts at y 68 (this 60 and KGUI_PAGE_RAIL_END_PAD), so its 4 px focus ring clears the glass corner. */
#define KGUI_PAGE_BODY_Y      60
#define KGUI_PAGE_BODY_H      398  /**< The body's height: down to y 458. */
/** A rail row's height, and the gap that makes the pitch 104 (kshell's row). */
#define KGUI_PAGE_RAIL_ROW_H       92
#define KGUI_PAGE_RAIL_GAP         12   /**< Between two rail rows. */
/** The room at the top of the Wi-Fi and Bluetooth pages that their pinned switch keeps: one row and its gap. */
#define KGUI_PAGE_PIN_H            (KGUI_PAGE_RAIL_ROW_H + KGUI_PAGE_RAIL_GAP)
/** Where the focused row's centre stands on the glass (kshell's rail keeps it fixed). */
#define KGUI_PAGE_RAIL_FOCUS_Y     215
/** The rail's ends: row 0's top under the page's top, and the last row's foot over its foot (kshell's RAIL_TOP 8). */
#define KGUI_PAGE_RAIL_END_PAD     8
/** How far under the top of the content a ringed row stands once the ring has walked down to the focus line. */
#define KGUI_PAGE_RAIL_WALK_PX     (KGUI_PAGE_RAIL_FOCUS_Y - (KGUI_PAGE_RAIL_ROW_H / 2) - KGUI_PAGE_BODY_Y - KGUI_PAGE_RAIL_END_PAD)
/** How a rail row's glyph is drawn: 30 px, so a 40 px glyph is scaled by this over 256. */
#define KGUI_PAGE_RAIL_GLYPH_PX    30
/** Where a row starts to fade out at the top and at the bottom, by its centre. */
#define KGUI_PAGE_RAIL_FADE_TOP_Y  0
#define KGUI_PAGE_RAIL_FADE_TOP_L  50   /**< The fade's length at the top. */
#define KGUI_PAGE_RAIL_FADE_BOT_Y  502  /**< Where the fade starts at the foot. */
#define KGUI_PAGE_RAIL_FADE_BOT_L  70   /**< The fade's length at the foot. */
/** A bar page row's: its left edge on the glass. */
#define KGUI_BAR_X            18
/** A row with no disc stands in a capsule under the cursor: its left edge, its width
 *  (kshell's 22 and 366) and the inset of its words. */
#define KGUI_PAGE_RAIL_CAPSULE_X   22
#define KGUI_PAGE_RAIL_CAPSULE_W   366  /**< The capsule's width. */
#define KGUI_PAGE_RAIL_CAPSULE_PAD 30   /**< The words' inset inside it. */
/** The row's disc and its glyph, from the left edge of the glass. */
#define KGUI_PAGE_RAIL_DISC        56
#define KGUI_PAGE_RAIL_DISC_X      30   /**< The disc's left edge. */
#define KGUI_PAGE_RAIL_NAME_X      104  /**< The name's and the value's left edge. */
/** The name's top and the value's top inside a rail row of two lines. */
#define KGUI_PAGE_RAIL_NAME_Y      6
#define KGUI_PAGE_RAIL_VALUE_Y     46   /**< The value's top. */
/** Where the face's time starts, in glass pixels from the left: the left of
 *  its first digit's box (kshell's HOUR_X). */
#define KGUI_FACE_HOUR_X       40
/** Where the face's time starts, in glass pixels from the top: the top of the
 *  digits' ink (kshell's HOUR_Y). At 104 px the clock face's digits are about
 *  75 px tall, so the ink runs from here to y 124. */
#define KGUI_FACE_HOUR_Y       46
/** The space after each character of the time, in pixels: the clock face's
 *  digits stand closer than their advance (kshell's letter space). */
#define KGUI_FACE_HOUR_SPACE   (-3)
/** Where the face's date starts, from the left (kshell's DATE_X). */
#define KGUI_FACE_DATE_X       40
/** Where the face's date line starts, from the top (kshell's DATE_Y). The
 *  "Turn off" the arc brings stands in the same place. */
#define KGUI_FACE_DATE_Y       128
/** The knock's "Press ● again": the blue dot's size (a capital's height in the
 *  body's face) and the gap between it and each word. */
#define KGUI_FACE_KNOCK_DOT_PX 20
#define KGUI_FACE_KNOCK_WORD   "Press"   /**< The word before the dot. */
#define KGUI_FACE_KNOCK_GAP    10   /**< Between the dot and each word. */
/** How wide the date's line may run: to 40 px from the right edge. */
#define KGUI_FACE_DATE_W       ((int32_t)KBSP_LCD_H_RES - (2 * KGUI_FACE_DATE_X))
/** Where the battery's body starts, from the left (kshell's PILE_X). */
#define KGUI_FACE_PILE_X       338
/** Where the battery's body starts, from the top (kshell's PILE_Y). */
#define KGUI_FACE_PILE_Y       46
/** The battery's body: its width. */
#define KGUI_FACE_PILE_W       31
/** The battery's body: its height. */
#define KGUI_FACE_PILE_H       20
/** The battery's outline, in pixels. */
#define KGUI_FACE_PILE_BORDER  2
/** The battery's corners. */
#define KGUI_FACE_PILE_RADIUS  5
/** The terminal beside the body: its width. */
#define KGUI_FACE_PILE_TIP_W   3
/** The terminal beside the body: its height. */
#define KGUI_FACE_PILE_TIP_H   7
/** The terminal's distance from the body's top. */
#define KGUI_FACE_PILE_TIP_Y   7
/** The fill's room inside the outline: its gap from the border, all round. */
#define KGUI_FACE_PILE_GAP     2
/** The fill at 100 %, in pixels. */
#define KGUI_FACE_PILE_FILL_W  23
/** The fill's height. */
#define KGUI_FACE_PILE_FILL_H  12
/** The fill is never narrower than this: a flat cell still shows a sliver. */
#define KGUI_FACE_PILE_FILL_MIN 3
/** The charging bolt's side, in pixels. */
#define KGUI_FACE_BOLT_PX      14
/** What the arc's line is drawn over, #552125: the Danger red at rest, as
 *  kshell draws it. Only the face's arc wears it, so the kit has no role
 *  for it. */
#define KGUI_FACE_ARC_REST_HEX 0x552125U
/** How tall Kody's scene on the face is. It is as wide as the glass and
 *  stands on its bottom edge, y 202..502, and every animation the face plays
 *  is drawn for exactly this canvas. */
#define KGUI_FACE_SCENE_H  334
#define KGUI_FACE_NOTICE_W 330             /**< The notice line's room before the hour places it. */
#define KGUI_NOTICE_IN_MS  99U             /**< It comes in: KS_T_PAGE on the 33 ms grid. */
#define KGUI_NOTICE_OUT_MS 99U             /**< It goes: KS_T_BACK on the same grid. */

extern lv_obj_t *kgui_screen_face;       /**< The lock screen: the time and the date over Kody's scene. */
extern lv_obj_t *kgui_screen_dark;       /**< The page the glass holds while the board sleeps: black, with nothing on it. */
extern lv_obj_t *kgui_screen_launcher;   /**< The rail of rows over the whole glass; the empty state. */
extern lv_obj_t *kgui_screen_loading;    /**< Kody in his wheel and the guided factory tests' prompt. */
extern lv_obj_t *kgui_screen_settings;   /**< The control centre. */
extern lv_obj_t *kgui_screen_touch_test; /**< The guided touch test's target. */

extern lv_subject_t *kgui_subject_app_count;    /**< Apps in the list, those held back until their folder is read included; 0 shows the empty state. */
extern lv_subject_t *kgui_subject_empty_text;   /**< The empty state's title. */
extern lv_subject_t *kgui_subject_loading_text; /**< The guided test's prompt, under Kody's wheel. */
extern lv_subject_t *kgui_subject_tp_text;      /**< The touch test's one line. */

/**
 * @brief Create the four subjects, then the five screens. Call after kui_init(); idempotent.
 */
void kgui_screens_init(void);

/* ---- The clock's figures -------------------------------------------------- */

#define KGUI_CLOCK_HOURS     24  /**< Hours in a day: a larger one is no time. */
#define KGUI_CLOCK_HOURS_12  12  /**< Hours on a 12 h clock's dial. */
#define KGUI_CLOCK_MINUTES   60  /**< Minutes in an hour: a larger one is no time. */

/**
 * @brief Whether an hour and a minute are a time of day.
 *
 * @param hour   The hour, 0..23.
 * @param minute The minute, 0..59.
 * @return true when both are in range; false for the "not known" the face
 *         shows as `--:--`.
 */
bool kgui_clock_known(int32_t hour, int32_t minute);

/**
 * @brief The hour as the clock's face reads it.
 *
 * The face's rule: a 12 h dial counts 1..12 (midnight and noon are 12) and
 * says nothing of AM or PM.
 *
 * @param hour 0..23, a time of day (kgui_clock_known()).
 * @param h12  true for the 12 h dial.
 * @return @p hour as it is read: 0..23, or 1..12 on the 12 h dial.
 */
int32_t kgui_clock_hour(int32_t hour, bool h12);

/**
 * @brief Show a time on the face: HH, a colon and MM in three labels, or
 *        --:-- dimmed while the time is not known.
 *
 * The three stand left to right from KGUI_FACE_HOUR_X, each one after the
 * one before it with KGUI_FACE_HOUR_SPACE after every character, so the
 * colon moves when the hour's figures change width: DM Sans has no tabular
 * figures. A label is written only when its text changes. The colon and the
 * hyphens are raised onto the digits' middle. The colon is Medium Grey while
 * the time is only approximate. No timer: AO_Gui calls this when the minute
 * moves. Does nothing before kgui_screen_face_build().
 *
 * @param hour   The hour, 0..23; anything else shows --:--.
 * @param minute The minute, 0..59; anything else shows --:--.
 * @param h12    Show the hour as 1..12, one figure below 10.
 * @param approx The time is one put back after a power cut and not yet
 *               corrected: the colon turns Medium Grey.
 */
void kgui_screen_face_time(int32_t hour, int32_t minute, bool h12, bool approx);

/** The strip's cells: the ten digits, then the colon. */
#define KGUI_FACE_STRIP_CELLS 11U

/**
 * @brief The clock's figures as the glass shows them while the board sleeps.
 *
 * One row of RGB565, packed with no padding: the ten digits 0 to 9, glyph_w
 * wide each, then the colon, colon_w wide; the row is 10 * glyph_w + colon_w
 * pixels wide and glyph_h tall. The time is written over the glass from it,
 * four digit cells and the colon side by side, and x and y say where that
 * rectangle stands: the face's own clock, at KGUI_FACE_HOUR_X. Every number
 * is even, which the panel's memory window asks of a rectangle.
 */
typedef struct {
    lv_draw_buf_t *buf;      /**< The pixels; NULL when there is no strip.   */
    uint16_t       glyph_w;  /**< One digit's cell, pixels.                  */
    uint16_t       glyph_h;  /**< The cells' height, pixels.                 */
    uint16_t       colon_w;  /**< The colon's cell, pixels.                  */
    uint16_t       x;        /**< Left edge of the time on the glass.        */
    uint16_t       y;        /**< Top edge of the time on the glass.         */
    uint8_t        adv[KGUI_FACE_STRIP_CELLS];  /**< From one figure to the next, the ten digits
                               *   then the colon: the face's own spacing.   */
} kgui_face_strip_t;

/**
 * @brief Render the clock's figures, white on black, into a new strip.
 *
 * In the face's own type (the kit's clock face and the page's text colour),
 * each figure centred in a cell as wide as the widest digit; the colon is
 * raised onto the digits' middle the way the face raises it. The buffer is LVGL's (the kit puts that heap in
 * PSRAM) and lives until kgui_screen_face_strip_free(). The figures stand on
 * the dark page, loaded or not: a snapshot needs no page on the glass. Call it
 * with no frame being drawn; the dark page is left bare.
 *
 * @param out Receives the strip; out->buf is NULL when it could not be made.
 * @return true when the strip was rendered.
 */
bool kgui_screen_face_strip(kgui_face_strip_t *out);

/**
 * @brief Give a strip back.
 * @param strip The strip kgui_screen_face_strip() rendered; its buffer is
 *              set to NULL. Nothing happens when it holds none.
 */
void kgui_screen_face_strip_free(kgui_face_strip_t *strip);

/* The builders kgui_screens_init() calls, one per file; each creates its screen. */
void kgui_screen_face_build(void);        /**< @brief Build kgui_screen_face. */

/** @brief What the battery on the face is coloured for. */
typedef enum {
    KGUI_PILE_MEDIUM = 0,  /**< On the cell, nothing to say: Medium Grey. */
    KGUI_PILE_CHARGE,      /**< The cable is in and the cell is charging: Charge green. */
    KGUI_PILE_LOW,         /**< Low and not charging: Action yellow.      */
    KGUI_PILE_ALERT        /**< The critical alert: Danger red.           */
} kgui_pile_t;

/**
 * @brief Draw the battery in the face's top right corner, or take it off.
 *
 * kshell's pile: a body of KGUI_FACE_PILE_W by KGUI_FACE_PILE_H with its
 * terminal, filled from the left to the share of the charge, never under
 * KGUI_FACE_PILE_FILL_MIN pixels. A bolt stands on it while the cable is in.
 * Does nothing before kgui_screen_face_build().
 *
 * @param shown true to draw it; false hides it (no cell, or a charge that is not low).
 * @param pct   The charge, 0 to 100.
 * @param kind  What colours it.
 * @param bolt  true while the cable is in.
 */
void kgui_screen_face_pile(bool shown, uint8_t pct, kgui_pile_t kind, bool bolt);

/**
 * @brief Draw the red arc along the top of the glass, with "Turn off" where
 *        the date is, or take both off.
 *
 * kshell's arc (kshell_power.c): KGUI_OFF_ARC_W wide, round the glass's top
 * corners and along the top, the Danger red over KGUI_FACE_ARC_REST_HEX,
 * filled from the left to the share given. While it is up "Turn off" takes
 * the date's place. Does nothing before kgui_screen_face_build().
 *
 * @param pm The share of the path that is filled, 0 to KGUI_OFF_FULL_PM;
 *           0 takes the arc and the words off and shows the date.
 */
void kgui_screen_face_arc(uint16_t pm);

/**
 * @brief Show "Press ● again" in the date's place, or put the date back.
 *
 * The first press of the bottom button at Rest; the second wakes the face
 * and takes it off. The arc's "Turn off" and a bare face win over it. Does
 * nothing before kgui_screen_face_build().
 *
 * @param shown true to show it, false to put the date back.
 */
void kgui_screen_face_knock(bool shown);

/** @brief The two levels of the settings tab. */
typedef enum {
    KGUI_LEVEL_BRIGHT = 0,  /**< Brightness. */
    KGUI_LEVEL_VOLUME,      /**< Volume. */
    KGUI_LEVEL_COUNT
} kgui_level_t;

/**
 * @brief A finger has a level: the fill follows it, pixel by pixel.
 *
 * Until kgui_screen_settings_level_release() the plain setters do not move it.
 *
 * @param which The level.
 * @param pm    Under the finger, in thousandths.
 */
void kgui_screen_settings_level_drag(kgui_level_t which, int32_t pm);

/**
 * @brief The finger has left: the fill settles on its step.
 *
 * @param which The level.
 * @param pct   The step, in percent.
 */
void kgui_screen_settings_level_release(kgui_level_t which, uint8_t pct);

/**
 * @brief Show a notice in the date's place, or take it away.
 *
 * The apocrifa's notice line: one line, cut with dots, rising 6 px as it fades
 * in. The date waits under it and comes back. Does nothing before
 * kgui_screen_face_build().
 *
 * @param text The notice; NULL to take it away.
 */
void kgui_screen_face_notice(const char *text);

/**
 * @brief Stand the date so its day falls right under the hour's colon.
 *
 * Asked when its words change; a new width of the hour asks by itself.
 */
void kgui_screen_face_date_place(void);

/**
 * @brief Move the face's content off its place: the clock, the date, the
 *        battery and Kody's box, all together.
 *
 * Against burn-in, at Rest. Nothing is laid out again: the objects keep
 * their places and are drawn moved (a style's translation), so (0, 0) puts
 * everything back exactly. Does nothing before kgui_screen_face_build().
 *
 * @param dx Pixels to the right; negative moves to the left.
 * @param dy Pixels down; negative moves up.
 */
void kgui_screen_face_shift(int32_t dx, int32_t dy);

/**
 * @brief The yellow pills that ask for what the clock lacks.
 *
 * @param set_time "Set the time" in the hour's place.
 * @param zone     "Choose your time zone" over the date.
 */
void kgui_screen_face_prompt(bool set_time, bool zone);

/**
 * @brief What the knock's row says after its dot.
 *
 * @param last "twice" or "again"; a literal.
 */
void kgui_screen_face_knock_say(const char *last);

/**
 * @brief Take the face's content home from where Rest left it, as a movement.
 *
 * Rest moves the content up to 4 px to save the panel; going home at once is
 * the jump Luismi saw between the sleeping Kody and the waking one. It goes
 * while Kody himself leaps instead. kgui_screen_face_shift() cancels it.
 *
 * @param delay_ms How long it stands still first.
 * @param ms       How long the way takes.
 */
void kgui_screen_face_home(uint32_t delay_ms, uint32_t ms);

/**
 * @brief Strip the face to Kody's scene, or put the clock, the date and the
 *        battery back.
 *
 * The face the glass holds while the board sleeps: the scene alone. The time,
 * the date and the battery are not drawn: the glass would keep a minute, a
 * day and a charge it no longer knows. Does nothing before
 * kgui_screen_face_build().
 *
 * @param bare true to hide the time, the date and the battery; false to show
 *             them again.
 */
void kgui_screen_face_bare(bool bare);

/**
 * @brief Dim the clock, the date and the battery by their own opacity, or
 *        bring them back.
 *
 * The panel's brightness is global, so while only Kody is awake at Rest the
 * rest of the face is kept low by object, "Press ● again" in the date's
 * place (kgui_screen_face_knock()).
 * Does nothing before kgui_screen_face_build().
 *
 * @param dim true to dim them to KGUI_PEEK_DIM_OPA; false for full.
 */
void kgui_screen_face_dim(bool dim);


/**
 * @brief Show or hide the date under the hour. The apocrifa's face shows it
 *        for a few seconds on waking and then the hour stands alone.
 *
 * @param shown true to show it.
 */
void kgui_screen_face_date(bool shown);

void kgui_screen_launcher_build(void);    /**< @brief Build kgui_screen_launcher. */
void kgui_screen_loading_build(void);     /**< @brief Build kgui_screen_loading. */

/**
 * @brief Let the loading screen's Kody go of his animation: the pack it
 *        came from is being retired. The next load of the screen plays the
 *        new pack's.
 */
void kgui_screen_loading_kody_stop(void);

void kgui_screen_settings_build(void);    /**< @brief Build kgui_screen_settings. */

/**
 * @brief Draw the brightness level at @p pct: the fill's width, the sun and
 *        the figure.
 *
 * @param pct How bright, KGUI_CC_LEVEL_MIN_PCT to 100.
 */
void kgui_screen_settings_level(uint8_t pct);

/**
 * @brief Draw the volume level at @p pct: the fill's width, the speaker and
 *        the figure. The brightness level's twin, under it.
 *
 * @param pct How loud, 0 to 100.
 */
void kgui_screen_settings_volume(uint8_t pct);

/**
 * @brief Fill the battery's circle to @p pct with a liquid of @p col.
 *
 * The circle is also a control (`ctl_bat`), which AO_Gui wires to the
 * Battery page. The surface is a wave that drifts while
 * kgui_screen_settings_wave() lets it; held, nothing is redrawn until the
 * charge or the colour changes.
 *
 * @param pct How much charge is left, 0 to 100.
 * @param col What the liquid is made of.
 */
void kgui_screen_settings_battery(uint8_t pct, lv_color_t col);

/**
 * @brief Let the battery's surface drift, or hold it still.
 *
 * While it runs the wave slides one column every KGUI_CC_WAVE_PERIOD_MS /
 * KGUI_CC_WAVE_COLS ms and only the circle is drawn again; held, the surface
 * stays where it is. A second call with the same word does nothing.
 *
 * @param run true to let it drift.
 */
void kgui_screen_settings_wave(bool run);
void kgui_screen_touch_test_build(void);  /**< @brief Build kgui_screen_touch_test. */

/**
 * @brief The touch test's target: a 40 px ring with a cross through it.
 * @param parent The parent object.
 * @return The target, named "tp_target_#"; children `ring`, `bar_h`, `bar_v`.
 */
lv_obj_t *kgui_tp_target_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_SCREENS_H */
