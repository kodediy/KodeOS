/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_dtext.c
 * @brief Host tests of the words of an app's details (kgui_dtext.h).
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "kode_defs.h"

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
    kgui_dtext_meta("games", "1.0", "1 KB", NULL, 0U);
    kgui_dtext_size(1U * MB, out, 0U);
    TEST_ASSERT_EQUAL_CHAR('x', out[0]);
    kgui_dtext_meta("games", "1.0", "1 KB", out, 0U);
    TEST_ASSERT_EQUAL_CHAR('x', out[0]);
}

void test_dtext_meta_longest_fits(void)
{
    char out[KGUI_DTEXT_META_MAX];

    kgui_dtext_meta("abcdefghijklmno", "123456789012345", "4096.0 MB", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Abcdefghijklmno \xC2\xB7 123456789012345 \xC2\xB7 4096.0 MB", out);
    TEST_ASSERT_LESS_THAN_UINT32(sizeof(out), strlen(out));
}

void test_dtext_meta_parts(void)
{
    char out[KGUI_DTEXT_META_MAX];

    kgui_dtext_meta("tools", "0.0.10", "2.8 MB", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Tools \xC2\xB7 0.0.10 \xC2\xB7 2.8 MB", out);
    kgui_dtext_meta("games", "v1.2.0", "812 KB", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Games \xC2\xB7 1.2.0 \xC2\xB7 812 KB", out);
    kgui_dtext_meta("", "1.0", "", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("1.0", out);
    kgui_dtext_meta("games", NULL, "3 KB", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Games \xC2\xB7 3 KB", out);
    kgui_dtext_meta(NULL, "", NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_dtext_meta_version_has_no_leading_zeros(void)
{
    char out[KGUI_DTEXT_META_MAX];

    kgui_dtext_meta(NULL, "00.07.05", NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("0.7.5", out);
    kgui_dtext_meta(NULL, "V10.00.100", NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("10.0.100", out);
    kgui_dtext_meta(NULL, "0.1.0", NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("0.1.0", out);
}

/* The longest store category does not fit the line with a version and a size. */
void test_dtext_meta_says_communication_short(void)
{
    char out[KGUI_DTEXT_META_MAX];

    kgui_dtext_meta("Communication", "0.7.5", "1.3 MB", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Comms \xC2\xB7 0.7.5 \xC2\xB7 1.3 MB", out);
    kgui_dtext_meta("communication", NULL, NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Comms", out);
    kgui_dtext_meta("Communications", "1.0", NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Communications \xC2\xB7 1.0", out);
}

void test_dtext_meta_fits_a_small_buffer(void)
{
    char out[8];

    kgui_dtext_meta("games", "1.2.3.4.5.6", "1 KB", out, sizeof(out));
    TEST_ASSERT_LESS_THAN_UINT32(sizeof(out), strlen(out));
}

/* A whole erase is said by the notice that names the app, not by a hint. */
void test_dtext_remove_end_of_a_whole_erase_has_no_hint(void)
{
    TEST_ASSERT_NULL(kgui_dtext_remove_end((uint8_t)KODE_OK));
}

void test_dtext_remove_end_says_what_went_wrong(void)
{
    TEST_ASSERT_EQUAL_STRING("Some files could not be removed", kgui_dtext_remove_end((uint8_t)KODE_IO_ERROR));
    TEST_ASSERT_EQUAL_STRING("Busy. Try again", kgui_dtext_remove_end((uint8_t)KODE_DENIED));
    TEST_ASSERT_EQUAL_STRING("Nothing to remove", kgui_dtext_remove_end((uint8_t)KODE_ERROR));
    TEST_ASSERT_EQUAL_STRING("No answer. Try again", kgui_dtext_remove_end((uint8_t)KODE_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING("No answer. Try again", kgui_dtext_remove_end((uint8_t)KODE_NO_RESOURCES));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dtext_size_kb_and_mb);
    RUN_TEST(test_dtext_size_edges);
    RUN_TEST(test_dtext_size_limits);
    RUN_TEST(test_dtext_null_and_empty_buffers);
    RUN_TEST(test_dtext_meta_longest_fits);
    RUN_TEST(test_dtext_meta_parts);
    RUN_TEST(test_dtext_meta_version_has_no_leading_zeros);
    RUN_TEST(test_dtext_meta_says_communication_short);
    RUN_TEST(test_dtext_meta_fits_a_small_buffer);
    RUN_TEST(test_dtext_remove_end_of_a_whole_erase_has_no_hint);
    RUN_TEST(test_dtext_remove_end_says_what_went_wrong);
    return UNITY_END();
}
