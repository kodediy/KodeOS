/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_return.c
 * @brief The wait of the first frame after an app (kgui_return.h).
 */
#include "kgui_return.h"

#include <stddef.h>

void kgui_return_begin(kgui_return_t *r, uint32_t now_ms)
{
    if ( NULL != r )
    {
        r->began     = true;
        r->holding   = true;
        r->listed    = false;
        r->asked     = false;
        r->late      = false;
        r->t0_ms     = now_ms;
        r->waited_ms = 0U;
    }
}

bool kgui_return_holds(const kgui_return_t *r)
{
    return (NULL != r) && r->holding;
}

void kgui_return_listed(kgui_return_t *r)
{
    if ( kgui_return_holds(r) )
    {
        r->listed = true;
    }
}

bool kgui_return_go(kgui_return_t *r, uint32_t now_ms)
{
    bool go = false;

    if ( kgui_return_holds(r) && !r->asked )
    {
        go = r->listed || (KGUI_LIST_WAIT_MS <= (now_ms - r->t0_ms));
        if ( go )
        {
            r->asked = true;
        }
    }
    return go;
}

void kgui_return_end(kgui_return_t *r, uint32_t now_ms)
{
    if ( kgui_return_holds(r) )
    {
        r->waited_ms = now_ms - r->t0_ms;
        r->late      = !r->listed;
        r->holding   = false;
    }
}

uint32_t kgui_return_wait(const kgui_return_t *r, uint32_t now_ms, uint32_t next)
{
    uint32_t wait = next;

    if ( kgui_return_holds(r) )
    {
        uint32_t const gone = now_ms - r->t0_ms;

        if ( KGUI_LIST_WAIT_MS > gone )
        {
            uint32_t const left = KGUI_LIST_WAIT_MS - gone;

            if ( left < wait )
            {
                wait = left;
            }
        }
        else
        {
            wait = 0U;
        }
    }
    return wait;
}
