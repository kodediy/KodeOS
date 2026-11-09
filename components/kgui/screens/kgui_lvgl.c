/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_lvgl.c
 * @brief Two of LVGL's own macros, expanded in one place (kgui_lvgl.h).
 */
#include "kgui_lvgl.h"

void kgui_text_size(lv_point_t *size, const char *text, const lv_font_t *font)
{
    /* cppcheck-suppress [misra-c2012-10.1, misra-c2012-12.2] ; LV_COORD_MAX is LVGL's own (1 << 29) - 1 */
    lv_text_get_size(size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
}

int32_t kgui_size_content(void)
{
    /* cppcheck-suppress [misra-c2012-10.1, misra-c2012-12.2] ; LV_SIZE_CONTENT is LVGL's own LV_COORD_SET_SPEC(2001), a shift of 1 */
    return LV_SIZE_CONTENT;
}
