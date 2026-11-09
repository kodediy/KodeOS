/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_frames.h
 * @brief Timing a motion by the frames AO_Gui hands to the panel.
 */
#ifndef KGUI_FRAMES_H
#define KGUI_FRAMES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits and units ----------------------------------------------------- */

#define KGUI_FRAMES_NAME_MAX  24U    /**< Room for a window's name and its terminator. */
#define KGUI_FRAMES_TAIL_REFRESHES 6U /**< How many refreshes a window that waits for its last frame waits for it, when none is drawn. */
#define KGUI_US_PER_MS        1000U  /**< Microseconds in a millisecond: the one name kgui's timing shares. */

/* ---- Windows -------------------------------------------------------------- */

/**
 * @brief Open a window for a motion; one that is running closes first.
 * @param name   The motion's name as its line prints it; copied, cut to
 *               KGUI_FRAMES_NAME_MAX - 1 characters.
 * @param now_us The time now in microseconds, any base that wraps at 32 bits.
 */
void kgui_frames_begin(const char *name, uint32_t now_us);

/**
 * @brief Count a frame in the running window; nothing without one.
 * @param now_us The time now, on the base kgui_frames_begin() was given.
 */
void kgui_frames_frame(uint32_t now_us);

/**
 * @brief Close the running window and log it; nothing without one.
 *
 * A window that drew no frame logs its line all the same, with 0 frames, a
 * zero average, a zero longest interval and an empty histogram: a motion the
 * glass never showed is the most telling reading. So does one that drew a
 * single frame, which has no interval to average.
 *
 * @param now_us The time now, on the base kgui_frames_begin() was given.
 */
void kgui_frames_end(uint32_t now_us);

/**
 * @brief Close the running window on the next frame, counting that frame
 *        first; nothing without a window.
 *
 * For a motion that ends in an animation's completion callback or at a
 * landing: the last value is drawn by the frame after the end is known, and
 * that frame belongs to the motion. A window that is begun, or ended, before
 * the frame comes closes as it always does.
 */
void kgui_frames_end_next(void);

/**
 * @brief Close a window that waits for a last frame no pass is going to draw.
 *
 * Called once a tick, after the pass. A window that was told it ends on the
 * next frame (kgui_frames_end_next()) and sees none within
 * KGUI_FRAMES_TAIL_REFRESHES refreshes of the first tick that asks is closed
 * as of its last frame, so the time the glass stood still is not counted in
 * it. Nothing without such a window.
 *
 * @param now_us The time now, on the base kgui_frames_begin() was given.
 */
void kgui_frames_tick(uint32_t now_us);

/**
 * @brief Whether a window is running.
 * @return true between kgui_frames_begin() and the end that closes it.
 */
bool kgui_frames_running(void);

/**
 * @brief Whether the running window is the one opened under a name.
 *
 * A motion whose window a later begin has replaced asks this before it
 * closes, so it never cuts the other's short.
 *
 * @param name The name kgui_frames_begin() was given.
 * @return true when a window runs and carries that name.
 */
bool kgui_frames_is(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_FRAMES_H */
