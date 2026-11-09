/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_rise.h
 * @brief Rise: how a system card with no origin arrives, and how long it refuses input.
 */
#ifndef KGUI_RISE_H
#define KGUI_RISE_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "kui_spring.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KGUI_RISE_PERIOD_MS  90U    /**< The spring's period: kshell's KS_T_PAGE rise, d = 0.09 s, b = 0. Tunable. */
#define KGUI_RISE_FALL_MS    80U    /**< How long a system card takes to go: kshell's KS_T_BACK fall. */
#define KGUI_RISE_INPUT_MS   700U   /**< How long a risen card ignores input, from the moment it began to rise. */

/** @brief One rising card. */
typedef struct {
    kui_spring_t sp;         /**< The progress, in thousandths of the way up. */
    lv_obj_t    *card;       /**< The card; not owned. */
    lv_obj_t    *veil;       /**< Over the card's content until its text comes in. */
    int32_t      from_y;     /**< Where the card begins: off the glass. */
    int32_t      to_y;       /**< Where it rests. */
    bool         moving;     /**< The spring runs. */
    bool         armed;      /**< The input gate counts: set by a start, kept until the card leaves. */
    uint32_t     t0_ms;      /**< When it began. */
    uint32_t     placed_ms;  /**< When the objects were last placed. */
} kgui_rise_t;

/**
 * @brief Where the card stands at a progress.
 *
 * @param from_y Where it begins.
 * @param to_y   Where it rests.
 * @param u      The progress, 0 to KGUI_GROW_UNITS.
 * @return The card's top.
 */
int32_t kgui_rise_y(int32_t from_y, int32_t to_y, int32_t u);

/**
 * @brief Whether a card that began to rise a time ago takes input.
 *
 * @param since_ms Milliseconds since it began.
 * @return true from KGUI_RISE_INPUT_MS on.
 */
bool kgui_rise_input_after(uint32_t since_ms);

/**
 * @brief Make the veil on a card, once.
 *
 * The veil is the card's own colour and corners, as big as the glass, the
 * card's last child and hidden until a rise puts it up. A second call keeps
 * what stands.
 *
 * @param r    Receives the card and the veil.
 * @param card The card of a system sheet.
 * @return true when the veil stands.
 */
bool kgui_rise_attach(kgui_rise_t *r, lv_obj_t *card);

/**
 * @brief Begin to rise: the card is put at the bottom, under its veil.
 *
 * @param r      The rise.
 * @param from_y Where the card begins: off the glass.
 * @param to_y   Where it rests.
 * @param now_ms The clock.
 */
void kgui_rise_start(kgui_rise_t *r, int32_t from_y, int32_t to_y, uint32_t now_ms);

/**
 * @brief Begin the input gate alone: the card stays where it is and refuses
 *        input for KGUI_RISE_INPUT_MS from the call.
 *
 * What a card taken over by another system card asks for: its glass is covered
 * already, so it does not rise again, but what it says is new.
 *
 * @param r      The rise.
 * @param now_ms The clock.
 */
void kgui_rise_gate(kgui_rise_t *r, uint32_t now_ms);

/**
 * @brief Step the card to the clock and place it when a placement is due.
 *
 * @param r      The rise.
 * @param now_ms The clock.
 * @return true on the step on which the card lands.
 */
bool kgui_rise_tick(kgui_rise_t *r, uint32_t now_ms);

/**
 * @brief The card leaves or is shown at once: the motion stops where the
 *        card stands, the veil is off and the input gate is open.
 *
 * @param r The rise.
 */
void kgui_rise_stop(kgui_rise_t *r);

/**
 * @brief Whether the card takes input now.
 *
 * @param r      The rise.
 * @param now_ms The clock.
 * @return true when it has not been made to rise, or KGUI_RISE_INPUT_MS have
 *         gone by since it began to.
 */
bool kgui_rise_input_open(const kgui_rise_t *r, uint32_t now_ms);

/**
 * @brief How long the input gate still stays shut.
 *
 * @param r      The rise.
 * @param now_ms The clock.
 * @return Milliseconds to the gate's opening; 0 when it is open.
 */
uint32_t kgui_rise_input_left(const kgui_rise_t *r, uint32_t now_ms);

/**
 * @brief How long the next tick may wait while the card moves or its input
 *        gate is closed.
 *
 * @param r      The rise.
 * @param now_ms The clock.
 * @param next   The wait the caller would take.
 * @return @p next, or the time to the next placement or to the gate's opening
 *         when that is less.
 */
uint32_t kgui_rise_wait(const kgui_rise_t *r, uint32_t now_ms, uint32_t next);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_RISE_H */
