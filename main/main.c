/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file main.c
 * @brief kodeOS's entry point.
 */
#include "kode_init.h"
#include "kgui.h"

void app_main(void)
{
    /* Initialize kode-sdk */
    kode_init();
    /* Start the kodeOS GUI AO */
    kgui_start();
}
