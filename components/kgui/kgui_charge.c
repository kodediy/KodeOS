/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_charge.c
 * @brief What a board on a cable is doing with it (kgui_charge.h).
 */
#include "kgui_charge.h"

#include <stdio.h>

#include "kdrv_bq25896.h"

#define CHARGE_NTC_WARM  2U   /**< NTC_FAULT: warm. */
#define CHARGE_NTC_HOT   6U   /**< NTC_FAULT: hot. */
#define CHARGE_FULL_PCT  100U /**< At this it is not charging any more. */

kgui_charge_t kgui_charge_state(bool vbus, uint8_t chrg, uint8_t ntc, uint8_t soc)
{
    kgui_charge_t state = KGUI_CHARGE_IDLE;

    if ( !vbus )
    {
        state = KGUI_CHARGE_NO_CABLE;
    }
    else if ( ((uint8_t)KDRV_BQ_CHRG_PRE == chrg) || ((uint8_t)KDRV_BQ_CHRG_FAST == chrg) )
    {
        state = KGUI_CHARGE_ON;
    }
    else if ( ((uint8_t)KDRV_BQ_CHRG_DONE == chrg) || (CHARGE_FULL_PCT <= soc) )
    {
        state = KGUI_CHARGE_FULL;
    }
    else if ( (CHARGE_NTC_WARM == ntc) || (CHARGE_NTC_HOT == ntc) )
    {
        state = KGUI_CHARGE_WARM;
    }
    else if ( 0U != ntc )
    {
        state = KGUI_CHARGE_COLD;
    }
    else
    {
        /* A cable, no charge, nothing to blame: `charge` off, or a moment before it starts. */
    }
    return state;
}

bool kgui_charge_held(kgui_charge_t state)
{
    return (KGUI_CHARGE_WARM == state) || (KGUI_CHARGE_COLD == state);
}

void kgui_charge_words(kgui_charge_t state, uint8_t soc, char *out, size_t cap)
{
    switch ( state )
    {
        case KGUI_CHARGE_ON: {
            (void)snprintf(out, cap, "Charging, %u %%", (unsigned)soc);
            break;
        }
        case KGUI_CHARGE_FULL: {
            (void)snprintf(out, cap, "Fully charged");
            break;
        }
        case KGUI_CHARGE_WARM: {
            (void)snprintf(out, cap, "Too warm to charge");
            break;
        }
        case KGUI_CHARGE_COLD: {
            (void)snprintf(out, cap, "Too cold to charge");
            break;
        }
        case KGUI_CHARGE_IDLE: {
            (void)snprintf(out, cap, "Plugged in");
            break;
        }
        case KGUI_CHARGE_NO_CABLE:
        default: {
            (void)snprintf(out, cap, "%s", "");
            break;
        }
    }
}

bool kgui_charge_notice(kgui_charge_t was, kgui_charge_t now, bool plugged, bool *owed,
                        uint8_t soc, char *out, size_t cap)
{
    bool say = false;

    if ( KGUI_CHARGE_NO_CABLE == now )
    {
        *owed = false;
    }
    else if ( plugged && (KGUI_CHARGE_IDLE == now) )
    {
        *owed = true;   /* One more reading: the charge may be about to start. */
    }
    else if ( plugged || *owed )
    {
        *owed = false;
        say   = true;
    }
    else
    {
        say = kgui_charge_held(now) && (was != now);
    }
    if ( say )
    {
        kgui_charge_words(now, soc, out, cap);
    }
    return say;
}
