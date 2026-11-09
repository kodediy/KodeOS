/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_ring.h
 * @brief The Ring: the circle that shows a write's progress, or that turns during a wait.
 */
#ifndef KGUI_RING_H
#define KGUI_RING_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "kgui_edge.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KGUI_RING_STEP_PCT      5U    /**< The ring is drawn in steps of this many percent. */
#define KGUI_RING_FULL_PCT      100U  /**< A whole ring. */
#define KGUI_RING_SPIN_STEPS    8U    /**< Positions of the turning ring in one turn. */
#define KGUI_RING_SPIN_STEP_MS  88U   /**< Between two positions: six of them are the apocrifa's 528 ms spin. */
#define KGUI_RING_SPIN_ARC_PCT  25U   /**< How much of the circle the turning ring covers. */
#define KGUI_RING_TURN_DEG      360   /**< A whole turn. */
#define KGUI_RING_TOP_DEG       270   /**< Where the ring begins: at the top of the circle. */

/** @brief What a ring is doing. */
typedef enum {
    KGUI_RING_IDLE = 0,   /**< Empty, and no clock runs. */
    KGUI_RING_PROGRESS,   /**< Showing a number somebody else reports. */
    KGUI_RING_SPIN,       /**< Turning, for a wait of no known length. */
    KGUI_RING_DONE,       /**< Full: the edge closed, or the write's progress is whole. */
    KGUI_RING_CLOSE       /**< The edge is closing and popping: kgui_ring_done(). */
} kgui_ring_mode_t;

/** @brief The objects and the path of a ring along the glass; private to the module. */
struct kgui_ring_glass;

/** @brief One ring. */
typedef struct {
    lv_obj_t         *arc;    /**< The arc of a circle ring; NULL for a glass ring, and until the ring is made. */
    struct kgui_ring_glass *glass; /**< A glass ring's objects; NULL for a circle. */
    kgui_ring_mode_t  mode;   /**< What it is doing. */
    uint32_t          t0_ms;  /**< When the form began. */
    uint8_t           pct;    /**< The share drawn, in percent: whole steps of KGUI_RING_STEP_PCT for the stepped forms. */
    uint16_t          pm;     /**< The share the drawing stands at, in thousandths of the ring. */
    uint8_t           spin;   /**< The position drawn while it turns. */
} kgui_ring_t;

/**
 * @brief The position of a turning ring a time after it began.
 *
 * @param turned_ms How long it has turned.
 * @return 0 to KGUI_RING_SPIN_STEPS - 1.
 */
uint8_t kgui_ring_spin_at(uint32_t turned_ms);

/**
 * @brief Make the ring: an arc of a width round the middle of a box, empty.
 *
 * The arc takes no touch. It starts at the top of the circle and fills
 * clockwise over a track.
 *
 * @param r      Receives the ring.
 * @param parent Where it stands.
 * @param size   The circle's outer diameter.
 * @param width  The line's width.
 * @param col    The colour it fills in.
 * @param track  The colour of the circle under it.
 * @return true when LVGL had room for it.
 */
bool kgui_ring_create(kgui_ring_t *r, lv_obj_t *parent, int32_t size,
                      int32_t width, lv_color_t col, lv_color_t track);

/**
 * @brief Make the edge: the glass ring of the system screens, empty.
 *
 * A glass ring KGUI_EDGE_W wide that runs along the glass, its outer side
 * flush with it (the line's middle 12 px in, radius 102), over a track of
 * KGUI_EDGE_TRACK_HEX. It begins at the top middle and fills clockwise; the
 * turning form is a quarter of it and goes round in one lap of
 * KGUI_EDGE_SPIN_STEPS steps. It takes no touch and covers the glass, so it
 * is made on a card of the whole glass.
 *
 * @param r      Receives the ring.
 * @param parent Where it stands, at the parent's top left.
 * @param col    The colour it fills in.
 * @return true when LVGL had room for it.
 */
bool kgui_ring_create_edge(kgui_ring_t *r, lv_obj_t *parent, lv_color_t col);

/**
 * @brief Change the colour it fills in.
 *
 * @param r   The ring.
 * @param col The colour.
 */
void kgui_ring_set_color(kgui_ring_t *r, lv_color_t col);

/**
 * @brief Move the ring to a place on its parent.
 *
 * A glass ring stands at its parent's top left and stays there.
 *
 * @param r The ring.
 * @param x Its left.
 * @param y Its top.
 */
void kgui_ring_place(kgui_ring_t *r, int32_t x, int32_t y);

/**
 * @brief Show the ring or take it off the glass.
 *
 * @param r    The ring.
 * @param show true to show it.
 */
void kgui_ring_show(kgui_ring_t *r, bool show);

/**
 * @brief Empty the ring.
 *
 * @param r The ring.
 */
void kgui_ring_cancel(kgui_ring_t *r);

/**
 * @brief Show a number: a writer's progress.
 *
 * @param r   The ring.
 * @param pct 0 to KGUI_RING_FULL_PCT; drawn in steps of KGUI_RING_STEP_PCT.
 */
void kgui_ring_progress(kgui_ring_t *r, uint8_t pct);

/**
 * @brief Close the edge: it fills to the whole, turns `ok` and pops.
 *
 * From where it stands the line runs on to the whole edge in
 * KGUI_EDGE_CLOSE_MS, then turns KUI_COLOR_OK and is KGUI_EDGE_POP_PX thicker
 * and back in two halves of KGUI_EDGE_POP_MS. Once is the whole gesture: asking again while it runs
 * does nothing. A circle ring is filled and left DONE.
 *
 * @param r      The ring.
 * @param now_ms The clock.
 */
void kgui_ring_done(kgui_ring_t *r, uint32_t now_ms);

/**
 * @brief Cut the edge: the fill turns the resting grey and a red dot stands
 *        where it stopped.
 *
 * What a write that failed shows. The ring keeps the share it was at;
 * kgui_ring_cancel() empties it and takes the dot away. A circle ring is not
 * cut.
 *
 * @param r The ring.
 */
void kgui_ring_cut(kgui_ring_t *r);

/**
 * @brief Begin the turning form.
 *
 * @param r      The ring.
 * @param now_ms The clock.
 */
void kgui_ring_spin(kgui_ring_t *r, uint32_t now_ms);

/**
 * @brief Step the ring to the clock.
 *
 * Draws what changed since the last step and nothing else.
 *
 * @param r      The ring.
 * @param now_ms The clock.
 */
void kgui_ring_tick(kgui_ring_t *r, uint32_t now_ms);

/**
 * @brief How long the next tick may wait while the ring moves.
 *
 * @param r      The ring.
 * @param now_ms The clock.
 * @param next   The wait the caller would take.
 * @return @p next, or the time to the ring's next step when that is less.
 */
uint32_t kgui_ring_wait(const kgui_ring_t *r, uint32_t now_ms, uint32_t next);

/**
 * @brief What the ring is doing.
 *
 * @param r The ring.
 * @return Its mode.
 */
kgui_ring_mode_t kgui_ring_mode(const kgui_ring_t *r);

/**
 * @brief How much of the ring is drawn.
 *
 * @param r The ring.
 * @return 0 to KGUI_RING_FULL_PCT.
 */
uint8_t kgui_ring_pct(const kgui_ring_t *r);

/**
 * @brief How much of the ring is drawn, to the thousandth.
 *
 * What a closing edge reports, so a number beside it can climb with it.
 *
 * @param r The ring.
 * @return 0 to KGUI_RING_FULL_PM.
 */
uint16_t kgui_ring_pm(const kgui_ring_t *r);

/**
 * @brief The ring's word for the log and for `gui_info`.
 *
 * @param r The ring.
 * @return "idle", "progress", "spin", "done" or "closing".
 */
const char *kgui_ring_word(const kgui_ring_t *r);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_RING_H */
