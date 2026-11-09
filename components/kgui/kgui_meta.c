/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_meta.c
 * @brief When the Apps list may appear (kgui_meta.h).
 */
#include "kgui_meta.h"

#define META_OPA_MAX  255U  /**< A list that shows whole. */

void kgui_meta_listed(kgui_meta_t *m, uint32_t now_ms)
{
    m->timing = true;
    m->t0_ms  = now_ms;
}

bool kgui_meta_due(const kgui_meta_t *m, uint32_t now_ms)
{
    return m->timing && ((now_ms - m->t0_ms) >= KGUI_META_WAIT_MS);
}

uint32_t kgui_meta_left_ms(const kgui_meta_t *m, uint32_t now_ms)
{
    uint32_t left = 0U;

    if ( m->timing && !kgui_meta_due(m, now_ms) )
    {
        left = KGUI_META_WAIT_MS - (now_ms - m->t0_ms);
    }
    return left;
}

bool kgui_meta_shows(const kgui_meta_t *m, uint32_t total, uint32_t known,
                     uint32_t now_ms)
{
    return m->open || (known >= total) || kgui_meta_due(m, now_ms);
}

bool kgui_meta_admits(bool known, bool due)
{
    return known || due;
}

uint8_t kgui_meta_reveal_opa(uint32_t gone_ms)
{
    uint32_t opa = META_OPA_MAX;

    if ( KGUI_LIST_REVEAL_MS > gone_ms )
    {
        opa = (gone_ms * META_OPA_MAX) / KGUI_LIST_REVEAL_MS;
    }
    return (uint8_t)opa;
}
