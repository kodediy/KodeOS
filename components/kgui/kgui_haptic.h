/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_haptic.h
 * @brief The interface's buzz: four named effects and one call.
 */
#ifndef KGUI_HAPTIC_H
#define KGUI_HAPTIC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The effects: how long each buzz holds the motor and how hard ----------- */

#define KGUI_HAPTIC_CLICK_MS    12U  /**< A click: an activation, in milliseconds. */
#define KGUI_HAPTIC_CLICK_GAIN  60U  /**< ...and its strength, in percent of full drive. */
#define KGUI_HAPTIC_THUD_MS     25U  /**< A thud: a limit reached or a refusal, in milliseconds. */
#define KGUI_HAPTIC_THUD_GAIN   80U  /**< ...and its strength, in percent of full drive. */
#define KGUI_HAPTIC_TICK_MS     6U   /**< A tick: a drag arming, or one focus step, in milliseconds. */
#define KGUI_HAPTIC_TICK_GAIN   40U  /**< ...and its strength, in percent of full drive. */
#define KGUI_HAPTIC_MARK_MS     18U  /**< A mark: a button hold reaching its time, in milliseconds. */
#define KGUI_HAPTIC_MARK_GAIN   70U  /**< ...and its strength, in percent of full drive. */

/* ---- The step tick's pace ---------------------------------------------------- */

#define KGUI_HAPTIC_MS_PER_S      1000U /**< Milliseconds in a second. */
#define KGUI_HAPTIC_STEPS_PER_S   14U   /**< The most step ticks Normal plays in one second. */
/** @brief The least time between two step ticks: a second shared out in
 *         KGUI_HAPTIC_STEPS_PER_S, rounded up so the rate never passes it. */
#define KGUI_HAPTIC_STEP_GAP_MS   ((KGUI_HAPTIC_MS_PER_S + (KGUI_HAPTIC_STEPS_PER_S - 1U)) / KGUI_HAPTIC_STEPS_PER_S)

/* ---- What silences it -------------------------------------------------------- */

#define KGUI_HAPTIC_QUIET_MIC   0x01U /**< The microphone records: a buzz would be in the recording. */
#define KGUI_HAPTIC_QUIET_IMU   0x02U /**< The IMU records: a buzz would be in the motion it measures. */
#define KGUI_HAPTIC_QUIET_SOURCES 2U  /**< How many of them there are. */

/** @brief Which buzz. */
typedef enum {
    KGUI_HAPTIC_CLICK = 0, /**< A control, Open, a tap that acts. */
    KGUI_HAPTIC_THUD,      /**< The end of a list, a refused gesture or door. */
    KGUI_HAPTIC_TICK,      /**< A curtain, card or lock pull that arms. */
    KGUI_HAPTIC_MARK,      /**< BOT's hold reaching home, and reaching the lock. */
    KGUI_HAPTIC_STEP,      /**< One focus step: the tick's shape, played at Normal only and spaced. */
    KGUI_HAPTIC_EFFECTS    /**< How many there are. */
} kgui_haptic_effect_t;

/** @brief How much of the interface buzzes. The order is kgui_haptic_words'. */
typedef enum {
    KGUI_HAPTIC_OFF = 0,   /**< Nothing. */
    KGUI_HAPTIC_LIGHT,     /**< The default: click, thud, tick and mark. */
    KGUI_HAPTIC_NORMAL,    /**< Light, and a tick per step. */
    KGUI_HAPTIC_LEVELS     /**< How many there are. */
} kgui_haptic_level_t;

/** @brief The words of the levels, in kgui_haptic_level_t's order: what
 *         `haptic_ui` and the log call them. */
extern const char *const kgui_haptic_words[KGUI_HAPTIC_LEVELS];

/**
 * @brief Choose how much of the interface buzzes.
 *
 * @param level A level; one past KGUI_HAPTIC_NORMAL is taken as Light, the
 *              default.
 */
void kgui_haptic_set_level(kgui_haptic_level_t level);

/**
 * @brief The level in force.
 *
 * @return The level.
 */
kgui_haptic_level_t kgui_haptic_level(void);

/**
 * @brief Say that something which a buzz would disturb starts or stops
 *        recording.
 *
 * A window that starts ends at its own budget whether or not the end is ever
 * said, so a verdict or an end that never arrives leaves the interface silent
 * no longer than the recording could last.
 *
 * @param source    KGUI_HAPTIC_QUIET_MIC or KGUI_HAPTIC_QUIET_IMU.
 * @param on        true while it records.
 * @param budget_ms The most the recording may take, from now; 0 for one that
 *                  records nothing. Not read when @p on is false.
 */
void kgui_haptic_quiet(uint8_t source, bool on, uint32_t budget_ms);

/**
 * @brief Lift every silence: nothing is recording any more.
 */
void kgui_haptic_quiet_clear(void);

/**
 * @brief What silences the interface now.
 *
 * A window whose budget has gone by is over.
 *
 * @return A mask of KGUI_HAPTIC_QUIET_*; 0 when nothing records.
 */
uint8_t kgui_haptic_quiet_mask(void);

/**
 * @brief Play one effect, if the level, the silences and the pace allow it.
 *
 * The decision and the buzz in one call. A buzz that is held back, and one
 * that AO_Haptic refuses (it is busy with the last), are counted and said at
 * debug, but not while a secret is being typed (kgui_haptic_secret()); neither
 * is an error for the caller.
 *
 * @param effect Which buzz.
 * @return true when AO_Haptic took the request.
 */
bool kgui_haptic_play(kgui_haptic_effect_t effect);

/**
 * @brief Say that a secret is being typed, or that it is not any more.
 *
 * A buzz per key is a count of the keys, so while it is on the debug
 * lines of kgui_haptic_play() - the buzz that played and the one held back -
 * say nothing. The buzzes themselves and their counts go on.
 *
 * @param on true while a secret is being typed.
 */
void kgui_haptic_secret(bool on);

/**
 * @brief How many buzzes were asked of AO_Haptic and how many were held back
 *        since boot.
 *
 * @param asked Out: requests AO_Haptic took.
 * @param held  Out: requests not made (level, silence, pace) or refused.
 */
void kgui_haptic_counts(uint32_t *asked, uint32_t *held);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_HAPTIC_H */
