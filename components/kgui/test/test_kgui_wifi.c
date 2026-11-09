/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_wifi.c
 * @brief Host tests of the radios' rows' words (kgui_wifi.h).
 */
#include <stddef.h>
#include <stdint.h>

#include "unity.h"

#include "kdrv_wifi.h"
#include "kgui_wifi.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_wifi_off_is_off_whatever_else(void)
{
    kgui_radio_view_t v = { false, true, true, true, true };

    TEST_ASSERT_EQUAL_STRING("Off", kgui_radio_words(&v));
    TEST_ASSERT_EQUAL_STRING("Off", kgui_radio_words(NULL));
}

void test_wifi_on_a_network_says_the_network(void)
{
    kgui_radio_view_t v = { true, true, false, false, false };

    TEST_ASSERT_NULL(kgui_radio_words(&v));
}

void test_wifi_off_its_network_says_it_is_getting_back(void)
{
    kgui_radio_view_t v = { true, false, true, false, false };

    TEST_ASSERT_EQUAL_STRING("Reconnecting...", kgui_radio_words(&v));
    v.reconnecting = false;
    TEST_ASSERT_EQUAL_STRING("On", kgui_radio_words(&v));
}

void test_a_dead_radio_chip_is_never_on(void)
{
    kgui_radio_view_t v = { true, true, false, false, true };

    TEST_ASSERT_EQUAL_STRING("Not responding", kgui_radio_words(&v));
    v.gave_up   = false;
    v.relinking = true;
    TEST_ASSERT_EQUAL_STRING("Reconnecting...", kgui_radio_words(&v));
}

void test_a_failed_join_says_why(void)
{
    TEST_ASSERT_EQUAL_STRING("Wrong password",
                             kgui_wifi_fail_words((uint8_t)KDRV_WIFI_FAIL_PASSWORD));
    TEST_ASSERT_EQUAL_STRING("Network not found",
                             kgui_wifi_fail_words((uint8_t)KDRV_WIFI_FAIL_NOT_FOUND));
    TEST_ASSERT_EQUAL_STRING("Could not join",
                             kgui_wifi_fail_words((uint8_t)KDRV_WIFI_FAIL_OTHER));
    TEST_ASSERT_EQUAL_STRING("Could not join",
                             kgui_wifi_fail_words((uint8_t)KDRV_WIFI_FAIL_NONE));
}

void test_only_a_wait_with_no_answer_goes_nowhere(void)
{
    TEST_ASSERT_TRUE(kgui_wifi_goes_nowhere("timeout"));
    TEST_ASSERT_TRUE(kgui_wifi_goes_nowhere("no_reply"));
    TEST_ASSERT_TRUE(kgui_wifi_goes_nowhere("broker_unreachable"));
    TEST_ASSERT_TRUE(kgui_wifi_goes_nowhere("broker_timeout"));
    TEST_ASSERT_FALSE(kgui_wifi_goes_nowhere("c5_busy"));
    TEST_ASSERT_FALSE(kgui_wifi_goes_nowhere("bad_answer"));
    TEST_ASSERT_FALSE(kgui_wifi_goes_nowhere("no_memory"));
    TEST_ASSERT_FALSE(kgui_wifi_goes_nowhere(""));
    TEST_ASSERT_FALSE(kgui_wifi_goes_nowhere(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_wifi_off_is_off_whatever_else);
    RUN_TEST(test_wifi_on_a_network_says_the_network);
    RUN_TEST(test_wifi_off_its_network_says_it_is_getting_back);
    RUN_TEST(test_a_dead_radio_chip_is_never_on);
    RUN_TEST(test_a_failed_join_says_why);
    RUN_TEST(test_only_a_wait_with_no_answer_goes_nowhere);
    return UNITY_END();
}
