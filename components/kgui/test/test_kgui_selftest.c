/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_selftest.c
 * @brief Host tests of what the Self-test page says about a run (kgui_selftest.h).
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "kgui_selftest.h"

#define ROWS_USER    21U    /**< Rows of a whole `user` run, as a board reports it. */
#define GUIDED_ROWS  4U     /**< Its rows that need a person: buttons, led, display, tp. */
#define ROWS_MOST    255U   /**< The most rows a tally can count. */
#define SMALL_CAP    4U     /**< A buffer too small for any verdict. */
#define GUARD_BYTE   0x5A   /**< What a buffer holds past its size before the call. */

/** @brief Unity's hook before each case: nothing to set. */
void setUp(void)
{
}

/** @brief Unity's hook after each case: nothing to put away. */
void tearDown(void)
{
}

/** @brief A tally of @p rows rows, @p passed of them passed and @p failed failed. */
static kgui_selftest_tally_t tally_of(uint8_t rows, uint8_t passed, uint8_t failed)
{
    kgui_selftest_tally_t t;

    t.rows   = rows;
    t.passed = passed;
    t.failed = failed;
    return t;
}

/** @brief The verdict of a tally, in a buffer of the size the header names. */
static void expect_verdict(uint8_t rows, uint8_t passed, uint8_t failed, const char *want)
{
    kgui_selftest_tally_t const t = tally_of(rows, passed, failed);
    char                        out[KGUI_SELFTEST_VERDICT_MAX];

    kgui_selftest_verdict(&t, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(want, out);
}

/* ---- The verdict ---- */

void test_no_run_kept_is_never_run(void)
{
    expect_verdict(0U, 0U, 0U, "Never run");
}

void test_every_row_passed_is_all_good(void)
{
    expect_verdict(ROWS_USER, ROWS_USER, 0U, "All good");
}

/* An unattended run: nobody answered the guided rows, nothing failed. */
void test_rows_nobody_judged_are_not_checked(void)
{
    expect_verdict(ROWS_USER, ROWS_USER - GUIDED_ROWS, 0U, "4 not checked");
}

void test_one_row_not_checked(void)
{
    expect_verdict(ROWS_USER, ROWS_USER - 1U, 0U, "1 not checked");
}

void test_one_failure_is_one_problem(void)
{
    expect_verdict(ROWS_USER, ROWS_USER - 1U, 1U, "1 problem");
}

/* A failure is what to tell first: the rows nobody judged are not counted then. */
void test_failures_are_said_before_rows_not_checked(void)
{
    expect_verdict(ROWS_USER, ROWS_USER - GUIDED_ROWS - 2U, 2U, "2 problems");
}

void test_the_longest_verdict_fits_its_buffer(void)
{
    expect_verdict(ROWS_MOST, 0U, 0U, "255 not checked");
    expect_verdict(ROWS_MOST, 0U, ROWS_MOST, "255 problems");
}

/* A tally whose passes and failures outnumber its rows judges nothing it has not got. */
void test_a_tally_that_does_not_add_up_has_none_unchecked(void)
{
    kgui_selftest_tally_t const t = tally_of(2U, 2U, 1U);

    TEST_ASSERT_EQUAL_UINT8(0U, kgui_selftest_unchecked(&t));
    expect_verdict(2U, 2U, 1U, "1 problem");
}

void test_a_small_buffer_is_cut_and_terminated(void)
{
    kgui_selftest_tally_t const t = tally_of(ROWS_USER, 0U, 0U);
    char                        out[SMALL_CAP + 1U];

    (void)memset(out, GUARD_BYTE, sizeof(out));
    kgui_selftest_verdict(&t, out, SMALL_CAP);
    TEST_ASSERT_EQUAL_STRING("21 ", out);
    TEST_ASSERT_EQUAL_INT8(GUARD_BYTE, (int8_t)out[SMALL_CAP]);
}

void test_no_buffer_is_left_alone(void)
{
    kgui_selftest_tally_t const t = tally_of(ROWS_USER, ROWS_USER, 0U);
    char                        out[1];

    out[0] = (char)GUARD_BYTE;
    kgui_selftest_verdict(&t, NULL, sizeof(out));
    kgui_selftest_verdict(&t, out, 0U);
    TEST_ASSERT_EQUAL_INT8(GUARD_BYTE, (int8_t)out[0]);
}

/* ---- All good ---- */

void test_all_good_only_when_every_row_passed(void)
{
    kgui_selftest_tally_t const none     = tally_of(0U, 0U, 0U);
    kgui_selftest_tally_t const all      = tally_of(ROWS_USER, ROWS_USER, 0U);
    kgui_selftest_tally_t const unjudged = tally_of(ROWS_USER, ROWS_USER - 1U, 0U);
    kgui_selftest_tally_t const failed   = tally_of(ROWS_USER, ROWS_USER - 1U, 1U);

    TEST_ASSERT_FALSE(kgui_selftest_all_good(&none));
    TEST_ASSERT_TRUE(kgui_selftest_all_good(&all));
    TEST_ASSERT_FALSE(kgui_selftest_all_good(&unjudged));
    TEST_ASSERT_FALSE(kgui_selftest_all_good(&failed));
}

void test_unchecked_counts_what_neither_passed_nor_failed(void)
{
    kgui_selftest_tally_t const t = tally_of(ROWS_USER, ROWS_USER - GUIDED_ROWS - 1U, 1U);

    TEST_ASSERT_EQUAL_UINT8(GUIDED_ROWS, kgui_selftest_unchecked(&t));
}

/* ---- The footer ---- */

void test_the_footer_before_any_run(void)
{
    kgui_selftest_tally_t const t = tally_of(0U, 0U, 0U);

    TEST_ASSERT_EQUAL_STRING("Takes a few minutes", kgui_selftest_footer(&t));
}

void test_the_footer_with_no_failure(void)
{
    kgui_selftest_tally_t const all      = tally_of(ROWS_USER, ROWS_USER, 0U);
    kgui_selftest_tally_t const unjudged = tally_of(ROWS_USER, ROWS_USER - GUIDED_ROWS, 0U);

    TEST_ASSERT_EQUAL_STRING("Tap a row to test it again", kgui_selftest_footer(&all));
    TEST_ASSERT_EQUAL_STRING("Tap a row to test it again", kgui_selftest_footer(&unjudged));
}

/* A failure says what to do next, a line each, and names no link. */
void test_the_footer_after_a_failure_says_what_to_do(void)
{
    kgui_selftest_tally_t const t = tally_of(ROWS_USER, ROWS_USER - 1U, 1U);

    TEST_ASSERT_EQUAL_STRING("Tap a row to test it again.\nStill failing?\nContact Kode support.",
                             kgui_selftest_footer(&t));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_run_kept_is_never_run);
    RUN_TEST(test_every_row_passed_is_all_good);
    RUN_TEST(test_rows_nobody_judged_are_not_checked);
    RUN_TEST(test_one_row_not_checked);
    RUN_TEST(test_one_failure_is_one_problem);
    RUN_TEST(test_failures_are_said_before_rows_not_checked);
    RUN_TEST(test_the_longest_verdict_fits_its_buffer);
    RUN_TEST(test_a_tally_that_does_not_add_up_has_none_unchecked);
    RUN_TEST(test_a_small_buffer_is_cut_and_terminated);
    RUN_TEST(test_no_buffer_is_left_alone);
    RUN_TEST(test_all_good_only_when_every_row_passed);
    RUN_TEST(test_unchecked_counts_what_neither_passed_nor_failed);
    RUN_TEST(test_the_footer_before_any_run);
    RUN_TEST(test_the_footer_with_no_failure);
    RUN_TEST(test_the_footer_after_a_failure_says_what_to_do);
    return UNITY_END();
}
