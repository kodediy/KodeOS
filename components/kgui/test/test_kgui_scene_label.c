/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_scene_label.c
 * @brief Host tests of the interface's words for Kody's scenes
 *        (kgui_scene_label.h).
 */
#include <string.h>

#include "unity.h"

#include "kgui_scene_label.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_scene_label_the_six(void)
{
    TEST_ASSERT_EQUAL_STRING("Train", kgui_scene_label("tren"));
    TEST_ASSERT_EQUAL_STRING("Clock", kgui_scene_label("reloj"));
    TEST_ASSERT_EQUAL_STRING("Rooftop", kgui_scene_label("azotea"));
    TEST_ASSERT_EQUAL_STRING("Camping", kgui_scene_label("camping"));
    TEST_ASSERT_EQUAL_STRING("Late night", kgui_scene_label("madrugada"));
    TEST_ASSERT_EQUAL_STRING("Park", kgui_scene_label("parque"));
    TEST_ASSERT_EQUAL_STRING("Breakfast", kgui_scene_label("desayuno"));
    TEST_ASSERT_EQUAL_STRING("Workshop", kgui_scene_label("taller"));
    TEST_ASSERT_EQUAL_STRING("Rain", kgui_scene_label("lluvia"));
    TEST_ASSERT_EQUAL_STRING("Beach", kgui_scene_label("playa"));
}

void test_scene_label_unknown_shows_as_given(void)
{
    const char *const name = "mazo";

    TEST_ASSERT_EQUAL_STRING("mazo", kgui_scene_label(name));
    TEST_ASSERT_EQUAL_STRING("Auto", kgui_scene_label("auto"));
    /* The match is exact: the store's names are lower case. */
    TEST_ASSERT_EQUAL_STRING("Tren", kgui_scene_label("Tren"));
    TEST_ASSERT_EQUAL_STRING("-", kgui_scene_label("-"));
}

void test_scene_label_null_and_empty(void)
{
    TEST_ASSERT_EQUAL_STRING("", kgui_scene_label(NULL));
    TEST_ASSERT_EQUAL_STRING("", kgui_scene_label(""));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_scene_label_the_six);
    RUN_TEST(test_scene_label_unknown_shows_as_given);
    RUN_TEST(test_scene_label_null_and_empty);
    return UNITY_END();
}
