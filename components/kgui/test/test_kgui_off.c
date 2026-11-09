/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_off.c
 * @brief Host tests of the face's turn-off arc (kgui_off.h).
 */
#include <stdint.h>

#include "unity.h"

#include "kgui_off.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_off_fill_waits_for_the_start(void)
{
    TEST_ASSERT_EQUAL_UINT16(0U, kgui_off_fill_pm(0U));
    TEST_ASSERT_EQUAL_UINT16(0U, kgui_off_fill_pm(GUI_OFF_ARC_FROM_MS));
    TEST_ASSERT_EQUAL_UINT16(0U, kgui_off_fill_pm(GUI_OFF_ARC_FROM_MS - 1U));
}

void test_off_fill_is_linear_and_whole_at_the_end(void)
{
    uint32_t const mid = (GUI_OFF_ARC_FROM_MS + GUI_OFF_ARC_TO_MS) / 2U;

    TEST_ASSERT_EQUAL_UINT16(500U, kgui_off_fill_pm(mid));
    TEST_ASSERT_EQUAL_UINT16(1000U, kgui_off_fill_pm(GUI_OFF_ARC_TO_MS));
    TEST_ASSERT_EQUAL_UINT16(1000U, kgui_off_fill_pm(GUI_OFF_ARC_TO_MS + 9000U));
    TEST_ASSERT_TRUE(kgui_off_fill_pm(mid + 100U) > kgui_off_fill_pm(mid));
}

void test_off_empty_takes_its_time_and_never_grows(void)
{
    TEST_ASSERT_EQUAL_UINT16(600U, kgui_off_empty_pm(600U, 0U));
    TEST_ASSERT_EQUAL_UINT16(300U, kgui_off_empty_pm(600U, GUI_OFF_ARC_EMPTY_MS / 2U));
    TEST_ASSERT_EQUAL_UINT16(0U, kgui_off_empty_pm(600U, GUI_OFF_ARC_EMPTY_MS));
    TEST_ASSERT_EQUAL_UINT16(0U, kgui_off_empty_pm(600U, 100000U));
    TEST_ASSERT_EQUAL_UINT16(0U, kgui_off_empty_pm(0U, 10U));
}

void test_off_split_empty_and_whole(void)
{
    kgui_off_parts_t p;

    kgui_off_split(0U, &p);
    TEST_ASSERT_EQUAL_UINT16(0U, p.left_pm);
    TEST_ASSERT_EQUAL_UINT16(0U, p.line_px);
    TEST_ASSERT_EQUAL_UINT16(0U, p.right_pm);

    kgui_off_split(KGUI_OFF_FULL_PM, &p);
    TEST_ASSERT_EQUAL_UINT16(1000U, p.left_pm);
    TEST_ASSERT_EQUAL_UINT16(KGUI_OFF_LINE_PX, p.line_px);
    TEST_ASSERT_EQUAL_UINT16(1000U, p.right_pm);

    kgui_off_split(60000U, &p);
    TEST_ASSERT_EQUAL_UINT16(1000U, p.left_pm);
    TEST_ASSERT_EQUAL_UINT16(KGUI_OFF_LINE_PX, p.line_px);
    TEST_ASSERT_EQUAL_UINT16(1000U, p.right_pm);
}

void test_off_split_fills_in_order(void)
{
    kgui_off_parts_t p;

    /* A tenth of the path is inside the first corner. */
    kgui_off_split(100U, &p);
    TEST_ASSERT_TRUE(0U < p.left_pm);
    TEST_ASSERT_TRUE(1000U > p.left_pm);
    TEST_ASSERT_EQUAL_UINT16(0U, p.line_px);
    TEST_ASSERT_EQUAL_UINT16(0U, p.right_pm);

    /* Half of it is the middle of the straight: the corner is whole. */
    kgui_off_split(500U, &p);
    TEST_ASSERT_EQUAL_UINT16(1000U, p.left_pm);
    TEST_ASSERT_TRUE(0U < p.line_px);
    TEST_ASSERT_TRUE(KGUI_OFF_LINE_PX > p.line_px);
    TEST_ASSERT_EQUAL_UINT16(0U, p.right_pm);

    /* Nine tenths is in the last corner: the straight is whole. */
    kgui_off_split(900U, &p);
    TEST_ASSERT_EQUAL_UINT16(1000U, p.left_pm);
    TEST_ASSERT_EQUAL_UINT16(KGUI_OFF_LINE_PX, p.line_px);
    TEST_ASSERT_TRUE(0U < p.right_pm);
    TEST_ASSERT_TRUE(1000U > p.right_pm);
}

void test_off_geometry_is_the_kshell_path(void)
{
    TEST_ASSERT_EQUAL_UINT32(160U, KGUI_OFF_CORNER_PX);
    TEST_ASSERT_EQUAL_UINT32(182U, KGUI_OFF_LINE_PX);
    TEST_ASSERT_EQUAL_UINT32(502U, KGUI_OFF_PATH_PX);
}

void test_off_fill_just_after_the_start_is_still_nothing(void)
{
    /* 1 ms of 3000 is a third of a thousandth: integer division drops it. */
    TEST_ASSERT_EQUAL_UINT16(0U, kgui_off_fill_pm(GUI_OFF_ARC_FROM_MS + 1U));
    TEST_ASSERT_EQUAL_UINT16(999U, kgui_off_fill_pm(GUI_OFF_ARC_TO_MS - 1U));
}

void test_off_empty_just_before_the_end_is_nearly_nothing(void)
{
    uint16_t const left = kgui_off_empty_pm(1000U, GUI_OFF_ARC_EMPTY_MS - 1U);

    TEST_ASSERT_TRUE(0U < left);
    TEST_ASSERT_TRUE(10U > left);
}

void test_off_split_at_the_corner_boundary(void)
{
    kgui_off_parts_t p;

    /* 318 thousandths of 502 px is 159 px: the corner is not whole yet. */
    kgui_off_split(318U, &p);
    TEST_ASSERT_TRUE(1000U > p.left_pm);
    TEST_ASSERT_EQUAL_UINT16(0U, p.line_px);
    /* 319 is 160 px: the corner is whole and the straight has not begun. */
    kgui_off_split(319U, &p);
    TEST_ASSERT_EQUAL_UINT16(1000U, p.left_pm);
    TEST_ASSERT_EQUAL_UINT16(0U, p.line_px);
    TEST_ASSERT_EQUAL_UINT16(0U, p.right_pm);
}

void test_off_split_never_goes_back_as_the_arc_fills(void)
{
    kgui_off_parts_t was = { 0U, 0U, 0U };

    for ( uint16_t pm = 0U; KGUI_OFF_FULL_PM >= pm; pm++ )
    {
        kgui_off_parts_t now;

        kgui_off_split(pm, &now);
        TEST_ASSERT_TRUE(now.left_pm >= was.left_pm);
        TEST_ASSERT_TRUE(now.line_px >= was.line_px);
        TEST_ASSERT_TRUE(now.right_pm >= was.right_pm);
        /* A piece starts only when the one before it is whole. */
        if ( 0U < now.line_px )
        {
            TEST_ASSERT_EQUAL_UINT16(1000U, now.left_pm);
        }
        if ( 0U < now.right_pm )
        {
            TEST_ASSERT_EQUAL_UINT16(KGUI_OFF_LINE_PX, now.line_px);
        }
        was = now;
    }
}

void test_off_a_hold_let_go_at_three_seconds_empties_to_nothing(void)
{
    /* The contract of the face: held to 3 s the arc stands at a third, and
     * once let go it is gone in EMPTY ms, never growing on the way. */
    uint16_t const at = kgui_off_fill_pm(3000U);
    uint16_t last = at;

    TEST_ASSERT_EQUAL_UINT16(333U, at);
    for ( uint32_t t = 0U; GUI_OFF_ARC_EMPTY_MS >= t; t += 10U )
    {
        uint16_t const pm = kgui_off_empty_pm(at, t);

        TEST_ASSERT_TRUE(pm <= last);
        last = pm;
    }
    TEST_ASSERT_EQUAL_UINT16(0U, last);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_fill_waits_for_the_start);
    RUN_TEST(test_off_fill_is_linear_and_whole_at_the_end);
    RUN_TEST(test_off_empty_takes_its_time_and_never_grows);
    RUN_TEST(test_off_split_empty_and_whole);
    RUN_TEST(test_off_split_fills_in_order);
    RUN_TEST(test_off_geometry_is_the_kshell_path);
    RUN_TEST(test_off_fill_just_after_the_start_is_still_nothing);
    RUN_TEST(test_off_empty_just_before_the_end_is_nearly_nothing);
    RUN_TEST(test_off_split_at_the_corner_boundary);
    RUN_TEST(test_off_split_never_goes_back_as_the_arc_fills);
    RUN_TEST(test_off_a_hold_let_go_at_three_seconds_empties_to_nothing);
    return UNITY_END();
}
