/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_edge.c
 * @brief Host tests of the edge's close and pop (kgui_edge.h).
 */
#include <stdint.h>

#include "unity.h"

#include "kgui_edge.h"

#define HALF_MS      (KGUI_EDGE_CLOSE_MS / 2U)  /**< Half of a close. */
#define STEPS        30U                        /**< Samples of a curve a case takes. */

/** @brief Unity's hook before each case: nothing to set up. */
void setUp(void)
{
}

/** @brief Unity's hook after each case: nothing to put away. */
void tearDown(void)
{
}

void test_close_begins_where_it_stood(void)
{
    TEST_ASSERT_EQUAL_UINT16(620U, kgui_ring_close_pm(620U, 0U));
    TEST_ASSERT_EQUAL_UINT16(0U, kgui_ring_close_pm(0U, 0U));
}

void test_close_ends_whole_and_stays(void)
{
    TEST_ASSERT_EQUAL_UINT16(KGUI_RING_FULL_PM, kgui_ring_close_pm(0U, KGUI_EDGE_CLOSE_MS));
    TEST_ASSERT_EQUAL_UINT16(KGUI_RING_FULL_PM, kgui_ring_close_pm(620U, KGUI_EDGE_CLOSE_MS));
    TEST_ASSERT_EQUAL_UINT16(KGUI_RING_FULL_PM, kgui_ring_close_pm(620U, KGUI_EDGE_CLOSE_MS + 500U));
}

void test_close_from_whole_stays_whole(void)
{
    for ( uint32_t t = 0U; KGUI_EDGE_CLOSE_MS >= t; t += 10U )
    {
        TEST_ASSERT_EQUAL_UINT16(KGUI_RING_FULL_PM, kgui_ring_close_pm(KGUI_RING_FULL_PM, t));
    }
}

void test_close_never_goes_back(void)
{
    uint16_t last = 100U;

    for ( uint32_t t = 0U; KGUI_EDGE_CLOSE_MS >= t; t += (KGUI_EDGE_CLOSE_MS / STEPS) )
    {
        uint16_t const pm = kgui_ring_close_pm(100U, t);

        TEST_ASSERT_TRUE(pm >= last);
        TEST_ASSERT_TRUE(KGUI_RING_FULL_PM >= pm);
        last = pm;
    }
}

void test_close_eases_out(void)
{
    /* Over half of the way is done by half of the time. */
    uint16_t const half = kgui_ring_close_pm(0U, HALF_MS);

    TEST_ASSERT_TRUE((KGUI_RING_FULL_PM / 2U) < half);
    TEST_ASSERT_TRUE(KGUI_RING_FULL_PM > half);
}

void test_pop_is_nothing_before_and_after(void)
{
    TEST_ASSERT_EQUAL_INT32(0, kgui_ring_pop_px(0U));
    TEST_ASSERT_EQUAL_INT32(0, kgui_ring_pop_px(2U * KGUI_EDGE_POP_MS));
    TEST_ASSERT_EQUAL_INT32(0, kgui_ring_pop_px(5000U));
}

void test_pop_peaks_between_its_halves(void)
{
    TEST_ASSERT_EQUAL_INT32(KGUI_EDGE_POP_PX, kgui_ring_pop_px(KGUI_EDGE_POP_MS));
}

void test_pop_rises_then_thins(void)
{
    int32_t last = 0;

    for ( uint32_t t = 0U; KGUI_EDGE_POP_MS >= t; t += 3U )
    {
        int32_t const px = kgui_ring_pop_px(t);

        TEST_ASSERT_TRUE(px >= last);
        last = px;
    }
    for ( uint32_t t = KGUI_EDGE_POP_MS; (2U * KGUI_EDGE_POP_MS) >= t; t += 3U )
    {
        int32_t const px = kgui_ring_pop_px(t);

        TEST_ASSERT_TRUE(px <= last);
        last = px;
    }
}

void test_done_is_close_then_pop(void)
{
    TEST_ASSERT_EQUAL_UINT32(KGUI_EDGE_CLOSE_MS + (2U * KGUI_EDGE_POP_MS), KGUI_EDGE_DONE_MS);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_close_begins_where_it_stood);
    RUN_TEST(test_close_ends_whole_and_stays);
    RUN_TEST(test_close_from_whole_stays_whole);
    RUN_TEST(test_close_never_goes_back);
    RUN_TEST(test_close_eases_out);
    RUN_TEST(test_pop_is_nothing_before_and_after);
    RUN_TEST(test_pop_peaks_between_its_halves);
    RUN_TEST(test_pop_rises_then_thins);
    RUN_TEST(test_done_is_close_then_pop);
    return UNITY_END();
}
