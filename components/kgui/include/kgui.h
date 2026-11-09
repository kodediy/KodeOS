/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui.h
 * @brief kgui: kodeOS's own interface, AO_Gui, started after kode_init().
 */
#ifndef KGUI_H_
#define KGUI_H_

#include "kode_sys.h"

#ifdef __cplusplus
extern "C" {
#endif

extern QActive * const AO_Gui;      /**< The GUI AO. */

/** @brief Construct AO_Gui. Prefer kgui_start(), which also starts it. */
void Gui_ctor(void);

/**
 * @brief Construct AO_Gui and start it, with its own queue and stack.
 *
 * Call after kode_init(). There is no ordering to respect: the panel is asked
 * for rather than waited on (DISPLAY_STATE_SIG), so it does not matter that
 * AO_Display announced itself before this AO existed.
 *
 * The console reaches this AO by publishing GUI_DUMP_REQ_SIG, so kodeOS's own
 * console does not link against kgui and a build without a GUI needs no
 * stubs - `gui_info` simply times out, which is the diagnostic it always was.
 * What kgui adds to the console it adds the way an app does: with the
 * console in the build, this registers the `assets_*` functions of Kody's
 * pack (KODE_FUNCTIONS_ADD()) and its settings, `lock`, `scene`, `clock`,
 * `kody_sleep` and the interface's own (KODE_CONFIG_ADD()), once AO_Gui has a
 * queue for them to reach. The zone is AO_Boot's; kgui shows it.
 */
void kgui_start(void);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_H_ */
