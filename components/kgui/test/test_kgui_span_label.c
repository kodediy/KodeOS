/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_span_label.c
 * @brief Host tests of the interface's words for a length of time and of the
 *        step round a setting's words (kgui_span_label.h).
 */
#include <stddef.h>
#include <stdint.h>

#include "unity.h"

#include "kgui_span_label.h"

/** @brief The words a setting offers, as its owner's table gives them. */
static const char *const s_words[] = { "20m", "1h", "6h", "24h", "7d", "never" };

#define WORDS_N  ((uint8_t)(sizeof(s_words) / sizeof(s_words[0])))  /**< How many words the cases step round. */

void setUp(void)
{
}

void tearDown(void)
{
}

void test_span_label_the_six(void)
{
    TEST_ASSERT_EQUAL_STRING("20 minutes", kgui_span_label("20m"));
    TEST_ASSERT_EQUAL_STRING("1 hour", kgui_span_label("1h"));
    TEST_ASSERT_EQUAL_STRING("6 hours", kgui_span_label("6h"));
    TEST_ASSERT_EQUAL_STRING("1 day", kgui_span_label("24h"));
    TEST_ASSERT_EQUAL_STRING("7 days", kgui_span_label("7d"));
    TEST_ASSERT_EQUAL_STRING("Never", kgui_span_label("never"));
}

void test_span_label_unknown_shows_as_given(void)
{
    TEST_ASSERT_EQUAL_STRING("5m", kgui_span_label("5m"));
    /* The match is exact: an owner's words are lower case. */
    TEST_ASSERT_EQUAL_STRING("1H", kgui_span_label("1H"));
}

void test_span_label_null_and_empty(void)
{
    TEST_ASSERT_EQUAL_STRING("", kgui_span_label(NULL));
    TEST_ASSERT_EQUAL_STRING("", kgui_span_label(""));
}

void test_span_next_goes_round_both_ways(void)
{
    TEST_ASSERT_EQUAL_UINT8(1U, kgui_span_next(s_words, WORDS_N, "20m", 1));
    TEST_ASSERT_EQUAL_UINT8(2U, kgui_span_next(s_words, WORDS_N, "1h", 1));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_span_next(s_words, WORDS_N, "never", 1));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_span_next(s_words, WORDS_N, "1h", -1));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(WORDS_N - 1U), kgui_span_next(s_words, WORDS_N, "20m", -1));
    /* Any step that is not back is forward. */
    TEST_ASSERT_EQUAL_UINT8(4U, kgui_span_next(s_words, WORDS_N, "24h", 0));
}

void test_span_next_from_a_word_the_list_lacks(void)
{
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_span_next(s_words, WORDS_N, "5m", 1));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(WORDS_N - 1U), kgui_span_next(s_words, WORDS_N, "5m", -1));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_span_next(s_words, WORDS_N, NULL, 1));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_span_next(s_words, WORDS_N, "", 1));
}

void test_span_next_with_no_list(void)
{
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_span_next(NULL, 0U, "1h", 1));
    TEST_ASSERT_EQUAL_UINT8(WORDS_N, kgui_span_next(NULL, WORDS_N, "1h", 1));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_span_next(s_words, 0U, "1h", 1));
    /* One word: every step stays on it. */
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_span_next(s_words, 1U, "1h", 1));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_span_next(s_words, 1U, "1h", -1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_span_label_the_six);
    RUN_TEST(test_span_label_unknown_shows_as_given);
    RUN_TEST(test_span_label_null_and_empty);
    RUN_TEST(test_span_next_goes_round_both_ways);
    RUN_TEST(test_span_next_from_a_word_the_list_lacks);
    RUN_TEST(test_span_next_with_no_list);
    return UNITY_END();
}
