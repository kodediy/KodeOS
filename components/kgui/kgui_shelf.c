/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_shelf.c
 * @brief The launcher's shelf of recents (kgui_shelf.h).
 */
#include "kgui_shelf.h"

#include <stdio.h>
#include <string.h>

#define SHELF_OPA_MAX 255  /**< A shelf that shows whole. */

int32_t kgui_shelf_x(uint8_t i, uint8_t n)
{
    int32_t x = KGUI_SHELF_MID_X;

    if ( (i < n) && ((uint8_t)KGUI_SHELF_MAX >= n) )
    {
        int32_t const step = (2 * (int32_t)i) - ((int32_t)n - 1);

        x = KGUI_SHELF_MID_X + ((step * KGUI_SHELF_PITCH) / 2);
    }
    return x;
}

uint8_t kgui_shelf_opa(int32_t cy)
{
    uint8_t opa = 0U;

    if ( KGUI_SHELF_TOP_Y <= cy )
    {
        opa = (uint8_t)SHELF_OPA_MAX;
    }
    else if ( KGUI_SHELF_GONE_Y < cy )
    {
        int32_t const share = (SHELF_OPA_MAX * (cy - KGUI_SHELF_GONE_Y))
                              / (KGUI_SHELF_TOP_Y - KGUI_SHELF_GONE_Y);

        opa = (uint8_t)share;
    }
    else
    {
        /* Gone. */
    }
    return opa;
}

int32_t kgui_shelf_cy(int32_t box_y)
{
    return (box_y + KGUI_ROW_MID) - KGUI_ROW_PITCH;
}

uint32_t kgui_shelf_scroll_rows(uint32_t n, bool shelf)
{
    uint32_t rows = n;

    if ( shelf && (KGUI_RAIL_BLOCK_MAX >= rows) )
    {
        rows = KGUI_RAIL_BLOCK_MAX + 1U;
    }
    return rows;
}

/**
 * @brief Whether a name is among the first names of a list.
 *
 * @param names The names.
 * @param n     How many to look at.
 * @param name  The name.
 * @return true when it is one of them.
 */
static bool shelf_has(char (*names)[KGUI_SHELF_NAME_MAX], uint8_t n, const char *name)
{
    bool found = false;

    for ( uint8_t i = 0U; (i < n) && (!found); ++i )
    {
        found = (0 == strcmp(names[i], name));
    }
    return found;
}

uint8_t kgui_recents_split(const char *list, char (*out)[KGUI_SHELF_NAME_MAX], uint8_t max)
{
    uint8_t n   = 0U;
    size_t  pos = 0U;

    if ( (NULL == out) || (NULL == list) )
    {
        return 0U;
    }
    while ( ('\0' != list[pos]) && (n < max) )
    {
        size_t const len = strcspn(&list[pos], "\n");

        if ( (0U < len) && (KGUI_SHELF_NAME_MAX > len) )
        {
            (void)memcpy(out[n], &list[pos], len);
            out[n][len] = '\0';
            if ( !shelf_has(out, n, out[n]) )
            {
                ++n;
            }
        }
        pos += len;
        if ( '\n' == list[pos] )
        {
            ++pos;
        }
    }
    return n;
}

bool kgui_recents_push(char *list, const char *name)
{
    char    names[KGUI_SHELF_MAX + 1U][KGUI_SHELF_NAME_MAX];
    char    out[KGUI_RECENTS_LEN];
    uint8_t n;
    uint8_t kept = 1U;
    size_t  len  = 0U;

    if ( (NULL == list) || (NULL == name) || ('\0' == name[0])
         || (strlen(name) >= KGUI_SHELF_NAME_MAX) || (NULL != strchr(name, '\n')) )
    {
        return false;
    }
    /* The name first, then the others as they were, without it. */
    (void)snprintf(names[0], sizeof(names[0]), "%s", name);
    n = kgui_recents_split(list, &names[1], (uint8_t)KGUI_SHELF_MAX);
    for ( uint8_t i = 1U; (i <= n) && ((uint8_t)KGUI_SHELF_MAX > kept); ++i )
    {
        if ( 0 != strcmp(names[i], name) )
        {
            if ( kept != i )
            {
                (void)memcpy(names[kept], names[i], sizeof(names[kept]));
            }
            ++kept;
        }
    }
    out[0] = '\0';
    for ( uint8_t i = 0U; i < kept; ++i )
    {
        int const w = snprintf(&out[len], sizeof(out) - len, "%s%s",
                               (0U == i) ? "" : "\n", names[i]);

        if ( (0 < w) && ((size_t)w < (sizeof(out) - len)) )
        {
            len += (size_t)w;
        }
    }
    bool const changed = (0 != strcmp(out, list));

    if ( changed )
    {
        (void)memcpy(list, out, len + 1U);
    }
    return changed;
}
