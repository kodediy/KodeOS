/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_shelf.h
 * @brief The launcher's shelf of recents: where its discs stand and fade, and the apps last run.
 */
#ifndef KGUI_SHELF_H
#define KGUI_SHELF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kgui_motion.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KGUI_SHELF_NAME_MAX 32U  /**< A recent's project name, terminator included (esp_app_desc_t.project_name). */
#define KGUI_RECENTS_LEN    (KGUI_SHELF_MAX * KGUI_SHELF_NAME_MAX) /**< The list as one string: three names, two line feeds and the terminator fit. */

/**
 * @brief Where a disc of the shelf stands across the glass.
 *
 * The discs are centred on the glass at the rail's pitch: KGUI_SHELF_MID_X +
 * (2i - (n - 1)) * KGUI_SHELF_PITCH / 2, so three stand at 93, 205 and 317,
 * two at 149 and 261, one at 205.
 *
 * @param i The disc, 0 for the latest.
 * @param n How many the shelf has, 1 to KGUI_SHELF_MAX.
 * @return Its middle's column, in pixels; the middle of the glass for an
 *         @p i that is not one of @p n.
 */
int32_t kgui_shelf_x(uint8_t i, uint8_t n);

/**
 * @brief How much of the shelf shows with its middle at a row.
 *
 * 255 from KGUI_SHELF_TOP_Y down, 0 from KGUI_SHELF_GONE_Y up, and linear in
 * between: the shelf leaves before the corner, as the rows are pushed in it.
 *
 * @param cy The discs' middle from the glass's top, in pixels.
 * @return The opacity, 0 to 255.
 */
uint8_t kgui_shelf_opa(int32_t cy);

/**
 * @brief The discs' middle on the glass for a box at a row.
 *
 * One pitch above the first row's middle, so the shelf moves with the content.
 *
 * @param box_y The top of the box the rows stand in, on the glass, in pixels.
 * @return The discs' middle from the glass's top, in pixels.
 */
int32_t kgui_shelf_cy(int32_t box_y);

/**
 * @brief How many rows the rail's scroll is to be worked out for.
 *
 * A short list is a block centred on the focus line (KGUI_RAIL_BLOCK_MAX); a
 * root with a shelf is not: its first row stands on the focus line at rest,
 * the shelf above it, so the scroll is the long list's.
 *
 * @param n     How many rows.
 * @param shelf true while the shelf shows.
 * @return The count for kgui_rows_scroll(): @p n, or one past the block.
 */
uint32_t kgui_shelf_scroll_rows(uint32_t n, bool shelf);

/**
 * @brief Read the list of recents into names.
 *
 * Empty lines are skipped, a name too long for its field is dropped, and a
 * name that is there already is not taken twice.
 *
 * @param list The list, one name per line; NULL reads as empty.
 * @param out  Out: the names, the latest first.
 * @param max  Room in @p out.
 * @return How many names were written.
 */
uint8_t kgui_recents_split(const char *list, char (*out)[KGUI_SHELF_NAME_MAX], uint8_t max);

/**
 * @brief Put a name at the front of the list of recents.
 *
 * A name that is there already moves to the front; the list keeps
 * KGUI_SHELF_MAX names at most, the oldest going. An empty name, or one too
 * long for its field, changes nothing.
 *
 * @param list The list, in and out: KGUI_RECENTS_LEN bytes, terminated.
 * @param name The project name of the app that ran.
 * @return true when the list changed.
 */
bool kgui_recents_push(char *list, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_SHELF_H */
