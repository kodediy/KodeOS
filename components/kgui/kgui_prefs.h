/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_prefs.h
 * @brief The interface's two settings: their keys, their words and their place in kgui's store.
 */
#ifndef KGUI_PREFS_H
#define KGUI_PREFS_H

#include <stdbool.h>
#include <stdint.h>

#include "kgui_haptic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KGUI_NVS_NAMESPACE      "kgui"        /**< kgui's own namespace: the AO that applies a setting is the one that stores it. */
#define KGUI_PREFS_KEY_HAPTIC   "haptic_ui"   /**< u8: how much of the interface buzzes (a kgui_haptic_level_t); absent is Light. */
#define KGUI_PREFS_KEY_USB      "usb_awake"   /**< u8: 1 = a cable keeps the Lock out of Rest; absent is off. */

/** @brief The two settings as the store holds them. */
typedef struct {
    kgui_haptic_level_t haptic;          /**< How much of the interface buzzes. */
    bool                usb_awake;       /**< A cable keeps the Lock out of Rest. */
} kgui_prefs_t;

/**
 * @brief Read the two settings from the store, once, at boot.
 *
 * A missing key is its default (Light, not awake on USB). A key
 * that does not read, or holds a byte the setting does not have, is the
 * default as well, said with a warning. The line the boot ends with,
 * `interface: ...`, names what came of it. The keys of the three settings
 * that were retired (`pulls`, `quiet_kody`, `motion`) are erased here when
 * the store still holds them.
 *
 * @param p Out: the settings.
 */
void kgui_prefs_load(kgui_prefs_t *p);

/**
 * @brief Keep one setting.
 *
 * Written even when it names the value already stored: a choice made on
 * purpose, which also mends a byte the boot could not read. A refusal is said
 * and the setting holds until the next reboot.
 *
 * @param key   One of the KGUI_PREFS_KEY_* names.
 * @param value The byte.
 * @return true when the store kept it.
 */
bool kgui_prefs_store(const char *key, uint8_t value);

/**
 * @brief Whether the store holds a setting, for `config`'s `stored=`.
 *
 * @param key One of the KGUI_PREFS_KEY_* names.
 * @return true when a byte is stored under it.
 */
bool kgui_prefs_is_stored(const char *key);

/**
 * @brief Whether a `config` key is one of the two.
 *
 * @param key The key a request names.
 * @return true for `haptic_ui` and `usb_awake`.
 */
bool kgui_prefs_is_key(const char *key);

/**
 * @brief A haptic level in the Screen card's words.
 *
 * @param level A level.
 * @return "Off", "Light" or "Normal".
 */
const char *kgui_prefs_haptic_label(kgui_haptic_level_t level);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_PREFS_H */
