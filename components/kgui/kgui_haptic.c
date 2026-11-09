/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_haptic.c
 * @brief The interface's buzz (kgui_haptic.h).
 */
#include "kgui_haptic.h"

#include "khal_time.h"
#include "kode_sys.h"
#include "kode_log.h"

/** @brief Log tag: kgui's own. */
#define TAG "kgui"

/** @brief One effect: its name, its length, its strength and the least level that plays it. */
typedef struct {
    const char         *name;   /**< What the log calls it.                          */
    uint16_t            ms;     /**< How long the motor is held.                     */
    uint8_t             gain;   /**< Drive, in percent of full scale.                */
    kgui_haptic_level_t least;  /**< The lowest level at which it plays.             */
} haptic_effect_t;

/** @brief The effects, by kgui_haptic_effect_t. */
static const haptic_effect_t s_effects[KGUI_HAPTIC_EFFECTS] = {
    [KGUI_HAPTIC_CLICK] = { "click", KGUI_HAPTIC_CLICK_MS, KGUI_HAPTIC_CLICK_GAIN,
                            KGUI_HAPTIC_LIGHT },
    [KGUI_HAPTIC_THUD]  = { "thud",  KGUI_HAPTIC_THUD_MS,  KGUI_HAPTIC_THUD_GAIN,
                            KGUI_HAPTIC_LIGHT },
    [KGUI_HAPTIC_TICK]  = { "tick",  KGUI_HAPTIC_TICK_MS,  KGUI_HAPTIC_TICK_GAIN,
                            KGUI_HAPTIC_LIGHT },
    [KGUI_HAPTIC_MARK]  = { "mark",  KGUI_HAPTIC_MARK_MS,  KGUI_HAPTIC_MARK_GAIN,
                            KGUI_HAPTIC_LIGHT },
    [KGUI_HAPTIC_STEP]  = { "step",  KGUI_HAPTIC_TICK_MS,  KGUI_HAPTIC_TICK_GAIN,
                            KGUI_HAPTIC_NORMAL },
};

const char *const kgui_haptic_words[KGUI_HAPTIC_LEVELS] = {
    [KGUI_HAPTIC_OFF]    = "off",
    [KGUI_HAPTIC_LIGHT]  = "light",
    [KGUI_HAPTIC_NORMAL] = "normal",
};

/* The state below is read and written on AO_Gui's thread alone, so nothing locks it. */
static kgui_haptic_level_t s_haptic_level = KGUI_HAPTIC_LIGHT; /**< The level in force: Light until the store says otherwise. */
static uint8_t             s_quiet;                     /**< KGUI_HAPTIC_QUIET_* of what records now. */
static uint32_t            s_quiet_from[KGUI_HAPTIC_QUIET_SOURCES]; /**< khal_time_ms() when each window began. */
static uint32_t            s_quiet_for[KGUI_HAPTIC_QUIET_SOURCES];  /**< How long each may last: its row's budget. */
static uint32_t            s_step_ms;                   /**< khal_time_ms() when the last step tick was asked. */
static bool                s_step_any;                  /**< A step tick has been asked since boot. */
static uint32_t            s_asked;                     /**< Requests AO_Haptic took. */
static uint32_t            s_held;                      /**< Requests not made, or refused. */

void kgui_haptic_set_level(kgui_haptic_level_t level)
{
    s_haptic_level = level;
    if ( KGUI_HAPTIC_LEVELS <= level )
    {
        s_haptic_level = KGUI_HAPTIC_LIGHT;
    }
}

kgui_haptic_level_t kgui_haptic_level(void)
{
    return s_haptic_level;
}

void kgui_haptic_quiet(uint8_t source, bool on, uint32_t budget_ms)
{
    uint32_t const now = khal_time_ms();

    for ( uint32_t i = 0U; KGUI_HAPTIC_QUIET_SOURCES > i; ++i )
    {
        uint8_t const bit = (uint8_t)(1U << i);

        if ( (source & bit) != 0U )
        {
            if ( on )
            {
                s_quiet         = (uint8_t)(s_quiet | bit);
                s_quiet_from[i] = now;
                s_quiet_for[i]  = budget_ms;
            }
            else
            {
                s_quiet = (uint8_t)(s_quiet & (uint8_t)~bit);
            }
        }
    }
}

void kgui_haptic_quiet_clear(void)
{
    s_quiet = 0U;
}

uint8_t kgui_haptic_quiet_mask(void)
{
    uint32_t const now = khal_time_ms();

    for ( uint32_t i = 0U; KGUI_HAPTIC_QUIET_SOURCES > i; ++i )
    {
        uint8_t const bit = (uint8_t)(1U << i);

        if ( ((s_quiet & bit) != 0U) && ((now - s_quiet_from[i]) >= s_quiet_for[i]) )
        {
            /* The row's budget has gone by with no verdict: the window is
             * over, as the row is. */
            s_quiet = (uint8_t)(s_quiet & (uint8_t)~bit);
        }
    }
    return s_quiet;
}

/**
 * @brief Whether the step tick may go out now: the pace of KGUI_HAPTIC_STEPS_PER_S.
 *
 * @param now khal_time_ms().
 * @return true when none went out in the last KGUI_HAPTIC_STEP_GAP_MS.
 */
static bool haptic_step_due(uint32_t now)
{
    return (!s_step_any) || ((now - s_step_ms) >= KGUI_HAPTIC_STEP_GAP_MS);
}

/**
 * @brief Why an effect is not played now, if it is not.
 *
 * @param effect Which buzz; in range.
 * @param now    khal_time_ms().
 * @return The reason in a word, or NULL when it may play.
 */
static const char *haptic_held_why(kgui_haptic_effect_t effect, uint32_t now)
{
    const char *why = NULL;

    if ( s_haptic_level < s_effects[effect].least )
    {
        why = "level";
    }
    else if ( kgui_haptic_quiet_mask() != 0U )
    {
        why = "recording";
    }
    else if ( (KGUI_HAPTIC_STEP == effect) && (!haptic_step_due(now)) )
    {
        why = "pace";
    }
    else
    {
        /* Nothing holds it back. */
    }
    return why;
}

/**
 * @brief Why AO_Haptic did not take a request, in words.
 *
 * @param st What kode_sys_haptic_play_buzz() answered.
 * @return "AO_Haptic is busy" for a buzz in flight, "no event in the pool" when
 *         the pool had none to post with, "AO_Haptic refused it" for the rest.
 */
static const char *haptic_refused_why(kode_status_t st)
{
    const char *why = "AO_Haptic refused it";

    if ( KODE_DENIED == st )
    {
        why = "AO_Haptic is busy";
    }
    else if ( KODE_NO_RESOURCES == st )
    {
        why = "no event in the pool";
    }
    else
    {
        /* Another answer: the generic words stand. */
    }
    return why;
}

static bool s_secret; /**< A secret is being typed: the debug lines of a buzz say nothing. */

void kgui_haptic_secret(bool on)
{
    s_secret = on;
}

bool kgui_haptic_play(kgui_haptic_effect_t effect)
{
    bool asked = false;

    if ( KGUI_HAPTIC_EFFECTS > effect )
    {
        uint32_t const now = khal_time_ms();
        const char    *why = haptic_held_why(effect, now);

        if ( NULL == why )
        {
            kode_status_t const st =
                kode_sys_haptic_play_buzz(s_effects[effect].ms,
                                          s_effects[effect].gain);

            if ( KODE_OK == st )
            {
                asked = true;
                ++s_asked;
                if ( KGUI_HAPTIC_STEP == effect )
                {
                    s_step_any = true;
                    s_step_ms  = now;
                }
                if ( !s_secret )
                {
                    KLOG_D(TAG, "haptic: %s, %u ms at %u %%",
                           s_effects[effect].name,
                           (unsigned)s_effects[effect].ms,
                           (unsigned)s_effects[effect].gain);
                }
            }
            else
            {
                why = haptic_refused_why(st);
            }
        }
        if ( NULL != why )
        {
            ++s_held;
            if ( !s_secret )
            {
                KLOG_D(TAG, "haptic: %s held back (%s)", s_effects[effect].name,
                       why);
            }
        }
    }
    return asked;
}

void kgui_haptic_counts(uint32_t *asked, uint32_t *held)
{
    if ( NULL != asked )
    {
        *asked = s_asked;
    }
    if ( NULL != held )
    {
        *held = s_held;
    }
}
