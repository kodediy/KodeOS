/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_backbar.h
 * @brief A page's title, centred on the glass, with the little arrow back at
 *        its left (Luismi, 5-nov): folders and Settings say where they are. It
 *        stays at the top: the list passes under it, into a black that fades.
 */
#ifndef KGUI_BACKBAR_H
#define KGUI_BACKBAR_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KGUI_BACKBAR_Y     12   /**< Where the words' line starts on the glass. */
#define KGUI_BACKBAR_H_PX  44   /**< That line's height. */
#define KGUI_BACKBAR_FADE  20   /**< Under it, the black that fades to the list. */
#define KGUI_BACKBAR_FOOT  (KGUI_BACKBAR_Y + KGUI_BACKBAR_H_PX + KGUI_BACKBAR_FADE) /**< Where it all ends. */

/**
 * @brief Make a bar, as wide as the glass, at its top.
 *
 * @param parent Where it goes.
 * @param on_tap Called on a click; a tap on it goes back.
 * @return The bar, or NULL.
 */
lv_obj_t *kgui_backbar_make(lv_obj_t *parent, lv_event_cb_t on_tap);

/**
 * @brief Say a title: the words are centred, and the arrow is not counted.
 *
 * @param bar   The bar.
 * @param title The words.
 */
void kgui_backbar_set(const lv_obj_t *bar, const char *title);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_BACKBAR_H */
