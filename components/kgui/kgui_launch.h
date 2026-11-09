/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_launch.h
 * @brief The launch of an installed app: icon flight, Ring, Tap pop, fade to black, then the run.
 */
#ifndef KGUI_LAUNCH_H
#define KGUI_LAUNCH_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "kgui_grow.h"
#include "kgui_icon.h"
#include "kgui_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Time and size ---------------------------------------------------------- */

#define KGUI_LAUNCH_FLIGHT_MS    220U  /**< The icon's flight: the motion table's. */
#define KGUI_LAUNCH_SPIN_MS      528U  /**< The Ring turns this long before it closes: six of its positions. Tunable. */
#define KGUI_LAUNCH_HOLD_MS      495U  /**< The Ring stands whole and green this long before the fade (Luismi, 5-nov). */
#define KGUI_LAUNCH_FADE_STEPS   3U    /**< Steps of the fade to black. */
#define KGUI_LAUNCH_FADE_STEP_MS 50U   /**< Between two of them. Tunable. */
#define KGUI_LAUNCH_DIM_STEPS    4U    /**< Steps the glass darkens in during the flight. */
#define KGUI_LAUNCH_ICON_PX      176   /**< The icon's side in the middle of the glass, in pixels: the apocrifa's 160. Tunable. */
#define KGUI_LAUNCH_RING_W       10     /**< The Ring's line, in pixels: the apocrifa's fine ring round a launching icon, not the glass edge's 12. */
#define KGUI_LAUNCH_RING_GAP_PX  15    /**< From the icon's edge to the Ring's inner edge, in pixels: a Ring of 200 round the icon of 160, as in the apocrifa. */
#define KGUI_LAUNCH_RING_PX      (KGUI_LAUNCH_ICON_PX + (2 * (KGUI_LAUNCH_RING_GAP_PX + KGUI_LAUNCH_RING_W)))  /**< The Ring's outer diameter, in pixels: the icon, the gap and the line on each side. */

#define KGUI_LAUNCH_EV_RUN       0x01U /**< The fade is over: the run may be asked for. */

/** @brief Where the scene is. */
typedef enum {
    KGUI_LAUNCH_IDLE = 0,  /**< No scene. */
    KGUI_LAUNCH_FLIGHT,    /**< The icon flies to the middle. */
    KGUI_LAUNCH_SPIN,      /**< The Ring turns. */
    KGUI_LAUNCH_CLOSE,     /**< The Ring closes green and the icon pops. */
    KGUI_LAUNCH_FADE,      /**< The glass goes black. */
    KGUI_LAUNCH_DONE       /**< Black: the run is asked for. */
} kgui_launch_stage_t;

/* ---- The model -------------------------------------------------------------- */

/**
 * @brief The stage a time after the start is in.
 *
 * @param t_ms Milliseconds since the scene began.
 * @return KGUI_LAUNCH_FLIGHT to KGUI_LAUNCH_DONE.
 */
kgui_launch_stage_t kgui_launch_stage_at(uint32_t t_ms);

/**
 * @brief When a stage begins.
 *
 * @param stage KGUI_LAUNCH_FLIGHT to KGUI_LAUNCH_DONE.
 * @return Milliseconds after the start.
 */
uint32_t kgui_launch_stage_start(kgui_launch_stage_t stage);

/**
 * @brief How far the flight has gone, as a share of the way.
 *
 * An ease-out along KGUI_LAUNCH_FLIGHT_MS: the first frame already moves and
 * the last one is still.
 *
 * @param t_ms Milliseconds since the scene began.
 * @return 0 to KGUI_GROW_UNITS.
 */
int32_t kgui_launch_flight_u(uint32_t t_ms);

/**
 * @brief The icon's middle and side at a time, from where it began to the middle of the glass.
 *
 * @param from The icon where the flight begins.
 * @param t_ms Milliseconds since the scene began.
 * @return The icon.
 */
kgui_grow_icon_t kgui_launch_icon_at(const kgui_grow_icon_t *from, uint32_t t_ms);

/**
 * @brief How dark the glass is under the icon.
 *
 * @param t_ms Milliseconds since the scene began.
 * @return A black layer's opacity, 0 to 255, in KGUI_LAUNCH_DIM_STEPS steps.
 */
uint8_t kgui_launch_dim_opa(uint32_t t_ms);

/**
 * @brief How black the whole scene is.
 *
 * @param t_ms Milliseconds since the scene began.
 * @return A black layer's opacity, 0 to 255, in KGUI_LAUNCH_FADE_STEPS steps.
 */
uint8_t kgui_launch_fade_opa(uint32_t t_ms);

/**
 * @brief The share of the Ring that is drawn while it closes.
 *
 * @param t_ms Milliseconds since the scene began.
 * @return KGUI_RING_SPIN_ARC_PCT before the close, up to KGUI_RING_FULL_PCT at its end.
 */
uint8_t kgui_launch_close_pct(uint32_t t_ms);

/* ---- The scene -------------------------------------------------------------- */

/**
 * @brief Make the scene and begin the flight.
 *
 * A scene that already runs is ended first. The scene covers the glass and
 * takes every touch.
 *
 * @param layer  What the scene stands on: the layer above every screen and card.
 * @param look   The app's icon.
 * @param from   The icon where it stands now: its middle and side, on the glass.
 * @param now_ms The clock.
 * @return true when the scene stands; false when LVGL had no room, and
 *         nothing is left on the glass.
 */
bool kgui_launch_begin(lv_obj_t *layer, const kgui_icon_spec_t *look,
                       const kgui_grow_icon_t *from, uint32_t now_ms);

/**
 * @brief Step the scene to the clock and place what is due.
 *
 * @param now_ms The clock.
 * @return KGUI_LAUNCH_EV_RUN on the step on which the fade is over, once.
 */
uint32_t kgui_launch_tick(uint32_t now_ms);

/**
 * @brief How long the next tick may wait while the scene moves.
 *
 * @param now_ms The clock.
 * @param next   The wait the caller would take.
 * @return @p next, or the time to the scene's next placement or step when that is less.
 */
uint32_t kgui_launch_wait(uint32_t now_ms, uint32_t next);

/** @brief Take the scene off the glass and forget it. */
void kgui_launch_end(void);

/**
 * @brief Whether there is a scene.
 *
 * @return true from kgui_launch_begin() until kgui_launch_end().
 */
bool kgui_launch_active(void);

/**
 * @brief Whether the run can still be called off.
 *
 * @return true while the scene is on the glass and the run has not been
 *         reported (KGUI_LAUNCH_EV_RUN).
 */
bool kgui_launch_stoppable(void);

/**
 * @brief The stage's word, for `gui_info` and the log.
 *
 * @return "idle", "flight", "ring", "close", "fade" or "done".
 */
const char *kgui_launch_word(void);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_LAUNCH_H */
