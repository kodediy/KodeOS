/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_curtain.h
 * @brief The curtain: two still pictures on a screen of their own.
 */
#ifndef KGUI_CURTAIN_H
#define KGUI_CURTAIN_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "kdrv_display.h"
#include "kgui_frames.h"
#include "kgui_motion.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Rate and look --------------------------------------------------------- */

#define KGUI_DRAG_MS_PER_S    1000U                     /**< Milliseconds in a second. */
#define KGUI_DRAG_PLACE_MS    KGUI_PLACE_EVERY_SECOND_MS /**< The least time between two placements of a dragged page's objects, in milliseconds: every second refresh. */
#define KGUI_CURTAIN_DIM_OPA  LV_OPA_40                 /**< How dark the picture below is under a fully open panel, when the curtain dims. */

/**
 * @brief How long until a page's objects are next due a placement: the rule
 *        of KGUI_DRAG_PLACE_MS, written once.
 *
 * The motions that keep the drag's frame rate - a dragged page, a card's
 * rise, the launch scene - place their objects at least
 * KGUI_DRAG_PLACE_MS apart, whoever asks. The Apps list's motions and the sheets'
 * rise set their own (KGUI_LIST_PLACE_MS, KGUI_PLACE_EVERY_REFRESH_MS).
 *
 * @param placed_ms When the page's objects were last placed.
 * @param now_ms    The clock.
 * @return The milliseconds left, 0 when a placement is due.
 */
uint32_t kgui_place_left(uint32_t placed_ms, uint32_t now_ms);

/**
 * @brief How long until a page's objects are next due a placement, for a
 *        motion that sets its own least time: the rule itself.
 *
 * @param placed_ms When the page's objects were last placed.
 * @param now_ms    The clock.
 * @param least_ms  The least time between two placements, in milliseconds.
 * @return The milliseconds left, 0 when a placement is due.
 */
uint32_t kgui_place_left_by(uint32_t placed_ms, uint32_t now_ms, uint32_t least_ms);

/* ---- The growing take ------------------------------------------------------- */

#define KGUI_CURTAIN_GROW_K        1000  /**< The growth at its end; a unit is a thousandth of the way. */
#define KGUI_CURTAIN_GROW_INK_FROM (KGUI_CURTAIN_GROW_K / 3)  /**< Out of a circle, the picture fades in over the last two thirds of the way (kshell's level zoom). */

/* ---- The armed cue ---------------------------------------------------------- */

#define KGUI_CURTAIN_LINE_GAP_PX 10                        /**< Pixels between the armed handle and the line under it. */
#define KGUI_CURTAIN_CUE_TEXT    "Release for Settings"    /**< The line under the armed handle: what letting go will do. */
#define KGUI_CURTAIN_LOCK_TEXT   "Release to lock"         /**< The hint in the top band while letting go of the lock pull would lock. */

/* ---- The lock pull ---------------------------------------------------------- */

#define KGUI_LOCK_PULL_RUBBER_PX 40      /**< The first pixels of the lock pull on which the face resists the finger a second time, on top of the page's own resistance. */
#define KGUI_LIST_STILL_MS       500U    /**< The list must have been still this long, in milliseconds, before a drag down on it can be the lock pull. */

/* ---- Pictures --------------------------------------------------------------- */

/** @brief What a curtain's pictures are taken of. */
typedef struct {
    lv_obj_t       *other;          /**< The screen to render: the panel that comes down when it is the panel, else the screen below. NULL takes nothing. */
    bool            other_is_panel; /**< true when @p other is the panel (the glass is the picture below), false when it is the screen below (the glass is the panel). */
    lv_obj_t const *handle;         /**< The settings screen's handle, a child of that screen: the armed cue is drawn exactly over it. NULL draws no cue. */
    bool            dims;           /**< The picture below darkens as the panel comes down. */
    bool            sideways;       /**< The pictures slide along the glass instead of down it: the panel comes in from the left and pushes the picture below out to the right (kshell's page slide, face <-> Apps). The offset runs 0 to the glass's width. */
    bool            mirrored;       /**< A sideways take runs the other way: the panel comes in from the right and pushes the picture below out to the left (a swipe right from the face into Apps, and its way back). Taken with @p sideways true. */
    bool            grows;          /**< The picture below grows over the panel's, which stands still. With no @p from, in place: it fades in over the whole glass (the tap from the face into Apps). With one, out of it: a rectangle goes from that circle's place, colour and radius to the whole glass, darkening to the page's black, and the picture inside it fades in over the last two thirds (kshell's level zoom). The offset runs 0 to the glass's width, the width being all of the panel: the picture below is whole at 0. Taken with @p sideways false. */
    bool            from;           /**< A growing take starts from a circle on the glass: @p from_area and @p from_color. */
    lv_area_t       from_area;      /**< That circle's place on the glass. */
    lv_color_t      from_color;     /**< Its colour. */
} kgui_curtain_spec_t;

/**
 * @brief Count a chunk handed to the panel.
 *
 * Called with every flush. The copy of the glass is skipped when no chunk
 * has gone to the panel since the last copy of the same screen: the glass is
 * the same, and so is the copy.
 */
void kgui_curtain_flushed(void);

/**
 * @brief Take the two pictures: the glass as it stands, and another screen.
 *
 * Makes the two buffers, the curtain's screen and its objects the first
 * time, and keeps them. The glass is copied from the front framebuffer
 * (skipped when it has not changed since the last copy of the same screen);
 * the other screen is rendered into its buffer every time, as it stands. The
 * area of the settings screen's handle is read now, for the armed cue to be
 * drawn over it. The cost is logged at D: `curtain: pictures in <a> ms (glass
 * <b> ms, <screen> <c> ms)`.
 *
 * @param spec What to take; see kgui_curtain_spec_t.
 * @return true when both pictures are ready; false when PSRAM refused a
 *         buffer, the screen or an object, the panel has no front
 *         framebuffer or the screen would not render, and the caller goes
 *         without a curtain.
 */
bool kgui_curtain_take(kgui_curtain_spec_t const *spec);

/**
 * @brief The lock pull's extra resistance: how far the face follows a finger.
 *
 * The first KGUI_LOCK_PULL_RUBBER_PX pixels of travel move the face by a share
 * that grows from nothing to the whole finger (the distance squared over
 * twice the rubber), and past them it follows one to one, always that much
 * behind. A finger that goes back up is not slowed.
 *
 * @param travel Pixels the finger has gone down from where the drag was claimed.
 * @return Pixels the page is fed in the finger's place.
 */
int32_t kgui_curtain_rubber(int32_t travel);

/* ---- The screen ------------------------------------------------------------- */

/**
 * @brief Put the curtain at an offset, loading its screen with a cut if the
 *        glass is not showing it yet.
 *
 * The pictures are on a screen of their own, so while it is loaded LVGL draws
 * them and nothing else. Nothing on that screen is clickable: a press during
 * the motion reaches no object, and what answers it is kgui's recognizer
 * (a catch). Nothing is drawn until the pictures are taken.
 *
 * @param offset Pixels the panel has come down, 0 (all above the glass) to
 *               the glass's height (the panel covers the glass); clamped.
 *               For a sideways take (kgui_curtain_spec_t::sideways) the
 *               pixels it has come in from the left, up to the glass's
 *               width, with the picture below pushed out to the right by
 *               the same distance. For a growing one
 *               (kgui_curtain_spec_t::grows) the same range, read as how
 *               little of the picture below has grown: whole at 0, gone at
 *               the glass's width.
 * @param armed  true when the finger has pulled the curtain far enough to
 *               open on release: the cue that says so is drawn, in the
 *               kit's selection blue over the panel's handle, with its line
 *               under it, and both go with the panel. Nothing is drawn for
 *               false, or when no handle was given to the take.
 */
void kgui_curtain_show(int32_t offset, bool armed);

/**
 * @brief Take the curtain off the glass by loading another screen, with a cut.
 *
 * Nothing without a curtain on the glass.
 *
 * @param back The screen the glass shows from here: the one the curtain
 *             landed on, or the one it started from.
 */
void kgui_curtain_hide(lv_obj_t *back);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_CURTAIN_H */
