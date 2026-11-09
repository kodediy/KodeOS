/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_motion.c
 * @brief Host tests of where a list stands (kgui_motion.h).
 */
#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

#include "kgui_motion.h"

/** @brief From the first row's start to the focus line, in pixels. */
#define KGUI_LIST_WALK  ((float)(KGUI_RAIL_FOCUS_Y - KGUI_LIST_FIRST_Y))

void setUp(void)
{
}

void tearDown(void)
{
}

void test_a_list_that_fills_the_glass_starts_at_the_top(void)
{
    TEST_ASSERT_TRUE(kgui_rows_fill(5U));
    TEST_ASSERT_FALSE(kgui_rows_fill(4U));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, (float)KGUI_LIST_FIRST_Y, (float)KGUI_ROW_MID - kgui_rows_top_scroll(0.0F));
}

void test_a_short_list_is_centred(void)
{
    /* Its middle on the focus line, whichever row the focus is on. */
    for ( uint32_t n = 1U; KGUI_RAIL_BLOCK_MAX >= n; ++n )
    {
        float const middle = ((float)n * (float)KGUI_ROW_PITCH) / 2.0F;

        TEST_ASSERT_FLOAT_WITHIN(0.01F, (float)KGUI_RAIL_FOCUS_Y, middle - kgui_rows_scroll(0.0F, n));
    }
}

void test_it_can_be_taken_down_to_the_focus_line(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01F, (float)KGUI_RAIL_FOCUS_Y,
                             (float)KGUI_ROW_MID - kgui_rows_top_scroll(-KGUI_LIST_WALK));
}

void test_past_the_walk_it_runs_as_any_list(void)
{
    float const u = kgui_rows_rest(4U);

    TEST_ASSERT_FLOAT_WITHIN(0.01F, kgui_rows_scroll(u, 10U), kgui_rows_top_scroll(u));
}

void test_a_finger_finds_the_way_back(void)
{
    for ( float u = -170.0F; 0.0F > u; u += 30.0F )
    {
        TEST_ASSERT_FLOAT_WITHIN(0.01F, u, kgui_rows_top_u(kgui_rows_top_scroll(u)));
    }
    for ( float u = 200.0F; 900.0F > u; u += 70.0F )
    {
        TEST_ASSERT_FLOAT_WITHIN(0.01F, u, kgui_rows_top_u(kgui_rows_top_scroll(u)));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_list_that_fills_the_glass_starts_at_the_top);
    RUN_TEST(test_a_short_list_is_centred);
    RUN_TEST(test_it_can_be_taken_down_to_the_focus_line);
    RUN_TEST(test_past_the_walk_it_runs_as_any_list);
    RUN_TEST(test_a_finger_finds_the_way_back);
    return UNITY_END();
}
