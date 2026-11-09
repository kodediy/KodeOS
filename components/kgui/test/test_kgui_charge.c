/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_charge.c
 * @brief Host tests of what a board on a cable says it is doing (kgui_charge.h).
 */
#include <stdint.h>

#include "unity.h"

#include "kdrv_bq25896.h"
#include "kgui_charge.h"

#define NTC_NONE 0U
#define NTC_WARM 2U
#define NTC_COOL 3U
#define NTC_COLD 5U
#define NTC_HOT  6U

static char s_words[32];

/** @brief Unity's hook before each case. */
void setUp(void)
{
    s_words[0] = '\0';
}

/** @brief Unity's hook after each case: nothing to put away. */
void tearDown(void)
{
}

void test_no_cable_is_no_cable_whatever_else_it_reads(void)
{
    TEST_ASSERT_EQUAL_INT(KGUI_CHARGE_NO_CABLE,
                          kgui_charge_state(false, KDRV_BQ_CHRG_FAST, NTC_HOT, 40U));
}

/* The finding: on a hot pack the glass said "Charging, 40 %" for an hour. */
void test_a_charge_held_for_temperature_is_not_charging(void)
{
    TEST_ASSERT_EQUAL_INT(KGUI_CHARGE_WARM, kgui_charge_state(true, KDRV_BQ_CHRG_NONE, NTC_WARM, 40U));
    TEST_ASSERT_EQUAL_INT(KGUI_CHARGE_WARM, kgui_charge_state(true, KDRV_BQ_CHRG_NONE, NTC_HOT, 40U));
    TEST_ASSERT_EQUAL_INT(KGUI_CHARGE_COLD, kgui_charge_state(true, KDRV_BQ_CHRG_NONE, NTC_COOL, 40U));
    TEST_ASSERT_EQUAL_INT(KGUI_CHARGE_COLD, kgui_charge_state(true, KDRV_BQ_CHRG_NONE, NTC_COLD, 40U));
}

/* What the chip says it does outranks a reading of the TS pin on its way out. */
void test_a_charge_running_is_charging_and_a_done_one_is_full(void)
{
    TEST_ASSERT_EQUAL_INT(KGUI_CHARGE_ON, kgui_charge_state(true, KDRV_BQ_CHRG_FAST, NTC_NONE, 40U));
    TEST_ASSERT_EQUAL_INT(KGUI_CHARGE_ON, kgui_charge_state(true, KDRV_BQ_CHRG_PRE, NTC_NONE, 2U));
    TEST_ASSERT_EQUAL_INT(KGUI_CHARGE_FULL, kgui_charge_state(true, KDRV_BQ_CHRG_DONE, NTC_NONE, 99U));
    TEST_ASSERT_EQUAL_INT(KGUI_CHARGE_FULL, kgui_charge_state(true, KDRV_BQ_CHRG_NONE, NTC_NONE, 100U));
}

void test_a_cable_with_nothing_happening_does_not_claim_a_charge(void)
{
    TEST_ASSERT_EQUAL_INT(KGUI_CHARGE_IDLE, kgui_charge_state(true, KDRV_BQ_CHRG_NONE, NTC_NONE, 40U));
    kgui_charge_words(KGUI_CHARGE_IDLE, 40U, s_words, sizeof(s_words));
    TEST_ASSERT_EQUAL_STRING("Plugged in", s_words);
}

void test_only_the_temperature_states_are_held(void)
{
    TEST_ASSERT_TRUE(kgui_charge_held(KGUI_CHARGE_WARM));
    TEST_ASSERT_TRUE(kgui_charge_held(KGUI_CHARGE_COLD));
    TEST_ASSERT_FALSE(kgui_charge_held(KGUI_CHARGE_ON));
    TEST_ASSERT_FALSE(kgui_charge_held(KGUI_CHARGE_IDLE));
    TEST_ASSERT_FALSE(kgui_charge_held(KGUI_CHARGE_NO_CABLE));
}

void test_a_plug_that_finds_a_charge_says_so_at_once(void)
{
    bool owed = false;

    TEST_ASSERT_TRUE(kgui_charge_notice(KGUI_CHARGE_NO_CABLE, KGUI_CHARGE_ON, true, &owed, 42U,
                                        s_words, sizeof(s_words)));
    TEST_ASSERT_EQUAL_STRING("Charging, 42 %", s_words);
    TEST_ASSERT_FALSE(owed);
}

void test_a_plug_on_a_hot_pack_says_why(void)
{
    bool owed = false;

    TEST_ASSERT_TRUE(kgui_charge_notice(KGUI_CHARGE_NO_CABLE, KGUI_CHARGE_WARM, true, &owed, 42U,
                                        s_words, sizeof(s_words)));
    TEST_ASSERT_EQUAL_STRING("Too warm to charge", s_words);
}

/* The chip takes a poll to start: the first look is not a verdict. */
void test_a_plug_that_sees_nothing_waits_one_reading(void)
{
    bool owed = false;

    TEST_ASSERT_FALSE(kgui_charge_notice(KGUI_CHARGE_NO_CABLE, KGUI_CHARGE_IDLE, true, &owed, 42U,
                                         s_words, sizeof(s_words)));
    TEST_ASSERT_TRUE(owed);
    TEST_ASSERT_TRUE(kgui_charge_notice(KGUI_CHARGE_IDLE, KGUI_CHARGE_ON, false, &owed, 42U,
                                        s_words, sizeof(s_words)));
    TEST_ASSERT_EQUAL_STRING("Charging, 42 %", s_words);
    TEST_ASSERT_FALSE(owed);
}

void test_a_plug_that_never_starts_says_only_what_is_true(void)
{
    bool owed = false;

    (void)kgui_charge_notice(KGUI_CHARGE_NO_CABLE, KGUI_CHARGE_IDLE, true, &owed, 42U,
                             s_words, sizeof(s_words));
    TEST_ASSERT_TRUE(kgui_charge_notice(KGUI_CHARGE_IDLE, KGUI_CHARGE_IDLE, false, &owed, 42U,
                                        s_words, sizeof(s_words)));
    TEST_ASSERT_EQUAL_STRING("Plugged in", s_words);
    TEST_ASSERT_FALSE(owed);
}

void test_a_cable_gone_owes_nothing(void)
{
    bool owed = true;

    TEST_ASSERT_FALSE(kgui_charge_notice(KGUI_CHARGE_IDLE, KGUI_CHARGE_NO_CABLE, false, &owed, 42U,
                                         s_words, sizeof(s_words)));
    TEST_ASSERT_FALSE(owed);
}

void test_a_charge_held_on_the_way_is_said_once(void)
{
    bool owed = false;

    TEST_ASSERT_TRUE(kgui_charge_notice(KGUI_CHARGE_ON, KGUI_CHARGE_COLD, false, &owed, 60U,
                                        s_words, sizeof(s_words)));
    TEST_ASSERT_EQUAL_STRING("Too cold to charge", s_words);
    TEST_ASSERT_FALSE(kgui_charge_notice(KGUI_CHARGE_COLD, KGUI_CHARGE_COLD, false, &owed, 60U,
                                         s_words, sizeof(s_words)));
}

void test_a_charge_that_runs_on_says_nothing(void)
{
    bool owed = false;

    TEST_ASSERT_FALSE(kgui_charge_notice(KGUI_CHARGE_ON, KGUI_CHARGE_ON, false, &owed, 60U,
                                         s_words, sizeof(s_words)));
    TEST_ASSERT_FALSE(kgui_charge_notice(KGUI_CHARGE_ON, KGUI_CHARGE_FULL, false, &owed, 100U,
                                         s_words, sizeof(s_words)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_cable_is_no_cable_whatever_else_it_reads);
    RUN_TEST(test_a_charge_held_for_temperature_is_not_charging);
    RUN_TEST(test_a_charge_running_is_charging_and_a_done_one_is_full);
    RUN_TEST(test_a_cable_with_nothing_happening_does_not_claim_a_charge);
    RUN_TEST(test_only_the_temperature_states_are_held);
    RUN_TEST(test_a_plug_that_finds_a_charge_says_so_at_once);
    RUN_TEST(test_a_plug_on_a_hot_pack_says_why);
    RUN_TEST(test_a_plug_that_sees_nothing_waits_one_reading);
    RUN_TEST(test_a_plug_that_never_starts_says_only_what_is_true);
    RUN_TEST(test_a_cable_gone_owes_nothing);
    RUN_TEST(test_a_charge_held_on_the_way_is_said_once);
    RUN_TEST(test_a_charge_that_runs_on_says_nothing);
    return UNITY_END();
}
