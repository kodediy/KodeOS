/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_details.h
 * @brief An app's details, and the two cards that delete it.
 */
#ifndef KGUI_DETAILS_H
#define KGUI_DETAILS_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Which pill of a page is meant. */
typedef enum {
    KGUI_DETAILS_PILL_LEFT = 0,  /**< The left pill: Open on the details, Keep on the question. */
    KGUI_DETAILS_PILL_RIGHT      /**< The right pill: Delete, on both. */
} kgui_details_pill_t;

/**
 * @brief What the details page says. The words are copied; the picture is not.
 */
typedef struct {
    lv_color_t            ground;       /**< The disc's colour when there is no picture. */
    const lv_image_dsc_t *pic;          /**< The app's picture, KGUI_ROW_ICON_SRC square; NULL for the glyph. */
    const char           *mono;         /**< The letters on the disc when there is no picture; NULL for the glyph. */
    const lv_image_dsc_t *glyph;        /**< The glyph on the disc when there are neither. */
    uint32_t              glyph_scale;  /**< The glyph's scale for a disc of KGUI_DETAILS_DISC_PX; LV_SCALE_NONE for its own size. */
    lv_color_t            ink;          /**< The glyph's colour. */
    const char           *name;         /**< The app's name. */
    const char           *meta;         /**< `<Category> · <version> · <size>` (kgui_dtext_meta()); "" for none. */
    const char           *description;  /**< What the app says of itself; "" for none. Cut at a whole word with dots when it does not fit. */
    bool                  openable;     /**< The app is in the list: the Open pill shows. */
    bool                  removable;    /**< An installed app, or a file that names its project: the Delete pill shows. */
} kgui_details_spec_t;

#define KGUI_DETAILS_DISC_PX 72  /**< The details' icon disc, in pixels. */

/**
 * @brief What a finger on a pill asks of the owner: a click.
 *
 * Called from LVGL's click on a pill, for every click: a press that
 * travelled is no tap, and telling it is the owner's. It must not delete the
 * card's objects.
 *
 * @param which The pill that was clicked.
 */
typedef void (*kgui_details_tap_fn)(kgui_details_pill_t which);

/**
 * @brief Build the pages on the sheet's card, once; a second call keeps what
 *        stands.
 *
 * The kit's handle and header disc are taken off the glass at every call
 * (the kit gives the disc back each time it clears the card), and given back
 * by kgui_details_close().
 *
 * @param card The card of the sheet; it must be as tall as the glass.
 * @param tap  What a click on a pill asks; NULL for nothing.
 * @return true when the pages stand, false when LVGL had no room.
 */
bool kgui_details_open(lv_obj_t *card, kgui_details_tap_fn tap);

/** @brief Take everything the module built off the glass and give the kit's parts back. */
void kgui_details_close(void);

/**
 * @brief Whether the pages are built.
 *
 * @return true between kgui_details_open() and kgui_details_close().
 */
bool kgui_details_is_open(void);

/**
 * @brief Show the details page for an app.
 *
 * The focus starts on the left pill, or on the only pill there is.
 *
 * @param spec What to say.
 */
void kgui_details_info(const kgui_details_spec_t *spec);

/**
 * @brief Show the question before Delete: Keep and Delete.
 *
 * The focus starts on Keep.
 *
 * @param name The app's name.
 */
void kgui_details_ask(const char *name);

/**
 * @brief Show the wait while the app leaves.
 *
 * @param name The app's name.
 */
void kgui_details_wait(const char *name);

/**
 * @brief Where the details' icon disc stands, for a launch that flies out of it.
 *
 * @param cx   Receives its middle, across.
 * @param cy   Receives its middle, down.
 * @param size Receives its side.
 * @return true when the details page is up and its disc has a place.
 */
bool kgui_details_origin(int32_t *cx, int32_t *cy, int32_t *size);

/**
 * @brief Show the focus ring on the focused pill, or take it off.
 *
 * The ring is the pad's cursor: it shows while a button is the input in use.
 *
 * @param shown true to show it.
 */
void kgui_details_ring(bool shown);

/**
 * @brief The pill that holds the focus on the page that is up.
 *
 * @return KGUI_DETAILS_PILL_LEFT or KGUI_DETAILS_PILL_RIGHT.
 */
kgui_details_pill_t kgui_details_focus(void);

/**
 * @brief Move the focus to a pill of the page that is up.
 *
 * Nothing moves when that pill is not on the glass.
 *
 * @param pill Where to.
 * @return true when the focus changed.
 */
bool kgui_details_focus_set(kgui_details_pill_t pill);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_DETAILS_H */
