/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_fault.c
 * @brief Host tests of the fault notice: its record, when its card rises and
 *        what it offers (kgui_fault.h).
 */
#include <string.h>

#include "unity.h"

#include "kgui_fault.h"

#define BOOT_MS   1200U   /**< The time of the boot's begin in these cases. */
#define INPUT_MS  5000U   /**< The time of the input after the card was taken down. */

void setUp(void)
{
}

void tearDown(void)
{
}

/** @brief A record of @p cls, with or without a dump. */
static kgui_fault_rec_t rec(kgui_fault_class_t cls, bool dump)
{
    kgui_fault_rec_t r;

    r.cls  = (uint8_t)cls;
    r.dump = dump;
    return r;
}

/** @brief A net with everything on: the offer in the build, a dump, a build that sends, Wi-Fi on,
 *         the channel on its way, a link, nothing waited. */
static kgui_fault_net_t net_waiting(void)
{
    kgui_fault_net_t n;

    n.offered   = true;
    n.dump      = true;
    n.built     = true;
    n.wifi_on   = true;
    n.chan      = (uint8_t)KGUI_FAULT_CHAN_COMING;
    n.link      = true;
    n.waited_ms = 0U;
    return n;
}

/** @brief A notice begun from a boot with nothing kept and a crash with a dump. */
static kgui_fault_t notice_owed(void)
{
    kgui_fault_t f;

    (void)memset(&f, 0, sizeof(f));
    kgui_fault_begin(&f, false, 0U, rec(KGUI_FAULT_CRASHED, true), BOOT_MS);
    return f;
}

void test_a_record_packs_and_unpacks(void)
{
    kgui_fault_rec_t back;

    TEST_ASSERT_EQUAL_HEX8(0x81U, kgui_fault_pack(rec(KGUI_FAULT_CRASHED, true)));
    TEST_ASSERT_EQUAL_HEX8(0x02U, kgui_fault_pack(rec(KGUI_FAULT_HUNG, false)));

    (void)memset(&back, 0, sizeof(back));
    TEST_ASSERT_TRUE(kgui_fault_unpack(0x81U, &back));
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_CRASHED, back.cls);
    TEST_ASSERT_TRUE(back.dump);

    TEST_ASSERT_TRUE(kgui_fault_unpack(0x02U, &back));
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_HUNG, back.cls);
    TEST_ASSERT_FALSE(back.dump);
}

void test_a_byte_that_is_no_record_is_refused(void)
{
    kgui_fault_rec_t back = rec(KGUI_FAULT_HUNG, true);

    TEST_ASSERT_FALSE(kgui_fault_unpack(0x00U, &back));
    TEST_ASSERT_FALSE(kgui_fault_unpack(0x03U, &back));
    TEST_ASSERT_FALSE(kgui_fault_unpack(0x41U, &back));
    TEST_ASSERT_FALSE(kgui_fault_unpack(0xFFU, &back));
    /* A refusal leaves the record as it was. */
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_HUNG, back.cls);
    TEST_ASSERT_TRUE(back.dump);
    TEST_ASSERT_FALSE(kgui_fault_unpack(0x81U, NULL));
}

void test_the_words_follow_the_class(void)
{
    TEST_ASSERT_EQUAL_STRING("It crashed", kgui_fault_words((uint8_t)KGUI_FAULT_CRASHED));
    TEST_ASSERT_EQUAL_STRING("It stopped responding", kgui_fault_words((uint8_t)KGUI_FAULT_HUNG));
    TEST_ASSERT_EQUAL_STRING("", kgui_fault_words((uint8_t)KGUI_FAULT_NONE));
    TEST_ASSERT_EQUAL_STRING("", kgui_fault_words(3U));
}

void test_no_dump_rises_at_once_with_ok_alone(void)
{
    kgui_fault_net_t n = net_waiting();

    n.dump = false;
    TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_OK, kgui_fault_verdict(&n));
}

void test_a_build_that_cannot_send_rises_at_once(void)
{
    kgui_fault_net_t n = net_waiting();

    n.built = false;
    TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_OK, kgui_fault_verdict(&n));
}

void test_a_build_that_offers_no_report_rises_at_once_with_ok_alone(void)
{
    /* Every network that would otherwise offer the send or wait for it. */
    uint8_t const chans[3] = { (uint8_t)KGUI_FAULT_CHAN_COMING, (uint8_t)KGUI_FAULT_CHAN_UP,
                               (uint8_t)KGUI_FAULT_CHAN_NEVER };
    uint32_t      i;
    uint32_t      bits;

    for ( i = 0U; i < 3U; i++ )
    {
        for ( bits = 0U; bits < 16U; bits++ )
        {
            kgui_fault_net_t n = net_waiting();

            n.offered = false;
            n.chan    = chans[i];
            n.dump    = (0U != (bits & 1U));
            n.built   = (0U != (bits & 2U));
            n.wifi_on = (0U != (bits & 4U));
            n.link    = (0U != (bits & 8U));
            TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_OK, kgui_fault_verdict(&n));
        }
    }
}

void test_a_build_that_offers_no_report_waits_for_nothing(void)
{
    kgui_fault_net_t n = net_waiting();

    /* The channel on its way and nothing waited: the notice rises at once. */
    n.offered = false;
    TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_OK, kgui_fault_verdict(&n));
    n.chan = (uint8_t)KGUI_FAULT_CHAN_UP;
    TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_OK, kgui_fault_verdict(&n));
}

void test_the_channel_up_with_the_link_offers_the_send(void)
{
    kgui_fault_net_t n = net_waiting();

    n.chan = (uint8_t)KGUI_FAULT_CHAN_UP;
    TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_SEND, kgui_fault_verdict(&n));
}

void test_the_channel_up_with_no_link_is_ok_alone(void)
{
    kgui_fault_net_t n = net_waiting();

    n.chan = (uint8_t)KGUI_FAULT_CHAN_UP;
    n.link = false;
    TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_OK, kgui_fault_verdict(&n));
}

void test_wifi_off_is_ok_alone_at_once(void)
{
    kgui_fault_net_t n = net_waiting();

    n.wifi_on = false;
    TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_OK, kgui_fault_verdict(&n));
}

void test_a_channel_that_will_not_come_is_ok_alone_at_once(void)
{
    kgui_fault_net_t n = net_waiting();

    n.chan = (uint8_t)KGUI_FAULT_CHAN_NEVER;
    TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_OK, kgui_fault_verdict(&n));
}

void test_a_channel_on_its_way_is_waited_for(void)
{
    kgui_fault_net_t n = net_waiting();

    TEST_ASSERT_EQUAL(KGUI_FAULT_WAIT, kgui_fault_verdict(&n));
    n.waited_ms = 29999U;
    TEST_ASSERT_EQUAL(KGUI_FAULT_WAIT, kgui_fault_verdict(&n));
    n.waited_ms = 30000U;
    TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_OK, kgui_fault_verdict(&n));
    TEST_ASSERT_EQUAL_UINT32(30000U, KGUI_FAULT_NET_WAIT_MS);
    TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_OK, kgui_fault_verdict(NULL));
}

void test_a_fault_boot_writes_its_record_and_is_due(void)
{
    kgui_fault_t f = notice_owed();

    TEST_ASSERT_TRUE(f.owed);
    TEST_ASSERT_TRUE(f.due);
    TEST_ASSERT_TRUE(f.store);
    TEST_ASSERT_EQUAL_UINT32(BOOT_MS, f.due_ms);
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_CRASHED, f.rec.cls);
    TEST_ASSERT_TRUE(f.rec.dump);
}

void test_a_newer_fault_replaces_the_record(void)
{
    kgui_fault_t f;

    (void)memset(&f, 0, sizeof(f));
    kgui_fault_begin(&f, true, 0x02U, rec(KGUI_FAULT_CRASHED, true), BOOT_MS);
    TEST_ASSERT_TRUE(f.owed);
    TEST_ASSERT_TRUE(f.due);
    TEST_ASSERT_TRUE(f.store);
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_CRASHED, f.rec.cls);
    TEST_ASSERT_TRUE(f.rec.dump);
}

void test_a_kept_record_is_due_at_a_boot_that_is_no_fault(void)
{
    kgui_fault_t f;

    (void)memset(&f, 0, sizeof(f));
    kgui_fault_begin(&f, true, 0x81U, rec(KGUI_FAULT_NONE, false), BOOT_MS);
    TEST_ASSERT_TRUE(f.owed);
    TEST_ASSERT_TRUE(f.due);
    TEST_ASSERT_FALSE(f.store);
    TEST_ASSERT_EQUAL_UINT32(BOOT_MS, f.due_ms);
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_CRASHED, f.rec.cls);
    TEST_ASSERT_TRUE(f.rec.dump);
}

void test_a_boot_with_nothing_kept_and_no_fault_owes_nothing(void)
{
    kgui_fault_t f;

    (void)memset(&f, 0xFF, sizeof(f));
    kgui_fault_begin(&f, false, 0x81U, rec(KGUI_FAULT_NONE, false), BOOT_MS);
    TEST_ASSERT_FALSE(f.owed);
    TEST_ASSERT_FALSE(f.due);
    TEST_ASSERT_FALSE(f.store);

    /* A kept byte that is no record owes nothing either. */
    kgui_fault_begin(&f, true, 0x41U, rec(KGUI_FAULT_NONE, false), BOOT_MS);
    TEST_ASSERT_FALSE(f.owed);
    TEST_ASSERT_FALSE(f.due);
    TEST_ASSERT_FALSE(f.store);
}

void test_a_boot_class_that_is_no_class_owes_nothing_of_its_own(void)
{
    kgui_fault_rec_t const boot = { 3U, true };
    kgui_fault_t           f;

    (void)memset(&f, 0xFF, sizeof(f));
    kgui_fault_begin(&f, false, 0U, boot, BOOT_MS);
    TEST_ASSERT_FALSE(f.owed);
    TEST_ASSERT_FALSE(f.due);
    TEST_ASSERT_FALSE(f.store);

    /* With a record kept, the kept one is owed, and nothing is stored. */
    kgui_fault_begin(&f, true, 0x81U, boot, BOOT_MS);
    TEST_ASSERT_TRUE(f.owed);
    TEST_ASSERT_TRUE(f.due);
    TEST_ASSERT_FALSE(f.store);
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_CRASHED, f.rec.cls);
    TEST_ASSERT_TRUE(f.rec.dump);
}

void test_a_card_taken_down_waits_for_an_input(void)
{
    kgui_fault_t f = notice_owed();

    kgui_fault_taken_down(&f);
    TEST_ASSERT_TRUE(f.owed);
    TEST_ASSERT_FALSE(f.due);
    kgui_fault_input(&f, INPUT_MS);
    TEST_ASSERT_TRUE(f.due);
    TEST_ASSERT_EQUAL_UINT32(INPUT_MS, f.due_ms);
    /* A later input does not move the time it became due. */
    kgui_fault_input(&f, INPUT_MS + 1000U);
    TEST_ASSERT_EQUAL_UINT32(INPUT_MS, f.due_ms);
}

void test_an_answer_ends_the_notice(void)
{
    kgui_fault_t f = notice_owed();

    kgui_fault_answered(&f);
    TEST_ASSERT_FALSE(f.owed);
    TEST_ASSERT_FALSE(f.due);
    kgui_fault_input(&f, INPUT_MS);
    TEST_ASSERT_FALSE(f.owed);
    TEST_ASSERT_FALSE(f.due);
}

void test_only_the_notice_and_its_result_yield(void)
{
    TEST_ASSERT_TRUE(kgui_fault_yields((uint8_t)KGUI_FAULT_FACE_NOTICE));
    TEST_ASSERT_FALSE(kgui_fault_yields((uint8_t)KGUI_FAULT_FACE_SENDING));
    TEST_ASSERT_TRUE(kgui_fault_yields((uint8_t)KGUI_FAULT_FACE_SENT));
    TEST_ASSERT_TRUE(kgui_fault_yields((uint8_t)KGUI_FAULT_FACE_FAILED));
}

void test_a_record_with_a_dump_gone_rises_with_ok_alone(void)
{
    kgui_fault_net_t n = net_waiting();

    n.chan = (uint8_t)KGUI_FAULT_CHAN_UP;
    n.link = true;
    n.dump = false;
    TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_OK, kgui_fault_verdict(&n));
}

void test_the_sends_timings(void)
{
    TEST_ASSERT_EQUAL_UINT32(1500U, KGUI_FAULT_SENT_MS);
    TEST_ASSERT_EQUAL_UINT32(75000U, KGUI_FAULT_SEND_GUARD_MS);
}

void test_a_radio_not_heard_yet_is_waited_for(void)
{
    kgui_fault_net_t n = net_waiting();

    /* What the fields read before the first state of this boot. */
    n.wifi_on = false;
    n.chan    = (uint8_t)KGUI_FAULT_CHAN_NEVER;
    kgui_fault_net_unheard(&n, false, false);
    TEST_ASSERT_TRUE(n.wifi_on);
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_CHAN_COMING, n.chan);
    TEST_ASSERT_EQUAL(KGUI_FAULT_WAIT, kgui_fault_verdict(&n));
    n.waited_ms = KGUI_FAULT_NET_WAIT_MS;
    TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_OK, kgui_fault_verdict(&n));
}

void test_a_radio_heard_is_taken_as_it_is(void)
{
    kgui_fault_net_t n = net_waiting();

    n.wifi_on = false;
    n.chan    = (uint8_t)KGUI_FAULT_CHAN_NEVER;
    kgui_fault_net_unheard(&n, true, true);
    TEST_ASSERT_FALSE(n.wifi_on);
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_CHAN_NEVER, n.chan);
    TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_OK, kgui_fault_verdict(&n));

    /* Wi-Fi heard off decides at once, the channel unheard or not. */
    n.chan = (uint8_t)KGUI_FAULT_CHAN_NEVER;
    kgui_fault_net_unheard(&n, true, false);
    TEST_ASSERT_FALSE(n.wifi_on);
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_CHAN_COMING, n.chan);
    TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_OK, kgui_fault_verdict(&n));

    /* A channel heard to be up is kept up while Wi-Fi is not heard. */
    n.chan = (uint8_t)KGUI_FAULT_CHAN_UP;
    kgui_fault_net_unheard(&n, false, true);
    TEST_ASSERT_TRUE(n.wifi_on);
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_CHAN_UP, n.chan);
    TEST_ASSERT_EQUAL(KGUI_FAULT_RISE_SEND, kgui_fault_verdict(&n));
    kgui_fault_net_unheard(NULL, false, false);
}

void test_a_sends_end_has_its_words(void)
{
    TEST_ASSERT_EQUAL_STRING("Report sent", kgui_fault_send_words((uint8_t)KGUI_FAULT_SEND_OK));
    TEST_ASSERT_EQUAL_STRING("The service refused it",
                             kgui_fault_send_words((uint8_t)KGUI_FAULT_SEND_REFUSED));
    TEST_ASSERT_EQUAL_STRING("No answer", kgui_fault_send_words((uint8_t)KGUI_FAULT_SEND_NO_ANSWER));
    TEST_ASSERT_EQUAL_STRING("Busy. Try again", kgui_fault_send_words((uint8_t)KGUI_FAULT_SEND_BUSY));
    TEST_ASSERT_EQUAL_STRING("No connection", kgui_fault_send_words((uint8_t)KGUI_FAULT_SEND_CHANNEL));
    TEST_ASSERT_EQUAL_STRING("Nothing to send", kgui_fault_send_words((uint8_t)KGUI_FAULT_SEND_NONE));
    /* An end this build does not know reads as no answer. */
    TEST_ASSERT_EQUAL_STRING("No answer", kgui_fault_send_words(6U));
    TEST_ASSERT_EQUAL_STRING("No answer", kgui_fault_send_words(0xFFU));
}

void test_a_sending_card_takes_the_answer(void)
{
    uint8_t face = (uint8_t)KGUI_FAULT_FACE_SENDING;

    TEST_ASSERT_TRUE(kgui_fault_on_answer(&face, true, (uint8_t)KGUI_FAULT_SEND_OK));
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_FACE_SENT, face);

    face = (uint8_t)KGUI_FAULT_FACE_SENDING;
    TEST_ASSERT_TRUE(kgui_fault_on_answer(&face, true, (uint8_t)KGUI_FAULT_SEND_REFUSED));
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_FACE_FAILED, face);

    face = (uint8_t)KGUI_FAULT_FACE_SENDING;
    TEST_ASSERT_TRUE(kgui_fault_on_answer(&face, true, 0xFFU));
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_FACE_FAILED, face);
}

void test_an_answer_with_no_sending_card_is_dropped(void)
{
    uint8_t face = (uint8_t)KGUI_FAULT_FACE_SENDING;

    /* The card was closed while it sent. */
    TEST_ASSERT_FALSE(kgui_fault_on_answer(&face, false, (uint8_t)KGUI_FAULT_SEND_OK));
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_FACE_SENDING, face);

    /* The guard already turned it to the failure. */
    face = (uint8_t)KGUI_FAULT_FACE_FAILED;
    TEST_ASSERT_FALSE(kgui_fault_on_answer(&face, true, (uint8_t)KGUI_FAULT_SEND_OK));
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_FACE_FAILED, face);

    face = (uint8_t)KGUI_FAULT_FACE_NOTICE;
    TEST_ASSERT_FALSE(kgui_fault_on_answer(&face, true, (uint8_t)KGUI_FAULT_SEND_OK));
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_FACE_NOTICE, face);

    face = (uint8_t)KGUI_FAULT_FACE_SENT;
    TEST_ASSERT_FALSE(kgui_fault_on_answer(&face, true, (uint8_t)KGUI_FAULT_SEND_REFUSED));
    TEST_ASSERT_EQUAL_UINT8(KGUI_FAULT_FACE_SENT, face);

    TEST_ASSERT_FALSE(kgui_fault_on_answer(NULL, true, (uint8_t)KGUI_FAULT_SEND_OK));
}

void test_a_send_with_no_answer_fails_at_the_guard(void)
{
    uint8_t const sending = (uint8_t)KGUI_FAULT_FACE_SENDING;

    TEST_ASSERT_EQUAL(KGUI_FAULT_STEP_STAY, kgui_fault_face_step(sending, 0U));
    TEST_ASSERT_EQUAL(KGUI_FAULT_STEP_STAY,
                      kgui_fault_face_step(sending, KGUI_FAULT_SEND_GUARD_MS - 1U));
    TEST_ASSERT_EQUAL(KGUI_FAULT_STEP_FAIL,
                      kgui_fault_face_step(sending, KGUI_FAULT_SEND_GUARD_MS));
}

void test_the_sent_card_leaves_by_itself(void)
{
    uint8_t const sent = (uint8_t)KGUI_FAULT_FACE_SENT;

    TEST_ASSERT_EQUAL(KGUI_FAULT_STEP_STAY, kgui_fault_face_step(sent, KGUI_FAULT_SENT_MS - 1U));
    TEST_ASSERT_EQUAL(KGUI_FAULT_STEP_CLOSE, kgui_fault_face_step(sent, KGUI_FAULT_SENT_MS));
}

void test_the_notice_and_the_failure_wait_for_a_person(void)
{
    TEST_ASSERT_EQUAL(KGUI_FAULT_STEP_STAY,
                      kgui_fault_face_step((uint8_t)KGUI_FAULT_FACE_NOTICE, 0xFFFFFFFFU));
    TEST_ASSERT_EQUAL(KGUI_FAULT_STEP_STAY,
                      kgui_fault_face_step((uint8_t)KGUI_FAULT_FACE_FAILED, 0xFFFFFFFFU));
    TEST_ASSERT_EQUAL(KGUI_FAULT_STEP_STAY, kgui_fault_face_step(4U, 0xFFFFFFFFU));
}

/** The press on the card, as kgui_fault_press() reads it. */
static kgui_fault_do_t press(kgui_fault_face_t face, bool offer, kgui_fault_pill_t pill,
                             bool remote)
{
    return kgui_fault_press((uint8_t)face, offer, (uint8_t)pill, remote);
}

void test_a_person_sends_the_report_from_the_notice(void)
{
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_SEND,
                      press(KGUI_FAULT_FACE_NOTICE, true, KGUI_FAULT_PILL_ANSWER, false));
}

void test_a_remote_send_is_refused(void)
{
    /* The first refusal: Send pressed by a line from the channel or a cron. */
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_REFUSED,
                      press(KGUI_FAULT_FACE_NOTICE, true, KGUI_FAULT_PILL_ANSWER, true));
}

void test_a_person_retries_from_the_failure(void)
{
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_RETRY,
                      press(KGUI_FAULT_FACE_FAILED, false, KGUI_FAULT_PILL_ANSWER, false));
    /* The notice's offer is no part of the failure. */
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_RETRY,
                      press(KGUI_FAULT_FACE_FAILED, true, KGUI_FAULT_PILL_ANSWER, false));
}

void test_a_remote_retry_is_refused(void)
{
    /* The second refusal: Retry pressed by a remote line. */
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_REFUSED,
                      press(KGUI_FAULT_FACE_FAILED, false, KGUI_FAULT_PILL_ANSWER, true));
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_REFUSED,
                      press(KGUI_FAULT_FACE_FAILED, true, KGUI_FAULT_PILL_ANSWER, true));
}

void test_ok_answers_the_notice_whoever_presses(void)
{
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_ANSWER,
                      press(KGUI_FAULT_FACE_NOTICE, true, KGUI_FAULT_PILL_LEFT, false));
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_ANSWER,
                      press(KGUI_FAULT_FACE_NOTICE, true, KGUI_FAULT_PILL_LEFT, true));
    /* OK alone is the answer pill. */
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_ANSWER,
                      press(KGUI_FAULT_FACE_NOTICE, false, KGUI_FAULT_PILL_ANSWER, false));
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_ANSWER,
                      press(KGUI_FAULT_FACE_NOTICE, false, KGUI_FAULT_PILL_ANSWER, true));
}

void test_close_leaves_the_failure_whoever_presses(void)
{
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_CLOSE,
                      press(KGUI_FAULT_FACE_FAILED, false, KGUI_FAULT_PILL_LEFT, false));
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_CLOSE,
                      press(KGUI_FAULT_FACE_FAILED, false, KGUI_FAULT_PILL_LEFT, true));
}

void test_the_sent_card_closes_at_any_press(void)
{
    kgui_fault_pill_t const pills[] = { KGUI_FAULT_PILL_LEFT, KGUI_FAULT_PILL_ANSWER,
                                        KGUI_FAULT_PILL_NONE };
    uint32_t i;

    for ( i = 0U; i < (sizeof(pills) / sizeof(pills[0])); i++ )
    {
        TEST_ASSERT_EQUAL(KGUI_FAULT_DO_CLOSE, press(KGUI_FAULT_FACE_SENT, false, pills[i], false));
        TEST_ASSERT_EQUAL(KGUI_FAULT_DO_CLOSE, press(KGUI_FAULT_FACE_SENT, false, pills[i], true));
    }
}

void test_a_press_off_the_pills_does_nothing_elsewhere(void)
{
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_NOTHING,
                      press(KGUI_FAULT_FACE_NOTICE, true, KGUI_FAULT_PILL_NONE, false));
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_NOTHING,
                      press(KGUI_FAULT_FACE_FAILED, false, KGUI_FAULT_PILL_NONE, false));
    /* The notice with OK alone has no left pill. */
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_NOTHING,
                      press(KGUI_FAULT_FACE_NOTICE, false, KGUI_FAULT_PILL_LEFT, false));
}

void test_no_pill_stands_while_the_report_goes(void)
{
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_NOTHING,
                      press(KGUI_FAULT_FACE_SENDING, true, KGUI_FAULT_PILL_LEFT, false));
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_NOTHING,
                      press(KGUI_FAULT_FACE_SENDING, true, KGUI_FAULT_PILL_ANSWER, false));
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_NOTHING,
                      press(KGUI_FAULT_FACE_SENDING, true, KGUI_FAULT_PILL_NONE, true));
}

void test_a_face_or_pill_that_is_none_does_nothing(void)
{
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_NOTHING,
                      kgui_fault_press(4U, true, (uint8_t)KGUI_FAULT_PILL_ANSWER, false));
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_NOTHING,
                      kgui_fault_press((uint8_t)KGUI_FAULT_FACE_NOTICE, true, 3U, false));
    TEST_ASSERT_EQUAL(KGUI_FAULT_DO_NOTHING,
                      kgui_fault_press((uint8_t)KGUI_FAULT_FACE_SENT, true, 3U, false));
}

void test_a_pill_is_named_by_what_it_shows(void)
{
    uint8_t const notice = (uint8_t)KGUI_FAULT_FACE_NOTICE;
    uint8_t const failed = (uint8_t)KGUI_FAULT_FACE_FAILED;
    uint8_t const left   = (uint8_t)KGUI_FAULT_PILL_LEFT;
    uint8_t const answer = (uint8_t)KGUI_FAULT_PILL_ANSWER;

    TEST_ASSERT_EQUAL_STRING("OK", kgui_fault_pill_word(notice, true, left));
    TEST_ASSERT_EQUAL_STRING("Send", kgui_fault_pill_word(notice, true, answer));
    TEST_ASSERT_EQUAL_STRING("OK", kgui_fault_pill_word(notice, false, answer));
    TEST_ASSERT_EQUAL_STRING("Close", kgui_fault_pill_word(failed, false, left));
    TEST_ASSERT_EQUAL_STRING("Retry", kgui_fault_pill_word(failed, false, answer));
}

void test_no_word_where_no_pill_stands(void)
{
    TEST_ASSERT_NULL(kgui_fault_pill_word((uint8_t)KGUI_FAULT_FACE_NOTICE, false,
                                          (uint8_t)KGUI_FAULT_PILL_LEFT));
    TEST_ASSERT_NULL(kgui_fault_pill_word((uint8_t)KGUI_FAULT_FACE_NOTICE, true,
                                          (uint8_t)KGUI_FAULT_PILL_NONE));
    TEST_ASSERT_NULL(kgui_fault_pill_word((uint8_t)KGUI_FAULT_FACE_SENDING, true,
                                          (uint8_t)KGUI_FAULT_PILL_ANSWER));
    TEST_ASSERT_NULL(kgui_fault_pill_word((uint8_t)KGUI_FAULT_FACE_SENT, true,
                                          (uint8_t)KGUI_FAULT_PILL_LEFT));
    TEST_ASSERT_NULL(kgui_fault_pill_word(4U, true, (uint8_t)KGUI_FAULT_PILL_ANSWER));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_record_packs_and_unpacks);
    RUN_TEST(test_a_byte_that_is_no_record_is_refused);
    RUN_TEST(test_the_words_follow_the_class);
    RUN_TEST(test_no_dump_rises_at_once_with_ok_alone);
    RUN_TEST(test_a_build_that_cannot_send_rises_at_once);
    RUN_TEST(test_a_build_that_offers_no_report_rises_at_once_with_ok_alone);
    RUN_TEST(test_a_build_that_offers_no_report_waits_for_nothing);
    RUN_TEST(test_the_channel_up_with_the_link_offers_the_send);
    RUN_TEST(test_the_channel_up_with_no_link_is_ok_alone);
    RUN_TEST(test_wifi_off_is_ok_alone_at_once);
    RUN_TEST(test_a_channel_that_will_not_come_is_ok_alone_at_once);
    RUN_TEST(test_a_channel_on_its_way_is_waited_for);
    RUN_TEST(test_a_fault_boot_writes_its_record_and_is_due);
    RUN_TEST(test_a_newer_fault_replaces_the_record);
    RUN_TEST(test_a_kept_record_is_due_at_a_boot_that_is_no_fault);
    RUN_TEST(test_a_boot_with_nothing_kept_and_no_fault_owes_nothing);
    RUN_TEST(test_a_boot_class_that_is_no_class_owes_nothing_of_its_own);
    RUN_TEST(test_a_card_taken_down_waits_for_an_input);
    RUN_TEST(test_an_answer_ends_the_notice);
    RUN_TEST(test_only_the_notice_and_its_result_yield);
    RUN_TEST(test_a_record_with_a_dump_gone_rises_with_ok_alone);
    RUN_TEST(test_the_sends_timings);
    RUN_TEST(test_a_radio_not_heard_yet_is_waited_for);
    RUN_TEST(test_a_radio_heard_is_taken_as_it_is);
    RUN_TEST(test_a_sends_end_has_its_words);
    RUN_TEST(test_a_sending_card_takes_the_answer);
    RUN_TEST(test_an_answer_with_no_sending_card_is_dropped);
    RUN_TEST(test_a_send_with_no_answer_fails_at_the_guard);
    RUN_TEST(test_the_sent_card_leaves_by_itself);
    RUN_TEST(test_the_notice_and_the_failure_wait_for_a_person);
    RUN_TEST(test_a_person_sends_the_report_from_the_notice);
    RUN_TEST(test_a_remote_send_is_refused);
    RUN_TEST(test_a_person_retries_from_the_failure);
    RUN_TEST(test_a_remote_retry_is_refused);
    RUN_TEST(test_ok_answers_the_notice_whoever_presses);
    RUN_TEST(test_close_leaves_the_failure_whoever_presses);
    RUN_TEST(test_the_sent_card_closes_at_any_press);
    RUN_TEST(test_a_press_off_the_pills_does_nothing_elsewhere);
    RUN_TEST(test_no_pill_stands_while_the_report_goes);
    RUN_TEST(test_a_face_or_pill_that_is_none_does_nothing);
    RUN_TEST(test_a_pill_is_named_by_what_it_shows);
    RUN_TEST(test_no_word_where_no_pill_stands);
    return UNITY_END();
}
