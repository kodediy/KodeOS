/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_c5up.c
 * @brief Host tests of the radio's own update (kgui_c5up.h).
 */
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "kgui_c5up.h"

/** The answer the service's document gives as its example. */
#define ANSWER_013 \
    "{\"version\":\"0.1.3\"," \
    "\"url\":\"https://cdn.kode.diy/c5/0.1.3-1a2b3c4d.bin\"," \
    "\"size\":1445888," \
    "\"sha256\":\"1a2b3c4d\"," \
    "\"publishedAt\":\"2026-11-20T10:00:00.000Z\"}"

/** The same release on a bench's plain link. */
#define ANSWER_PLAIN \
    "{\"version\":\"0.1.3\",\"url\":\"http://192.168.1.20:8000/kode_c5.bin\"}"

#define MIN      "0.1.1"  /**< The minimum the tests read against. */
#define HTTP_OK  200U
#define HTTP_404 404U
#define HTTP_500 500U

void setUp(void)
{
}

void tearDown(void)
{
}

/** @brief A 200 that came whole, carrying @p json. */
static kgui_c5up_reply_t reply_ok(const char *json)
{
    kgui_c5up_reply_t r;

    r.reached = true;
    r.status  = HTTP_OK;
    r.whole   = true;
    r.body    = json;
    r.len     = strlen(json);
    return r;
}

/** @brief A reply with no body and @p status. */
static kgui_c5up_reply_t reply_status(uint16_t status)
{
    kgui_c5up_reply_t r;

    r.reached = true;
    r.status  = status;
    r.whole   = true;
    r.body    = NULL;
    r.len     = 0U;
    return r;
}

/** @brief The first question of a boot, against MIN, with nothing tried. */
static kgui_c5up_rule_t rule_first(void)
{
    kgui_c5up_rule_t rule;

    rule.min      = MIN;
    rule.tried    = "";
    rule.plain_ok = false;
    rule.asks     = 1U;
    return rule;
}

/** @brief A free glass on the face. */
static kgui_osrel_glass_t glass_free(void)
{
    kgui_osrel_glass_t g;

    (void)memset(&g, 0, sizeof(g));
    g.screen = KGUI_OSREL_SCREEN_FACE;
    return g;
}

void test_judged_owes_the_question_once_the_c5_is_too_old(void)
{
    TEST_ASSERT_EQUAL(KGUI_C5UP_OWED, kgui_c5up_judged(KGUI_C5UP_IDLE, true));
    TEST_ASSERT_EQUAL(KGUI_C5UP_IDLE, kgui_c5up_judged(KGUI_C5UP_IDLE, false));
}

void test_judged_leaves_every_later_stage_alone(void)
{
    static const kgui_c5up_stage_t LATER[] = {
        KGUI_C5UP_OWED, KGUI_C5UP_ASKING, KGUI_C5UP_AGAIN, KGUI_C5UP_DUE,
        KGUI_C5UP_STARTED, KGUI_C5UP_NONE,
    };

    for ( size_t i = 0U; (sizeof(LATER) / sizeof(LATER[0])) > i; ++i )
    {
        /* The update's own link loss reads as unknown: nothing moves. */
        TEST_ASSERT_EQUAL(LATER[i], kgui_c5up_judged(LATER[i], false));
        TEST_ASSERT_EQUAL(LATER[i], kgui_c5up_judged(LATER[i], true));
    }
}

void test_may_ask_only_when_owed_linked_and_alone(void)
{
    TEST_ASSERT_TRUE(kgui_c5up_may_ask(KGUI_C5UP_OWED, true, false));
    TEST_ASSERT_FALSE(kgui_c5up_may_ask(KGUI_C5UP_OWED, false, false));
    TEST_ASSERT_FALSE(kgui_c5up_may_ask(KGUI_C5UP_OWED, true, true));
    TEST_ASSERT_FALSE(kgui_c5up_may_ask(KGUI_C5UP_IDLE, true, false));
    TEST_ASSERT_FALSE(kgui_c5up_may_ask(KGUI_C5UP_ASKING, true, false));
    TEST_ASSERT_FALSE(kgui_c5up_may_ask(KGUI_C5UP_AGAIN, true, false));
    TEST_ASSERT_FALSE(kgui_c5up_may_ask(KGUI_C5UP_DUE, true, false));
    TEST_ASSERT_FALSE(kgui_c5up_may_ask(KGUI_C5UP_NONE, true, false));
}

void test_answer_a_release_that_meets_the_minimum_is_due(void)
{
    kgui_c5up_reply_t const r    = reply_ok(ANSWER_013);
    kgui_c5up_rule_t const  rule = rule_first();
    kgui_osrel_t            rel;
    kgui_c5up_why_t         why  = KGUI_C5UP_WHY_UNREAD;

    (void)memset(&rel, 0, sizeof(rel));
    TEST_ASSERT_EQUAL(KGUI_C5UP_DUE, kgui_c5up_answer(&r, &rule, &rel, &why));
    TEST_ASSERT_EQUAL(KGUI_C5UP_WHY_RELEASE, why);
    TEST_ASSERT_EQUAL_STRING("0.1.3", rel.version);
    TEST_ASSERT_EQUAL_STRING("https://cdn.kode.diy/c5/0.1.3-1a2b3c4d.bin", rel.url);
}

void test_answer_the_minimum_itself_is_due(void)
{
    kgui_c5up_reply_t const r    = reply_ok(ANSWER_013);
    kgui_c5up_rule_t        rule = rule_first();
    kgui_osrel_t            rel;

    rule.min = "0.1.3";
    TEST_ASSERT_EQUAL(KGUI_C5UP_DUE, kgui_c5up_answer(&r, &rule, &rel, NULL));
}

void test_answer_a_release_older_than_the_minimum_is_not_installed(void)
{
    kgui_c5up_reply_t const r    = reply_ok(ANSWER_013);
    kgui_c5up_rule_t        rule = rule_first();
    kgui_osrel_t            rel;
    kgui_c5up_why_t         why  = KGUI_C5UP_WHY_RELEASE;

    (void)memset(&rel, 0, sizeof(rel));
    rule.min = "0.1.10";   /* as numbers: 0.1.10 is after 0.1.3 */
    TEST_ASSERT_EQUAL(KGUI_C5UP_NONE, kgui_c5up_answer(&r, &rule, &rel, &why));
    TEST_ASSERT_EQUAL(KGUI_C5UP_WHY_TOO_OLD, why);
    TEST_ASSERT_EQUAL_STRING("", rel.version);
}

void test_answer_the_release_taken_already_is_not_installed_again(void)
{
    kgui_c5up_reply_t const r    = reply_ok(ANSWER_013);
    kgui_c5up_rule_t        rule = rule_first();
    kgui_osrel_t            rel;
    kgui_c5up_why_t         why  = KGUI_C5UP_WHY_RELEASE;

    rule.tried = "0.1.3";
    TEST_ASSERT_EQUAL(KGUI_C5UP_NONE, kgui_c5up_answer(&r, &rule, &rel, &why));
    TEST_ASSERT_EQUAL(KGUI_C5UP_WHY_TRIED, why);
    /* Another release than the one taken is a new try. */
    rule.tried = "0.1.2";
    TEST_ASSERT_EQUAL(KGUI_C5UP_DUE, kgui_c5up_answer(&r, &rule, &rel, &why));
    rule.tried = NULL;
    TEST_ASSERT_EQUAL(KGUI_C5UP_DUE, kgui_c5up_answer(&r, &rule, &rel, &why));
}

void test_answer_404_is_no_release(void)
{
    kgui_c5up_reply_t const r    = reply_status(HTTP_404);
    kgui_c5up_rule_t const  rule = rule_first();
    kgui_c5up_why_t         why  = KGUI_C5UP_WHY_RELEASE;

    TEST_ASSERT_EQUAL(KGUI_C5UP_NONE, kgui_c5up_answer(&r, &rule, NULL, &why));
    TEST_ASSERT_EQUAL(KGUI_C5UP_WHY_NO_RELEASE, why);
}

void test_answer_that_does_not_read_installs_nothing(void)
{
    kgui_c5up_rule_t const rule = rule_first();
    kgui_c5up_reply_t      r    = reply_status(HTTP_500);
    kgui_c5up_why_t        why  = KGUI_C5UP_WHY_RELEASE;

    TEST_ASSERT_EQUAL(KGUI_C5UP_NONE, kgui_c5up_answer(&r, &rule, NULL, &why));
    TEST_ASSERT_EQUAL(KGUI_C5UP_WHY_UNREAD, why);
    r = reply_ok(ANSWER_013);
    r.whole = false;
    TEST_ASSERT_EQUAL(KGUI_C5UP_NONE, kgui_c5up_answer(&r, &rule, NULL, &why));
    TEST_ASSERT_EQUAL(KGUI_C5UP_WHY_UNREAD, why);
    r = reply_ok("{\"version\":\"v0.1.3\",\"url\":\"https://cdn.kode.diy/c5/x.bin\"}");
    TEST_ASSERT_EQUAL(KGUI_C5UP_NONE, kgui_c5up_answer(&r, &rule, NULL, &why));
    TEST_ASSERT_EQUAL(KGUI_C5UP_WHY_UNREAD, why);
    r = reply_status(HTTP_OK);   /* a 200 with no body */
    TEST_ASSERT_EQUAL(KGUI_C5UP_NONE, kgui_c5up_answer(&r, &rule, NULL, &why));
    TEST_ASSERT_EQUAL(KGUI_C5UP_WHY_UNREAD, why);
    r = reply_ok(ANSWER_013);
    TEST_ASSERT_EQUAL(KGUI_C5UP_NONE, kgui_c5up_answer(&r, NULL, NULL, &why));
    TEST_ASSERT_EQUAL(KGUI_C5UP_WHY_UNREAD, why);
}

void test_answer_a_plain_link_only_from_a_plain_service(void)
{
    kgui_c5up_reply_t const r    = reply_ok(ANSWER_PLAIN);
    kgui_c5up_rule_t        rule = rule_first();
    kgui_osrel_t            rel;

    TEST_ASSERT_EQUAL(KGUI_C5UP_NONE, kgui_c5up_answer(&r, &rule, &rel, NULL));
    rule.plain_ok = true;
    TEST_ASSERT_EQUAL(KGUI_C5UP_DUE, kgui_c5up_answer(&r, &rule, &rel, NULL));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.20:8000/kode_c5.bin", rel.url);
}

void test_answer_unreached_is_asked_again_a_bounded_number_of_times(void)
{
    kgui_c5up_reply_t r    = reply_status(0U);
    kgui_c5up_rule_t  rule = rule_first();
    kgui_c5up_why_t   why  = KGUI_C5UP_WHY_RELEASE;

    r.reached = false;
    for ( uint8_t asks = 1U; KGUI_OSREL_BOOT_ASKS > asks; ++asks )
    {
        rule.asks = asks;
        TEST_ASSERT_EQUAL(KGUI_C5UP_AGAIN, kgui_c5up_answer(&r, &rule, NULL, &why));
        TEST_ASSERT_EQUAL(KGUI_C5UP_WHY_UNREACHED, why);
    }
    rule.asks = KGUI_OSREL_BOOT_ASKS;
    TEST_ASSERT_EQUAL(KGUI_C5UP_NONE, kgui_c5up_answer(&r, &rule, NULL, &why));
    TEST_ASSERT_EQUAL(KGUI_C5UP_WHY_GAVE_UP, why);
    rule.asks = 1U;
    TEST_ASSERT_EQUAL(KGUI_C5UP_AGAIN, kgui_c5up_answer(NULL, &rule, NULL, &why));
}

void test_may_start_needs_a_due_release_the_power_and_a_free_glass(void)
{
    kgui_osrel_glass_t g = glass_free();

    TEST_ASSERT_TRUE(kgui_c5up_may_start(KGUI_C5UP_DUE, true, &g));
    TEST_ASSERT_FALSE(kgui_c5up_may_start(KGUI_C5UP_DUE, false, &g));
    TEST_ASSERT_FALSE(kgui_c5up_may_start(KGUI_C5UP_STARTED, true, &g));
    TEST_ASSERT_FALSE(kgui_c5up_may_start(KGUI_C5UP_OWED, true, &g));
    TEST_ASSERT_FALSE(kgui_c5up_may_start(KGUI_C5UP_DUE, true, NULL));
    g.hand = true;
    TEST_ASSERT_FALSE(kgui_c5up_may_start(KGUI_C5UP_DUE, true, &g));
    g = glass_free();
    g.writing = true;
    TEST_ASSERT_FALSE(kgui_c5up_may_start(KGUI_C5UP_DUE, true, &g));
    g = glass_free();
    g.rest = true;
    TEST_ASSERT_FALSE(kgui_c5up_may_start(KGUI_C5UP_DUE, true, &g));
}

void test_unanswered_start_is_due_again_a_bounded_number_of_times(void)
{
    for ( uint8_t starts = 1U; KGUI_C5UP_STARTS > starts; ++starts )
    {
        TEST_ASSERT_EQUAL(KGUI_C5UP_DUE, kgui_c5up_unanswered(starts));
    }
    TEST_ASSERT_EQUAL(KGUI_C5UP_NONE, kgui_c5up_unanswered(KGUI_C5UP_STARTS));
}

void test_holds_os_from_owed_to_started(void)
{
    TEST_ASSERT_FALSE(kgui_c5up_holds_os(KGUI_C5UP_IDLE));
    TEST_ASSERT_TRUE(kgui_c5up_holds_os(KGUI_C5UP_OWED));
    TEST_ASSERT_TRUE(kgui_c5up_holds_os(KGUI_C5UP_ASKING));
    TEST_ASSERT_TRUE(kgui_c5up_holds_os(KGUI_C5UP_AGAIN));
    TEST_ASSERT_TRUE(kgui_c5up_holds_os(KGUI_C5UP_DUE));
    TEST_ASSERT_TRUE(kgui_c5up_holds_os(KGUI_C5UP_STARTED));
    TEST_ASSERT_FALSE(kgui_c5up_holds_os(KGUI_C5UP_NONE));
}

void test_words(void)
{
    TEST_ASSERT_EQUAL_STRING("owed", kgui_c5up_stage_word(KGUI_C5UP_OWED));
    TEST_ASSERT_EQUAL_STRING("none", kgui_c5up_stage_word(KGUI_C5UP_NONE));
    TEST_ASSERT_EQUAL_STRING("?", kgui_c5up_stage_word((kgui_c5up_stage_t)99));
    TEST_ASSERT_EQUAL_STRING("no release for this kodeOS",
                             kgui_c5up_why_words(KGUI_C5UP_WHY_NO_RELEASE));
    TEST_ASSERT_EQUAL_STRING("?", kgui_c5up_why_words((kgui_c5up_why_t)99));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_judged_owes_the_question_once_the_c5_is_too_old);
    RUN_TEST(test_judged_leaves_every_later_stage_alone);
    RUN_TEST(test_may_ask_only_when_owed_linked_and_alone);
    RUN_TEST(test_answer_a_release_that_meets_the_minimum_is_due);
    RUN_TEST(test_answer_the_minimum_itself_is_due);
    RUN_TEST(test_answer_a_release_older_than_the_minimum_is_not_installed);
    RUN_TEST(test_answer_the_release_taken_already_is_not_installed_again);
    RUN_TEST(test_answer_404_is_no_release);
    RUN_TEST(test_answer_that_does_not_read_installs_nothing);
    RUN_TEST(test_answer_a_plain_link_only_from_a_plain_service);
    RUN_TEST(test_answer_unreached_is_asked_again_a_bounded_number_of_times);
    RUN_TEST(test_may_start_needs_a_due_release_the_power_and_a_free_glass);
    RUN_TEST(test_unanswered_start_is_due_again_a_bounded_number_of_times);
    RUN_TEST(test_holds_os_from_owed_to_started);
    RUN_TEST(test_words);
    return UNITY_END();
}
