/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_wifi.c
 * @brief The words of the radios' rows (kgui_wifi.h).
 */
#include "kgui_wifi.h"

#include <stddef.h>
#include <string.h>

#include "kaos.h"
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

const char *kgui_ble_pair_words(bool open, bool wait, bool linked)
{
    const char *words = NULL;

    if ( wait || (open && linked) )
    {
        words = "When the other leaves";
    }
    else if ( open )
    {
        words = "Visible to new devices";
    }
    else
    {
        /* Not asked for: the row says its name and nothing else. */
    }
    return words;
}

bool kgui_wifi_fail_is_password(uint8_t fail, int8_t rssi)
{
    bool password = false;

    if ( (uint8_t)KDRV_WIFI_FAIL_PASSWORD == fail )
    {
        /* The network turned the password down, and said so. */
        password = true;
    }
    else if ( (uint8_t)KDRV_WIFI_FAIL_HANDSHAKE == fail )
    {
        /* The handshake did not finish. The stack gives one reason for a
         * wrong password and for a signal that went during it: a network
         * heard weakly lost the signal, any other had the password wrong. */
        password = ((int8_t)KGUI_WIFI_RSSI_NONE == rssi) || ((int8_t)KGUI_WIFI_WEAK_DBM <= rssi);
    }
    else
    {
        /* Not the password's doing. */
    }
    return password;
}

const char *kgui_wifi_fail_words(uint8_t fail, int8_t rssi)
{
    const char *words = "Could not join";

    if ( kgui_wifi_fail_is_password(fail, rssi) )
    {
        words = "Wrong password";
    }
    else if ( (uint8_t)KDRV_WIFI_FAIL_HANDSHAKE == fail )
    {
        words = "Signal lost";
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

kgui_join_t kgui_wifi_join_read(const char *asked, uint8_t state, const char *ssid,
                                bool joining)
{
    kgui_join_t read = KGUI_JOIN_OUT;

    if ( (uint8_t)KDRV_WIFI_GOT_IP == state )
    {
        if ( (NULL != asked) && (NULL != ssid) && (strcmp(asked, ssid) == 0) )
        {
            read = KGUI_JOIN_MADE;
        }
    }
    else if ( joining )
    {
        /* Not its answer: the join is still out. */
    }
    else if ( (uint8_t)KDRV_WIFI_DISCONNECTED == state )
    {
        read = KGUI_JOIN_TURNED;
    }
    else if ( (uint8_t)KDRV_WIFI_IDLE == state )
    {
        read = KGUI_JOIN_NOTHING;
    }
    else
    {
        /* Still connecting. */
    }
    return read;
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

kgui_scan_next_t kgui_wifi_scan_next(bool ok, bool cut, bool own_join)
{
    kgui_scan_next_t next = KGUI_SCAN_NEXT_CYCLE;

    if ( (!ok) && ((!cut) || own_join) )
    {
        next = KGUI_SCAN_NEXT_SOON;
    }
    return next;
}

kgui_chan_press_t kgui_wifi_chan_press(uint8_t state)
{
    kgui_chan_press_t press = KGUI_CHAN_PRESS_OFF;

    if ( ((uint8_t)CHAN_OFF == state) || ((uint8_t)CHAN_WAITING == state)
         || ((uint8_t)CHAN_REFUSED == state) || ((uint8_t)CHAN_NO_IDENTITY == state) )
    {
        press = KGUI_CHAN_PRESS_ON;
    }
    else if ( (uint8_t)CHAN_NO_NET == state )
    {
        press = KGUI_CHAN_PRESS_NO_NET;
    }
    else
    {
        /* Joining, up, or a state this build does not know: the press leaves. */
    }
    return press;
}
