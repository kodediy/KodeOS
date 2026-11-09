/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_syscard.c
 * @brief The words and the decisions of three system cards (kgui_syscard.h).
 */
#include "kgui_syscard.h"

#include <stddef.h>
#include <stdio.h>

#include "kaos.h"
#include "kode_defs.h"

bool kgui_stopped_is(int end)
{
    return ((int)KBOOT_APP_END_PANIC == end) || ((int)KBOOT_APP_END_WATCHDOG == end)
           || ((int)KBOOT_APP_END_BROWNOUT == end);
}

const char *kgui_stopped_reason(int end)
{
    const char *why = "";

    if ( (int)KBOOT_APP_END_PANIC == end )
    {
        why = KGUI_STOPPED_CRASHED;
    }
    else if ( (int)KBOOT_APP_END_WATCHDOG == end )
    {
        why = KGUI_STOPPED_HUNG;
    }
    else if ( (int)KBOOT_APP_END_BROWNOUT == end )
    {
        why = KGUI_STOPPED_BROWNOUT;
    }
    else
    {
        /* A clean end, or no app: no card, no reason. */
    }
    return why;
}

bool kgui_crit_due(kgui_crit_t *c, bool critical, bool charging, uint8_t soc_pct)
{
    bool due = false;

    if ( NULL != c )
    {
        if ( 0U < soc_pct )
        {
            c->armed = true;
        }
        if ( critical )
        {
            due = c->armed && !c->shown;
        }
        else if ( charging || ((KGUI_BAT_CRIT_PCT + KGUI_BAT_CRIT_REARM_PCT) <= soc_pct) )
        {
            c->shown = false;
        }
        else
        {
            /* A reading just over the line: the entry stands. */
        }
    }
    return due;
}

void kgui_crit_taken(kgui_crit_t *c)
{
    if ( NULL != c )
    {
        c->shown = true;
    }
}

bool kgui_crit_waits(bool locked, bool system, bool guided, bool parked)
{
    return locked || system || guided || parked;
}

const char *kgui_reset_why(uint8_t step, uint32_t status)
{
    static char stopped[KGUI_RESET_WHY_MAX];  /* the last "Stopped at" line written */
    const char *why = "";

    if ( (uint32_t)KODE_OK == status )
    {
        /* The reset happened: AO_Boot restarts the board, nothing to say. */
    }
    else if ( ((uint8_t)KBOOT_RESET_STEP_FIND == step) && ((uint32_t)KODE_DENIED == status) )
    {
        why = KGUI_RESET_NO_WELCOME;
    }
    else if ( ((uint8_t)KBOOT_RESET_STEP_FIND == step) && ((uint32_t)KODE_NO_RESOURCES == status) )
    {
        why = KGUI_RESET_BUSY;
    }
    else
    {
        switch ( step )
        {
            case (uint8_t)KBOOT_RESET_STEP_FIND:
                why = KGUI_RESET_NOTHING;
                break;
            case (uint8_t)KBOOT_RESET_STEP_LINK:
                why = KGUI_RESET_NO_RADIO;
                break;
            case (uint8_t)KBOOT_RESET_STEP_INSTALL:
                why = KGUI_RESET_NO_INSTALL;
                break;
            case (uint8_t)KBOOT_RESET_STEP_WIPE:
                why = KGUI_RESET_RADIO_QUIT;
                break;
            case (uint8_t)KBOOT_RESET_STEP_APPS:
                why = KGUI_RESET_APPS_STAY;
                break;
            case (uint8_t)KBOOT_RESET_STEP_NVS:
                why = KGUI_RESET_SOME_STAY;
                break;
            case (uint8_t)KBOOT_RESET_STEP_REQUEST:
                why = KGUI_RESET_ALL_GONE;
                break;
            default:
                (void)snprintf(stopped, sizeof(stopped), KGUI_RESET_STOPPED_AT "%s",
                               boot_reset_step_name(step));
                why = stopped;
                break;
        }
    }
    return why;
}

const char *kgui_reset_sub(uint8_t step)
{
    bool const nothing = ((uint8_t)KBOOT_RESET_STEP_FIND == step)
                         || ((uint8_t)KBOOT_RESET_STEP_LINK == step)
                         || ((uint8_t)KBOOT_RESET_STEP_INSTALL == step);

    const char *sub = KGUI_RESET_PARTLY_SUB;

    if ( nothing )
    {
        sub = KGUI_RESET_REFUSED_SUB;
    }
    return sub;
}
