/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_sim.c
 * @brief kodeOS's screens for the kit's simulator.
 */
#include <stdlib.h>
#include <string.h>

#include "kui_sim.h"
#include "kgui_screens.h"

/** @brief The face. @return kgui_screen_face. */
static lv_obj_t *get_face(void) { return kgui_screen_face; }
/** @brief The launcher. @return kgui_screen_launcher. */
static lv_obj_t *get_launcher(void) { return kgui_screen_launcher; }
/** @brief The loading screen. @return kgui_screen_loading. */
static lv_obj_t *get_loading(void) { return kgui_screen_loading; }
/** @brief The settings grid. @return kgui_screen_settings. */
static lv_obj_t *get_settings(void) { return kgui_screen_settings; }
/** @brief The touch test. @return kgui_screen_touch_test. */
static lv_obj_t *get_touch_test(void) { return kgui_screen_touch_test; }

const kui_sim_screen_t kui_sim_screens[] = {
    { "face", get_face },
    { "launcher", get_launcher },
    { "loading", get_loading },
    { "settings", get_settings },
    { "touch_test", get_touch_test },
};
const uint32_t kui_sim_screen_count = (uint32_t)(sizeof(kui_sim_screens) / sizeof(kui_sim_screens[0]));

void kui_sim_setup(void)
{
    kgui_screens_init();
}

/**
 * @brief A whole string as a decimal integer.
 * @param s   The text.
 * @param out The value.
 * @return false when anything but digits (and a leading sign) is there.
 */
static bool parse_int(const char *s, int32_t *out)
{
    char *end = NULL;
    long const v = strtol(s, &end, 10);
    if ( (end == s) || ('\0' != *end) )
    {
        return false;
    }
    *out = (int32_t)v;
    return true;
}

bool kui_sim_set(const char *subject, const char *value)
{
    int32_t n = 0;
    if ( strcmp(subject, "app_count") == 0 )
    {
        if ( !parse_int(value, &n) )
        {
            return false;
        }
        lv_subject_set_int(kgui_subject_app_count, n);
        return true;
    }
    if ( strcmp(subject, "empty_text") == 0 )
    {
        lv_subject_set_string(kgui_subject_empty_text, value);
        return true;
    }
    if ( strcmp(subject, "loading_text") == 0 )
    {
        lv_subject_set_string(kgui_subject_loading_text, value);
        return true;
    }
    if ( strcmp(subject, "tp_text") == 0 )
    {
        lv_subject_set_string(kgui_subject_tp_text, value);
        return true;
    }
    return false;
}
