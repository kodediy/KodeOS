/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_welcome.h
 * @brief The way back from Welcome is not cold: the landing on the face.
 */
#ifndef KGUI_WELCOME_H
#define KGUI_WELCOME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KGUI_WELCOME_KEY      "welcome"          /**< The key, in the apps' namespace (KODE_NVS_NAMESPACE), where the app writes it. */
#define KGUI_WELCOME_PENDING  "pending"          /**< The app reached its last screen. */
#define KGUI_WELCOME_DONE     "done"             /**< The person was greeted. */
#define KGUI_WELCOME_TEXT     "You're all set."  /**< What Kody reads. */

#define KGUI_WELCOME_GREET_MS 1800U  /**< From Kody starting to wake to the ring: kshell's GREET_DELAY_MS. */
#define KGUI_WELCOME_RING_W   10     /**< The stroke: kshell's RING_W, and the app's own ring (KD_PB_GROSOR). */
#define KGUI_WELCOME_FILL_MS  500U   /**< The last eighth fills: kshell's LAST_FILL_MS. */
#define KGUI_WELCOME_POP_MS   99U    /**< Each half of the pop: kshell's DONE_POP_MS. */
#define KGUI_WELCOME_POP_PX   4      /**< How much thicker at the top of the pop: kshell's, for a stroke under 20 px. */
#define KGUI_WELCOME_HOLD_MS  2000U  /**< The closed ring stays: kshell's GREEN_HOLD_MS. */
#define KGUI_WELCOME_FADE_MS  400U   /**< ...and goes: kshell's FADE_MS. */
#define KGUI_WELCOME_FULL_PM  1000   /**< A whole last eighth, in thousandths. */

/** @brief The landing's mark, read once at boot. */
typedef struct {
    bool     pending;  /**< The key said `pending`, and the greeting has not come. */
    bool     armed;    /**< Kody has started to wake: the clock runs. */
    uint32_t woke_ms;  /**< When he did. */
} kgui_welcome_t;

/**
 * @brief Take what the store said.
 *
 * @param w    The mark.
 * @param word The key's value; NULL for no key.
 */
void kgui_welcome_load(kgui_welcome_t *w, const char *word);

/**
 * @brief Whether this boot lands from Welcome: the face is its first frame.
 *
 * @param w The mark.
 * @return true until the greeting has come or the mark was dropped.
 */
bool kgui_welcome_pending(const kgui_welcome_t *w);

/**
 * @brief Drop the mark for this boot.
 *
 * @param w The mark.
 */
void kgui_welcome_drop(kgui_welcome_t *w);

/**
 * @brief Whether the greeting goes now. It answers true once.
 *
 * @param w      The mark.
 * @param woke   Kody has started to wake.
 * @param bare   Nothing covers the face.
 * @param now_ms The clock.
 * @return true when KGUI_WELCOME_GREET_MS have passed since he started to wake
 *         and the face is bare; the mark is spent then.
 */
bool kgui_welcome_due(kgui_welcome_t *w, bool woke, bool bare, uint32_t now_ms);

/**
 * @brief Whether the console may write a word in the key.
 *
 * @param word The word.
 * @return true for `pending` and `done`.
 */
bool kgui_welcome_word_ok(const char *word);

/**
 * @brief The length of the ring's path.
 *
 * The path runs flush with the glass (410 x 502, corners of 114) for a stroke of
 * KGUI_WELCOME_RING_W: its middle 5 px in, corners of 109, clockwise from the top
 * middle, as kshell's edge at that width and the app's ring do.
 *
 * @return Its length, in pixels.
 */
float kgui_welcome_path_len(void);

/**
 * @brief The point a distance along the path.
 *
 * @param d How far from the top middle; past the end is the path's closing point.
 * @param x Receives the point's x, on the glass.
 * @param y Receives its y.
 */
void kgui_welcome_path_at(float d, float *x, float *y);

/**
 * @brief How much of the path the ring covers.
 *
 * @param pm How much of the last eighth is filled, 0 to KGUI_WELCOME_FULL_PM.
 * @return The length drawn, from seven eighths of the path to all of it.
 */
float kgui_welcome_fill_len(int32_t pm);

/**
 * @brief Put the ring on the glass, over everything, and run it to its end.
 *
 * The ring takes no touch and goes by itself; it needs no tick (lv_anim moves
 * it). LVGL's thread.
 *
 * @return true when LVGL had room for it.
 */
bool kgui_welcome_ring_start(void);

/**
 * @brief Take the ring off the glass now; nothing if it has gone. LVGL's thread.
 */
void kgui_welcome_ring_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_WELCOME_H */
