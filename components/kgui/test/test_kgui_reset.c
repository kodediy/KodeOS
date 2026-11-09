/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_reset.c
 * @brief Host tests of the line the factory reset's card shows when the reset
 *        did not happen (kgui_syscard.h).
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "kaos.h"
#include "kode_defs.h"
#include "kgui_syscard.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_reset_why_no_welcome_app(void)
{
    TEST_ASSERT_EQUAL_STRING("The welcome app is not on this Kode",
                             kgui_reset_why((uint8_t)KBOOT_RESET_STEP_FIND, (uint32_t)KODE_DENIED));
}

void test_reset_why_busy(void)
{
    TEST_ASSERT_EQUAL_STRING("Busy: try again in a moment",
                             kgui_reset_why((uint8_t)KBOOT_RESET_STEP_FIND, (uint32_t)KODE_NO_RESOURCES));
}

void test_reset_why_says_what_is_gone_and_what_stays(void)
{
    TEST_ASSERT_EQUAL_STRING(KGUI_RESET_NO_RADIO,
                             kgui_reset_why((uint8_t)KBOOT_RESET_STEP_LINK, (uint32_t)KODE_ERROR));
    TEST_ASSERT_EQUAL_STRING(KGUI_RESET_NO_INSTALL,
                             kgui_reset_why((uint8_t)KBOOT_RESET_STEP_INSTALL, (uint32_t)KODE_IO_ERROR));
    TEST_ASSERT_EQUAL_STRING(KGUI_RESET_RADIO_QUIT,
                             kgui_reset_why((uint8_t)KBOOT_RESET_STEP_WIPE, (uint32_t)KODE_ERROR));
    TEST_ASSERT_EQUAL_STRING(KGUI_RESET_APPS_STAY,
                             kgui_reset_why((uint8_t)KBOOT_RESET_STEP_APPS, (uint32_t)KODE_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING(KGUI_RESET_SOME_STAY,
                             kgui_reset_why((uint8_t)KBOOT_RESET_STEP_NVS, (uint32_t)KODE_NO_RESOURCES));
    TEST_ASSERT_EQUAL_STRING(KGUI_RESET_ALL_GONE,
                             kgui_reset_why((uint8_t)KBOOT_RESET_STEP_REQUEST, (uint32_t)KODE_ERROR));
}

void test_reset_why_find_failing_otherwise_says_nothing_went(void)
{
    /* Only the two refusals have words of their own. */
    TEST_ASSERT_EQUAL_STRING(KGUI_RESET_NOTHING,
                             kgui_reset_why((uint8_t)KBOOT_RESET_STEP_FIND, (uint32_t)KODE_TIMEOUT));
}

void test_reset_why_never_names_a_step_of_the_firmware(void)
{
    static const uint8_t steps[] = {
        (uint8_t)KBOOT_RESET_STEP_FIND, (uint8_t)KBOOT_RESET_STEP_LINK,
        (uint8_t)KBOOT_RESET_STEP_INSTALL, (uint8_t)KBOOT_RESET_STEP_WIPE,
        (uint8_t)KBOOT_RESET_STEP_APPS, (uint8_t)KBOOT_RESET_STEP_NVS,
        (uint8_t)KBOOT_RESET_STEP_REQUEST,
    };

    for ( size_t i = 0U; (sizeof(steps) / sizeof(steps[0])) > i; ++i )
    {
        TEST_ASSERT_NULL(strstr(kgui_reset_why(steps[i], (uint32_t)KODE_ERROR),
                                KGUI_RESET_STOPPED_AT));
    }
}

void test_reset_sub_is_not_done_until_something_went(void)
{
    TEST_ASSERT_EQUAL_STRING(KGUI_RESET_REFUSED_SUB, kgui_reset_sub((uint8_t)KBOOT_RESET_STEP_FIND));
    TEST_ASSERT_EQUAL_STRING(KGUI_RESET_REFUSED_SUB, kgui_reset_sub((uint8_t)KBOOT_RESET_STEP_LINK));
    TEST_ASSERT_EQUAL_STRING(KGUI_RESET_REFUSED_SUB, kgui_reset_sub((uint8_t)KBOOT_RESET_STEP_INSTALL));
    TEST_ASSERT_EQUAL_STRING(KGUI_RESET_PARTLY_SUB, kgui_reset_sub((uint8_t)KBOOT_RESET_STEP_WIPE));
    TEST_ASSERT_EQUAL_STRING(KGUI_RESET_PARTLY_SUB, kgui_reset_sub((uint8_t)KBOOT_RESET_STEP_APPS));
    TEST_ASSERT_EQUAL_STRING(KGUI_RESET_PARTLY_SUB, kgui_reset_sub((uint8_t)KBOOT_RESET_STEP_NVS));
    TEST_ASSERT_EQUAL_STRING(KGUI_RESET_PARTLY_SUB, kgui_reset_sub((uint8_t)KBOOT_RESET_STEP_REQUEST));
}

void test_reset_why_a_reset_that_worked_says_nothing(void)
{
    TEST_ASSERT_EQUAL_STRING("", kgui_reset_why((uint8_t)KBOOT_RESET_STEP_DONE, (uint32_t)KODE_OK));
}

void test_reset_why_a_step_out_of_range(void)
{
    TEST_ASSERT_EQUAL_STRING("Stopped at ?", kgui_reset_why(UINT8_MAX, (uint32_t)KODE_ERROR));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reset_why_no_welcome_app);
    RUN_TEST(test_reset_why_busy);
    RUN_TEST(test_reset_why_says_what_is_gone_and_what_stays);
    RUN_TEST(test_reset_why_find_failing_otherwise_says_nothing_went);
    RUN_TEST(test_reset_why_never_names_a_step_of_the_firmware);
    RUN_TEST(test_reset_sub_is_not_done_until_something_went);
    RUN_TEST(test_reset_why_a_reset_that_worked_says_nothing);
    RUN_TEST(test_reset_why_a_step_out_of_range);
    return UNITY_END();
}
