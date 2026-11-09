/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_return.h
 * @brief The return to the app's place: how long the first frame after an app waits for the list.
 */
#ifndef KGUI_RETURN_H
#define KGUI_RETURN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Time ------------------------------------------------------------------ */

#define KGUI_LIST_WAIT_MS  800U  /**< The longest the first frame waits for the installed list's end; the board showed it 504 ms into the boot. */

/** @brief One wait. */
typedef struct {
    bool     began;      /**< A wait was begun on this boot, and has ended or not. */
    bool     holding;    /**< The first frame is held back. */
    bool     listed;     /**< The installed list ended while it was held back. */
    bool     asked;      /**< The frame was asked to go: kgui_return_go() said so once. */
    bool     late;       /**< The wait ended with no list: at the ceiling, or cut short. */
    uint32_t t0_ms;      /**< When the wait began. */
    uint32_t waited_ms;  /**< How long it lasted, once it ended. */
} kgui_return_t;

/* ---- The wait -------------------------------------------------------------- */

/**
 * @brief Begin the wait.
 *
 * @param r      The wait.
 * @param now_ms The clock.
 */
void kgui_return_begin(kgui_return_t *r, uint32_t now_ms);

/**
 * @brief Whether the first frame is held back.
 *
 * @param r The wait.
 * @return true from kgui_return_begin() until kgui_return_end().
 */
bool kgui_return_holds(const kgui_return_t *r);

/**
 * @brief The installed list has ended.
 *
 * @param r The wait. Nothing happens when it does not hold.
 */
void kgui_return_listed(kgui_return_t *r);

/**
 * @brief Whether the first frame may go now, said once.
 *
 * @param r      The wait.
 * @param now_ms The clock.
 * @return true on the first call after the list has ended or KGUI_LIST_WAIT_MS
 *         have gone by; false before, after, and when the wait does not hold.
 */
bool kgui_return_go(kgui_return_t *r, uint32_t now_ms);

/**
 * @brief End the wait: the frame goes.
 *
 * @param r      The wait.
 * @param now_ms The clock.
 */
void kgui_return_end(kgui_return_t *r, uint32_t now_ms);

/**
 * @brief How long the next tick may wait while the first frame is held back.
 *
 * @param r      The wait.
 * @param now_ms The clock.
 * @param next   The wait the caller would take.
 * @return @p next, or the time left to the ceiling when that is less.
 */
uint32_t kgui_return_wait(const kgui_return_t *r, uint32_t now_ms, uint32_t next);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_RETURN_H */
