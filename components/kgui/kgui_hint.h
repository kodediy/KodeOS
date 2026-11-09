/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_hint.h
 * @brief The hint: a few words over whatever is on the glass, in the rows of the top band.
 */
#ifndef KGUI_HINT_H
#define KGUI_HINT_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KGUI_HINT_MS       2000U        /**< How long a hint stays over the top band's rows, in milliseconds. */
#define KGUI_HINT_NONE     UINT32_MAX   /**< What kgui_hint_left_ms() answers while no timed hint is up. */

#define KGUI_HINT_LINES_MAX  2   /**< The most lines a hint takes in the top band's rows: a third is not shown. */

/**
 * @brief Build the hint and its clock on a layer, once.
 *
 * The hint is made last on @p parent, so it is drawn above whatever was built
 * there before it - the card of a sheet included. It starts hidden. A second
 * call does nothing.
 *
 * @param parent The layer: lv_layer_top().
 * @return true when the hint stands; false when LVGL had no room for it
 *         (nothing is left half built, and every other call here then does
 *         nothing).
 */
bool kgui_hint_create(lv_obj_t *parent);

/**
 * @brief Give a label the hint's look: the small face in cream, centred, on
 *        a dark capsule, wrapped at a pill's width.
 *
 * One style, kept here and shared: the hint itself and every label that
 * reads like it - the curtain's edge label - wear the same one. The label's
 * size and place are the caller's.
 *
 * @param label The label.
 */
void kgui_hint_dress(lv_obj_t *label);

/**
 * @brief Say in a few words why an input did nothing.
 *
 * Logged at INFO with the same words, and shown in one label over the top
 * band's rows for KGUI_HINT_MS; a second hint takes the label over and the
 * time starts again. Words that do not fit one line of the glass wrap to a
 * second, centred in the band's rows; a copy that needs more than
 * KGUI_HINT_LINES_MAX lines loses the rest.
 *
 * @param words The line, a literal or a string that stays for the call.
 */
void kgui_hint(const char *words);

/**
 * @brief Say in a few words why an input did nothing, for a time of its own.
 *
 * As kgui_hint(), with the words staying @p ms instead of KGUI_HINT_MS.
 *
 * @param words The line, a literal or a string that stays for the call.
 * @param ms    How long they stay, in milliseconds.
 */
void kgui_hint_for(const char *words, uint32_t ms);

/**
 * @brief How long the hint that ends by itself has left.
 *
 * What the renderer sleeps no longer than, where nothing else wakes it: at
 * Rest the tick waits a second, and the hint would be taken away up to that
 * late.
 *
 * @return Milliseconds, 0 when it is due; KGUI_HINT_NONE while no timed hint
 *         is up (a held hint has no end of its own).
 */
uint32_t kgui_hint_left_ms(void);

/**
 * @brief Take the hint away, whichever it is: the timed one as well as a
 *        held one.
 *
 * Nothing happens when no hint is up. What a screen that is about to change
 * calls, so a line that belonged to the old picture does not stand over the
 * new one.
 */
void kgui_hint_dismiss(void);

/**
 * @brief Say what letting go would do, until it would not.
 *
 * The words stay over the top band's rows, with no clock, until
 * kgui_hint_clear() or another hint takes them away; the same words asked
 * again change nothing, and are logged once at INFO, as kgui_hint() logs.
 *
 * @param words The line, a literal: the same pointer is the same line.
 */
void kgui_hint_hold(const char *words);

/**
 * @brief Take a held hint away; nothing when none is held.
 */
void kgui_hint_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_HINT_H */
