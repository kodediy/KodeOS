/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_prefs.c
 * @brief The interface's two settings in kgui's store (kgui_prefs.h).
 */
#include "kgui_prefs.h"

#include <string.h>

#include "khal_nvs.h"
#include "kode_log.h"

/** @brief Log tag: kgui's own. */
#define TAG "kgui"

#define PREFS_FLAG_MAX        1U  /**< The largest byte a switch holds: 0 or 1. */

#define PREFS_RETIRED_PULLS  "pulls"       /**< A retired u8: the pull down from the top band. */
#define PREFS_RETIRED_QUIET  "quiet_kody"   /**< A retired u8: Kody kept to the face and Rest. */
#define PREFS_RETIRED_MOTION "motion"       /**< A retired u8: reduced motion. */

/**
 * @brief Read one setting's byte.
 *
 * @param key      The setting's key.
 * @param fallback What stands while the store holds nothing usable.
 * @param limit    The largest byte the setting has.
 * @return The stored byte, or @p fallback.
 */
static uint8_t prefs_read(const char *key, uint8_t fallback, uint8_t limit)
{
    uint8_t             value = 0U;
    uint8_t             out   = fallback;
    kode_status_t const st    = khal_nvs_get_u8(KGUI_NVS_NAMESPACE, key, &value);

    if ( KODE_OK == st )
    {
        if ( value <= limit )
        {
            out = value;
        }
        else
        {
            KLOG_W(TAG, "kgui/%s holds %u, which the setting does not have; "
                        "the default stands", key, (unsigned)value);
        }
    }
    else if ( KODE_DENIED != st )
    {
        KLOG_W(TAG, "kgui/%s does not read; the default stands", key);
    }
    else
    {
        /* Nothing stored: the default. */
    }
    return out;
}

/**
 * @brief Erase a key no setting reads any more, if the store still holds it.
 *
 * @param key The retired key.
 */
static void prefs_retire(const char *key)
{
    uint8_t value = 0U;

    if ( (khal_nvs_get_u8(KGUI_NVS_NAMESPACE, key, &value) == KODE_OK)
         && (khal_nvs_erase(KGUI_NVS_NAMESPACE, key) != KODE_OK) )
    {
        KLOG_W(TAG, "the store kept kgui/%s, which nothing reads; it is erased at the "
                    "next boot", key);
    }
}

void kgui_prefs_load(kgui_prefs_t *p)
{
    prefs_retire(PREFS_RETIRED_PULLS);
    prefs_retire(PREFS_RETIRED_QUIET);
    prefs_retire(PREFS_RETIRED_MOTION);

    p->haptic = (kgui_haptic_level_t)prefs_read(KGUI_PREFS_KEY_HAPTIC,
                                                (uint8_t)KGUI_HAPTIC_LIGHT,
                                                (uint8_t)KGUI_HAPTIC_NORMAL);
    p->usb_awake = (prefs_read(KGUI_PREFS_KEY_USB, 0U, PREFS_FLAG_MAX) != 0U);
}

bool kgui_prefs_store(const char *key, uint8_t value)
{
    bool const kept = (khal_nvs_set_u8(KGUI_NVS_NAMESPACE, key, value) == KODE_OK);

    if ( !kept )
    {
        KLOG_W(TAG, "the store refused kgui/%s; the setting holds until the "
                    "next reboot", key);
    }
    return kept;
}

bool kgui_prefs_is_stored(const char *key)
{
    uint8_t value = 0U;

    return (khal_nvs_get_u8(KGUI_NVS_NAMESPACE, key, &value) == KODE_OK);
}

bool kgui_prefs_is_key(const char *key)
{
    return (strcmp(key, KGUI_PREFS_KEY_HAPTIC) == 0)
           || (strcmp(key, KGUI_PREFS_KEY_USB) == 0);
}

const char *kgui_prefs_haptic_label(kgui_haptic_level_t level)
{
    static const char *const LABELS[KGUI_HAPTIC_LEVELS] = {
        "Off", "Light", "Normal",
    };
    const char *label = LABELS[KGUI_HAPTIC_LIGHT];

    if ( KGUI_HAPTIC_LEVELS > level )
    {
        label = LABELS[level];
    }
    return label;
}
