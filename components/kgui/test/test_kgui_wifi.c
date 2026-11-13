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
    /* A refusal that is no timeout is the password, said outright, however
     * the network was heard. */
    TEST_ASSERT_EQUAL_STRING("Wrong password",
                             kgui_wifi_fail_words((uint8_t)KDRV_WIFI_FAIL_PASSWORD, -40));
    TEST_ASSERT_EQUAL_STRING("Wrong password",
                             kgui_wifi_fail_words((uint8_t)KDRV_WIFI_FAIL_PASSWORD, -90));
    TEST_ASSERT_EQUAL_STRING("Network not found",
                             kgui_wifi_fail_words((uint8_t)KDRV_WIFI_FAIL_NOT_FOUND, -40));
    TEST_ASSERT_EQUAL_STRING("Could not join",
                             kgui_wifi_fail_words((uint8_t)KDRV_WIFI_FAIL_OTHER, -40));
    TEST_ASSERT_EQUAL_STRING("Could not join",
                             kgui_wifi_fail_words((uint8_t)KDRV_WIFI_FAIL_NONE, -40));
}

void test_a_handshake_that_timed_out_is_told_by_the_signal(void)
{
    /* The stack gives one reason for a wrong password and for a signal that
     * went during the handshake: how the network was heard decides. */
    TEST_ASSERT_EQUAL_STRING("Wrong password",
                             kgui_wifi_fail_words((uint8_t)KDRV_WIFI_FAIL_HANDSHAKE, -40));
    TEST_ASSERT_EQUAL_STRING("Wrong password",
                             kgui_wifi_fail_words((uint8_t)KDRV_WIFI_FAIL_HANDSHAKE,
                                                  (int8_t)KGUI_WIFI_WEAK_DBM));
    TEST_ASSERT_EQUAL_STRING("Signal lost",
                             kgui_wifi_fail_words((uint8_t)KDRV_WIFI_FAIL_HANDSHAKE,
                                                  (int8_t)(KGUI_WIFI_WEAK_DBM - 1)));
    /* A hidden network was never heard: the password is the likelier of the two. */
    TEST_ASSERT_EQUAL_STRING("Wrong password",
                             kgui_wifi_fail_words((uint8_t)KDRV_WIFI_FAIL_HANDSHAKE,
                                                  (int8_t)KGUI_WIFI_RSSI_NONE));
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

void test_pair_new_device_says_visible_only_with_the_link_free(void)
{
    TEST_ASSERT_NULL(kgui_ble_pair_words(false, false, false));
    TEST_ASSERT_NULL(kgui_ble_pair_words(false, false, true));
    TEST_ASSERT_EQUAL_STRING("Visible to new devices",
                             kgui_ble_pair_words(true, false, false));
    /* Asked with a device connected: nothing is on the air yet. */
    TEST_ASSERT_EQUAL_STRING("When the other leaves",
                             kgui_ble_pair_words(false, true, true));
    /* The time runs and a device took the link: the same is true. */
    TEST_ASSERT_EQUAL_STRING("When the other leaves",
                             kgui_ble_pair_words(true, false, true));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_wifi_off_is_off_whatever_else);
    RUN_TEST(test_wifi_on_a_network_says_the_network);
    RUN_TEST(test_wifi_off_its_network_says_it_is_getting_back);
    RUN_TEST(test_a_dead_radio_chip_is_never_on);
    RUN_TEST(test_a_failed_join_says_why);
    RUN_TEST(test_a_handshake_that_timed_out_is_told_by_the_signal);
    RUN_TEST(test_only_a_wait_with_no_answer_goes_nowhere);
    RUN_TEST(test_pair_new_device_says_visible_only_with_the_link_free);
    return UNITY_END();
}
