/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_off.h
 * @brief The face's red arc: how far it has run, and how much of each piece of the path that is.
 */
#ifndef KGUI_OFF_H
#define KGUI_OFF_H

#include <stdint.h>

#include "kgui_edge.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GUI_OFF_ARC_FROM_MS   2000U /**< How long the bottom button is held on the face before the arc starts to fill. */
#define GUI_OFF_ARC_TO_MS     5000U /**< How long it is held for the arc to be full, and "Turn off?" to open: well past home's 0.6 s and short of the PMIC's 16 s QON reset. */
#define GUI_OFF_ARC_EMPTY_MS  160U  /**< How long a full or part arc takes to empty when the button is let go. */
#define KGUI_OFF_FULL_PM      1000U /**< A whole arc, in thousandths. */
#define KGUI_OFF_ARC_W        ((uint32_t)KGUI_EDGE_W)       /**< The arc's line, in pixels: the edge's (kgui_edge.h). */
#define KGUI_OFF_ARC_R        ((uint32_t)KGUI_EDGE_R)       /**< The radius of its corners, to the middle of the line: the edge's. */
#define KGUI_OFF_ARC_EDGE     ((uint32_t)KGUI_EDGE_LINE_IN) /**< The middle of the line stands this far in from the glass: the edge's. */
#define KGUI_OFF_GLASS_W      410U  /**< The glass's width, which the straight spans; the face checks it against the board. */
#define KGUI_OFF_QUARTER_NUM  157U  /**< A quarter of a circle is its radius times this over KGUI_OFF_QUARTER_DEN (pi / 2). */
#define KGUI_OFF_QUARTER_DEN  100U  /**< See KGUI_OFF_QUARTER_NUM. */
#define KGUI_OFF_CORNER_PX    ((KGUI_OFF_ARC_R * KGUI_OFF_QUARTER_NUM) / KGUI_OFF_QUARTER_DEN) /**< The length of one corner of the path, a quarter of a circle, in pixels (160). */
#define KGUI_OFF_LINE_PX      (KGUI_OFF_GLASS_W - (2U * (KGUI_OFF_ARC_EDGE + KGUI_OFF_ARC_R))) /**< The length of the straight between the corners, in pixels (182). */
#define KGUI_OFF_PATH_PX      ((2U * KGUI_OFF_CORNER_PX) + KGUI_OFF_LINE_PX) /**< The whole path: corner, straight, corner. */

/** @brief How much of each piece of the path is drawn. */
typedef struct {
    uint16_t left_pm;   /**< The top left corner, in thousandths of the corner. */
    uint16_t line_px;   /**< The straight, in pixels from its left end.         */
    uint16_t right_pm;  /**< The top right corner, in thousandths of the corner. */
} kgui_off_parts_t;

/**
 * @brief How far the arc has run while the button is held.
 *
 * Nothing before GUI_OFF_ARC_FROM_MS, then linear, whole at GUI_OFF_ARC_TO_MS.
 *
 * @param held_ms How long the bottom button has been down.
 * @return 0 to KGUI_OFF_FULL_PM.
 */
uint16_t kgui_off_fill_pm(uint32_t held_ms);

/**
 * @brief How much is left of the arc a time after the button was let go.
 *
 * Linear, empty after GUI_OFF_ARC_EMPTY_MS whatever it stood at.
 *
 * @param from_pm  Where the arc stood when the button was let go.
 * @param since_ms How long ago that was.
 * @return 0 to @p from_pm.
 */
uint16_t kgui_off_empty_pm(uint16_t from_pm, uint32_t since_ms);

/**
 * @brief Split a share of the path over its three pieces.
 *
 * The arc fills along the path from the top left corner, so the straight
 * starts when the corner is whole, and the top right corner when the straight
 * is.
 *
 * @param pm  The share of the whole path, 0 to KGUI_OFF_FULL_PM (more is whole).
 * @param out Receives the pieces.
 */
void kgui_off_split(uint16_t pm, kgui_off_parts_t *out);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_OFF_H */
