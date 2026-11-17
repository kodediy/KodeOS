/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_wface.h
 * @brief Whose face the card of a write wears when the write is about no row.
 *
 * A write this launcher did not start (a console `install`, the Kode channel)
 * is about no row of Apps, and its card used to wear the grey disc. The
 * writer's reports name the project being written, and an app's data folder
 * is named for its project and may be on the board before the app is: the
 * card wears that folder's name, icon and colour. This is the part that
 * decides, from each report, whether the card keeps the face it has, drops
 * it, takes a new one, or asks the folder behind it again; the face itself and
 * its folder's question are the launcher's.
 *
 * A card can carry more than one transfer: the fetch of an app's data folder
 * and the install it announced, or an install that starts again once its
 * copies are on the card. The reports of the later one name no project until
 * its image is read. The card keeps its face through them, and the folder is
 * asked again, once: the transfer before may be the one that wrote it.
 */
#ifndef KGUI_WFACE_H
#define KGUI_WFACE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KGUI_WFACE_PROJECT_MAX  32U    /**< A project name, terminator included: KODE_APP_PROJECT_MAX. */
#define KGUI_WFACE_WAIT_MS      5000U  /**< The longest the card waits off the glass for its folder's words, from the report that names the app: a folder already on the board answers in well under a second, and an app with no folder still gets its card. Tunable. */

/** @brief What a report does to the card's face. */
typedef enum {
    KGUI_WFACE_KEEP = 0,  /**< Nothing: the same app, or still nobody.           */
    KGUI_WFACE_DROP,      /**< The card is about no app any more: the face goes. */
    KGUI_WFACE_TAKE,      /**< Another app: its face is to be taken.             */
    KGUI_WFACE_AGAIN      /**< The same face: its folder is to be asked again.   */
} kgui_wface_act_t;

/** @brief Whose face the card wears. */
typedef struct {
    char project[KGUI_WFACE_PROJECT_MAX];  /**< The app's project name; "" for nobody. */
    bool owed;                             /**< Its folder is still to be asked.       */
    bool unnamed;                          /**< The last report named nobody, and the
                                            *   folder was asked again for it.         */
    bool known;                            /**< The face carries its folder's words:
                                            *   the card may rise.                     */
    uint32_t t0_ms;                        /**< When a report named the app.           */
} kgui_wface_t;

/**
 * @brief The face carries its folder's words: the app's name, from its data
 *        folder or from a row that heard from it.
 *
 * @param w The card's face.
 */
void kgui_wface_known(kgui_wface_t *w);

/**
 * @brief Whether the card of an app's write about no row waits off the glass.
 *
 * Until a report names the app the card has only the file's name, which for
 * a download is the last part of its URL: it waits for the name however long
 * that takes. Named, it has only the project's name and letters in place of
 * the icon until its folder's words are there: it waits for them, and for no
 * longer than KGUI_WFACE_WAIT_MS from the report that named the app, since an
 * app that has no folder is written all the same.
 *
 * @param w      The card's face.
 * @param now_ms The clock.
 * @return true while no report has named the app, or while the face has no
 *         words and the wait has not run out.
 */
bool kgui_wface_waits(const kgui_wface_t *w, uint32_t now_ms);

/**
 * @brief A report of a write arrived: say what it does to the card's face.
 *
 * @param w       The card's face.
 * @param app     The write is an app's; false for kodeOS and the radio.
 * @param project The project the report names; "" when it names none.
 * @param now_ms  The clock: the wait for the folder counts from the report
 *                that names an app the card did not wear.
 * @return KGUI_WFACE_TAKE for an app other than the one the card wears, with
 *         @c owed set; KGUI_WFACE_DROP when the card wore a face and the
 *         report is no app's; KGUI_WFACE_AGAIN, with @c owed set, for the first
 *         of an app's reports that name nobody on a card that wears a face;
 *         KGUI_WFACE_KEEP otherwise.
 */
kgui_wface_act_t kgui_wface_report(kgui_wface_t *w, bool app, const char *project,
                                   uint32_t now_ms);

/**
 * @brief The write ended: the card's face is nobody's.
 *
 * @param w The card's face.
 */
void kgui_wface_end(kgui_wface_t *w);

/**
 * @brief Whether the card wears a face.
 *
 * @param w The card's face.
 * @return true when a report has named an app and the write has not ended.
 */
bool kgui_wface_has(const kgui_wface_t *w);

/**
 * @brief Whether a folder's answer is about the card's face.
 *
 * @param w       The card's face.
 * @param project The project the answer names.
 * @return true when the card wears a face and it is that project's.
 */
bool kgui_wface_is(const kgui_wface_t *w, const char *project);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_WFACE_H */
