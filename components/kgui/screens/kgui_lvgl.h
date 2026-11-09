/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_lvgl.h
 * @brief Two of LVGL's own macros, expanded in one place.
 */
#ifndef KGUI_LVGL_H
#define KGUI_LVGL_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Use these, never LV_COORD_MAX or LV_SIZE_CONTENT: MISRA 10.1 and 12.2 refuse their expansion. */

/**
 * @brief Measure a text on one line, in a font.
 *
 * @param size Receives the width and the height.
 * @param text The words.
 * @param font The font they are set in.
 */
void kgui_text_size(lv_point_t *size, const char *text, const lv_font_t *font);

/**
 * @brief LVGL's `LV_SIZE_CONTENT`: the size an object takes from what it holds.
 *
 * @return The value to give a width or a height.
 */
int32_t kgui_size_content(void);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_LVGL_H */
