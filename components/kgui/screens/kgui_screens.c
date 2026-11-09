/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_screens.c
 * @brief The four subjects, and the order the screens are built in.
 */
#include "kgui_screens.h"

/** Room for the longest string a subject carries, terminator included. */
#define SUBJECT_TEXT_LEN 256U

lv_subject_t *kgui_subject_app_count = NULL;
lv_subject_t *kgui_subject_empty_text = NULL;
lv_subject_t *kgui_subject_loading_text = NULL;
lv_subject_t *kgui_subject_tp_text = NULL;

/** The string subjects' buffers: the value and the previous one, each. */
static char s_empty_text[SUBJECT_TEXT_LEN];
static char s_empty_text_prev[SUBJECT_TEXT_LEN];
static char s_loading_text[SUBJECT_TEXT_LEN];
static char s_loading_text_prev[SUBJECT_TEXT_LEN];
static char s_tp_text[SUBJECT_TEXT_LEN];
static char s_tp_text_prev[SUBJECT_TEXT_LEN];

/**
 * @brief An integer subject.
 * @param value Its first value.
 * @return The subject.
 */
static lv_subject_t *int_subject(int32_t value)
{
    lv_subject_t *s = lv_subject_create(LV_SUBJECT_TYPE_INT);
    lv_subject_set_int(s, value);
    return s;
}

/**
 * @brief A string subject over two static buffers.
 * @param buf   The value's buffer, SUBJECT_TEXT_LEN long.
 * @param prev  The previous value's buffer, as long.
 * @param value Its first value.
 * @return The subject.
 */
static lv_subject_t *string_subject(char *buf, char *prev, const char *value)
{
    lv_subject_t *s = lv_subject_create(LV_SUBJECT_TYPE_STRING);
    lv_subject_set_string_buffer_static(s, buf, prev, SUBJECT_TEXT_LEN);
    lv_subject_set_string(s, value);
    return s;
}

bool kgui_clock_known(int32_t hour, int32_t minute)
{
    return (0 <= hour) && (KGUI_CLOCK_HOURS > hour)
           && (0 <= minute) && (KGUI_CLOCK_MINUTES > minute);
}

int32_t kgui_clock_hour(int32_t hour, bool h12)
{
    int32_t read = hour;

    if ( h12 )
    {
        read = hour % KGUI_CLOCK_HOURS_12;
        if ( 0 == read )
        {
            read = KGUI_CLOCK_HOURS_12;
        }
    }
    return read;
}

void kgui_screens_init(void)
{
    if ( NULL != kgui_subject_app_count )
    {
        return;
    }
    /* The first strings are padded to the longest message each carries
     * (20, 42 and 42 characters), as the screens have always started. */
    kgui_subject_app_count = int_subject(0);
    kgui_subject_empty_text = string_subject(s_empty_text, s_empty_text_prev,
                                             "No apps yet.        ");
    kgui_subject_loading_text = string_subject(s_loading_text, s_loading_text_prev,
                                               "Installing...                             ");
    kgui_subject_tp_text = string_subject(s_tp_text, s_tp_text_prev,
                                          "Tap the target  (1/5)                     ");
    kgui_screen_face_build();
    kgui_screen_launcher_build();
    kgui_screen_loading_build();
    kgui_screen_settings_build();
    kgui_screen_touch_test_build();
}
