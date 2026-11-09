/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_welcome.c
 * @brief The landing after Welcome: the mark, when it is due, and the ring's path.
 */
#include <math.h>
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

/* ---- The ring's path: the app's and kshell's own, 10 px flush with the glass ---- */

static void assert_at(float d, float x, float y)
{
    float px = -1.0F;
    float py = -1.0F;

    kgui_welcome_path_at(d, &px, &py);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, x, px);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, y, py);
}

void test_the_path_walks_the_glass_clockwise_from_the_top_middle(void)
{
    float const q = (3.14159265F * 109.0F) / 2.0F;

    assert_at(0.0F, 205.0F, 5.0F);
    assert_at(91.0F, 296.0F, 5.0F);                       /* the top ends */
    assert_at(91.0F + q, 405.0F, 114.0F);                 /* the right side starts */
    assert_at(91.0F + q + 274.0F, 405.0F, 388.0F);
    assert_at(91.0F + (2.0F * q) + 274.0F, 296.0F, 497.0F); /* the bottom starts */
    assert_at(273.0F + (2.0F * q) + 274.0F, 114.0F, 497.0F);
    assert_at(273.0F + (3.0F * q) + 274.0F, 5.0F, 388.0F);  /* the left side starts */
    assert_at(273.0F + (3.0F * q) + 548.0F, 5.0F, 114.0F);
    assert_at(273.0F + (4.0F * q) + 548.0F, 114.0F, 5.0F);  /* the last of the top */
}

void test_the_path_closes_where_it_began(void)
{
    float const len = kgui_welcome_path_len();

    TEST_ASSERT_FLOAT_WITHIN(0.01F, 912.0F + (2.0F * 3.14159265F * 109.0F), len);
    assert_at(len, 205.0F, 5.0F);
    assert_at(len + 100.0F, 205.0F, 5.0F);
}

void test_a_corner_stays_on_its_circle(void)
{
    float const q = (3.14159265F * 109.0F) / 2.0F;
    float x = 0.0F;
    float y = 0.0F;

    kgui_welcome_path_at(91.0F + (q / 2.0F), &x, &y);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 109.0F, hypotf(x - 296.0F, y - 114.0F));
    TEST_ASSERT_TRUE(296.0F < x);
    TEST_ASSERT_TRUE(114.0F > y);
}

void test_the_fill_runs_the_last_eighth(void)
{
    float const len = kgui_welcome_path_len();

    TEST_ASSERT_FLOAT_WITHIN(0.01F, len * 0.875F, kgui_welcome_fill_len(0));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, len * 0.9375F, kgui_welcome_fill_len(500));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, len, kgui_welcome_fill_len(1000));
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
    RUN_TEST(test_the_path_walks_the_glass_clockwise_from_the_top_middle);
    RUN_TEST(test_the_path_closes_where_it_began);
    RUN_TEST(test_a_corner_stays_on_its_circle);
    RUN_TEST(test_the_fill_runs_the_last_eighth);
    return UNITY_END();
}
