/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_welcome.c
 * @brief The landing after Welcome: the mark and when it is due.
 */
#include <stdbool.h>

#include "unity.h"
#include "kgui_welcome.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/* ---- The mark ---- */

void test_only_pending_is_pending(void)
{
    kgui_welcome_t w;

    kgui_welcome_load(&w, "pending");
    TEST_ASSERT_TRUE(kgui_welcome_pending(&w));
    kgui_welcome_load(&w, "done");
    TEST_ASSERT_FALSE(kgui_welcome_pending(&w));
    kgui_welcome_load(&w, "started");
    TEST_ASSERT_FALSE(kgui_welcome_pending(&w));
    kgui_welcome_load(&w, NULL);
    TEST_ASSERT_FALSE(kgui_welcome_pending(&w));
}

void test_the_console_words(void)
{
    TEST_ASSERT_TRUE(kgui_welcome_word_ok("pending"));
    TEST_ASSERT_TRUE(kgui_welcome_word_ok("done"));
    TEST_ASSERT_FALSE(kgui_welcome_word_ok("started"));
    TEST_ASSERT_FALSE(kgui_welcome_word_ok(""));
    TEST_ASSERT_FALSE(kgui_welcome_word_ok(NULL));
}

/* ---- When it is due ---- */

void test_a_normal_boot_is_never_due(void)
{
    kgui_welcome_t w;

    kgui_welcome_load(&w, NULL);
    TEST_ASSERT_FALSE(kgui_welcome_due(&w, true, true, 0U));
    TEST_ASSERT_FALSE(kgui_welcome_due(&w, true, true, 100000U));
}

void test_due_counts_from_kody_waking(void)
{
    kgui_welcome_t w;

    kgui_welcome_load(&w, "pending");
    /* The face is up long before he wakes: that time does not count. */
    TEST_ASSERT_FALSE(kgui_welcome_due(&w, false, true, 5000U));
    TEST_ASSERT_FALSE(kgui_welcome_due(&w, true, true, 6000U));
    TEST_ASSERT_FALSE(kgui_welcome_due(&w, true, true, 6000U + KGUI_WELCOME_GREET_MS - 1U));
    TEST_ASSERT_TRUE(kgui_welcome_due(&w, true, true, 6000U + KGUI_WELCOME_GREET_MS));
}

void test_a_card_over_the_face_makes_it_wait(void)
{
    kgui_welcome_t w;

    kgui_welcome_load(&w, "pending");
    TEST_ASSERT_FALSE(kgui_welcome_due(&w, true, false, 0U));
    TEST_ASSERT_FALSE(kgui_welcome_due(&w, true, false, 10000U));
    TEST_ASSERT_TRUE(kgui_welcome_due(&w, true, true, 10001U));
}

void test_due_once(void)
{
    kgui_welcome_t w;

    kgui_welcome_load(&w, "pending");
    TEST_ASSERT_FALSE(kgui_welcome_due(&w, true, true, 0U));
    TEST_ASSERT_TRUE(kgui_welcome_due(&w, true, true, KGUI_WELCOME_GREET_MS));
    TEST_ASSERT_FALSE(kgui_welcome_pending(&w));
    TEST_ASSERT_FALSE(kgui_welcome_due(&w, true, true, KGUI_WELCOME_GREET_MS + 1U));
}

void test_the_clock_wraps(void)
{
    kgui_welcome_t w;
    uint32_t const t0 = 0xFFFFFF00U;

    kgui_welcome_load(&w, "pending");
    TEST_ASSERT_FALSE(kgui_welcome_due(&w, true, true, t0));
    TEST_ASSERT_TRUE(kgui_welcome_due(&w, true, true, t0 + KGUI_WELCOME_GREET_MS));
}

/* ---- While the greeting has the face ---- */

void test_the_greeting_holds_the_face_until_its_line_is_through(void)
{
    kgui_welcome_t w;

    kgui_welcome_load(&w, "pending");
    /* Owed: it holds whatever the face shows. */
    TEST_ASSERT_TRUE(kgui_welcome_greeting(&w, false));
    TEST_ASSERT_FALSE(kgui_welcome_due(&w, true, true, 0U));
    TEST_ASSERT_TRUE(kgui_welcome_due(&w, true, true, KGUI_WELCOME_GREET_MS));
    /* Said, on the very tick it is: its line waits for Kody, then shows. */
    TEST_ASSERT_TRUE(kgui_welcome_greeting(&w, true));
    kgui_welcome_notice(&w, true);
    TEST_ASSERT_TRUE(kgui_welcome_greeting(&w, true));
    /* Through: the face has no line left. */
    TEST_ASSERT_FALSE(kgui_welcome_greeting(&w, false));
    kgui_welcome_notice(&w, false);
    /* A later line on the face is somebody else's. */
    TEST_ASSERT_FALSE(kgui_welcome_greeting(&w, true));
}

void test_no_greeting_holds_nothing(void)
{
    kgui_welcome_t w;

    kgui_welcome_load(&w, NULL);
    TEST_ASSERT_FALSE(kgui_welcome_greeting(&w, false));
    TEST_ASSERT_FALSE(kgui_welcome_greeting(&w, true));
    /* A Welcome that died on its way out is not greeted. */
    kgui_welcome_load(&w, "pending");
    kgui_welcome_drop(&w);
    TEST_ASSERT_FALSE(kgui_welcome_greeting(&w, true));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_only_pending_is_pending);
    RUN_TEST(test_the_console_words);
    RUN_TEST(test_a_normal_boot_is_never_due);
    RUN_TEST(test_due_counts_from_kody_waking);
    RUN_TEST(test_a_card_over_the_face_makes_it_wait);
    RUN_TEST(test_due_once);
    RUN_TEST(test_the_clock_wraps);
    RUN_TEST(test_the_greeting_holds_the_face_until_its_line_is_through);
    RUN_TEST(test_no_greeting_holds_nothing);
    return UNITY_END();
}
