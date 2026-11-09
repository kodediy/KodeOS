/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_wifi.c
 * @brief The words of the radios' rows (kgui_wifi.h).
 */
#include "kgui_wifi.h"

#include <stddef.h>
#include <string.h>

#include "kdrv_wifi.h"

const char *kgui_radio_words(kgui_radio_view_t const *v)
{
    const char *words = "On";

    if ( (NULL == v) || (!v->on) )
    {
        words = "Off";
    }
    else if ( v->gave_up )
    {
        words = "Not responding";
    }
    else if ( v->relinking )
    {
        words = "Reconnecting...";
    }
    else if ( v->linked )
    {
        words = NULL;
    }
    else if ( v->reconnecting )
    {
        words = "Reconnecting...";
    }
    else
    {
        /* On, and on no network: what it is. */
    }
    return words;
}

const char *kgui_wifi_fail_words(uint8_t fail)
{
    const char *words = "Could not join";

    if ( (uint8_t)KDRV_WIFI_FAIL_PASSWORD == fail )
    {
        words = "Wrong password";
    }
    else if ( (uint8_t)KDRV_WIFI_FAIL_NOT_FOUND == fail )
    {
        words = "Network not found";
    }
    else
    {
        /* Anything else: the network would not have us, for a reason the
         * person cannot act on. */
    }
    return words;
}

bool kgui_wifi_goes_nowhere(const char *why)
{
    static const char * const NOWHERE[] = {
        "timeout", "no_reply", "broker_unreachable", "broker_timeout",
    };
    bool found = false;

    if ( NULL != why )
    {
        for ( size_t i = 0U; (!found) && (i < (sizeof(NOWHERE) / sizeof(NOWHERE[0]))); ++i )
        {
            found = (strcmp(why, NOWHERE[i]) == 0);
        }
    }
    return found;
}
