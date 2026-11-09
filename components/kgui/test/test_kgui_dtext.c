/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_dtext.c
 * @brief Host tests of the words of an app's details (kgui_dtext.h).
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "kgui_dtext.h"

#define KB 1024U            /**< Bytes in a KB. */
#define MB (1024U * 1024U)  /**< Bytes in a MB. */

void setUp(void)
{
}

void tearDown(void)
{
}

void test_dtext_size_kb_and_mb(void)
{
    char out[KGUI_DTEXT_SIZE_MAX];

    kgui_dtext_size(812U * KB, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("812 KB", out);
    kgui_dtext_size((1U * MB) + (MB / 10U), out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("1.1 MB", out);
    kgui_dtext_size(3U * MB, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("3.0 MB", out);
}

void test_dtext_size_edges(void)
{
    char out[KGUI_DTEXT_SIZE_MAX];

    kgui_dtext_size(0U, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
    kgui_dtext_size(100U, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("1 KB", out);
    /* 1023.5 KB and more rounds to a megabyte, never to "1024 KB". */
    kgui_dtext_size(MB - 1U, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("1.0 MB", out);
    kgui_dtext_size(4000U * KB, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("3.9 MB", out);
}

void test_dtext_size_limits(void)
{
    char out[KGUI_DTEXT_SIZE_MAX];

    kgui_dtext_size(1023U * KB, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("1023 KB", out);
    kgui_dtext_size(1U * MB, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("1.0 MB", out);
    kgui_dtext_size(UINT32_MAX, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("4096.0 MB", out);
}

void test_dtext_null_and_empty_buffers(void)
{
    char out[4] = { 'x', 'x', 'x', 'x' };

    kgui_dtext_size(1U * MB, NULL, 0U);
    kgui_dtext_date("Nov  2 2026", NULL, 0U);
    kgui_dtext_meta("1.0", "games", NULL, 0U);
    kgui_dtext_size(1U * MB, out, 0U);
    TEST_ASSERT_EQUAL_CHAR('x', out[0]);
    kgui_dtext_date("Nov  2 2026", out, 0U);
    TEST_ASSERT_EQUAL_CHAR('x', out[0]);
    kgui_dtext_meta("1.0", "games", out, 0U);
    TEST_ASSERT_EQUAL_CHAR('x', out[0]);
}

void test_dtext_meta_longest_fits(void)
{
    char out[KGUI_DTEXT_META_MAX];

    kgui_dtext_meta("123456789012345", "abcdefghijklmno", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("v123456789012345 \xC2\xB7 Abcdefghijklmno", out);
    TEST_ASSERT_LESS_THAN_UINT32(sizeof(out), strlen(out));
}

void test_dtext_date_day_first(void)
{
    char out[KGUI_DTEXT_DATE_MAX];

    kgui_dtext_date("Nov  2 2026", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("2 Nov 2026", out);
    kgui_dtext_date("Oct 28 2026", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("28 Oct 2026", out);
    kgui_dtext_date("Nov 12 2026", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("12 Nov 2026", out);
}

void test_dtext_date_unknown_is_empty(void)
{
    char out[KGUI_DTEXT_DATE_MAX];

    kgui_dtext_date("", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
    kgui_dtext_date(NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
    kgui_dtext_date("2026-11-02", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
    kgui_dtext_date("Nov  2 20x6", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_dtext_meta_parts(void)
{
    char out[KGUI_DTEXT_META_MAX];

    kgui_dtext_meta("1.2.0", "games", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("v1.2.0 \xC2\xB7 Games", out);
    kgui_dtext_meta("v0.3", "tools", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("v0.3 \xC2\xB7 Tools", out);
    kgui_dtext_meta("1.0", "", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("v1.0", out);
    kgui_dtext_meta(NULL, "games", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Games", out);
    kgui_dtext_meta("", NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_dtext_meta_fits_a_small_buffer(void)
{
    char out[8];

    kgui_dtext_meta("1.2.3.4.5.6", "games", out, sizeof(out));
    TEST_ASSERT_LESS_THAN_UINT32(sizeof(out), strlen(out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dtext_size_kb_and_mb);
    RUN_TEST(test_dtext_size_edges);
    RUN_TEST(test_dtext_size_limits);
    RUN_TEST(test_dtext_null_and_empty_buffers);
    RUN_TEST(test_dtext_meta_longest_fits);
    RUN_TEST(test_dtext_date_day_first);
    RUN_TEST(test_dtext_date_unknown_is_empty);
    RUN_TEST(test_dtext_meta_parts);
    RUN_TEST(test_dtext_meta_fits_a_small_buffer);
    return UNITY_END();
}
