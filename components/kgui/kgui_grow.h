/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_grow.h
 * @brief What the rising sheets and the launch share: progress scale, icon place and the Tap pop.
 */
#ifndef KGUI_GROW_H
#define KGUI_GROW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The progress ---------------------------------------------------------- */

#define KGUI_GROW_UNITS       1000 /**< The progress at rest; a unit is a thousandth of the way. */
#define KGUI_GROW_TEXT_AT     900  /**< The progress at which a system card's words begin to appear: 90 % of the way. Tunable. */

/** @brief The flying icon: its middle and its side. */
typedef struct {
    int32_t cx;    /**< Middle, across. */
    int32_t cy;    /**< Middle, down. */
    int32_t size;  /**< Its side. */
} kgui_grow_icon_t;

/**
 * @brief How much of a system card's words shows at a progress.
 *
 * Nothing up to KGUI_GROW_TEXT_AT, all of it at rest, and a straight ramp
 * between.
 *
 * @param u The progress.
 * @return An opacity, 0 to 255.
 */
uint8_t kgui_grow_text_opa(int32_t u);

/* ---- The Tap pop ---------------------------------------------------------- */

#define KGUI_POP_ONE        256U /**< The scale of 1.0, as LVGL reads it. */
#define KGUI_POP_PEAK_PCT   106U /**< The middle pose: 1.06. Tunable. */
#define KGUI_POP_POSE_MS    99U  /**< From one pose to the next. Tunable. */
#define KGUI_POP_TOTAL_MS   (2U * KGUI_POP_POSE_MS) /**< The whole pop. */

/**
 * @brief The scale of the pressed button a time after the press.
 *
 * @param elapsed_ms Milliseconds since the pop began.
 * @return KGUI_POP_ONE, rising to the peak at KGUI_POP_POSE_MS and back at
 *         KGUI_POP_TOTAL_MS; KGUI_POP_ONE after it.
 */
uint32_t kgui_pop_scale(uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_GROW_H */
