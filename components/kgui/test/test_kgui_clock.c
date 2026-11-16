/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_clock.c
 * @brief Host tests of the Time page's arithmetic (kgui_clock.h).
 */
/* setenv(), tzset(), gmtime_r() and localtime_r() are POSIX's. */
#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "unity.h"

#include "kgui_clock.h"
#include "kode_log.h"
#include "kdrv_clocks.h"

/** Madrid's zone, as the table holds it. */
#define TZ_MADRID  "CET-1CEST,M3.5.0,M10.5.0/3"
/** Tokyo's: east of UTC, no summer time. */
#define TZ_TOKYO   "JST-9"
/** The longest zone string the system keeps (KODE_CONFIG_VALUE_MAX). */
#define ZONE_MAX   32U
/** A minute, in seconds. */
#define MINUTE_S   60
/** An hour, in seconds. */
#define HOUR_S     3600LL
/** A second, in microseconds. */
#define SECOND_US  1000000ULL
/** 2026-11-16T14:00:00Z. */
#define NOV16_14Z  1794837600LL
/** 2026-03-29T00:30:00Z: 01:30 in Madrid, half an hour before summer time. */
#define MAR29_0030Z 1774744200LL
/** 2026-03-29T01:30:00Z: 03:30 in Madrid, half an hour after it. */
#define MAR29_0130Z 1774747800LL

/** @brief A local time, as a person reads it. */
static struct tm local_of(int32_t year, int32_t mon, int32_t day, int32_t hour, int32_t min)
{
    struct tm t;

    (void)memset(&t, 0, sizeof(t));
    t.tm_year  = (int)(year - KGUI_CLOCK_TM_YEAR0);
    t.tm_mon   = (int)mon;
    t.tm_mday  = (int)day;
    t.tm_hour  = (int)hour;
    t.tm_min   = (int)min;
    t.tm_isdst = -1;
    return t;
}

/** @brief Run the C library in @p tz, as AO_Boot does on the board. */
static void zone_in(const char *tz)
{
    (void)setenv("TZ", tz, 1);
    tzset();
}

void setUp(void)
{
    zone_in(KGUI_CLOCK_ZONE_UTC);
}

void tearDown(void)
{
}

/* ---- The limits agree with the system's ---- */

void test_clock_limits_are_the_system_ones(void)
{
    struct tm first;
    time_t const at = (time_t)KODE_CLOCK_KNOWN_AFTER_S;

    (void)gmtime_r(&at, &first);
    TEST_ASSERT_EQUAL_INT(KGUI_CLOCK_YEAR_MIN, first.tm_year + KGUI_CLOCK_TM_YEAR0);
    TEST_ASSERT_EQUAL_INT(0, first.tm_mon);
    TEST_ASSERT_EQUAL_INT(1, first.tm_mday);
    TEST_ASSERT_EQUAL_INT((int)KDRV_CLOCKS_WALL_YEAR_MAX, KGUI_CLOCK_YEAR_MAX);
}

void test_clock_utc_end_is_the_year_after_the_last(void)
{
    struct tm    t;
    time_t const last = (time_t)(KGUI_CLOCK_UTC_END_S - 1LL);
    time_t const end  = (time_t)KGUI_CLOCK_UTC_END_S;

    (void)gmtime_r(&last, &t);
    TEST_ASSERT_EQUAL_INT(KGUI_CLOCK_YEAR_MAX, t.tm_year + KGUI_CLOCK_TM_YEAR0);
    TEST_ASSERT_EQUAL_INT(11, t.tm_mon);
    TEST_ASSERT_EQUAL_INT(31, t.tm_mday);
    TEST_ASSERT_EQUAL_INT(23, t.tm_hour);
    (void)gmtime_r(&end, &t);
    TEST_ASSERT_EQUAL_INT(KGUI_CLOCK_YEAR_MAX + 1, t.tm_year + KGUI_CLOCK_TM_YEAR0);
}

/* ---- Days of a month ---- */

void test_clock_month_days(void)
{
    TEST_ASSERT_EQUAL_UINT8(31U, kgui_clock_month_days(2026, 0));
    TEST_ASSERT_EQUAL_UINT8(28U, kgui_clock_month_days(2026, 1));
    TEST_ASSERT_EQUAL_UINT8(29U, kgui_clock_month_days(2028, 1));
    TEST_ASSERT_EQUAL_UINT8(28U, kgui_clock_month_days(2100, 1));   /* a century, no leap */
    TEST_ASSERT_EQUAL_UINT8(29U, kgui_clock_month_days(2000, 1));   /* every 400 years, leap */
    TEST_ASSERT_EQUAL_UINT8(30U, kgui_clock_month_days(2026, 10));
    TEST_ASSERT_EQUAL_UINT8(31U, kgui_clock_month_days(2026, 11));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_clock_month_days(2026, 12));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_clock_month_days(2026, -1));
}

/* ---- The steps ---- */

void test_clock_hour_and_minute_go_round_and_keep_the_day(void)
{
    struct tm t = local_of(2026, 10, 16, 23, 59);

    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_MINUTE, 1));
    TEST_ASSERT_EQUAL_INT(0, t.tm_min);
    TEST_ASSERT_EQUAL_INT(23, t.tm_hour);
    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_HOUR, 1));
    TEST_ASSERT_EQUAL_INT(0, t.tm_hour);
    TEST_ASSERT_EQUAL_INT(16, t.tm_mday);
    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_HOUR, -1));
    TEST_ASSERT_EQUAL_INT(23, t.tm_hour);
    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_MINUTE, -1));
    TEST_ASSERT_EQUAL_INT(59, t.tm_min);
    TEST_ASSERT_EQUAL_INT(16, t.tm_mday);
    /* Any step that is not back is forward: the top button's 0. */
    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_MINUTE, 0));
    TEST_ASSERT_EQUAL_INT(0, t.tm_min);
}

void test_clock_day_goes_round_inside_its_month(void)
{
    struct tm t = local_of(2026, 10, 30, 12, 0);     /* 30 November */

    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_DAY, 1));
    TEST_ASSERT_EQUAL_INT(1, t.tm_mday);
    TEST_ASSERT_EQUAL_INT(10, t.tm_mon);
    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_DAY, -1));
    TEST_ASSERT_EQUAL_INT(30, t.tm_mday);

    t = local_of(2028, 1, 29, 12, 0);                 /* 29 February, leap */
    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_DAY, 1));
    TEST_ASSERT_EQUAL_INT(1, t.tm_mday);
    TEST_ASSERT_EQUAL_INT(1, t.tm_mon);
    t = local_of(2027, 1, 1, 12, 0);                  /* 1 February, not leap */
    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_DAY, -1));
    TEST_ASSERT_EQUAL_INT(28, t.tm_mday);
    TEST_ASSERT_EQUAL_INT(2027 - KGUI_CLOCK_TM_YEAR0, t.tm_year);
}

void test_clock_month_goes_round_and_trims_the_day(void)
{
    struct tm t = local_of(2026, 0, 31, 12, 0);      /* 31 January */

    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_MONTH, 1));
    TEST_ASSERT_EQUAL_INT(1, t.tm_mon);
    TEST_ASSERT_EQUAL_INT(28, t.tm_mday);
    t = local_of(2028, 0, 31, 12, 0);
    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_MONTH, 1));
    TEST_ASSERT_EQUAL_INT(29, t.tm_mday);
    t = local_of(2026, 4, 31, 12, 0);                 /* 31 May */
    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_MONTH, -1));
    TEST_ASSERT_EQUAL_INT(3, t.tm_mon);
    TEST_ASSERT_EQUAL_INT(30, t.tm_mday);
    t = local_of(2026, 11, 15, 12, 0);                /* December */
    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_MONTH, 1));
    TEST_ASSERT_EQUAL_INT(0, t.tm_mon);
    TEST_ASSERT_EQUAL_INT(2026 - KGUI_CLOCK_TM_YEAR0, t.tm_year);
    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_MONTH, -1));
    TEST_ASSERT_EQUAL_INT(11, t.tm_mon);
    TEST_ASSERT_EQUAL_INT(2026 - KGUI_CLOCK_TM_YEAR0, t.tm_year);
}

void test_clock_year_stops_at_both_ends_and_trims_the_29th(void)
{
    struct tm t = local_of(2028, 1, 29, 12, 0);

    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_YEAR, 1));
    TEST_ASSERT_EQUAL_INT(2029 - KGUI_CLOCK_TM_YEAR0, t.tm_year);
    TEST_ASSERT_EQUAL_INT(28, t.tm_mday);

    t = local_of(KGUI_CLOCK_YEAR_MIN, 5, 1, 12, 0);
    TEST_ASSERT_FALSE(kgui_clock_step(&t, KGUI_CLOCK_YEAR, -1));
    TEST_ASSERT_EQUAL_INT(KGUI_CLOCK_YEAR_MIN - KGUI_CLOCK_TM_YEAR0, t.tm_year);
    t = local_of(KGUI_CLOCK_YEAR_MAX, 5, 1, 12, 0);
    TEST_ASSERT_FALSE(kgui_clock_step(&t, KGUI_CLOCK_YEAR, 1));
    TEST_ASSERT_EQUAL_INT(KGUI_CLOCK_YEAR_MAX - KGUI_CLOCK_TM_YEAR0, t.tm_year);
    TEST_ASSERT_TRUE(kgui_clock_step(&t, KGUI_CLOCK_YEAR, -1));
    TEST_ASSERT_EQUAL_INT(KGUI_CLOCK_YEAR_MAX - 1 - KGUI_CLOCK_TM_YEAR0, t.tm_year);
}

void test_clock_step_refuses_what_is_no_row(void)
{
    struct tm t = local_of(2026, 5, 1, 12, 0);

    TEST_ASSERT_FALSE(kgui_clock_step(NULL, KGUI_CLOCK_DAY, 1));
    TEST_ASSERT_FALSE(kgui_clock_step(&t, (kgui_clock_field_t)99, 1));
    TEST_ASSERT_EQUAL_INT(1, t.tm_mday);
}

/* ---- The UTC a local reading names ---- */

void test_clock_utc_of_reads_in_the_zone_in_force(void)
{
    struct tm const t = local_of(2026, 10, 16, 14, 0);
    int64_t utc = 0;

    TEST_ASSERT_TRUE(kgui_clock_utc_of(&t, 0U, &utc));
    TEST_ASSERT_EQUAL_INT64(NOV16_14Z, utc);
    zone_in(TZ_MADRID);
    TEST_ASSERT_TRUE(kgui_clock_utc_of(&t, 0U, &utc));
    TEST_ASSERT_EQUAL_INT64(NOV16_14Z - HOUR_S, utc);
    TEST_ASSERT_TRUE(kgui_clock_utc_of(&t, (uint32_t)MINUTE_S, &utc));
    TEST_ASSERT_EQUAL_INT64(NOV16_14Z - HOUR_S + MINUTE_S, utc);
}

void test_clock_a_hand_set_hour_survives_a_change_of_zone(void)
{
    /* The rule for a time the Time page set: a change of zone keeps the
     * reading and moves the clock's UTC. 14:05 set by hand in UTC; the zone
     * becomes Madrid's 30 s later; the page still reads 14:05, now 13:05Z. */
    struct tm const was = local_of(2026, 10, 16, 14, 5);
    int64_t         utc = 0;
    struct tm       now;
    time_t          at;

    zone_in(TZ_MADRID);
    TEST_ASSERT_TRUE(kgui_clock_utc_of(&was, 30U, &utc));
    at = (time_t)utc;
    (void)localtime_r(&at, &now);
    TEST_ASSERT_EQUAL_INT(14, now.tm_hour);
    TEST_ASSERT_EQUAL_INT(5, now.tm_min);
    TEST_ASSERT_EQUAL_INT(30, now.tm_sec);
    TEST_ASSERT_EQUAL_INT(16, now.tm_mday);
    TEST_ASSERT_EQUAL_INT64(NOV16_14Z + (5 * MINUTE_S) + 30 - HOUR_S, utc);
}

void test_clock_an_hour_step_crosses_the_spring_gap_both_ways(void)
{
    /* 02:30 on 29 March 2026 does not happen in Madrid: the clocks go from
     * 02:00 to 03:00. A press on Hour still moves the clock one way or the
     * other, across the gap. */
    struct tm const before = local_of(2026, 2, 29, 1, 30);
    struct tm const after  = local_of(2026, 2, 29, 3, 30);
    int64_t         utc    = 0;

    zone_in(TZ_MADRID);
    TEST_ASSERT_TRUE(kgui_clock_step_to(&before, KGUI_CLOCK_HOUR, 1, &utc));
    TEST_ASSERT_EQUAL_INT64(MAR29_0130Z, utc);
    TEST_ASSERT_TRUE(kgui_clock_step_to(&after, KGUI_CLOCK_HOUR, -1, &utc));
    TEST_ASSERT_EQUAL_INT64(MAR29_0030Z, utc);
}

void test_clock_step_to_refuses_a_step_that_does_nothing(void)
{
    struct tm const top   = local_of(KGUI_CLOCK_YEAR_MAX, 5, 1, 12, 0);
    struct tm const t     = local_of(2026, 10, 16, 14, 0);
    struct tm const early = local_of(KGUI_CLOCK_YEAR_MIN, 0, 1, 10, 0);
    int64_t         utc   = 0;

    TEST_ASSERT_FALSE(kgui_clock_step_to(&top, KGUI_CLOCK_YEAR, 1, &utc));
    TEST_ASSERT_FALSE(kgui_clock_step_to(NULL, KGUI_CLOCK_HOUR, 1, &utc));
    TEST_ASSERT_FALSE(kgui_clock_step_to(&t, KGUI_CLOCK_HOUR, 1, NULL));
    TEST_ASSERT_TRUE(kgui_clock_step_to(&t, KGUI_CLOCK_HOUR, 1, &utc));
    TEST_ASSERT_EQUAL_INT64(NOV16_14Z + HOUR_S, utc);
    /* 1 January 2026 at 10:00 in Tokyo is 01:00Z: an hour back is the first
     * second of 2026 in UTC, which the clock does not count as known. */
    zone_in(TZ_TOKYO);
    TEST_ASSERT_FALSE(kgui_clock_step_to(&early, KGUI_CLOCK_HOUR, -1, &utc));
}

/* ---- Whose time the clock is ---- */

void test_clock_hand_holds_while_nobody_else_wrote(void)
{
    uint64_t const at30 = ((uint64_t)NOV16_14Z + 30ULL) * SECOND_US;

    /* Set at 14:00:00Z, read 30 s later. */
    TEST_ASSERT_TRUE(kgui_clock_hand_holds(NOV16_14Z, 30000U, at30));
    TEST_ASSERT_TRUE(kgui_clock_hand_holds(NOV16_14Z, 30000U, at30 + (SECOND_US / 2ULL)));
    TEST_ASSERT_TRUE(kgui_clock_hand_holds(NOV16_14Z, 30000U, at30 - (SECOND_US / 2ULL)));
    TEST_ASSERT_TRUE(kgui_clock_hand_holds(NOV16_14Z, 30000U, at30 + SECOND_US));
    /* A `time set`, the Kode channel or a time server wrote it since. */
    TEST_ASSERT_FALSE(kgui_clock_hand_holds(NOV16_14Z, 30000U, at30 + (2ULL * SECOND_US)));
    TEST_ASSERT_FALSE(kgui_clock_hand_holds(NOV16_14Z, 30000U, at30 - (2ULL * SECOND_US)));
    TEST_ASSERT_FALSE(kgui_clock_hand_holds(NOV16_14Z, 30000U,
                                            at30 - ((uint64_t)HOUR_S * SECOND_US)));
    TEST_ASSERT_FALSE(kgui_clock_hand_holds(-1LL, 0U, 0ULL));
}

void test_clock_utc_of_refuses_what_reads_as_never_set(void)
{
    /* The first hours of 2026 in Tokyo are still 2025 in UTC. */
    struct tm const early = local_of(KGUI_CLOCK_YEAR_MIN, 0, 1, 5, 0);
    struct tm const later = local_of(KGUI_CLOCK_YEAR_MIN, 0, 1, 10, 0);
    int64_t utc = 0;

    zone_in(TZ_TOKYO);
    TEST_ASSERT_FALSE(kgui_clock_utc_of(&early, 0U, &utc));
    TEST_ASSERT_TRUE(kgui_clock_utc_of(&later, 0U, &utc));
    TEST_ASSERT_TRUE((int64_t)KODE_CLOCK_KNOWN_AFTER_S < utc);
    TEST_ASSERT_FALSE(kgui_clock_utc_of(NULL, 0U, &utc));
    TEST_ASSERT_FALSE(kgui_clock_utc_of(&later, 0U, NULL));
}

void test_clock_utc_of_holds_the_clock_s_own_ends(void)
{
    struct tm const first     = local_of(KGUI_CLOCK_YEAR_MIN, 0, 1, 0, 0);
    struct tm const next      = local_of(KGUI_CLOCK_YEAR_MIN, 0, 1, 0, 1);
    struct tm const last      = local_of(KGUI_CLOCK_YEAR_MAX, 11, 31, 23, 59);
    struct tm const last_west = local_of(KGUI_CLOCK_YEAR_MAX, 11, 31, 20, 0);
    int64_t utc = 0;

    /* After KODE_CLOCK_KNOWN_AFTER_S, never on it: khal_time_wall_known()'s. */
    TEST_ASSERT_FALSE(kgui_clock_utc_of(&first, 0U, &utc));
    TEST_ASSERT_TRUE(kgui_clock_utc_of(&next, 0U, &utc));
    TEST_ASSERT_TRUE(kgui_clock_utc_of(&last, 0U, &utc));
    /* 20:00 on the last night of 2105 in Honolulu is 2106 in UTC. */
    zone_in("HST10");
    TEST_ASSERT_FALSE(kgui_clock_utc_of(&last_west, 0U, &utc));
}

/* ---- The zones ---- */

void test_clock_zone_find_and_label(void)
{
    uint8_t const n = KGUI_CLOCK_ZONES;

    TEST_ASSERT_EQUAL_UINT8(21U, n);
    TEST_ASSERT_EQUAL_STRING("UTC", kgui_clock_zone_label(""));
    TEST_ASSERT_EQUAL_STRING("UTC", kgui_clock_zone_label(KGUI_CLOCK_ZONE_UTC));
    TEST_ASSERT_EQUAL_STRING("Madrid, Paris", kgui_clock_zone_label(TZ_MADRID));
    TEST_ASSERT_EQUAL_STRING("Custom", kgui_clock_zone_label("EST5"));
    TEST_ASSERT_EQUAL_STRING("Custom", kgui_clock_zone_label(NULL));
    TEST_ASSERT_EQUAL_UINT8(n, kgui_clock_zone_find("EST5"));
    TEST_ASSERT_EQUAL_UINT8(kgui_clock_zone_find(KGUI_CLOCK_ZONE_UTC), kgui_clock_zone_find(""));
    TEST_ASSERT_EQUAL_STRING(TZ_MADRID, kgui_clock_zone_tz(kgui_clock_zone_find(TZ_MADRID)));
    TEST_ASSERT_EQUAL_STRING(KGUI_CLOCK_ZONE_UTC, kgui_clock_zone_tz(n));
}

void test_clock_every_zone_finds_itself(void)
{
    uint8_t const n = KGUI_CLOCK_ZONES;

    for ( uint8_t i = 0U; i < n; ++i )
    {
        const char * const tz = kgui_clock_zone_tz(i);

        TEST_ASSERT_TRUE(ZONE_MAX >= strlen(tz));
        TEST_ASSERT_EQUAL_UINT8(i, kgui_clock_zone_find(tz));
        TEST_ASSERT_NOT_EQUAL(0, strcmp("Custom", kgui_clock_zone_label(tz)));
    }
}

void test_clock_zone_step_goes_round_both_ways(void)
{
    uint8_t const n   = KGUI_CLOCK_ZONES;
    uint8_t const utc = kgui_clock_zone_find("");

    TEST_ASSERT_EQUAL_STRING("London, Lisbon",
                             kgui_clock_zone_label(kgui_clock_zone_tz(kgui_clock_zone_step("", 1))));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(utc + 1U), kgui_clock_zone_step(KGUI_CLOCK_ZONE_UTC, 1));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(utc - 1U), kgui_clock_zone_step(KGUI_CLOCK_ZONE_UTC, -1));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_clock_zone_step(kgui_clock_zone_tz((uint8_t)(n - 1U)), 1));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(n - 1U), kgui_clock_zone_step(kgui_clock_zone_tz(0U), -1));
    /* A zone the console typed steps from UTC's place. */
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(utc + 1U), kgui_clock_zone_step("EST5", 1));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(utc - 1U), kgui_clock_zone_step("EST5", -1));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(utc + 1U), kgui_clock_zone_step(NULL, 0));
}

/* ---- What the face asks for ---- */

void test_clock_ask_one_thing_at_a_time(void)
{
    /* Never told the time: the time, whatever the zone. */
    TEST_ASSERT_EQUAL_INT(KGUI_CLOCK_ASK_TIME, kgui_clock_ask(false, false, false, false));
    TEST_ASSERT_EQUAL_INT(KGUI_CLOCK_ASK_TIME, kgui_clock_ask(false, true, false, false));
    TEST_ASSERT_EQUAL_INT(KGUI_CLOCK_ASK_TIME, kgui_clock_ask(false, true, true, false));
    /* A time server answered and nobody chose a zone: the zone. */
    TEST_ASSERT_EQUAL_INT(KGUI_CLOCK_ASK_ZONE, kgui_clock_ask(true, true, false, false));
    /* Both known: nothing. */
    TEST_ASSERT_EQUAL_INT(KGUI_CLOCK_ASK_NONE, kgui_clock_ask(true, true, true, false));
    /* Before AO_Boot has said which zone is in force, nothing is guessed. */
    TEST_ASSERT_EQUAL_INT(KGUI_CLOCK_ASK_NONE, kgui_clock_ask(true, false, false, false));
}

void test_clock_ask_nothing_at_rest(void)
{
    TEST_ASSERT_EQUAL_INT(KGUI_CLOCK_ASK_NONE, kgui_clock_ask(false, false, false, true));
    TEST_ASSERT_EQUAL_INT(KGUI_CLOCK_ASK_NONE, kgui_clock_ask(true, true, false, true));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_clock_limits_are_the_system_ones);
    RUN_TEST(test_clock_utc_end_is_the_year_after_the_last);
    RUN_TEST(test_clock_month_days);
    RUN_TEST(test_clock_hour_and_minute_go_round_and_keep_the_day);
    RUN_TEST(test_clock_day_goes_round_inside_its_month);
    RUN_TEST(test_clock_month_goes_round_and_trims_the_day);
    RUN_TEST(test_clock_year_stops_at_both_ends_and_trims_the_29th);
    RUN_TEST(test_clock_step_refuses_what_is_no_row);
    RUN_TEST(test_clock_utc_of_reads_in_the_zone_in_force);
    RUN_TEST(test_clock_a_hand_set_hour_survives_a_change_of_zone);
    RUN_TEST(test_clock_an_hour_step_crosses_the_spring_gap_both_ways);
    RUN_TEST(test_clock_step_to_refuses_a_step_that_does_nothing);
    RUN_TEST(test_clock_hand_holds_while_nobody_else_wrote);
    RUN_TEST(test_clock_utc_of_holds_the_clock_s_own_ends);
    RUN_TEST(test_clock_utc_of_refuses_what_reads_as_never_set);
    RUN_TEST(test_clock_zone_find_and_label);
    RUN_TEST(test_clock_every_zone_finds_itself);
    RUN_TEST(test_clock_zone_step_goes_round_both_ways);
    RUN_TEST(test_clock_ask_one_thing_at_a_time);
    RUN_TEST(test_clock_ask_nothing_at_rest);
    return UNITY_END();
}
