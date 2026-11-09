/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_test_dot.h
 * @brief The E5 test's screens and patterns on kodeOS's glass.
 */
#ifndef KGUI_TEST_DOT_H
#define KGUI_TEST_DOT_H

#include <stdint.h>

#include "kqp.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The request ---- */

/** @brief A request for the glass, copied out of its event. */
typedef struct {
    uint8_t kind;                          /**< TEST_DOT_SCR_* or TEST_DOT_PAT_*. */
    uint8_t seq;                           /**< The request's number, echoed in the answer. */
    uint8_t len;                           /**< Bytes of @c arg that count.       */
    uint8_t arg[TEST_DOT_SCREEN_ARG_MAX];  /**< The screen's states, or the pattern's one byte. */
} kgui_td_req_t;

/**
 * @brief Copy a request out of its event, so it outlives the event.
 * @param out The copy.
 * @param e   The request.
 * @return Nothing: a NULL argument copies nothing.
 */
void kgui_td_copy(kgui_td_req_t *out, TestDotScreenEvt const *e);

/* ---- The canvas ---- */

/**
 * @brief Put what @p r names on @p page, in place of what was there.
 *
 * The canvas the last request built is deleted before the next is built: two
 * whole-glass canvases at once would take twice the LVGL heap. An auto screen
 * over an auto screen only changes the cells that changed. TEST_DOT_PAT_OFF
 * leaves the page bare.
 * @param page The page the canvas goes on.
 * @param r    The request; TEST_DOT_SCR_END clears and builds nothing.
 * @return Nothing: a canvas LVGL cannot make leaves the page bare and is logged.
 */
void kgui_td_show(lv_obj_t *page, kgui_td_req_t const *r);

/**
 * @brief Delete the canvas, if there is one.
 * @return Nothing.
 */
void kgui_td_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_TEST_DOT_H */
