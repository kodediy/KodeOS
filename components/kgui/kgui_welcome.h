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

#define KGUI_WELCOME_GREET_MS 1800U  /**< From Kody being put in his scene to the greeting: kshell's GREET_DELAY_MS. */

/** @brief The landing's mark, read once at boot. */
typedef struct {
    bool     pending;  /**< The key said `pending`, and the greeting has not come. */
    bool     armed;    /**< Kody has been put in his scene: the clock runs. */
    uint32_t woke_ms;  /**< When he was. */
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
 * @param woke   Kody has been put in his scene, waking up or in his loop.
 * @param bare   Nothing covers the face.
 * @param now_ms The clock.
 * @return true when KGUI_WELCOME_GREET_MS have passed since he was and the
 *         face is bare; the mark is spent then.
 */
bool kgui_welcome_due(kgui_welcome_t *w, bool woke, bool bare, uint32_t now_ms);

/**
 * @brief Whether the console may write a word in the key.
 *
 * @param word The word.
 * @return true for `pending` and `done`.
 */
bool kgui_welcome_word_ok(const char *word);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_WELCOME_H */
