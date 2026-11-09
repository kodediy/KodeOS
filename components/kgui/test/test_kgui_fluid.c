/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_fluid.c
 * @brief Host tests of the battery's liquid (kgui_fluid.h).
 */
#include <math.h>
#include <stdint.h>

#include "unity.h"

#include "kgui_fluid.h"

#define SIDE 100
#define PI   3.14159265F

static kgui_fluid_t s_f;
static uint8_t      s_a8[SIDE * SIDE];

void setUp(void)
{
    kgui_fluid_init(&s_f, SIDE);
}

void tearDown(void)
{
}

/** @brief How much of the disc the picture fills, 0 to 1. */
static float filled(void)
{
    float const c = ((float)SIDE - 1.0F) / 2.0F;
    float       in = 0.0F;
    float       wet = 0.0F;

    kgui_fluid_draw(&s_f, s_a8, SIDE, SIDE);
    for ( int y = 0; SIDE > y; ++y )
    {
        for ( int x = 0; SIDE > x; ++x )
        {
            float const dx = (float)x - c;
            float const dy = (float)y - c;

            if ( ((dx * dx) + (dy * dy)) <= ((float)SIDE * (float)SIDE) / 4.0F )
            {
                in += 1.0F;
                wet += (float)s_a8[(y * SIDE) + x] / 255.0F;
            }
        }
    }
    return wet / in;
}

void test_the_volume_is_the_same_at_any_angle(void)
{
    kgui_fluid_fill(&s_f, 0.6F, true);
    for ( float a = -PI; PI > a; a += 0.4F )
    {
        kgui_fluid_rest(&s_f, a);
        TEST_ASSERT_FLOAT_WITHIN(0.01F, 0.6F, filled());
    }
}

void test_it_lies_at_the_bottom_when_upright_and_at_the_top_upside_down(void)
{
    kgui_fluid_fill(&s_f, 0.3F, true);
    kgui_fluid_draw(&s_f, s_a8, SIDE, SIDE);
    TEST_ASSERT_EQUAL_UINT8(255U, s_a8[(90 * SIDE) + 50]);
    TEST_ASSERT_EQUAL_UINT8(0U, s_a8[(10 * SIDE) + 50]);
    kgui_fluid_rest(&s_f, PI);
    kgui_fluid_draw(&s_f, s_a8, SIDE, SIDE);
    TEST_ASSERT_EQUAL_UINT8(0U, s_a8[(90 * SIDE) + 50]);
    TEST_ASSERT_EQUAL_UINT8(255U, s_a8[(10 * SIDE) + 50]);
}

void test_tilting_right_gathers_it_on_the_low_side(void)
{
    /* Up leaning to the right: down is to the left of the glass's foot. */
    kgui_fluid_fill(&s_f, 0.5F, true);
    kgui_fluid_rest(&s_f, PI / 2.0F);
    kgui_fluid_draw(&s_f, s_a8, SIDE, SIDE);
    TEST_ASSERT_EQUAL_UINT8(255U, s_a8[(50 * SIDE) + 10]);
    TEST_ASSERT_EQUAL_UINT8(0U, s_a8[(50 * SIDE) + 90]);
}

void test_it_swings_past_level_and_comes_to_rest(void)
{
    float most = 0.0F;
    int   n = 0;

    kgui_fluid_fill(&s_f, 0.5F, true);
    while ( (600 > n) && kgui_fluid_step(&s_f, 0.6F, 0.0F) )
    {
        if ( s_f.angle > most )
        {
            most = s_f.angle;
        }
        ++n;
    }
    TEST_ASSERT_TRUE(0.60F < most);          /* it passes level a little, as a thick liquid does */
    TEST_ASSERT_TRUE(0.68F > most);
    TEST_ASSERT_TRUE(450 > n);               /* within 15 s it has stopped */
    TEST_ASSERT_FLOAT_WITHIN(0.02F, 0.6F, s_f.angle);
}

void test_a_still_dot_draws_nothing(void)
{
    kgui_fluid_fill(&s_f, 0.5F, true);
    TEST_ASSERT_FALSE(kgui_fluid_step(&s_f, 0.0F, 0.0F));
}

void test_a_turn_makes_ripples_and_they_keep_the_volume(void)
{
    float tallest = 0.0F;

    kgui_fluid_fill(&s_f, 0.5F, true);
    for ( int n = 0; 40 > n; ++n )
    {
        float sum = 0.0F;

        (void)kgui_fluid_step(&s_f, (3 > n) ? 1.0F : 0.0F, (2 == n) ? 1.0F : 0.0F);
        for ( unsigned i = 0U; KGUI_FLUID_COLS > i; ++i )
        {
            sum += s_f.h[i];
            if ( fabsf(s_f.h[i]) > tallest )
            {
                tallest = fabsf(s_f.h[i]);
            }
        }
        TEST_ASSERT_FLOAT_WITHIN(0.01F, 0.0F, sum);
    }
    TEST_ASSERT_TRUE(0.3F < tallest);
    TEST_ASSERT_TRUE(tallest <= (0.31F * 50.0F));
}

void test_the_full_turn_takes_the_short_way(void)
{
    kgui_fluid_rest(&s_f, 3.0F);
    (void)kgui_fluid_step(&s_f, -3.0F, 0.0F);
    TEST_ASSERT_TRUE(0.0F < s_f.spin);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_volume_is_the_same_at_any_angle);
    RUN_TEST(test_it_lies_at_the_bottom_when_upright_and_at_the_top_upside_down);
    RUN_TEST(test_tilting_right_gathers_it_on_the_low_side);
    RUN_TEST(test_it_swings_past_level_and_comes_to_rest);
    RUN_TEST(test_a_still_dot_draws_nothing);
    RUN_TEST(test_a_turn_makes_ripples_and_they_keep_the_volume);
    RUN_TEST(test_the_full_turn_takes_the_short_way);
    return UNITY_END();
}
