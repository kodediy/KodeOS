/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_wface.c
 * @brief Host tests of whose face a write's card wears (kgui_wface.h).
 */
#include <string.h>

#include "unity.h"

#include "kgui_wface.h"

#define T0 1000U   /* when a report came, on the AO's clock */

void setUp(void) {}
void tearDown(void) {}

void test_a_report_that_names_an_app_takes_its_face(void)
{
    kgui_wface_t w = { 0 };

    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_TAKE, kgui_wface_report(&w, true, "snake", T0));
    TEST_ASSERT_TRUE(kgui_wface_is(&w, "snake"));
    TEST_ASSERT_TRUE_MESSAGE(w.owed, "a face just taken has its folder still to ask");
}

void test_the_same_app_again_changes_nothing(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "snake", T0);
    w.owed = false;                              /* its folder was asked */
    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_KEEP, kgui_wface_report(&w, true, "snake", T0));
    TEST_ASSERT_FALSE_MESSAGE(w.owed, "every percent would ask the folder again");
    TEST_ASSERT_TRUE(kgui_wface_is(&w, "snake"));
}

void test_a_report_that_names_nobody_keeps_a_card_with_no_face(void)
{
    kgui_wface_t w = { 0 };

    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_KEEP, kgui_wface_report(&w, true, "", T0));
    TEST_ASSERT_FALSE(kgui_wface_has(&w));
}

void test_another_app_takes_the_face_over(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "ladder", T0);   /* what the host called it */
    w.owed = false;
    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_TAKE, kgui_wface_report(&w, true, "snake", T0));
    TEST_ASSERT_TRUE(kgui_wface_is(&w, "snake"));
    TEST_ASSERT_FALSE(kgui_wface_is(&w, "ladder"));
    TEST_ASSERT_TRUE(w.owed);
}

void test_a_write_that_is_no_app_drops_the_face(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "snake", T0);
    /* kodeOS or the radio, whatever its report carries. */
    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_DROP, kgui_wface_report(&w, false, "snake", T0));
    TEST_ASSERT_FALSE(kgui_wface_has(&w));
    TEST_ASSERT_FALSE(w.owed);
    /* And with none to drop, nothing happens. */
    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_KEEP, kgui_wface_report(&w, false, "", T0));
}

void test_an_app_report_with_no_name_keeps_the_face_and_asks_its_folder_again(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "snake", T0);
    w.owed = false;                              /* its folder was asked */
    /* The next transfer on the card, its image still unread: the install a
     * fetch announced, or an install that starts again after its copies. */
    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_AGAIN, kgui_wface_report(&w, true, "", T0));
    TEST_ASSERT_TRUE_MESSAGE(kgui_wface_is(&w, "snake"),
        "the card lost its face between a fetch and the install it announced");
    TEST_ASSERT_TRUE_MESSAGE(w.owed,
        "the folder the transfer before may have written was not asked again");
}

void test_a_nameless_stretch_asks_the_folder_once(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "snake", T0);
    (void)kgui_wface_report(&w, true, "", T0);
    w.owed = false;                              /* asked again */
    /* The same report every second while a download opens. */
    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_KEEP, kgui_wface_report(&w, true, "", T0));
    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_KEEP, kgui_wface_report(&w, true, "", T0));
    TEST_ASSERT_FALSE_MESSAGE(w.owed, "every second would ask the folder again");
    TEST_ASSERT_TRUE(kgui_wface_is(&w, "snake"));
}

void test_the_image_naming_the_same_app_changes_nothing(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "snake", T0);
    (void)kgui_wface_report(&w, true, "", T0);
    w.owed = false;
    /* The first chunk is read and names the app the card already wears. */
    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_KEEP, kgui_wface_report(&w, true, "snake", T0));
    TEST_ASSERT_FALSE(w.owed);
    /* A second nameless stretch (the install starts again) asks once more. */
    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_AGAIN, kgui_wface_report(&w, true, "", T0));
    TEST_ASSERT_TRUE(w.owed);
}

void test_the_image_naming_another_app_takes_its_face(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "ladder", T0);   /* what the fetch was for */
    (void)kgui_wface_report(&w, true, "", T0);
    w.owed = false;
    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_TAKE, kgui_wface_report(&w, true, "snake", T0));
    TEST_ASSERT_TRUE(kgui_wface_is(&w, "snake"));
    TEST_ASSERT_TRUE(w.owed);
}

void test_a_write_that_is_no_app_drops_a_face_kept_through_a_nameless_stretch(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "snake", T0);
    (void)kgui_wface_report(&w, true, "", T0);
    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_DROP, kgui_wface_report(&w, false, "", T0));
    TEST_ASSERT_FALSE(kgui_wface_has(&w));
    TEST_ASSERT_FALSE(w.owed);
    /* And the next card starts clean: a nameless report asks nothing. */
    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_KEEP, kgui_wface_report(&w, true, "", T0));
    TEST_ASSERT_FALSE(w.owed);
}

void test_the_end_of_a_write_forgets_the_face(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "snake", T0);
    kgui_wface_end(&w);
    TEST_ASSERT_FALSE(kgui_wface_has(&w));
    TEST_ASSERT_FALSE(w.owed);
    /* The same app written again asks its folder again: it may have changed. */
    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_TAKE, kgui_wface_report(&w, true, "snake", T0));
}

void test_an_answer_is_the_cards_only_by_its_project(void)
{
    kgui_wface_t w = { 0 };

    TEST_ASSERT_FALSE_MESSAGE(kgui_wface_is(&w, ""),
        "a busy answer names nobody, and a card with no face is nobody");
    (void)kgui_wface_report(&w, true, "snake", T0);
    TEST_ASSERT_FALSE(kgui_wface_is(&w, ""));
    TEST_ASSERT_FALSE(kgui_wface_is(&w, "snak"));
    TEST_ASSERT_TRUE(kgui_wface_is(&w, "snake"));
}

void test_a_name_longer_than_a_project_is_cut_not_overrun(void)
{
    kgui_wface_t w;
    char         big[KGUI_WFACE_PROJECT_MAX + 8U];

    (void)memset(&w, 0, sizeof(w));
    (void)memset(big, 'x', sizeof(big) - 1U);
    big[sizeof(big) - 1U] = '\0';
    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_TAKE, kgui_wface_report(&w, true, big, T0));
    TEST_ASSERT_EQUAL_UINT(KGUI_WFACE_PROJECT_MAX - 1U, strlen(w.project));
}

/* ---- When the card may rise ------------------------------------------------- */

#define LATE (T0 + (10U * KGUI_WFACE_WAIT_MS))   /* long after any wait */

void test_a_card_waits_while_nobody_is_named(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "", T0);      /* a download, still unread */
    TEST_ASSERT_TRUE_MESSAGE(kgui_wface_waits(&w, T0 + 100U),
        "a card with no name would show the file's");
    TEST_ASSERT_TRUE_MESSAGE(kgui_wface_waits(&w, LATE),
        "a download slow to open would show the file's name once the wait ran out");
}

void test_a_card_waits_while_its_folder_has_not_given_its_words(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "snake", T0);
    TEST_ASSERT_TRUE_MESSAGE(kgui_wface_waits(&w, T0 + 100U),
        "the card would wear the project's name and its letters");
}

void test_a_card_rises_once_its_folder_gave_its_words(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "snake", T0);
    kgui_wface_known(&w);
    TEST_ASSERT_FALSE(kgui_wface_waits(&w, T0 + 100U));
}

void test_a_card_rises_when_the_wait_runs_out(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "snake", T0);   /* an app with no folder */
    TEST_ASSERT_TRUE(kgui_wface_waits(&w, T0 + KGUI_WFACE_WAIT_MS - 1U));
    TEST_ASSERT_FALSE_MESSAGE(kgui_wface_waits(&w, T0 + KGUI_WFACE_WAIT_MS),
        "a write with no folder would go by with no card at all");
}

void test_the_wait_counts_from_the_report_that_names_the_app(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "", T0);         /* the download opens slowly */
    (void)kgui_wface_report(&w, true, "snake", LATE);
    TEST_ASSERT_TRUE_MESSAGE(kgui_wface_waits(&w, LATE + 100U),
        "the card would rise with the project's name before its folder answered");
    TEST_ASSERT_FALSE(kgui_wface_waits(&w, LATE + KGUI_WFACE_WAIT_MS));
}

void test_the_same_app_named_again_does_not_count_again(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "snake", T0);
    /* Every report of the write names it. */
    (void)kgui_wface_report(&w, true, "snake", T0 + KGUI_WFACE_WAIT_MS - 1U);
    TEST_ASSERT_FALSE_MESSAGE(kgui_wface_waits(&w, T0 + KGUI_WFACE_WAIT_MS),
        "an app with no folder would never get its card");
}

void test_the_wait_counts_across_the_clock_wrapping(void)
{
    kgui_wface_t w = { 0 };
    uint32_t const named = UINT32_MAX - 10U;

    (void)kgui_wface_report(&w, true, "snake", named);
    TEST_ASSERT_TRUE(kgui_wface_waits(&w, named + 100U));
    TEST_ASSERT_FALSE(kgui_wface_waits(&w, named + KGUI_WFACE_WAIT_MS));
}

void test_another_app_waits_for_its_own_words(void)
{
    kgui_wface_t w = { 0 };
    uint32_t const other = T0 + KGUI_WFACE_WAIT_MS - 1U;

    (void)kgui_wface_report(&w, true, "ladder", T0);
    kgui_wface_known(&w);
    (void)kgui_wface_report(&w, true, "snake", other);
    TEST_ASSERT_TRUE_MESSAGE(kgui_wface_waits(&w, other + 100U),
        "the card would rise with the other app's words");
}

void test_a_nameless_stretch_keeps_the_words(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "snake", T0);
    kgui_wface_known(&w);
    /* The install a fetch announced: its image is still unread. */
    TEST_ASSERT_EQUAL_INT(KGUI_WFACE_AGAIN, kgui_wface_report(&w, true, "", T0 + 50U));
    TEST_ASSERT_FALSE(kgui_wface_waits(&w, T0 + 100U));
}

void test_a_new_write_forgets_the_words_and_counts_again(void)
{
    kgui_wface_t w = { 0 };

    (void)kgui_wface_report(&w, true, "snake", T0);
    kgui_wface_known(&w);
    kgui_wface_end(&w);
    TEST_ASSERT_FALSE(kgui_wface_has(&w));
    (void)kgui_wface_report(&w, true, "snake", T0 + KGUI_WFACE_WAIT_MS);
    TEST_ASSERT_TRUE(kgui_wface_waits(&w, T0 + KGUI_WFACE_WAIT_MS + 100U));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_report_that_names_an_app_takes_its_face);
    RUN_TEST(test_the_same_app_again_changes_nothing);
    RUN_TEST(test_a_report_that_names_nobody_keeps_a_card_with_no_face);
    RUN_TEST(test_another_app_takes_the_face_over);
    RUN_TEST(test_a_write_that_is_no_app_drops_the_face);
    RUN_TEST(test_an_app_report_with_no_name_keeps_the_face_and_asks_its_folder_again);
    RUN_TEST(test_a_nameless_stretch_asks_the_folder_once);
    RUN_TEST(test_the_image_naming_the_same_app_changes_nothing);
    RUN_TEST(test_the_image_naming_another_app_takes_its_face);
    RUN_TEST(test_a_write_that_is_no_app_drops_a_face_kept_through_a_nameless_stretch);
    RUN_TEST(test_the_end_of_a_write_forgets_the_face);
    RUN_TEST(test_an_answer_is_the_cards_only_by_its_project);
    RUN_TEST(test_a_name_longer_than_a_project_is_cut_not_overrun);
    RUN_TEST(test_a_card_waits_while_nobody_is_named);
    RUN_TEST(test_a_card_waits_while_its_folder_has_not_given_its_words);
    RUN_TEST(test_a_card_rises_once_its_folder_gave_its_words);
    RUN_TEST(test_a_card_rises_when_the_wait_runs_out);
    RUN_TEST(test_the_wait_counts_from_the_report_that_names_the_app);
    RUN_TEST(test_the_same_app_named_again_does_not_count_again);
    RUN_TEST(test_the_wait_counts_across_the_clock_wrapping);
    RUN_TEST(test_another_app_waits_for_its_own_words);
    RUN_TEST(test_a_nameless_stretch_keeps_the_words);
    RUN_TEST(test_a_new_write_forgets_the_words_and_counts_again);
    return UNITY_END();
}
