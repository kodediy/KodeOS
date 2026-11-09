/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_keyboard.h
 * @brief The keypad card's content: a field, one yellow pill and twelve keys that type by multitap.
 */
#ifndef KGUI_KEYBOARD_H
#define KGUI_KEYBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"
#include "kui_multitap.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Where the card's header ends, in the card's own rows: the handle and the title. A drag down that starts above it closes the card; below it, the field and the keys are the keypad's own. */
#define KGUI_KBD_HEAD_FOOT_Y  34

/** @brief The controls of the card, in the order the focus walks them row by row. */
typedef enum {
    KGUI_KBD_CTL_GO = 0,  /**< The yellow pill beside the field. */
    KGUI_KBD_CTL_SHOW,    /**< The Show password row. */
    KGUI_KBD_CTL_K1,      /**< The key 1. */
    KGUI_KBD_CTL_K2,      /**< The key 2. */
    KGUI_KBD_CTL_K3,      /**< The key 3. */
    KGUI_KBD_CTL_K4,      /**< The key 4. */
    KGUI_KBD_CTL_K5,      /**< The key 5. */
    KGUI_KBD_CTL_K6,      /**< The key 6. */
    KGUI_KBD_CTL_K7,      /**< The key 7. */
    KGUI_KBD_CTL_K8,      /**< The key 8. */
    KGUI_KBD_CTL_K9,      /**< The key 9. */
    KGUI_KBD_CTL_MODE,    /**< The mode key. */
    KGUI_KBD_CTL_K0,      /**< The key 0. */
    KGUI_KBD_CTL_DELETE,  /**< Delete. */
    KGUI_KBD_CTL_COUNT    /**< How many controls there are. */
} kgui_kbd_ctl_t;

/** @brief Which way the pad moves the focus. */
typedef enum {
    KGUI_KBD_UP = 0,  /**< One row up, from the first row to the pill. */
    KGUI_KBD_DOWN,    /**< One row down, from the pill back to the first row. */
    KGUI_KBD_LEFT,    /**< One key to the left along a row of keys. */
    KGUI_KBD_RIGHT    /**< One key to the right along a row of keys. */
} kgui_kbd_dir_t;

/** @brief What a press of a control did. */
typedef enum {
    KGUI_KBD_NONE = 0,  /**< Nothing changed: Delete found nothing, or the keypad is held. */
    KGUI_KBD_EDITED,    /**< The text, the mode or the switch changed. */
    KGUI_KBD_FULL,      /**< The text is at its cap and the press would have added a character: it did not. */
    KGUI_KBD_GO         /**< The yellow pill was pressed: what it means is the caller's. */
} kgui_kbd_result_t;

/** @brief What one text is typed for. The words are literals: the field points at them. */
typedef struct {
    const char *title;        /**< The card's title: a network's name; copied. */
    const char *placeholder;  /**< What the empty field asks for: "Password". */
    const char *go;           /**< The yellow pill's word: "Join". */
    uint8_t cap;              /**< The most characters the text may hold: KUI_MULTITAP_TEXT_MAX or KUI_MULTITAP_SSID_MAX. */
    bool secret;              /**< The text is a password: hidden as dots, with the Show password row to show it. Otherwise it is a name: plain as typed, and the row is not there. */
    uint8_t min;              /**< Below this many characters the yellow pill stands grey (the apocrifa's: WPA asks 8); 0 keeps it lit. Pressing it grey still asks the owner, who says why. */
} kgui_kbd_spec_t;

/**
 * @brief What a finger on a control asks of the owner.
 *
 * Called from LVGL's click callback of the control, for every click: a press
 * that travelled is no tap, and telling it is the owner's. The control's
 * objects must not be deleted from inside it.
 *
 * @param ctl The control that was clicked.
 */
typedef void (*kgui_kbd_tap_fn)(kgui_kbd_ctl_t ctl);

/**
 * @brief Build the keypad on a card, once; a second call keeps what stands.
 *
 * The content is one transparent object that covers the card, made last so
 * it is drawn over what the kit put there. Nothing is left half built.
 *
 * @param card The card of the sheet; it must be as tall as the glass.
 * @param tap  What a finger on a control asks; NULL for nothing.
 * @return true when the keypad stands, false when LVGL had no room.
 */
bool kgui_keyboard_open(lv_obj_t *card, kgui_kbd_tap_fn tap);

/**
 * @brief Take the keypad off the card and forget the text.
 *
 * Wipes first (kgui_keyboard_wipe()), then deletes every object it made.
 * Nothing when it is not open.
 */
void kgui_keyboard_close(void);

/**
 * @brief Whether the keypad stands on a card.
 *
 * @return true between a successful kgui_keyboard_open() and kgui_keyboard_close().
 */
bool kgui_keyboard_is_open(void);

/**
 * @brief Start a new text: an empty field in the lower-case mode, the focus on the key 5.
 *
 * Whatever was typed is wiped (kui_multitap_init() zeroes the buffer), the
 * switch goes back to hiding, and a hold is let go. Nothing when the keypad
 * is not open.
 *
 * @param spec   What the text is for.
 * @param now_ms The clock now, in milliseconds.
 */
void kgui_keyboard_set(const kgui_kbd_spec_t *spec, uint32_t now_ms);

/**
 * @brief Forget the text, and keep the keypad standing.
 *
 * Zeroes every byte of the text and of the characters the field shows, and
 * the field reads empty. The keypad stays as it is, so a card that is
 * leaving the glass does not slide away blank.
 */
void kgui_keyboard_wipe(void);

/**
 * @brief Empty the field, as Delete held does (the apocrifa's), and keep the
 *        rest of the card as it is.
 */
void kgui_keyboard_clear(void);

/**
 * @brief Hold the keypad while something is done with the text.
 *
 * The field says @p word instead of the text, the pill is dimmed and no
 * control acts until the hold is let go; the pad still moves the focus.
 *
 * @param word What the field says, a literal; NULL lets the hold go.
 * @param now_ms The clock now, in milliseconds.
 */
void kgui_keyboard_hold(const char *word, uint32_t now_ms);

/**
 * @brief Move the focus one control the way the pad asks.
 *
 * Up and down walk the rows - the pill, the switch, the four rows of keys -
 * and wrap at both ends, the switch left out when there is none; left and
 * right walk a row of keys and wrap; the pill and the switch have no left and
 * no right. Coming back down into the keys lands on the column the focus last
 * left them by.
 *
 * @param dir Which way.
 */
void kgui_keyboard_move(kgui_kbd_dir_t dir);

/**
 * @brief Put the focus on one control, as a tap does.
 *
 * @param ctl The control.
 */
void kgui_keyboard_focus(kgui_kbd_ctl_t ctl);

/**
 * @brief The control that has the focus.
 *
 * @return The control; the key 5 before the first kgui_keyboard_set().
 */
kgui_kbd_ctl_t kgui_keyboard_focused(void);

/**
 * @brief Press a control.
 *
 * A key goes through kui_multitap_press(), which decides whether it types,
 * cycles, deletes or steps the mode; the switch flips; the pill only reports
 * itself. The field and the keys' words are redrawn when something changed.
 * While the keypad is held, or not open, nothing is done.
 *
 * @param ctl    The control.
 * @param now_ms The clock now, in milliseconds.
 * @return What the press did.
 */
kgui_kbd_result_t kgui_keyboard_press(kgui_kbd_ctl_t ctl, uint32_t now_ms);

/**
 * @brief The keypad's share of the render tick.
 *
 * Commits a pending character once KUI_MULTITAP_MS has passed and redraws the
 * field when the last character stops being plain. It must be called more
 * often than KUI_MULTITAP_MS; it touches LVGL only when something changed.
 *
 * @param now_ms The clock now, in milliseconds, from the counter every press uses.
 */
void kgui_keyboard_tick(uint32_t now_ms);

/**
 * @brief The text typed.
 *
 * @return The text, NUL-terminated, valid until the next press, set, wipe or close. The caller must not log it and must copy it only where it is to go.
 */
const char *kgui_keyboard_text(void);

/**
 * @brief How many characters the text holds.
 *
 * @return The count, the pending character included. It is as secret as the text: it is never logged either.
 */
size_t kgui_keyboard_len(void);

/**
 * @brief The word of the mode the keys are in.
 *
 * @return "abc", "ABC", "123" or "#+=": a literal, and nothing of the text.
 */
const char *kgui_keyboard_mode_word(void);

/**
 * @brief Whether the switch shows every character plain.
 *
 * @return true while Show password is on.
 */
bool kgui_keyboard_shows_all(void);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_KEYBOARD_H */
