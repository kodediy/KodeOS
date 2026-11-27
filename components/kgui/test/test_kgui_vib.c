/**
 * @file test_kgui_vib.c
 * @brief The motor's level for the volume's stop and the Vibration row, and
 *        the scale apps' buzzes get.
 */
#include "unity.h"

#include "kgui_vib.h"

void setUp(void) {}
void tearDown(void) {}

void test_silence_turns_the_motor_off_whatever_the_row(void)
{
    TEST_ASSERT_EQUAL_INT(KGUI_HAPTIC_OFF, kgui_vib_level(KGUI_VIB_VOL_SILENT, KGUI_HAPTIC_OFF));
    TEST_ASSERT_EQUAL_INT(KGUI_HAPTIC_OFF, kgui_vib_level(KGUI_VIB_VOL_SILENT, KGUI_HAPTIC_LIGHT));
    TEST_ASSERT_EQUAL_INT(KGUI_HAPTIC_OFF, kgui_vib_level(KGUI_VIB_VOL_SILENT, KGUI_HAPTIC_NORMAL));
}

void test_the_vibrate_stop_turns_the_motor_on_when_the_row_is_off(void)
{
    TEST_ASSERT_EQUAL_INT(KGUI_HAPTIC_LIGHT, kgui_vib_level(KGUI_VIB_VOL_VIBRATE, KGUI_HAPTIC_OFF));
}

void test_the_vibrate_stop_keeps_a_row_that_buzzes(void)
{
    TEST_ASSERT_EQUAL_INT(KGUI_HAPTIC_LIGHT, kgui_vib_level(KGUI_VIB_VOL_VIBRATE, KGUI_HAPTIC_LIGHT));
    TEST_ASSERT_EQUAL_INT(KGUI_HAPTIC_NORMAL, kgui_vib_level(KGUI_VIB_VOL_VIBRATE, KGUI_HAPTIC_NORMAL));
}

void test_with_sound_the_row_decides(void)
{
    TEST_ASSERT_EQUAL_INT(KGUI_HAPTIC_OFF, kgui_vib_level(KGUI_VIB_VOL_SOUND, KGUI_HAPTIC_OFF));
    TEST_ASSERT_EQUAL_INT(KGUI_HAPTIC_LIGHT, kgui_vib_level(KGUI_VIB_VOL_SOUND, KGUI_HAPTIC_LIGHT));
    TEST_ASSERT_EQUAL_INT(KGUI_HAPTIC_NORMAL, kgui_vib_level(KGUI_VIB_VOL_SOUND, KGUI_HAPTIC_NORMAL));
}

void test_a_motor_that_is_off_silences_the_buzzes_of_apps(void)
{
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_vib_scale(KGUI_HAPTIC_OFF));
}

void test_light_and_normal_leave_the_buzzes_of_apps_whole(void)
{
    TEST_ASSERT_EQUAL_UINT8(100U, kgui_vib_scale(KGUI_HAPTIC_LIGHT));
    TEST_ASSERT_EQUAL_UINT8(100U, kgui_vib_scale(KGUI_HAPTIC_NORMAL));
}

void test_the_row_off_or_the_volume_silent_silences_apps_and_nothing_else_does(void)
{
    /* The two cases of the report: the row off with sound, and silence with
     * the row on. */
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_vib_scale(kgui_vib_level(KGUI_VIB_VOL_SOUND, KGUI_HAPTIC_OFF)));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_vib_scale(kgui_vib_level(KGUI_VIB_VOL_SILENT, KGUI_HAPTIC_NORMAL)));
    TEST_ASSERT_EQUAL_UINT8(100U, kgui_vib_scale(kgui_vib_level(KGUI_VIB_VOL_VIBRATE, KGUI_HAPTIC_OFF)));
    TEST_ASSERT_EQUAL_UINT8(100U, kgui_vib_scale(kgui_vib_level(KGUI_VIB_VOL_SOUND, KGUI_HAPTIC_LIGHT)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_silence_turns_the_motor_off_whatever_the_row);
    RUN_TEST(test_the_vibrate_stop_turns_the_motor_on_when_the_row_is_off);
    RUN_TEST(test_the_vibrate_stop_keeps_a_row_that_buzzes);
    RUN_TEST(test_with_sound_the_row_decides);
    RUN_TEST(test_a_motor_that_is_off_silences_the_buzzes_of_apps);
    RUN_TEST(test_light_and_normal_leave_the_buzzes_of_apps_whole);
    RUN_TEST(test_the_row_off_or_the_volume_silent_silences_apps_and_nothing_else_does);
    return UNITY_END();
}
