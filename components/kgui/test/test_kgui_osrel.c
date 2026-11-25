/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_osrel.c
 * @brief Host tests of the update service's answer, About's row and the card
 *        at boot (kgui_osrel.h).
 */
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "kgui_osrel.h"

/** The service's answer as it stood on 2026-11-10. */
#define ANSWER_LIVE \
    "{\"version\":\"00.00.24\"," \
    "\"url\":\"https://cdn.kode.diy/kodeos/00.00.24-03a0331d.kfw\"," \
    "\"size\":1675238," \
    "\"sha256\":\"03a0331d32b75b1d6d76d9beda89a3003e5433f113380f9e499f52e9a289e0a9\"," \
    "\"publishedAt\":\"2026-11-04T11:58:25.326Z\"}"

/** The boot's first question. */
#define FIRST 1U

void setUp(void)
{
}

void tearDown(void)
{
}

/** @brief Parse a literal, terminator left out. */
static bool parse(const char *json, bool plain_ok, kgui_osrel_t *out)
{
    return kgui_osrel_parse(json, strlen(json), plain_ok, out);
}

void test_parse_the_live_answer(void)
{
    kgui_osrel_t rel;

    (void)memset(&rel, 0, sizeof(rel));
    TEST_ASSERT_TRUE(parse(ANSWER_LIVE, false, &rel));
    TEST_ASSERT_EQUAL_STRING("00.00.24", rel.version);
    TEST_ASSERT_EQUAL_STRING("https://cdn.kode.diy/kodeos/00.00.24-03a0331d.kfw", rel.url);
}

void test_parse_any_order_spaces_and_new_fields(void)
{
    kgui_osrel_t rel;

    (void)memset(&rel, 0, sizeof(rel));
    TEST_ASSERT_TRUE(parse("{ \"notes\" : [\"a\", {\"b\": 1}],\n"
                           "  \"url\" : \"https://cdn.kode.diy/kodeos/0.1.2-a1b2c3d4.kfw\" ,\n"
                           "  \"min\" : {\"version\": \"9.9.9\"},\n"
                           "  \"version\" : \"0.1.2\" }",
                           false, &rel));
    TEST_ASSERT_EQUAL_STRING("0.1.2", rel.version);
    TEST_ASSERT_EQUAL_STRING("https://cdn.kode.diy/kodeos/0.1.2-a1b2c3d4.kfw", rel.url);
}

void test_parse_takes_the_top_level_alone(void)
{
    kgui_osrel_t rel;

    /* Both fields exist, one level down: that is not this answer. */
    TEST_ASSERT_FALSE(parse("{\"latest\":{\"version\":\"0.1.2\","
                            "\"url\":\"https://cdn.kode.diy/kodeos/x.kfw\"}}",
                            false, &rel));
    /* A value that reads like a key is a value. */
    TEST_ASSERT_TRUE(parse("{\"note\":\"version\",\"version\":\"1.2.3\","
                           "\"url\":\"https://cdn.kode.diy/kodeos/x.kfw\"}",
                           false, &rel));
    TEST_ASSERT_EQUAL_STRING("1.2.3", rel.version);
}

void test_parse_refuses_a_missing_field(void)
{
    kgui_osrel_t rel;

    TEST_ASSERT_FALSE(parse("{\"version\":\"0.1.2\"}", false, &rel));
    TEST_ASSERT_FALSE(parse("{\"url\":\"https://cdn.kode.diy/kodeos/x.kfw\"}", false, &rel));
    TEST_ASSERT_FALSE(parse("{\"error\":\"no_release\"}", false, &rel));
    TEST_ASSERT_FALSE(parse("{}", false, &rel));
    TEST_ASSERT_FALSE(parse("", false, &rel));
    TEST_ASSERT_FALSE(parse("404 Not Found", false, &rel));
    TEST_ASSERT_FALSE(kgui_osrel_parse(NULL, 4U, false, &rel));
    TEST_ASSERT_FALSE(kgui_osrel_parse(ANSWER_LIVE, strlen(ANSWER_LIVE), false, NULL));
    /* A number is not a version, and a version is a string. */
    TEST_ASSERT_FALSE(parse("{\"version\":24,\"url\":\"https://cdn.kode.diy/x.kfw\"}",
                            false, &rel));
}

void test_parse_refuses_a_version_that_is_not_three_groups(void)
{
    static const char *const BAD[] = {
        "0.1", "0.1.2.3", "v0.1.2", "0.1.2-rc1", "0..2", ".1.2", "0.1.", "0.1.a",
        "00000.1.2", "", " 0.1.2", "0.1.2 ",
    };
    kgui_osrel_t rel;
    char         json[160];

    for ( size_t i = 0U; (sizeof(BAD) / sizeof(BAD[0])) > i; ++i )
    {
        (void)snprintf(json, sizeof(json),
                       "{\"version\":\"%s\",\"url\":\"https://cdn.kode.diy/kodeos/x.kfw\"}",
                       BAD[i]);
        TEST_ASSERT_FALSE_MESSAGE(parse(json, false, &rel), BAD[i]);
    }
}

void test_parse_refuses_a_link_it_would_not_follow(void)
{
    static const char *const BAD[] = {
        "http://cdn.kode.diy/kodeos/x.kfw",     /* plain, and not a bench build */
        "ftp://cdn.kode.diy/kodeos/x.kfw",
        "cdn.kode.diy/kodeos/x.kfw",
        "https://",
        "https://cdn.kode.diy/kodeos/x y.kfw",
        "https://cdn.kode.diy/kodeos/x\\/y.kfw",
        "",
    };
    kgui_osrel_t rel;
    char         json[400];
    char         longest[KGUI_OSREL_URL_MAX + 16U];

    for ( size_t i = 0U; (sizeof(BAD) / sizeof(BAD[0])) > i; ++i )
    {
        (void)snprintf(json, sizeof(json), "{\"version\":\"0.1.2\",\"url\":\"%s\"}", BAD[i]);
        TEST_ASSERT_FALSE_MESSAGE(parse(json, false, &rel), BAD[i]);
    }

    /* One character more than fits is refused, never cut. */
    (void)memset(longest, 'a', sizeof(longest));
    (void)memcpy(longest, "https://", 8U);
    longest[KGUI_OSREL_URL_MAX] = '\0';
    (void)snprintf(json, sizeof(json), "{\"version\":\"0.1.2\",\"url\":\"%s\"}", longest);
    TEST_ASSERT_FALSE(parse(json, false, &rel));
    longest[KGUI_OSREL_URL_MAX - 1U] = '\0';
    (void)snprintf(json, sizeof(json), "{\"version\":\"0.1.2\",\"url\":\"%s\"}", longest);
    TEST_ASSERT_TRUE(parse(json, false, &rel));
    TEST_ASSERT_EQUAL_UINT(KGUI_OSREL_URL_MAX - 1U, strlen(rel.url));
}

void test_parse_takes_a_plain_link_on_a_bench_alone(void)
{
    static const char JSON[] =
        "{\"version\":\"0.1.2\",\"url\":\"http://192.168.1.20:8000/kodeos.kfw\"}";
    kgui_osrel_t rel;

    TEST_ASSERT_FALSE(parse(JSON, false, &rel));
    TEST_ASSERT_TRUE(parse(JSON, true, &rel));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.20:8000/kodeos.kfw", rel.url);
}

void test_parse_refuses_a_cut_answer_and_keeps_out(void)
{
    kgui_osrel_t rel;

    (void)snprintf(rel.version, sizeof(rel.version), "1.1.1");
    (void)snprintf(rel.url, sizeof(rel.url), "kept");
    /* Cut inside the link: the closing quote never comes. */
    TEST_ASSERT_FALSE(kgui_osrel_parse(ANSWER_LIVE, 60U, false, &rel));
    TEST_ASSERT_EQUAL_STRING("1.1.1", rel.version);
    TEST_ASSERT_EQUAL_STRING("kept", rel.url);
    /* A terminator inside the bytes ends the answer there. */
    TEST_ASSERT_FALSE(kgui_osrel_parse("{\"version\":\"0.1.2\"\0,\"url\":\"https://a/b\"}",
                                       41U, false, &rel));
}

void test_newer_compares_groups_as_numbers(void)
{
    TEST_ASSERT_TRUE(kgui_osrel_newer("0.1.0", "00.00.24"));
    TEST_ASSERT_FALSE(kgui_osrel_newer("00.00.24", "0.1.0"));
    TEST_ASSERT_TRUE(kgui_osrel_newer("0.1.10", "0.1.9"));
    TEST_ASSERT_FALSE(kgui_osrel_newer("0.1.9", "0.1.10"));
    TEST_ASSERT_TRUE(kgui_osrel_newer("1.0.0", "0.99.99"));
    TEST_ASSERT_TRUE(kgui_osrel_newer("0.2.0", "0.1.9999"));
    TEST_ASSERT_TRUE(kgui_osrel_newer("0.1.2", "0.1.1"));
}

void test_newer_never_offers_the_same_version(void)
{
    TEST_ASSERT_FALSE(kgui_osrel_newer("0.1.1", "0.1.1"));
    TEST_ASSERT_FALSE(kgui_osrel_newer("00.01.00", "0.1.0"));
    TEST_ASSERT_FALSE(kgui_osrel_newer("0.1.0", "00.01.00"));
}

void test_newer_is_false_for_what_it_cannot_read(void)
{
    TEST_ASSERT_FALSE(kgui_osrel_newer("9.9.9", "dev"));
    TEST_ASSERT_FALSE(kgui_osrel_newer("9.9.9", "0.1.1-dirty"));
    TEST_ASSERT_FALSE(kgui_osrel_newer("latest", "0.1.1"));
    TEST_ASSERT_FALSE(kgui_osrel_newer("9.9", "0.1.1"));
    TEST_ASSERT_FALSE(kgui_osrel_newer(NULL, "0.1.1"));
    TEST_ASSERT_FALSE(kgui_osrel_newer("9.9.9", NULL));
    TEST_ASSERT_FALSE(kgui_osrel_newer("", ""));
}

void test_power_needs_a_cable_or_a_fifth_of_the_battery(void)
{
    TEST_ASSERT_TRUE(kgui_osrel_power_ok(100U, false));
    TEST_ASSERT_TRUE(kgui_osrel_power_ok((uint8_t)KGUI_OSREL_MIN_PCT, false));
    TEST_ASSERT_FALSE(kgui_osrel_power_ok((uint8_t)(KGUI_OSREL_MIN_PCT - 1U), false));
    TEST_ASSERT_FALSE(kgui_osrel_power_ok(0U, false));
    TEST_ASSERT_TRUE(kgui_osrel_power_ok(0U, true));
}

/** @brief The row for one set of facts. */
static kgui_osrel_row_t row(bool service, bool card, bool linked, bool power,
                            kgui_osrel_check_t check)
{
    kgui_osrel_view_t const view = { service, card, linked, power, check };

    return kgui_osrel_row(&view);
}

void test_row_the_card_wins_and_a_build_with_no_service_says_as_before(void)
{
    TEST_ASSERT_EQUAL(KGUI_OSREL_ROW_CARD, row(true, true, true, true, KGUI_OSREL_AVAILABLE));
    TEST_ASSERT_EQUAL(KGUI_OSREL_ROW_CARD, row(false, true, false, false, KGUI_OSREL_NEVER));
    TEST_ASSERT_EQUAL(KGUI_OSREL_ROW_NONE, row(false, false, true, true, KGUI_OSREL_AVAILABLE));
    TEST_ASSERT_EQUAL(KGUI_OSREL_ROW_NONE, kgui_osrel_row(NULL));
}

void test_row_with_no_network_says_so_whatever_was_known(void)
{
    TEST_ASSERT_EQUAL(KGUI_OSREL_ROW_OFFLINE, row(true, false, false, true, KGUI_OSREL_NEVER));
    TEST_ASSERT_EQUAL(KGUI_OSREL_ROW_OFFLINE, row(true, false, false, true, KGUI_OSREL_AVAILABLE));
    TEST_ASSERT_EQUAL(KGUI_OSREL_ROW_OFFLINE, row(true, false, false, true, KGUI_OSREL_FAILED));
}

void test_row_follows_the_check(void)
{
    TEST_ASSERT_EQUAL(KGUI_OSREL_ROW_CHECKING, row(true, false, true, true, KGUI_OSREL_NEVER));
    TEST_ASSERT_EQUAL(KGUI_OSREL_ROW_CHECKING, row(true, false, true, true, KGUI_OSREL_ASKING));
    TEST_ASSERT_EQUAL(KGUI_OSREL_ROW_CURRENT, row(true, false, true, true, KGUI_OSREL_CURRENT));
    TEST_ASSERT_EQUAL(KGUI_OSREL_ROW_FAILED, row(true, false, true, true, KGUI_OSREL_FAILED));
    /* A question AO_Net never took to the service did not check either. */
    TEST_ASSERT_EQUAL(KGUI_OSREL_ROW_FAILED, row(true, false, true, true, KGUI_OSREL_UNREACHED));
    TEST_ASSERT_EQUAL(KGUI_OSREL_ROW_AVAILABLE, row(true, false, true, true, KGUI_OSREL_AVAILABLE));
    TEST_ASSERT_EQUAL(KGUI_OSREL_ROW_CHARGE, row(true, false, true, false, KGUI_OSREL_AVAILABLE));
    /* The battery only matters to an install. */
    TEST_ASSERT_EQUAL(KGUI_OSREL_ROW_CURRENT, row(true, false, true, false, KGUI_OSREL_CURRENT));
}

void test_row_what_the_top_button_does(void)
{
    TEST_ASSERT_TRUE(kgui_osrel_row_installs(KGUI_OSREL_ROW_CARD));
    TEST_ASSERT_TRUE(kgui_osrel_row_installs(KGUI_OSREL_ROW_AVAILABLE));
    TEST_ASSERT_FALSE(kgui_osrel_row_installs(KGUI_OSREL_ROW_CHARGE));
    TEST_ASSERT_FALSE(kgui_osrel_row_installs(KGUI_OSREL_ROW_CURRENT));
    TEST_ASSERT_FALSE(kgui_osrel_row_installs(KGUI_OSREL_ROW_CHECKING));
    TEST_ASSERT_FALSE(kgui_osrel_row_installs(KGUI_OSREL_ROW_OFFLINE));
    TEST_ASSERT_FALSE(kgui_osrel_row_installs(KGUI_OSREL_ROW_FAILED));
    TEST_ASSERT_FALSE(kgui_osrel_row_installs(KGUI_OSREL_ROW_NONE));

    TEST_ASSERT_TRUE(kgui_osrel_row_asks(KGUI_OSREL_ROW_CURRENT));
    TEST_ASSERT_TRUE(kgui_osrel_row_asks(KGUI_OSREL_ROW_FAILED));
    TEST_ASSERT_FALSE(kgui_osrel_row_asks(KGUI_OSREL_ROW_CHARGE));
    TEST_ASSERT_FALSE(kgui_osrel_row_asks(KGUI_OSREL_ROW_AVAILABLE));
    TEST_ASSERT_FALSE(kgui_osrel_row_asks(KGUI_OSREL_ROW_CARD));
    TEST_ASSERT_FALSE(kgui_osrel_row_asks(KGUI_OSREL_ROW_CHECKING));
    TEST_ASSERT_FALSE(kgui_osrel_row_asks(KGUI_OSREL_ROW_OFFLINE));
    TEST_ASSERT_FALSE(kgui_osrel_row_asks(KGUI_OSREL_ROW_NONE));
}

void test_row_words(void)
{
    char text[KGUI_OSREL_TEXT_MAX];

    kgui_osrel_row_text(KGUI_OSREL_ROW_NONE, NULL, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("No image on the card", text);
    kgui_osrel_row_text(KGUI_OSREL_ROW_CARD, "0.1.2", text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("kodeos.bin on the card", text);
    kgui_osrel_row_text(KGUI_OSREL_ROW_AVAILABLE, "0.1.2", text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("kodeOS 0.1.2 available", text);
    kgui_osrel_row_text(KGUI_OSREL_ROW_CHARGE, "0.1.2", text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("Charge to install kodeOS 0.1.2", text);
    kgui_osrel_row_text(KGUI_OSREL_ROW_CURRENT, "0.1.2", text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("kodeOS is up to date", text);
    kgui_osrel_row_text(KGUI_OSREL_ROW_CHECKING, NULL, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("Checking...", text);
    kgui_osrel_row_text(KGUI_OSREL_ROW_OFFLINE, NULL, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("Join Wi-Fi to check", text);
    kgui_osrel_row_text(KGUI_OSREL_ROW_FAILED, NULL, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("Could not check", text);
}

void test_row_words_fit_the_longest_version_and_a_short_buffer(void)
{
    char text[KGUI_OSREL_TEXT_MAX];
    char small[8];

    kgui_osrel_row_text(KGUI_OSREL_ROW_CHARGE, "1234.1234.1234", text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("Charge to install kodeOS 1234.1234.1234", text);
    kgui_osrel_row_text(KGUI_OSREL_ROW_AVAILABLE, NULL, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("kodeOS  available", text);
    kgui_osrel_row_text(KGUI_OSREL_ROW_AVAILABLE, "0.1.2", small, sizeof(small));
    TEST_ASSERT_EQUAL_STRING("kodeOS ", small);
    /* Nowhere to write is nothing written. */
    kgui_osrel_row_text(KGUI_OSREL_ROW_AVAILABLE, "0.1.2", NULL, 0U);
}

void test_offer_follows_the_boot_question(void)
{
    /* The question goes out, then its answer decides. */
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_ASKED,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_UNASKED, KGUI_OSREL_ASKING, FIRST));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_ASKED,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_ASKED, KGUI_OSREL_ASKING, FIRST));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_DUE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_ASKED, KGUI_OSREL_AVAILABLE, FIRST));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_NONE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_ASKED, KGUI_OSREL_CURRENT, FIRST));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_NONE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_ASKED, KGUI_OSREL_FAILED, FIRST));
    /* An answer already known when the boot's question would go out. */
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_DUE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_UNASKED, KGUI_OSREL_AVAILABLE, FIRST));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_NONE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_UNASKED, KGUI_OSREL_CURRENT, FIRST));
    /* No memory for the answer, an answer that did not read, or nobody to
     * ask: once per boot. */
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_NONE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_UNASKED, KGUI_OSREL_FAILED, FIRST));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_NONE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_UNASKED, KGUI_OSREL_NEVER, FIRST));
}

void test_offer_asks_again_when_the_question_did_not_reach_the_service(void)
{
    /* AO_Net refused it (its one worker busy with the Kode channel's join),
     * or did not take it: the boot asks again. */
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_AGAIN,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_ASKED, KGUI_OSREL_UNREACHED, FIRST));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_AGAIN,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_UNASKED, KGUI_OSREL_UNREACHED, FIRST));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_AGAIN,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_AGAIN, KGUI_OSREL_UNREACHED,
                                              (uint8_t)(KGUI_OSREL_BOOT_ASKS - 1U)));
    /* Asked again: its answer decides, as the first one's would have. */
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_ASKED,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_AGAIN, KGUI_OSREL_ASKING, 2U));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_DUE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_AGAIN, KGUI_OSREL_AVAILABLE, 2U));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_NONE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_AGAIN, KGUI_OSREL_CURRENT, 2U));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_NONE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_AGAIN, KGUI_OSREL_FAILED, 2U));
}

void test_offer_asks_again_a_bounded_number_of_times(void)
{
    /* More than one question, and a wait between them. */
    TEST_ASSERT_TRUE(1U < KGUI_OSREL_BOOT_ASKS);
    TEST_ASSERT_TRUE(0U < KGUI_OSREL_BOOT_AGAIN_MS);
    /* The last question spent, the boot gives up: no card. */
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_NONE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_ASKED, KGUI_OSREL_UNREACHED,
                                              (uint8_t)KGUI_OSREL_BOOT_ASKS));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_NONE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_AGAIN, KGUI_OSREL_UNREACHED,
                                              (uint8_t)KGUI_OSREL_BOOT_ASKS));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_NONE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_ASKED, KGUI_OSREL_UNREACHED, UINT8_MAX));
}

void test_offer_due_goes_only_when_the_release_is_taken_back(void)
{
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_NONE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_DUE, KGUI_OSREL_CURRENT, FIRST));
    /* A later question that failed, did not reach the service, or is on its
     * way, keeps the release kept. */
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_DUE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_DUE, KGUI_OSREL_FAILED, FIRST));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_DUE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_DUE, KGUI_OSREL_UNREACHED, FIRST));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_DUE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_DUE, KGUI_OSREL_ASKING, FIRST));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_DUE,
                      kgui_osrel_offer_follow(KGUI_OSREL_OFFER_DUE, KGUI_OSREL_AVAILABLE, FIRST));
}

void test_offer_rises_once_a_boot(void)
{
    static const kgui_osrel_check_t CHECKS[] = {
        KGUI_OSREL_NEVER, KGUI_OSREL_ASKING, KGUI_OSREL_CURRENT,
        KGUI_OSREL_AVAILABLE, KGUI_OSREL_FAILED, KGUI_OSREL_UNREACHED,
    };

    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_SHOWN, kgui_osrel_offer_shown(KGUI_OSREL_OFFER_DUE));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_SHOWN, kgui_osrel_offer_shown(KGUI_OSREL_OFFER_SHOWN));
    /* Nothing was due: writing the card changes nothing. */
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_NONE, kgui_osrel_offer_shown(KGUI_OSREL_OFFER_NONE));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_ASKED, kgui_osrel_offer_shown(KGUI_OSREL_OFFER_ASKED));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_UNASKED, kgui_osrel_offer_shown(KGUI_OSREL_OFFER_UNASKED));
    TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_AGAIN, kgui_osrel_offer_shown(KGUI_OSREL_OFFER_AGAIN));
    /* Shown and none stay what they are until the next boot. */
    for ( size_t i = 0U; (sizeof(CHECKS) / sizeof(CHECKS[0])) > i; ++i )
    {
        TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_SHOWN,
                          kgui_osrel_offer_follow(KGUI_OSREL_OFFER_SHOWN, CHECKS[i], FIRST));
        TEST_ASSERT_EQUAL(KGUI_OSREL_OFFER_NONE,
                          kgui_osrel_offer_follow(KGUI_OSREL_OFFER_NONE, CHECKS[i], FIRST));
    }
}

void test_offer_words(void)
{
    TEST_ASSERT_EQUAL_STRING("unasked", kgui_osrel_offer_word(KGUI_OSREL_OFFER_UNASKED));
    TEST_ASSERT_EQUAL_STRING("asked", kgui_osrel_offer_word(KGUI_OSREL_OFFER_ASKED));
    TEST_ASSERT_EQUAL_STRING("due", kgui_osrel_offer_word(KGUI_OSREL_OFFER_DUE));
    TEST_ASSERT_EQUAL_STRING("shown", kgui_osrel_offer_word(KGUI_OSREL_OFFER_SHOWN));
    TEST_ASSERT_EQUAL_STRING("none", kgui_osrel_offer_word(KGUI_OSREL_OFFER_NONE));
    TEST_ASSERT_EQUAL_STRING("again", kgui_osrel_offer_word(KGUI_OSREL_OFFER_AGAIN));
    TEST_ASSERT_EQUAL_STRING("?", kgui_osrel_offer_word((kgui_osrel_offer_t)99));
}

/** @brief A free glass on @p screen: nothing up, nothing moving, no hand on the board. */
static kgui_osrel_glass_t glass_free(kgui_osrel_screen_t screen)
{
    kgui_osrel_glass_t g;

    (void)memset(&g, 0, sizeof(g));
    g.screen = screen;
    return g;
}

void test_may_rise_on_the_face_apps_or_settings_when_free(void)
{
    kgui_osrel_glass_t g = glass_free(KGUI_OSREL_SCREEN_FACE);

    TEST_ASSERT_TRUE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g));
    g = glass_free(KGUI_OSREL_SCREEN_APPS);
    TEST_ASSERT_TRUE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g));
    g = glass_free(KGUI_OSREL_SCREEN_SETTINGS);
    TEST_ASSERT_TRUE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g));
}

void test_may_rise_never_at_rest_or_on_any_other_screen(void)
{
    kgui_osrel_glass_t g = glass_free(KGUI_OSREL_SCREEN_FACE);

    /* The face at Rest is still the face's screen state: Rest is its look. */
    g.rest = true;
    TEST_ASSERT_FALSE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g));
    /* The boot's wait, the way back from an app, a guided test, the station's test, asleep. */
    g = glass_free(KGUI_OSREL_SCREEN_OTHER);
    TEST_ASSERT_FALSE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g));
    TEST_ASSERT_FALSE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, NULL));
}

void test_glass_free_reads_the_glass_alone(void)
{
    kgui_osrel_glass_t g = glass_free(KGUI_OSREL_SCREEN_APPS);

    TEST_ASSERT_TRUE(kgui_osrel_glass_free(&g));
    g.moving = true;
    TEST_ASSERT_FALSE(kgui_osrel_glass_free(&g));
    g = glass_free(KGUI_OSREL_SCREEN_OTHER);
    TEST_ASSERT_FALSE(kgui_osrel_glass_free(&g));
    TEST_ASSERT_FALSE(kgui_osrel_glass_free(NULL));
}

void test_may_rise_only_while_the_offer_is_due(void)
{
    static const kgui_osrel_offer_t NOT_DUE[] = {
        KGUI_OSREL_OFFER_UNASKED, KGUI_OSREL_OFFER_ASKED, KGUI_OSREL_OFFER_SHOWN,
        KGUI_OSREL_OFFER_NONE, KGUI_OSREL_OFFER_AGAIN,
    };
    kgui_osrel_glass_t const g = glass_free(KGUI_OSREL_SCREEN_FACE);

    for ( size_t i = 0U; (sizeof(NOT_DUE) / sizeof(NOT_DUE[0])) > i; ++i )
    {
        TEST_ASSERT_FALSE(kgui_osrel_offer_may_rise(NOT_DUE[i], &g));
    }
}

void test_may_rise_waits_for_each_thing_that_holds_the_glass(void)
{
    static const kgui_osrel_screen_t SCREENS[] = {
        KGUI_OSREL_SCREEN_FACE, KGUI_OSREL_SCREEN_APPS, KGUI_OSREL_SCREEN_SETTINGS,
    };

    for ( size_t i = 0U; (sizeof(SCREENS) / sizeof(SCREENS[0])) > i; ++i )
    {
        kgui_osrel_glass_t g;

        g = glass_free(SCREENS[i]);
        g.card = true;
        TEST_ASSERT_FALSE_MESSAGE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g), "card");
        g = glass_free(SCREENS[i]);
        g.locked = true;
        TEST_ASSERT_FALSE_MESSAGE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g), "locked");
        g = glass_free(SCREENS[i]);
        g.prompt = true;
        TEST_ASSERT_FALSE_MESSAGE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g), "prompt");
        g = glass_free(SCREENS[i]);
        g.writing = true;
        TEST_ASSERT_FALSE_MESSAGE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g), "writing");
        g = glass_free(SCREENS[i]);
        g.deciding = true;
        TEST_ASSERT_FALSE_MESSAGE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g), "deciding");
        g = glass_free(SCREENS[i]);
        g.resetting = true;
        TEST_ASSERT_FALSE_MESSAGE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g), "resetting");
        g = glass_free(SCREENS[i]);
        g.parked = true;
        TEST_ASSERT_FALSE_MESSAGE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g), "parked");
        g = glass_free(SCREENS[i]);
        g.moving = true;
        TEST_ASSERT_FALSE_MESSAGE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g), "moving");
        g = glass_free(SCREENS[i]);
        g.hand = true;
        TEST_ASSERT_FALSE_MESSAGE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g), "hand");
        g = glass_free(SCREENS[i]);
        g.greeting = true;
        TEST_ASSERT_FALSE_MESSAGE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g), "greeting");
        g = glass_free(SCREENS[i]);
        g.rest = true;
        TEST_ASSERT_FALSE_MESSAGE(kgui_osrel_offer_may_rise(KGUI_OSREL_OFFER_DUE, &g), "rest");
    }
}

void test_card_button_follows_the_network_then_the_battery(void)
{
    TEST_ASSERT_EQUAL(KGUI_OSREL_CARD_READY, kgui_osrel_card(true, true));
    TEST_ASSERT_EQUAL(KGUI_OSREL_CARD_CHARGE, kgui_osrel_card(true, false));
    TEST_ASSERT_EQUAL(KGUI_OSREL_CARD_OFFLINE, kgui_osrel_card(false, true));
    /* Both missing: the network first, as About's row says. */
    TEST_ASSERT_EQUAL(KGUI_OSREL_CARD_OFFLINE, kgui_osrel_card(false, false));
}

void test_card_words(void)
{
    char text[KGUI_OSREL_TEXT_MAX];
    char small[8];
    char charge[KGUI_OSREL_TEXT_MAX];

    kgui_osrel_card_title("0.1.6", text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("kodeOS 0.1.6", text);
    kgui_osrel_card_title("1234.1234.1234", text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("kodeOS 1234.1234.1234", text);
    kgui_osrel_card_title(NULL, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("kodeOS", text);
    kgui_osrel_card_title("", text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("kodeOS", text);
    kgui_osrel_card_title("0.1.2", small, sizeof(small));
    TEST_ASSERT_EQUAL_STRING("kodeOS ", small);
    kgui_osrel_card_title("0.1.2", NULL, 0U);

    /* The charge is the module's own threshold, said as a number. */
    (void)snprintf(charge, sizeof(charge), "Charge to %u%% or plug in.",
                   (unsigned)KGUI_OSREL_MIN_PCT);
    kgui_osrel_card_body(KGUI_OSREL_CARD_CHARGE, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING(charge, text);
    TEST_ASSERT_EQUAL_STRING("Charge to 20% or plug in.", text);
    kgui_osrel_card_body(KGUI_OSREL_CARD_OFFLINE, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("Join Wi-Fi to update.", text);
    kgui_osrel_card_body(KGUI_OSREL_CARD_READY, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("A new version is ready.", text);
    kgui_osrel_card_body(KGUI_OSREL_CARD_CHARGE, NULL, 0U);

    TEST_ASSERT_EQUAL_STRING("kodeOS", KGUI_OSREL_CARD_WORD);
    TEST_ASSERT_EQUAL_STRING("Later", KGUI_OSREL_CARD_LATER);
    TEST_ASSERT_EQUAL_STRING("Update", KGUI_OSREL_CARD_BUTTON);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_the_live_answer);
    RUN_TEST(test_parse_any_order_spaces_and_new_fields);
    RUN_TEST(test_parse_takes_the_top_level_alone);
    RUN_TEST(test_parse_refuses_a_missing_field);
    RUN_TEST(test_parse_refuses_a_version_that_is_not_three_groups);
    RUN_TEST(test_parse_refuses_a_link_it_would_not_follow);
    RUN_TEST(test_parse_takes_a_plain_link_on_a_bench_alone);
    RUN_TEST(test_parse_refuses_a_cut_answer_and_keeps_out);
    RUN_TEST(test_newer_compares_groups_as_numbers);
    RUN_TEST(test_newer_never_offers_the_same_version);
    RUN_TEST(test_newer_is_false_for_what_it_cannot_read);
    RUN_TEST(test_power_needs_a_cable_or_a_fifth_of_the_battery);
    RUN_TEST(test_row_the_card_wins_and_a_build_with_no_service_says_as_before);
    RUN_TEST(test_row_with_no_network_says_so_whatever_was_known);
    RUN_TEST(test_row_follows_the_check);
    RUN_TEST(test_row_what_the_top_button_does);
    RUN_TEST(test_row_words);
    RUN_TEST(test_row_words_fit_the_longest_version_and_a_short_buffer);
    RUN_TEST(test_offer_follows_the_boot_question);
    RUN_TEST(test_offer_asks_again_when_the_question_did_not_reach_the_service);
    RUN_TEST(test_offer_asks_again_a_bounded_number_of_times);
    RUN_TEST(test_offer_due_goes_only_when_the_release_is_taken_back);
    RUN_TEST(test_offer_rises_once_a_boot);
    RUN_TEST(test_offer_words);
    RUN_TEST(test_may_rise_on_the_face_apps_or_settings_when_free);
    RUN_TEST(test_may_rise_never_at_rest_or_on_any_other_screen);
    RUN_TEST(test_glass_free_reads_the_glass_alone);
    RUN_TEST(test_may_rise_only_while_the_offer_is_due);
    RUN_TEST(test_may_rise_waits_for_each_thing_that_holds_the_glass);
    RUN_TEST(test_card_button_follows_the_network_then_the_battery);
    RUN_TEST(test_card_words);
    return UNITY_END();
}
