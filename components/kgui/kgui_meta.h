/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_meta.h
 * @brief When the Apps list may appear: the wait for the apps' metadata.
 */
#ifndef KGUI_META_H
#define KGUI_META_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KGUI_META_WAIT_MS    1000U /**< How long after a listing ends the list waits for the categories still to be read before it shows what it has. Tunable. */
#define KGUI_LIST_REVEAL_MS  150U  /**< How long the list and its bar take to fade in when the first rows appear. Tunable. */

/** @brief The wait. */
typedef struct {
    bool     open;    /**< The list has appeared: from now on rows are let on one by one. */
    bool     timing;  /**< t0_ms is the end of a listing. */
    uint32_t t0_ms;   /**< When the last listing ended: the wait counts from it. */
} kgui_meta_t;

/**
 * @brief A listing has ended and the lists stand settled: the wait counts from now.
 *
 * @param m      The wait.
 * @param now_ms The clock.
 */
void kgui_meta_listed(kgui_meta_t *m, uint32_t now_ms);

/**
 * @brief Whether the wait has run out.
 *
 * @param m      The wait.
 * @param now_ms The clock.
 * @return true KGUI_META_WAIT_MS after the last listing ended; false before
 *         it, and when no listing has ended.
 */
bool kgui_meta_due(const kgui_meta_t *m, uint32_t now_ms);

/**
 * @brief How long until the wait runs out.
 *
 * @param m      The wait.
 * @param now_ms The clock.
 * @return The milliseconds left; 0 when it has run out or no listing has ended.
 */
uint32_t kgui_meta_left_ms(const kgui_meta_t *m, uint32_t now_ms);

/**
 * @brief Whether the list may be shown now.
 *
 * @param m       The wait.
 * @param total   The rows there are.
 * @param known   Of them, the rows whose category is known.
 * @param now_ms  The clock.
 * @return true once the list has appeared, when every row's category is known,
 *         or when the wait has run out.
 */
bool kgui_meta_shows(const kgui_meta_t *m, uint32_t total, uint32_t known,
                     uint32_t now_ms);

/**
 * @brief Whether a row is let on.
 *
 * @param known The row's category is known.
 * @param due   The wait has run out (kgui_meta_due()).
 * @return true for a row whose category is known or whose wait has run out.
 */
bool kgui_meta_admits(bool known, bool due);

/**
 * @brief The opacity of the list and its bar a time after they began to appear.
 *
 * @param gone_ms Milliseconds since the fade began.
 * @return 0, rising in a straight line to 255 at KGUI_LIST_REVEAL_MS and 255
 *         after it.
 */
uint8_t kgui_meta_reveal_opa(uint32_t gone_ms);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_META_H */
