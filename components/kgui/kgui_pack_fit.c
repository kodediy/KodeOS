/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_pack_fit.c
 * @brief Whether a pack was drawn for this face (kgui_pack_fit.h).
 */
#include "kgui_pack_fit.h"

void kgui_pack_fit_reset(kgui_pack_fit_t *fit)
{
    fit->wakes_refused = 0U;
    fit->passed        = false;
}

void kgui_pack_fit_passed(kgui_pack_fit_t *fit)
{
    fit->passed = true;
}

bool kgui_pack_fit_wake_refused(kgui_pack_fit_t *fit)
{
    bool give_up = false;

    if ( (!fit->passed) && (KGUI_PACK_FIT_WAKES > fit->wakes_refused) )
    {
        fit->wakes_refused++;
        give_up = (KGUI_PACK_FIT_WAKES == fit->wakes_refused);
    }
    return give_up;
}
