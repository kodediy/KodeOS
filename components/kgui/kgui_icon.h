/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_icon.h
 * @brief An app's icon as an object: the round box the launch draws it in.
 */
#ifndef KGUI_ICON_H
#define KGUI_ICON_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What an icon looks like: its picture, or the letters that stand for it. */
typedef struct {
    const lv_image_dsc_t *image;   /**< The picture, KGUI_ROW_ICON_SRC square; NULL for letters. */
    char                  mono[3]; /**< The letters, upper case, when there is no picture. */
    lv_color_t            ground;  /**< What the letters are drawn on, and the disc under a picture. */
    lv_color_t            ink;     /**< The letters' colour. */
} kgui_icon_spec_t;

/** @brief An icon's objects. */
typedef struct {
    lv_obj_t             *disc;      /**< The round box. */
    lv_obj_t             *img;       /**< The picture. */
    lv_obj_t             *mono;      /**< The letters. */
    const lv_image_dsc_t *pic;       /**< The picture it is dressed with, KGUI_ROW_ICON_SRC square; NULL for letters. */
} kgui_icon_t;

/**
 * @brief A plain object: no style, no touch, no scroll.
 *
 * @param parent Its parent.
 * @param x      Its left.
 * @param y      Its top.
 * @param w      Its width.
 * @param h      Its height.
 * @return The object, or NULL when LVGL had no room.
 */
lv_obj_t *kgui_plain(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);

/**
 * @brief Make an icon's box: round, clipping, with a picture and letters in it.
 *
 * @param ic     Receives its objects.
 * @param parent Its parent.
 * @param size   Its side.
 * @return true when all of it stands.
 */
bool kgui_icon_make(kgui_icon_t *ic, lv_obj_t *parent, int32_t size);

/**
 * @brief Put an icon at a size.
 *
 * The picture is scaled to the side. Letters are not scaled.
 *
 * @param ic   The icon.
 * @param size Its side.
 */
void kgui_icon_size(kgui_icon_t *ic, int32_t size);

/**
 * @brief Dress an icon as an app's: its picture, or its letters on their ground.
 *
 * @param ic   The icon.
 * @param spec The app's icon.
 * @param size Its side.
 */
void kgui_icon_dress(kgui_icon_t *ic, const kgui_icon_spec_t *spec, int32_t size);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_ICON_H */
