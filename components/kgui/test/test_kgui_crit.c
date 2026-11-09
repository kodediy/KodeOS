/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_crit.c
 * @brief Host tests of the critical battery card's latch (kgui_syscard.h).
 */
#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

#include "kgui_syscard.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/** @brief A latch that has seen a cell, as a board running on one has. */
static kgui_crit_t latch_with_a_cell(void)
{
    kgui_crit_t c = { false, false };

    TEST_ASSERT_FALSE(kgui_crit_due(&c, false, false, 50U));
    return c;
}

void test_crit_is_due_once_per_entry(void)
{
    kgui_crit_t c = latch_with_a_cell();

    TEST_ASSERT_TRUE(kgui_crit_due(&c, true, false, 10U));
    kgui_crit_taken(&c);
    TEST_ASSERT_FALSE(kgui_crit_due(&c, true, false, 9U));
}

void test_crit_that_waited_is_still_due(void)
{
    /* The reading taken during the rest finds the glass parked and takes
     * nothing: the wake, and every reading after it, still find the card due. */
    kgui_crit_t c = latch_with_a_cell();

    TEST_ASSERT_TRUE(kgui_crit_due(&c, true, false, 10U));
    TEST_ASSERT_TRUE(kgui_crit_waits(false, false, false, true));
    TEST_ASSERT_TRUE(kgui_crit_due(&c, true, false, 8U));
    TEST_ASSERT_TRUE(kgui_crit_due(&c, true, false, 6U));
}

void test_crit_waits_for_each_thing_that_has_the_glass(void)
{
    TEST_ASSERT_TRUE(kgui_crit_waits(true, false, false, false));
    TEST_ASSERT_TRUE(kgui_crit_waits(false, true, false, false));
    TEST_ASSERT_TRUE(kgui_crit_waits(false, false, true, false));
    TEST_ASSERT_TRUE(kgui_crit_waits(false, false, false, true));
}

void test_crit_rises_on_a_clear_glass(void)
{
    TEST_ASSERT_FALSE(kgui_crit_waits(false, false, false, false));
}

void test_crit_opens_again_on_the_cable_and_on_the_climb(void)
{
    kgui_crit_t c = latch_with_a_cell();

    TEST_ASSERT_TRUE(kgui_crit_due(&c, true, false, 10U));
    kgui_crit_taken(&c);
    TEST_ASSERT_FALSE(kgui_crit_due(&c, false, false, KGUI_BAT_CRIT_PCT + KGUI_BAT_CRIT_REARM_PCT - 1U));
    TEST_ASSERT_FALSE(kgui_crit_due(&c, true, false, 10U));
    TEST_ASSERT_FALSE(kgui_crit_due(&c, false, true, 10U));
    TEST_ASSERT_TRUE(kgui_crit_due(&c, true, false, 10U));
}

void test_crit_is_never_due_with_no_cell_seen(void)
{
    kgui_crit_t c = { false, false };

    TEST_ASSERT_FALSE(kgui_crit_due(&c, true, false, 0U));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crit_is_due_once_per_entry);
    RUN_TEST(test_crit_that_waited_is_still_due);
    RUN_TEST(test_crit_waits_for_each_thing_that_has_the_glass);
    RUN_TEST(test_crit_rises_on_a_clear_glass);
    RUN_TEST(test_crit_opens_again_on_the_cable_and_on_the_climb);
    RUN_TEST(test_crit_is_never_due_with_no_cell_seen);
    return UNITY_END();
}
