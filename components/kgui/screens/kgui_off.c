/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_off.c
 * @brief The arc's numbers (kgui_off.h); adapts kodediy/kode-dot-os, kshell_power.c,
 *        whose arc timing (from 2 s to 5 s) it keeps.
 */
#include "kgui_off.h"

_Static_assert(GUI_OFF_ARC_TO_MS > GUI_OFF_ARC_FROM_MS, "the arc fills in a positive time");

uint16_t kgui_off_fill_pm(uint32_t held_ms)
{
    uint32_t pm = 0U;

    if ( GUI_OFF_ARC_TO_MS <= held_ms )
    {
        pm = KGUI_OFF_FULL_PM;
    }
    else if ( GUI_OFF_ARC_FROM_MS < held_ms )
    {
        pm = ((held_ms - GUI_OFF_ARC_FROM_MS) * KGUI_OFF_FULL_PM)
             / (GUI_OFF_ARC_TO_MS - GUI_OFF_ARC_FROM_MS);
    }
    else
    {
        /* Not yet. */
    }
    return (uint16_t)pm;
}

uint16_t kgui_off_empty_pm(uint16_t from_pm, uint32_t since_ms)
{
    uint32_t pm = 0U;

    if ( GUI_OFF_ARC_EMPTY_MS > since_ms )
    {
        pm = ((uint32_t)from_pm * (GUI_OFF_ARC_EMPTY_MS - since_ms))
             / GUI_OFF_ARC_EMPTY_MS;
    }
    return (uint16_t)pm;
}

/**
 * @brief How much of a piece of the path is drawn.
 *
 * @param run The path run into the piece, in pixels; more than the piece is whole.
 * @param len The piece's length, in pixels.
 * @return The pixels of the piece that are drawn.
 */
static uint32_t piece_px(uint32_t run, uint32_t len)
{
    uint32_t px = len;

    if ( run < len )
    {
        px = run;
    }
    return px;
}

void kgui_off_split(uint16_t pm, kgui_off_parts_t *out)
{
    uint32_t share = pm;
    uint32_t run;
    uint32_t px;

    if ( KGUI_OFF_FULL_PM < share )
    {
        share = KGUI_OFF_FULL_PM;
    }
    run = (share * KGUI_OFF_PATH_PX) / KGUI_OFF_FULL_PM;
    px  = piece_px(run, KGUI_OFF_CORNER_PX);
    out->left_pm = (uint16_t)((px * KGUI_OFF_FULL_PM) / KGUI_OFF_CORNER_PX);
    run -= px;
    px = piece_px(run, KGUI_OFF_LINE_PX);
    out->line_px = (uint16_t)px;
    run -= px;
    px = piece_px(run, KGUI_OFF_CORNER_PX);
    out->right_pm = (uint16_t)((px * KGUI_OFF_FULL_PM) / KGUI_OFF_CORNER_PX);
}
