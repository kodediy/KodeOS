/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_shelf.c
 * @brief Host tests of the shelf of recents (kgui_shelf.h).
 */
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "kgui_shelf.h"

#define CORNER ((int32_t)KBSP_LCD_CORNER_R_PX)  /**< The glass's corner radius. */
#define WIDTH  ((int32_t)KBSP_LCD_H_RES)        /**< The glass's width. */
#define DISC_R (KGUI_RAIL_ICON / 2)             /**< A disc's radius. */

void setUp(void) {}
void tearDown(void) {}

void test_shelf_x_centres_the_discs(void)
{
    TEST_ASSERT_EQUAL_INT32(205, kgui_shelf_x(0U, 1U));
    TEST_ASSERT_EQUAL_INT32(149, kgui_shelf_x(0U, 2U));
    TEST_ASSERT_EQUAL_INT32(261, kgui_shelf_x(1U, 2U));
    TEST_ASSERT_EQUAL_INT32(93, kgui_shelf_x(0U, 3U));
    TEST_ASSERT_EQUAL_INT32(205, kgui_shelf_x(1U, 3U));
    TEST_ASSERT_EQUAL_INT32(317, kgui_shelf_x(2U, 3U));
    /* A disc that is not one of them stands in the middle. */
    TEST_ASSERT_EQUAL_INT32(205, kgui_shelf_x(3U, 3U));
    TEST_ASSERT_EQUAL_INT32(205, kgui_shelf_x(0U, 4U));
}

void test_shelf_gaps_are_the_rails(void)
{
    /* 32 px between two discs, and from the shelf to the first row's disc. */
    TEST_ASSERT_EQUAL_INT32(32, (kgui_shelf_x(1U, 3U) - kgui_shelf_x(0U, 3U)) - KGUI_RAIL_ICON);
    TEST_ASSERT_EQUAL_INT32(139, KGUI_SHELF_FULL_Y);
    TEST_ASSERT_EQUAL_INT32(32, (KGUI_RAIL_FOCUS_Y - KGUI_SHELF_FULL_Y) - KGUI_RAIL_ICON);
}

void test_shelf_opa_fades_before_the_corner(void)
{
    /* Whole where a root that fills the glass starts, and anywhere below. */
    TEST_ASSERT_EQUAL_UINT8(255U, kgui_shelf_opa(KGUI_SHELF_TOP_Y));
    TEST_ASSERT_EQUAL_UINT8(255U, kgui_shelf_opa(139));
    TEST_ASSERT_EQUAL_UINT8(255U, kgui_shelf_opa(211));
    TEST_ASSERT_UINT8_WITHIN(15U, 127U, kgui_shelf_opa(50));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_shelf_opa(45));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_shelf_opa(-200));
    /* The focus on the second row takes the list up by 29 px: it is gone. */
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_shelf_opa(KGUI_SHELF_TOP_Y - 29));
}

void test_shelf_never_shows_in_the_corner(void)
{
    /* No disc that shows at all reaches out of the corner's circle. */
    for ( int32_t cy = KGUI_SHELF_FULL_Y; cy > -KGUI_RAIL_ICON; --cy )
    {
        if ( kgui_shelf_opa(cy) == 0U )
        {
            continue;
        }
        for ( uint8_t n = 1U; (uint8_t)KGUI_SHELF_MAX >= n; ++n )
        {
            for ( uint8_t i = 0U; i < n; ++i )
            {
                int32_t const x  = kgui_shelf_x(i, n);
                int32_t const kx = (x < (WIDTH / 2)) ? CORNER : (WIDTH - CORNER);

                TEST_ASSERT_TRUE(cy - DISC_R >= 0);
                if ( (CORNER > cy) && (((CORNER > x) || (x > (WIDTH - CORNER)))) )
                {
                    float const d = hypotf((float)(x - kx), (float)(cy - CORNER));

                    TEST_ASSERT_TRUE_MESSAGE((d + (float)DISC_R) <= (float)CORNER,
                                             "a disc that shows is in the corner");
                }
            }
        }
    }
}

void test_shelf_follows_the_box(void)
{
    /* With the first row on the focus line, the shelf is one pitch above it. */
    int32_t const box_y = KGUI_RAIL_FOCUS_Y - KGUI_ROW_MID;

    TEST_ASSERT_EQUAL_INT32(KGUI_SHELF_FULL_Y, kgui_shelf_cy(box_y));
    TEST_ASSERT_EQUAL_INT32(KGUI_SHELF_FULL_Y - 56, kgui_shelf_cy(box_y - 56));
}

void test_shelf_root_scrolls_as_a_long_list(void)
{
    TEST_ASSERT_EQUAL_UINT32(2U, kgui_shelf_scroll_rows(2U, false));
    TEST_ASSERT_EQUAL_UINT32(KGUI_RAIL_BLOCK_MAX + 1U, kgui_shelf_scroll_rows(2U, true));
    TEST_ASSERT_EQUAL_UINT32(KGUI_RAIL_BLOCK_MAX + 1U, kgui_shelf_scroll_rows(0U, true));
    TEST_ASSERT_EQUAL_UINT32(9U, kgui_shelf_scroll_rows(9U, true));
}

void test_recents_push_order_and_no_repeats(void)
{
    char list[KGUI_RECENTS_LEN] = "";

    TEST_ASSERT_TRUE(kgui_recents_push(list, "chess"));
    TEST_ASSERT_EQUAL_STRING("chess", list);
    TEST_ASSERT_TRUE(kgui_recents_push(list, "pop"));
    TEST_ASSERT_EQUAL_STRING("pop\nchess", list);
    TEST_ASSERT_TRUE(kgui_recents_push(list, "duel"));
    TEST_ASSERT_EQUAL_STRING("duel\npop\nchess", list);
    /* One already there goes back to the front. */
    TEST_ASSERT_TRUE(kgui_recents_push(list, "chess"));
    TEST_ASSERT_EQUAL_STRING("chess\nduel\npop", list);
    /* The one at the front again changes nothing. */
    TEST_ASSERT_FALSE(kgui_recents_push(list, "chess"));
    TEST_ASSERT_EQUAL_STRING("chess\nduel\npop", list);
}

void test_recents_push_keeps_three(void)
{
    char list[KGUI_RECENTS_LEN] = "c\nb\na";

    TEST_ASSERT_TRUE(kgui_recents_push(list, "d"));
    TEST_ASSERT_EQUAL_STRING("d\nc\nb", list);
    /* Three of the longest names fit. */
    {
        char long_list[KGUI_RECENTS_LEN] = "";
        char name[KGUI_SHELF_NAME_MAX];
        char names[KGUI_SHELF_MAX][KGUI_SHELF_NAME_MAX];

        for ( uint8_t i = 0U; 4U > i; ++i )
        {
            (void)memset(name, (int)('a' + i), sizeof(name) - 1U);
            name[sizeof(name) - 1U] = '\0';
            TEST_ASSERT_TRUE(kgui_recents_push(long_list, name));
        }
        TEST_ASSERT_EQUAL_UINT8(3U, kgui_recents_split(long_list, names, (uint8_t)KGUI_SHELF_MAX));
        TEST_ASSERT_EQUAL_CHAR('d', names[0][0]);
        TEST_ASSERT_EQUAL_CHAR('b', names[2][0]);
        TEST_ASSERT_EQUAL_size_t(KGUI_SHELF_NAME_MAX - 1U, strlen(names[2]));
    }
}

void test_recents_push_refuses_nothing_names(void)
{
    char list[KGUI_RECENTS_LEN] = "a";
    char too_long[KGUI_SHELF_NAME_MAX + 1U];

    (void)memset(too_long, 'x', sizeof(too_long) - 1U);
    too_long[sizeof(too_long) - 1U] = '\0';
    TEST_ASSERT_FALSE(kgui_recents_push(list, ""));
    TEST_ASSERT_FALSE(kgui_recents_push(list, NULL));
    TEST_ASSERT_FALSE(kgui_recents_push(list, too_long));
    TEST_ASSERT_FALSE(kgui_recents_push(list, "a\nb"));
    TEST_ASSERT_FALSE(kgui_recents_push(NULL, "a"));
    TEST_ASSERT_EQUAL_STRING("a", list);
}

void test_recents_split_reads_what_nvs_held(void)
{
    char    names[KGUI_SHELF_MAX][KGUI_SHELF_NAME_MAX];
    uint8_t n;

    n = kgui_recents_split("chess\npop\nduel", names, (uint8_t)KGUI_SHELF_MAX);
    TEST_ASSERT_EQUAL_UINT8(3U, n);
    TEST_ASSERT_EQUAL_STRING("chess", names[0]);
    TEST_ASSERT_EQUAL_STRING("pop", names[1]);
    TEST_ASSERT_EQUAL_STRING("duel", names[2]);
    /* Blank lines, a repeat and anything past the room are dropped. */
    n = kgui_recents_split("\nchess\n\nchess\npop\n", names, 2U);
    TEST_ASSERT_EQUAL_UINT8(2U, n);
    TEST_ASSERT_EQUAL_STRING("chess", names[0]);
    TEST_ASSERT_EQUAL_STRING("pop", names[1]);
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_recents_split("", names, (uint8_t)KGUI_SHELF_MAX));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_recents_split(NULL, names, (uint8_t)KGUI_SHELF_MAX));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_shelf_x_centres_the_discs);
    RUN_TEST(test_shelf_gaps_are_the_rails);
    RUN_TEST(test_shelf_opa_fades_before_the_corner);
    RUN_TEST(test_shelf_never_shows_in_the_corner);
    RUN_TEST(test_shelf_follows_the_box);
    RUN_TEST(test_shelf_root_scrolls_as_a_long_list);
    RUN_TEST(test_recents_push_order_and_no_repeats);
    RUN_TEST(test_recents_push_keeps_three);
    RUN_TEST(test_recents_push_refuses_nothing_names);
    RUN_TEST(test_recents_split_reads_what_nvs_held);
    return UNITY_END();
}
