/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_grow.c
 * @brief The progress scale the sheets and the launch share, and the Tap pop (kgui_grow.h).
 */
#include "kgui_grow.h"

#include <stddef.h>

#define GROW_OPA_MAX      255   /**< A text that shows whole. */

uint8_t kgui_grow_text_opa(int32_t u)
{
    int32_t opa = 0;

    if ( KGUI_GROW_UNITS <= u )
    {
        opa = GROW_OPA_MAX;
    }
    else if ( KGUI_GROW_TEXT_AT < u )
    {
        opa = ((u - KGUI_GROW_TEXT_AT) * GROW_OPA_MAX)
              / (KGUI_GROW_UNITS - KGUI_GROW_TEXT_AT);
    }
    else
    {
        /* Before the text's moment. */
    }
    return (uint8_t)opa;
}

uint32_t kgui_pop_scale(uint32_t elapsed_ms)
{
    uint32_t const peak  = (KGUI_POP_ONE * KGUI_POP_PEAK_PCT) / 100U;
    uint32_t const rise  = peak - KGUI_POP_ONE;
    uint32_t       scale = KGUI_POP_ONE;

    if ( KGUI_POP_POSE_MS >= elapsed_ms )
    {
        scale = KGUI_POP_ONE + ((rise * elapsed_ms) / KGUI_POP_POSE_MS);
    }
    else if ( KGUI_POP_TOTAL_MS > elapsed_ms )
    {
        scale = peak - ((rise * (elapsed_ms - KGUI_POP_POSE_MS)) / KGUI_POP_POSE_MS);
    }
    else
    {
        /* Past the last pose. */
    }
    return scale;
}
