/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_edge.h
 * @brief The edge's numbers: its geometry, its times and the pure curves of its close and pop.
 */
#ifndef KGUI_EDGE_H
#define KGUI_EDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KGUI_RING_FULL_PM       1000U /**< A whole ring in thousandths: the unit of kgui_ring_pm(). */

/* ---- The edge ---- */

#define KGUI_EDGE_W             24    /**< The edge's line, in pixels: its outer side is flush with the glass. */
#define KGUI_EDGE_LINE_IN       12    /**< The middle of the line stands this far in from the glass: half the line (kshell's EDGE). */
#define KGUI_EDGE_R             102   /**< The radius of its corners, to the middle of the line: the glass's corner less KGUI_EDGE_LINE_IN (kshell's R). The face's red arc runs the same path (kgui_off.h). */
#define KGUI_EDGE_TRACK_HEX     0x1E1E1EU /**< The track under the edge's fill, 0xRRGGBB: kshell's own grey, drawn by the edge alone, so no kit role carries it. */
#define KGUI_EDGE_SPIN_STEPS    22U   /**< Positions of the edge's turning arc in one lap: with KGUI_RING_SPIN_STEP_MS, one lap of 1980 ms. */
#define KGUI_EDGE_CLOSE_MS      297U  /**< How long the edge takes to close when the work is done. */
#define KGUI_EDGE_POP_MS        99U   /**< Each half of the pop: the line thickens, then thins. */
#define KGUI_EDGE_POP_PX        8     /**< How much thicker the line is at the top of the pop, in pixels. */
#define KGUI_EDGE_DONE_MS       (KGUI_EDGE_CLOSE_MS + (2U * KGUI_EDGE_POP_MS)) /**< The whole of a done: the close and the pop. */
#define KGUI_EDGE_STEP_MS       33U   /**< How often a closing edge is drawn again. */
#define KGUI_EDGE_DOT_D         15    /**< The dot where a cut edge stopped, in pixels. */

/**
 * @brief The share of the edge a close has reached.
 *
 * @param from_pm  Where the close began, in thousandths.
 * @param since_ms How long it has run.
 * @return @p from_pm to KGUI_RING_FULL_PM, easing out.
 */
uint16_t kgui_ring_close_pm(uint16_t from_pm, uint32_t since_ms);

/**
 * @brief How much thicker the closed edge's line is, a time after it closed.
 *
 * @param since_ms Milliseconds since the edge was whole.
 * @return 0 to KGUI_EDGE_POP_PX and back to 0, in pixels.
 */
int32_t kgui_ring_pop_px(uint32_t since_ms);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_EDGE_H */
