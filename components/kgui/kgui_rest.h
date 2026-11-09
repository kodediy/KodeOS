/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_rest.h
 * @brief The Lock at Rest: the numbers it runs on and the cycle of its burn-in shift.
 */
#ifndef KGUI_REST_H
#define KGUI_REST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The numbers Rest runs on ----------------------------------------------- */

#define KGUI_REST_TICK_MS      1000U  /**< How long AO_Gui's tick waits at Rest, in milliseconds. */
#define KGUI_SETTLE_STEP_MS    90U    /**< Between two steps of the brightness, dimming or waking, in milliseconds. */
#define KGUI_WOKEN_REST_MS     60000U /**< How long a Lock woken and left alone stays awake before it rests again, in milliseconds. */
#define KGUI_REST_SHIFT_PX     4      /**< The farthest the face's content moves from its place on each axis, in pixels. */
#define KGUI_REST_CYCLE_LEN    9U     /**< Places in the cycle of offsets: three on each axis. */
#define KGUI_SLEEP_CUE_MS      990U   /**< BOT held this long on the face and Kody starts to fall asleep (30 ticks of 33 ms). */
#define KGUI_VEIL_MS           363U   /**< Kody's last frame fading out over the next one, colour to outline and back: the apocrifa's 350 ms on the 33 ms grid (11 ticks). */
#define KGUI_FALL_MAX_MS       3300U  /**< The longest <scene>_duerme takes (90 frames) and a little: past it the fall goes on regardless. */
#define KGUI_PEEK_MS           5940U  /**< How long the first TOP press at Rest waits for the second (180 ticks of 33 ms). */
#define KGUI_KNOCK_OUT_MS      198U   /**< The knock's row goes (6 ticks). */
#define KGUI_KNOCK_BEAT_HALF_MS 495U  /**< The blue dot, down or up: a beat a second (15 ticks). */
#define KGUI_KNOCK_BEAT_LOW_OPA 153U  /**< The blue dot at its faintest (of 255): Kody Blue is dark on black, so the dot stays readable at the bottom of the beat. */
#define KGUI_PEEK_DIM_OPA      90U    /**< The clock's and the date's opacity (of 255) while Kody alone is awake. */
#define KGUI_REST_SLEEP_CALM_MS  2000U /**< Settled this long, Rest asks AO_LowPower for ASLEEP, in milliseconds. */
#define KGUI_REST_SLEEP_AGAIN_MS 60000U /**< An ask that did not end asleep is made again after this, in milliseconds. */

/**
 * @brief The brightness steps of Rest, in percent of the stored level, from
 *        the stored level itself down to the Rest level: the apocrifa's half
 *        rest, 60 % of the person's (its decision 0016).
 *
 * Meant for an array initialiser: dimming plays them in this order, waking
 * in the other.
 */
#define KGUI_REST_BRIGHT_STEPS  100U, 90U, 80U, 70U, 60U

/* ---- The burn-in cycle ------------------------------------------------------ */

/** @brief One place of the cycle: how far the content stands from home. */
typedef struct {
    int32_t x;  /**< Pixels to the right of home; negative is to the left. */
    int32_t y;  /**< Pixels below home; negative is above it.              */
} kgui_rest_offset_t;

/**
 * @brief One place of the fixed cycle of offsets.
 *
 * The cycle starts at home, then visits the eight places a step of
 * KGUI_REST_SHIFT_PX on each axis makes round it, once each, and starts again.
 *
 * @param step The place: any number, taken modulo KGUI_REST_CYCLE_LEN.
 * @return Its offset, at most KGUI_REST_SHIFT_PX on either axis.
 */
kgui_rest_offset_t kgui_rest_offset(uint32_t step);

/* ---- Asleep at Rest ----------------------------------------------------------- */

/** @brief What Rest knows when it decides whether to ask for ASLEEP. */
typedef struct {
    bool     settled;   /**< Kody stands still in his outline, the glass is at the
                          *   Rest level, and nothing moves or is held.        */
    bool     cable;     /**< A cable is in: the board does not sleep on one.  */
    uint32_t calm_ms;   /**< How long it has been settled, in milliseconds.    */
    bool     asked;     /**< Rest has asked since the board last slept.        */
    uint32_t asked_ms;  /**< How long ago it asked, in milliseconds.           */
} kgui_rest_sleep_t;

/**
 * @brief Whether Rest asks AO_LowPower for ASLEEP now.
 *
 * Settled for KGUI_REST_SLEEP_CALM_MS, on the battery, and either not asked
 * yet or asked KGUI_REST_SLEEP_AGAIN_MS ago or more: an ask that met a busy
 * owner, or a cable that came and went, is not repeated every tick.
 *
 * @param s What Rest knows.
 * @return true when it asks.
 */
bool kgui_rest_sleep_due(kgui_rest_sleep_t const *s);

/**
 * @brief How long is left of a wait.
 *
 * @param gone_ms  Time gone since the wait began, in milliseconds.
 * @param after_ms The wait, in milliseconds.
 * @return The time left, 0 once it is over.
 */
uint32_t kgui_rest_left_ms(uint32_t gone_ms, uint32_t after_ms);

/**
 * @brief Where the hour's rectangle goes on the sleeping glass.
 *
 * The face's own clock at home, moved by the place of the cycle the content
 * stands at, on an even column and row (the panel's memory window asks for
 * them; KGUI_REST_SHIFT_PX is even, so an even home keeps every place exact).
 *
 * @param home_x  The rectangle's left edge at home, in glass pixels.
 * @param home_y  Its top edge at home.
 * @param w       Its width; the whole of it must stand on the glass.
 * @param h       Its height.
 * @param at      The place of the cycle.
 * @param glass_w The glass's width.
 * @param glass_h The glass's height.
 * @param[out] x  The left edge to write at.
 * @param[out] y  The top edge to write at.
 * @return false when the rectangle would not fit on the glass there; @p x and
 *         @p y are then left alone.
 */
bool kgui_rest_strip_at(int32_t home_x, int32_t home_y, int32_t w, int32_t h,
                        kgui_rest_offset_t at, int32_t glass_w, int32_t glass_h,
                        uint16_t *x, uint16_t *y);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_REST_H */
