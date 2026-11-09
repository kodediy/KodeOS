/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_pack_fit.c
 * @brief Host tests of when a pack is given up for its canvas (kgui_pack_fit.h).
 */
#include "unity.h"

#include "kgui_pack_fit.h"

/** @brief Unity's hook before each case: nothing to set. */
void setUp(void)
{
}

/** @brief Unity's hook after each case: nothing to put away. */
void tearDown(void)
{
}

/* The old pack on a real unit: 410 x 288 in all its files. */
void test_a_pack_whose_first_two_wakes_are_refused_is_given_up(void)
{
    kgui_pack_fit_t fit;

    kgui_pack_fit_reset(&fit);
    TEST_ASSERT_FALSE(kgui_pack_fit_wake_refused(&fit));
    TEST_ASSERT_TRUE(kgui_pack_fit_wake_refused(&fit));
}

void test_it_is_given_up_once(void)
{
    kgui_pack_fit_t fit;

    kgui_pack_fit_reset(&fit);
    (void)kgui_pack_fit_wake_refused(&fit);
    (void)kgui_pack_fit_wake_refused(&fit);
    TEST_ASSERT_FALSE(kgui_pack_fit_wake_refused(&fit));
}

/* The pack 2026.11.05.2 on the bench: only tren is stale. */
void test_one_stale_scene_is_not_the_pack(void)
{
    kgui_pack_fit_t fit;

    kgui_pack_fit_reset(&fit);
    TEST_ASSERT_FALSE(kgui_pack_fit_wake_refused(&fit));
    kgui_pack_fit_passed(&fit);
    TEST_ASSERT_FALSE(kgui_pack_fit_wake_refused(&fit));
    TEST_ASSERT_FALSE(kgui_pack_fit_wake_refused(&fit));
}

void test_a_pack_that_has_passed_is_never_given_up(void)
{
    kgui_pack_fit_t fit;

    kgui_pack_fit_reset(&fit);
    kgui_pack_fit_passed(&fit);
    TEST_ASSERT_FALSE(kgui_pack_fit_wake_refused(&fit));
    TEST_ASSERT_FALSE(kgui_pack_fit_wake_refused(&fit));
}

void test_a_new_pack_starts_over(void)
{
    kgui_pack_fit_t fit;

    kgui_pack_fit_reset(&fit);
    (void)kgui_pack_fit_wake_refused(&fit);
    kgui_pack_fit_reset(&fit);
    TEST_ASSERT_FALSE(kgui_pack_fit_wake_refused(&fit));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_pack_whose_first_two_wakes_are_refused_is_given_up);
    RUN_TEST(test_it_is_given_up_once);
    RUN_TEST(test_one_stale_scene_is_not_the_pack);
    RUN_TEST(test_a_pack_that_has_passed_is_never_given_up);
    RUN_TEST(test_a_new_pack_starts_over);
    return UNITY_END();
}
