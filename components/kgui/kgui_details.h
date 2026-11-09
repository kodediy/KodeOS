/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_details.h
 * @brief An app's details, and the two cards that remove it.
 */
#ifndef KGUI_DETAILS_H
#define KGUI_DETAILS_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    const char           *meta;         /**< `v<version> · <Category>` (kgui_dtext_meta()); "" for none. */
    const char           *size;         /**< The Size row's value (kgui_dtext_size()); "" leaves the row out. */
    const char           *built;        /**< The Built row's value (kgui_dtext_date()); "" leaves the row out. */
    const char           *description;  /**< What the app says of itself, 52 characters at most; "" for none. */
    bool                  removable;    /**< An installed app: the Remove pill shows. */
} kgui_details_spec_t;

#define KGUI_DETAILS_DISC_PX 72  /**< The details' icon disc, in pixels. */

/**
 * @brief What a finger on the pill asks of the owner: a click.
 *
 * Called from LVGL's click on the pill, for every click: a press that
 * travelled is no tap, and telling it is the owner's. It must not delete the
 * card's objects.
 */
typedef void (*kgui_details_tap_fn)(void);

/**
 * @brief Build the pages on the sheet's card, once; a second call keeps what
 *        stands.
 *
 * The kit's handle and header disc are taken off the glass at every call
 * (the kit gives the disc back each time it clears the card), and given back
 * by kgui_details_close().
 *
 * @param card The card of the sheet; it must be as tall as the glass.
 * @param tap  What a click on the pill asks; NULL for nothing.
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
 * @param spec What to say.
 */
void kgui_details_info(const kgui_details_spec_t *spec);

/**
 * @brief Show the question before Remove.
 *
 * @param name  The app's name.
 * @param copy  true when a copy of the image stays on the SD card, so the
 *              caption says so; false leaves that line out.
 */
void kgui_details_ask(const char *name, bool copy);

/**
 * @brief Show the wait while the app leaves.
 *
 * @param name The app's name.
 */
void kgui_details_wait(const char *name);

/**
 * @brief Show the focus ring on the pill, or take it off.
 *
 * The ring is the pad's cursor: it shows while a button is the input in use.
 *
 * @param shown true to show it.
 */
void kgui_details_ring(bool shown);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_DETAILS_H */
