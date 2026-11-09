/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_rest.c
 * @brief Host tests of Rest's numbers (kgui_rest.h).
 */
#include <stdint.h>

#include "unity.h"

#include "kgui_rest.h"

/** @brief Unity's hook before each case: nothing to set. */
void setUp(void)
{
}

/** @brief Unity's hook after each case: nothing to put away. */
void tearDown(void)
{
}

void test_the_times_are_ticks_of_33(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, KGUI_SLEEP_CUE_MS % 33U);
    TEST_ASSERT_EQUAL_UINT32(0U, KGUI_VEIL_MS % 33U);
    TEST_ASSERT_EQUAL_UINT32(0U, KGUI_PEEK_MS % 33U);
}

void test_the_cycle_stays_near_home(void)
{
    for ( uint32_t i = 0U; i < (2U * KGUI_REST_CYCLE_LEN); ++i )
    {
        kgui_rest_offset_t const o = kgui_rest_offset(i);

        TEST_ASSERT_TRUE((o.x >= -KGUI_REST_SHIFT_PX) && (KGUI_REST_SHIFT_PX >= o.x));
        TEST_ASSERT_TRUE((o.y >= -KGUI_REST_SHIFT_PX) && (KGUI_REST_SHIFT_PX >= o.y));
    }
}

/** @brief A Rest that has been settled long enough and never asked. */
static kgui_rest_sleep_t calm_rest(void)
{
    kgui_rest_sleep_t s = { .settled = true, .cable = false,
                            .calm_ms = KGUI_REST_SLEEP_CALM_MS,
                            .asked = false, .asked_ms = 0U };

    return s;
}

void test_a_settled_rest_on_the_battery_asks_to_sleep(void)
{
    kgui_rest_sleep_t s = calm_rest();

    TEST_ASSERT_TRUE(kgui_rest_sleep_due(&s));
    s.calm_ms = KGUI_REST_SLEEP_CALM_MS - 1U;
    TEST_ASSERT_FALSE(kgui_rest_sleep_due(&s));
}

void test_rest_does_not_ask_while_anything_moves_or_on_a_cable(void)
{
    kgui_rest_sleep_t s = calm_rest();

    s.settled = false;
    TEST_ASSERT_FALSE(kgui_rest_sleep_due(&s));
    s = calm_rest();
    s.cable = true;
    TEST_ASSERT_FALSE(kgui_rest_sleep_due(&s));
}

void test_an_ask_is_not_repeated_until_its_time(void)
{
    kgui_rest_sleep_t s = calm_rest();

    s.asked    = true;
    s.asked_ms = KGUI_REST_SLEEP_AGAIN_MS - 1U;
    TEST_ASSERT_FALSE(kgui_rest_sleep_due(&s));
    s.asked_ms = KGUI_REST_SLEEP_AGAIN_MS;
    TEST_ASSERT_TRUE(kgui_rest_sleep_due(&s));
}

void test_the_time_left_never_goes_below_zero(void)
{
    TEST_ASSERT_EQUAL_UINT32(1800000U, kgui_rest_left_ms(0U, 1800000U));
    TEST_ASSERT_EQUAL_UINT32(1U, kgui_rest_left_ms(1799999U, 1800000U));
    TEST_ASSERT_EQUAL_UINT32(0U, kgui_rest_left_ms(1800000U, 1800000U));
    TEST_ASSERT_EQUAL_UINT32(0U, kgui_rest_left_ms(UINT32_MAX, 1800000U));
}

void test_the_hour_moves_with_the_cycle_on_even_pixels(void)
{
    for ( uint32_t i = 0U; i < KGUI_REST_CYCLE_LEN; ++i )
    {
        kgui_rest_offset_t const at = kgui_rest_offset(i);
        uint16_t x = 0U;
        uint16_t y = 0U;

        TEST_ASSERT_TRUE(kgui_rest_strip_at(40, 46, 322, 78, at, 410, 502, &x, &y));
        TEST_ASSERT_EQUAL_INT(40 + at.x, (int)x);
        TEST_ASSERT_EQUAL_INT(46 + at.y, (int)y);
        TEST_ASSERT_EQUAL_UINT(0U, (unsigned)(x % 2U));
        TEST_ASSERT_EQUAL_UINT(0U, (unsigned)(y % 2U));
    }
}

void test_an_odd_home_goes_down_to_even(void)
{
    kgui_rest_offset_t const home = { 0, 0 };
    uint16_t x = 0U;
    uint16_t y = 0U;

    TEST_ASSERT_TRUE(kgui_rest_strip_at(41, 47, 10, 10, home, 410, 502, &x, &y));
    TEST_ASSERT_EQUAL_UINT(40U, (unsigned)x);
    TEST_ASSERT_EQUAL_UINT(46U, (unsigned)y);
}

void test_an_hour_off_the_glass_is_not_written(void)
{
    kgui_rest_offset_t const left = { -KGUI_REST_SHIFT_PX, 0 };
    kgui_rest_offset_t const right = { KGUI_REST_SHIFT_PX, 0 };
    uint16_t x = 7U;
    uint16_t y = 7U;

    TEST_ASSERT_FALSE(kgui_rest_strip_at(2, 46, 10, 10, left, 410, 502, &x, &y));
    TEST_ASSERT_FALSE(kgui_rest_strip_at(400, 46, 10, 10, right, 410, 502, &x, &y));
    TEST_ASSERT_FALSE(kgui_rest_strip_at(40, 46, 0, 10, right, 410, 502, &x, &y));
    TEST_ASSERT_EQUAL_UINT(7U, (unsigned)x);
    TEST_ASSERT_EQUAL_UINT(7U, (unsigned)y);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_times_are_ticks_of_33);
    RUN_TEST(test_the_cycle_stays_near_home);
    RUN_TEST(test_a_settled_rest_on_the_battery_asks_to_sleep);
    RUN_TEST(test_rest_does_not_ask_while_anything_moves_or_on_a_cable);
    RUN_TEST(test_an_ask_is_not_repeated_until_its_time);
    RUN_TEST(test_the_time_left_never_goes_below_zero);
    RUN_TEST(test_the_hour_moves_with_the_cycle_on_even_pixels);
    RUN_TEST(test_an_odd_home_goes_down_to_even);
    RUN_TEST(test_an_hour_off_the_glass_is_not_written);
    return UNITY_END();
}
