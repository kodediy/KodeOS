/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_motion.h
 * @brief The launcher's rail: where its rows stand, and the motions from one row to another.
 */
#ifndef KGUI_MOTION_H
#define KGUI_MOTION_H

#include <stdbool.h>
#include <stdint.h>

#include "kbsp_board.h"
#include "kdrv_display.h"
#include "kgui_frames.h"
#include "kui_drag.h"
#include "kui_tokens.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Placement ------------------------------------------------------------ */

/* A placement is never owed before the least time below since the last one. A
 * tick that comes a little early must still place, so that the placements land
 * on the refreshes they are meant for: the least time between two of them is
 * the refreshes they are apart less the early share of one. */
#define KGUI_PLACE_EARLY_DIV        2U  /**< A tick may come early by a refresh over this: half a refresh. */
#define KGUI_PLACE_SECOND_REFRESHES 2U  /**< The refreshes between two placements when every second one gets one. */
#define KGUI_PLACE_EVERY_REFRESH_MS \
    ((KDRV_DISPLAY_FRAME_US - (KDRV_DISPLAY_FRAME_US / KGUI_PLACE_EARLY_DIV)) / KGUI_US_PER_MS) /**< The least time between two placements when every refresh gets one, in milliseconds: the one pacing of a sheet's rise. */
#define KGUI_PLACE_EVERY_SECOND_MS \
    (((KGUI_PLACE_SECOND_REFRESHES * KDRV_DISPLAY_FRAME_US) - (KDRV_DISPLAY_FRAME_US / KGUI_PLACE_EARLY_DIV)) / KGUI_US_PER_MS) /**< The least time between two placements when every second refresh gets one, in milliseconds: the pacing of a dragged page. */

/** The least time between two placements of the list's objects, in milliseconds, for Step, Glide, Rubber, a drag and the level slide alike: KGUI_PLACE_EVERY_REFRESH_MS, or KGUI_PLACE_EVERY_SECOND_MS for the lighter rate. The one line that sets the list's frame rate. */
#define KGUI_LIST_PLACE_MS          KGUI_PLACE_EVERY_REFRESH_MS

/* ---- The rows ------------------------------------------------------------- */

#define KGUI_ROW_H        112                       /**< Every row's height, in pixels: the rail's pitch. */
#define KGUI_ROW_GAP      0                         /**< Between two rows, in pixels: the rail's rows touch. */
#define KGUI_ROW_PITCH    (KGUI_ROW_H + KGUI_ROW_GAP) /**< From one row's top to the next one's, in pixels: the distance between two rests of u. */
#define KGUI_ROW_MID      (KGUI_ROW_H / 2)          /**< The first row's middle from the content's top, in pixels: c0. */

_Static_assert((2 * KGUI_ROW_MID) == KGUI_ROW_H,
               "kgui_rows_light_y() is u only while the first row's middle is exactly half a row");

/* ---- The rail ------------------------------------------------------------- */

#define KGUI_LIST_FIRST_Y   72   /**< A list that fills the glass: its first row's middle where it starts (Luismi, 5-nov). */
#define KGUI_LIST_PULL_BAND 44   /**< A drag down that starts this high on Apps always pulls Settings, wherever the list stands. */
#define KGUI_SWIPE_EDGE_BAND KGUI_LIST_PULL_BAND /**< A press that lands this close to the glass's top or bottom edge may be landed again past the edge's first samples: the pull band, at both edges. */
#define KGUI_EDGE_SETTLE_PX 40   /**< How far from where it came down such a press is landed again when its first samples went the wrong way, in pixels. */
#define KGUI_RAIL_FOCUS_Y   251  /**< The focused row's middle on the glass, in pixels: the focus line. */
#define KGUI_RAIL_TOP       99   /**< A row whose icon starts above this is pushed in along the glass, in pixels. */
#define KGUI_RAIL_BOTTOM    403  /**< A row whose icon ends below this is pushed in along the glass, in pixels. */
#define KGUI_RAIL_BLOCK_MAX 4U   /**< A list of this many rows or fewer is a block centred on the focus line and does not scroll. */
#define KGUI_RAIL_ICON      80   /**< A row's icon disc, in pixels. */
#define KGUI_RAIL_ICON_X    30   /**< The disc's left edge from the row's, in pixels. */
#define KGUI_RAIL_NAME_X    134  /**< The name's left edge from the row's, in pixels. */
#define KGUI_RAIL_NAME_W    258  /**< The name's width, in pixels. */
#define KGUI_RAIL_EDGE_FULL ((3 * KGUI_RAIL_ICON) / 4) /**< A row this far from the glass's top or bottom edge, in pixels, shows whole. */
#define KGUI_RAIL_EDGE_GONE (-(KGUI_RAIL_ICON / 2))    /**< A row whose middle is this far past the edge, in pixels, is gone. */

/* ---- The shelf ------------------------------------------------------------ */

/* The last apps run, a row of discs lying above the root's first row: the
 * rail's row turned on its side, the same 80 px disc on the same 112 px pitch,
 * so every gap is 32 px across and down (the launcher design of 5-nov,
 * kgui_shelf.h). It moves with the content and fades on its own before the
 * corners. Its motions are multiples of 33 ms. */
#define KGUI_SHELF_MAX          3U    /**< Recents on the shelf, the latest on the left. */
#define KGUI_SHELF_PITCH        112   /**< From one disc's middle to the next one's, in pixels: KGUI_ROW_PITCH. */
#define KGUI_SHELF_MID_X        205   /**< The shelf's middle: the glass's. */
#define KGUI_SHELF_FULL_Y       (KGUI_RAIL_FOCUS_Y - KGUI_ROW_PITCH) /**< 139: the discs' middle with the first row on the focus line, whole. */
#define KGUI_SHELF_TOP_Y        56    /**< The discs' middle where a root that fills the glass starts: the top of it (Luismi, 5-nov). */
#define KGUI_SHELF_GONE_Y       45    /**< A middle this high, in pixels, shows nothing. */
#define KGUI_SHELF_TAP_Y        50    /**< A disc whose middle is higher than this takes no tap. */
#define KGUI_SHELF_HIT_PAD      12    /**< Each disc's touch reaches this far past it: 104 x 104. */
#define KGUI_SHELF_LABEL_Y      52    /**< The focused recent's name's middle, with the shelf whole. */
#define KGUI_SHELF_LABEL_W      300   /**< The name's width, dotted past it. */
#define KGUI_SHELF_LABEL_RISE   6     /**< The name comes in rising this far, in pixels. */
#define KGUI_SHELF_NUDGE_PX     8     /**< The ring at an end leans out this far and comes back. */
#define KGUI_SHELF_CLIMB_MS     132U  /**< The ring from the first row up to the shelf, or back down. */
#define KGUI_SHELF_SIDE_MS      99U   /**< The ring from one recent to the next. */
#define KGUI_SHELF_NUDGE_MS     66U   /**< Each half of the lean at an end. */
#define KGUI_SHELF_LABEL_IN_MS  99U   /**< The name coming in. */
#define KGUI_SHELF_LABEL_OUT_MS 66U   /**< The name going, and each half of a change. */
#define KGUI_SHELF_POP_MS       132U  /**< Each disc coming in from the face. */
#define KGUI_SHELF_POP_STAGGER_MS 33U /**< One disc after the other, left to right. */
#define KGUI_SHELF_POP_FROM_PCT 90U   /**< The size a disc comes in from, in hundredths of its own. */

/** @brief How a row stands against the glass's rounded edge. */
typedef struct {
    int32_t dx;    /**< How far the corner pushes the row in, in pixels. */
    uint8_t opa;   /**< How much of the row shows, 0 to 255. */
    bool    gone;  /**< The row is wholly off the glass. */
    bool    outside; /**< The row's icon is not wholly inside the rail: a tap on it only focuses it. */
} kgui_rail_t;

/* ---- Step ----------------------------------------------------------------- */

#define KGUI_STEP_PERIOD_MS  145U /**< Step's spring: 90 % of the way in 90 ms, the apocrifa's KS_T_PAGE. */
#define KGUI_PAGE_SCROLL_MS  180U /**< A settings page's own animated scroll, a pad step or a landing, in milliseconds: the time a row of Apps takes under Step. */
#define KGUI_PAD_HOLD_MS     400U /**< A pad button held this long, in milliseconds, starts to repeat. */
#define KGUI_PAD_REPEAT_MS   120U /**< Then it steps a row every this many milliseconds. */
#define KGUI_PAD_FAST_AFTER  8U   /**< After this many repeats a step takes KGUI_PAD_FAST_ROWS rows. */
#define KGUI_PAD_FAST_ROWS   2U   /**< Rows a repeat steps once the hold has gone on. */

/* ---- Glide ---------------------------------------------------------------- */

#define KGUI_GLIDE_DECAY     0.992F /**< The fling's speed after one millisecond is this share of the speed before it. */
#define KGUI_GLIDE_REACH_MS  (KGUI_GLIDE_DECAY / (1.0F - KGUI_GLIDE_DECAY)) /**< How long the release speed would run: the milliseconds its decay adds up to. */
#define KGUI_GLIDE_MAX_ROWS  5U     /**< A fling carries the list at most this many rows. */
#define KGUI_GLIDE_PERIOD_MS 280U   /**< Glide's landing spring: d = 0.28 s, from the release speed. */
#define KGUI_GLIDE_ZETA      0.9F   /**< Its damping ratio (b = 0.1): it crosses its row once and comes back, by 0.15 % of the travel. */

/* ---- Rubber --------------------------------------------------------------- */

#define KGUI_RUBBER_LIMIT_PX      72.0F  /**< The most a pull past an end shows, in pixels: f(x) = x d c / (d + c x) with d this. */
#define KGUI_RUBBER_GAIN          0.55F  /**< The c of that rubber: how much of a short pull shows. */
#define KGUI_RUBBER_PERIOD_MS     300U   /**< The way back: d = 0.30 s, from the release speed. */
#define KGUI_RUBBER_TOUCH_PULL_PX 36.0F  /**< The pull a pad that meets an end stands for, in pixels, shown through the same rubber. */

/* ---- Slide ---------------------------------------------------------------- */

#define KGUI_SLIDE_PAGE_MS         120U /**< A page slides in this long, in milliseconds, easing out: kshell's KS_T_MOVE since 14-oct. */
#define KGUI_SLIDE_LEAVE_PERIOD_MS (KGUI_SLIDE_PAGE_MS / 2U) /**< The content leaves: half the page's time. */

/* ---- Rest ----------------------------------------------------------------- */

#define KGUI_REST_PX          KUI_DRAG_REST_PX          /**< A position this close to its target, in pixels, is there: the page's own rest. */
#define KGUI_REST_SPEED       KUI_DRAG_REST_SPEED_PX_MS /**< A speed under this, in pixels per millisecond, is none. */
#define KGUI_REST_UNITS       3.0F                      /**< A progress counted in thousandths is at its end within this: about a pixel of the longest travel it stands for. */
#define KGUI_REST_UNITS_SPEED 0.15F                     /**< ...and one that moves slower than this, in thousandths a millisecond, is still. */

/**
 * @brief A float cut to the nearest whole pixel, halves away from zero.
 *
 * @param v The value, within the range of an int32_t.
 * @return The nearest integer.
 */
int32_t kgui_round_px(float v);

/* ---- Rubber --------------------------------------------------------------- */

/**
 * @brief What a pull past an end shows.
 *
 * f(x) = x d c / (d + c x), d and c being KGUI_RUBBER_LIMIT_PX and
 * KGUI_RUBBER_GAIN: nearly c of a short pull, and never more than d.
 *
 * @param past How far the finger has gone past the end, not negative.
 * @return How far the list shows it.
 */
float kgui_rubber(float past);

/**
 * @brief The pull that shows a distance: kgui_rubber() the other way.
 *
 * @param shown How far the list stands past the end, not negative.
 * @return The pull; a distance at the limit gives a long one.
 */
float kgui_rubber_pull(float shown);

/**
 * @brief Where a list stands that is asked to go to a position, as it shows.
 *
 * A position within the first and the last row is shown as it is. One past an
 * end is shown through kgui_rubber(), so a spring that carries the list past
 * an end draws it the same way a finger does, and no further than
 * KGUI_RUBBER_LIMIT_PX.
 *
 * @param raw The position asked for, before the rubber.
 * @param lo  The position of the first row.
 * @param hi  The position of the last row.
 * @return The position to draw.
 */
float kgui_rubber_shown(float raw, float lo, float hi);

/**
 * @brief The position that shows as a given one: kgui_rubber_shown() the
 *        other way.
 *
 * @param shown The position drawn.
 * @param lo    The position of the first row.
 * @param hi    The position of the last row.
 * @return The position, before the rubber.
 */
float kgui_rubber_raw(float shown, float lo, float hi);

/* ---- The rows ------------------------------------------------------------- */

/**
 * @brief The height of the rows together.
 *
 * @param n How many rows.
 * @return n rows, in pixels; 0 for no rows.
 */
int32_t kgui_rows_content(uint32_t n);

/**
 * @brief Where u stands when a row is the selection and nothing moves.
 *
 * @param row The row.
 * @return row * KGUI_ROW_PITCH, in pixels.
 */
float kgui_rows_rest(uint32_t row);

/**
 * @brief Where u stands at the last row: the end of its travel.
 *
 * @param n How many rows.
 * @return The last row's rest; 0 for none or one row.
 */
float kgui_rows_span(uint32_t n);

/**
 * @brief The row nearest the focus line at a u.
 *
 * @param n How many rows.
 * @param u The selection coordinate, in pixels; one outside the rows' own
 *          travel is the row at that end.
 * @return round(u / KGUI_ROW_PITCH) kept to the rows; 0 for no rows.
 */
uint32_t kgui_rows_nearest(uint32_t n, float u);

/**
 * @brief How far the content has gone up at a u: s(u).
 *
 * u + c0 - KGUI_RAIL_FOCUS_Y while the list scrolls, and the one offset that
 * centres the block while it does not (KGUI_RAIL_BLOCK_MAX). Linear in u, so a
 * pull past an end moves the content one pixel for one pixel.
 *
 * @param u The selection coordinate, in pixels.
 * @param n How many rows.
 * @return The content's offset, in pixels; negative where the first row
 *         stands below the glass's top.
 */
float kgui_rows_scroll(float u, uint32_t n);

/**
 * @brief Whether a list fills the glass: only then does it start at the top.
 *
 * Fewer rows are a block centred on the focus line (KGUI_RAIL_BLOCK_MAX).
 *
 * @param n How many rows.
 * @return true when the rows are at least as tall as the glass.
 */
bool kgui_rows_fill(uint32_t n);

/**
 * @brief kgui_rows_top_scroll() and kgui_rows_top_u() for a list whose first
 *        row's middle starts at @p first_y (a root with a shelf above it).
 *
 * @param v       The u, or the content's offset.
 * @param first_y Where the first row's middle starts on the glass.
 * @return The content's offset, or the u.
 */
float kgui_rows_top_scroll_at(float v, float first_y);
float kgui_rows_top_u_at(float v, float first_y);

/**
 * @brief s(u) for a list that fills the glass (Luismi, 5-nov).
 *
 * It starts with the first row at KGUI_LIST_FIRST_Y, not on the focus line, and
 * that is only where it starts: u under 0, down to the walk from there to the
 * focus line (KGUI_RAIL_FOCUS_Y - KGUI_LIST_FIRST_Y), takes the first row down
 * to the focus line, and from u = that walk it runs as any list does, the row
 * at u on the line. Between 0 and the walk the list stands still and the ring
 * walks down its rows.
 *
 * @param u The selection coordinate, in pixels.
 * @return The content's offset, in pixels.
 */
float kgui_rows_top_scroll(float u);

/**
 * @brief The u that gives an offset: what a finger on the content asks for.
 *
 * @param s The content's offset, in pixels.
 * @return u, never inside the stretch where the content stands still.
 */
float kgui_rows_top_u(float s);

/**
 * @brief Where the focus stands on the glass: c0 + u - s(u).
 *
 * @param u The selection coordinate, in pixels.
 * @param n How many rows.
 * @return The focused row's middle from the glass's top, in pixels:
 *         KGUI_RAIL_FOCUS_Y while the list scrolls.
 */
float kgui_rows_focus_y(float u, uint32_t n);

/**
 * @brief Where the ring's top stands in the content at a u.
 *
 * The first row's middle is half a row from the content's top, so the ring,
 * which stands on the focus, has its top at u. A u past an end, a pull the
 * rubber shows, leaves the ring on the end row: it goes with the content.
 *
 * @param u The selection coordinate as drawn, in pixels.
 * @param n How many rows.
 * @return u kept to the rows' travel, in pixels.
 */
float kgui_rows_light_y(float u, uint32_t n);

/**
 * @brief The row a fling lands on.
 *
 * The release speed is carried KGUI_GLIDE_REACH_MS ahead and the row nearest
 * that is the one, at most KGUI_GLIDE_MAX_ROWS from the row the list stood
 * nearest when the finger lifted.
 *
 * @param n   How many rows; at least one.
 * @param u   The selection coordinate at the release, in pixels.
 * @param vel u's speed at the release, pixels per millisecond, positive when
 *            the content goes up.
 * @return The row.
 */
uint32_t kgui_glide_row(uint32_t n, float u, float vel);

/* ---- The edge ------------------------------------------------------------- */

/**
 * @brief How a row stands against the glass's rounded edge.
 *
 * A row whose icon is outside the rail is pushed in along the corner's curve,
 * and its opacity follows its middle's distance to the nearest of the glass's
 * top and bottom edges: whole from KGUI_RAIL_EDGE_FULL in, none once the
 * middle is KGUI_RAIL_EDGE_GONE past the edge. The icon and the name share
 * it, so the name never leaves before its icon.
 *
 * @param cy  The row's middle from the glass's top, in pixels.
 * @param out Receives how the row stands.
 */
void kgui_rail_edge(int32_t cy, kgui_rail_t *out);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_MOTION_H */
