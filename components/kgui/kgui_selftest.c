/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_selftest.c
 * @brief What the Self-test page says about the last run (kgui_selftest.h).
 */
#include "kgui_selftest.h"

#include <stdio.h>

bool kgui_selftest_all_good(const kgui_selftest_tally_t *t)
{
    return (0U < t->rows) && (t->rows == t->passed) && (0U == t->failed);
}

uint8_t kgui_selftest_unchecked(const kgui_selftest_tally_t *t)
{
    uint32_t const judged = (uint32_t)t->passed + (uint32_t)t->failed;
    uint8_t        none   = 0U;

    if ( judged < (uint32_t)t->rows )
    {
        none = (uint8_t)((uint32_t)t->rows - judged);
    }
    return none;
}

void kgui_selftest_verdict(const kgui_selftest_tally_t *t, char *out, size_t cap)
{
    uint8_t const unchecked = kgui_selftest_unchecked(t);

    if ( (NULL == out) || (0U == cap) )
    {
        return;
    }
    /* A failure is told first; the rows nobody judged only when none failed. */
    if ( 0U == t->rows )
    {
        (void)snprintf(out, cap, "%s", KGUI_SELFTEST_NEVER);
    }
    else if ( 1U == t->failed )
    {
        (void)snprintf(out, cap, "1 problem");
    }
    else if ( 1U < t->failed )
    {
        (void)snprintf(out, cap, "%u problems", (unsigned)t->failed);
    }
    else if ( 0U < unchecked )
    {
        (void)snprintf(out, cap, "%u not checked", (unsigned)unchecked);
    }
    else
    {
        (void)snprintf(out, cap, "%s", KGUI_SELFTEST_ALL_GOOD);
    }
}

const char *kgui_selftest_footer(const kgui_selftest_tally_t *t)
{
    const char *foot = KGUI_SELFTEST_FOOT_AGAIN;

    if ( 0U == t->rows )
    {
        foot = KGUI_SELFTEST_FOOT_FIRST;
    }
    else if ( 0U < t->failed )
    {
        foot = KGUI_SELFTEST_FOOT_FAILED;
    }
    else
    {
        /* Nothing failed: a row not checked is tested again the same way. */
    }
    return foot;
}
