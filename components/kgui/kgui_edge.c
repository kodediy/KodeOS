/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_edge.c
 * @brief The edge's close and pop (kgui_edge.h); adapts kodediy/kode-dot-os, kshell_edge.c.
 */
#include "kgui_edge.h"

#define RING_POP_UP_MS      KGUI_EDGE_POP_MS /**< The pop's rise. */
#define RING_POP_TOTAL_MS   (2U * KGUI_EDGE_POP_MS) /**< The pop, up and down. */

uint16_t kgui_ring_close_pm(uint16_t from_pm, uint32_t since_ms)
{
    uint32_t t = since_ms;
    uint32_t left;
    uint32_t ease;
    uint32_t gap;

    if ( KGUI_EDGE_CLOSE_MS < t )
    {
        t = KGUI_EDGE_CLOSE_MS;
    }
    left = KGUI_RING_FULL_PM - ((t * KGUI_RING_FULL_PM) / KGUI_EDGE_CLOSE_MS);
    ease = KGUI_RING_FULL_PM - ((left * left) / KGUI_RING_FULL_PM);
    gap  = KGUI_RING_FULL_PM - (uint32_t)from_pm;

    return (uint16_t)((uint32_t)from_pm + ((gap * ease) / KGUI_RING_FULL_PM));
}

int32_t kgui_ring_pop_px(uint32_t since_ms)
{
    uint32_t px = 0U;

    if ( RING_POP_UP_MS > since_ms )
    {
        px = ((uint32_t)KGUI_EDGE_POP_PX * since_ms) / RING_POP_UP_MS;
    }
    else if ( RING_POP_TOTAL_MS > since_ms )
    {
        px = ((uint32_t)KGUI_EDGE_POP_PX * (RING_POP_TOTAL_MS - since_ms)) / RING_POP_UP_MS;
    }
    else
    {
        /* The pop is over. */
    }
    return (int32_t)px;
}
