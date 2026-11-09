/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_syscard.h
 * @brief What the system cards say and when: a stopped app, a critical battery, a factory reset.
 */
#ifndef KGUI_SYSCARD_H
#define KGUI_SYSCARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The app that stopped -------------------------------------------------- */

#define KGUI_STOPPED_ANON      "An app"                 /**< The name when the app is not known. */
#define KGUI_STOPPED_SUB       "Stopped"                /**< Under the app's name, as its second line. */
#define KGUI_STOPPED_AGAIN     "Run again"              /**< The card's one button. */
#define KGUI_STOPPED_CRASHED   "It crashed"             /**< Why, after a panic. */
#define KGUI_STOPPED_HUNG      "It stopped responding"  /**< Why, after a watchdog. */
#define KGUI_STOPPED_BROWNOUT  "The battery dipped"     /**< Why, after a brownout. */

/**
 * @brief Whether the way an app ended raises the "stopped" card.
 *
 * @param end How AO_Boot says the app before this boot ended: a
 *            kboot_app_end_t, as an integer.
 * @return true for a panic, a watchdog or a brownout; false for a clean end,
 *         for a boot that is not back from an app and for a number that is
 *         none of them.
 */
bool kgui_stopped_is(int end);

/**
 * @brief The card's one line of why.
 *
 * @param end How the app ended: a kboot_app_end_t, as an integer.
 * @return KGUI_STOPPED_CRASHED, KGUI_STOPPED_HUNG or KGUI_STOPPED_BROWNOUT;
 *         "" for an end that raises no card. Never NULL.
 */
const char *kgui_stopped_reason(int end);

/* ---- The battery ----------------------------------------------------------- */

#define KGUI_BAT_CRIT_PCT        10U  /**< At or under this charge, and not charging, the battery is critical. */
#define KGUI_BAT_CRIT_REARM_PCT  3U   /**< The card rises again only after the charge climbs this far above critical, or the cable charges. */
#define KGUI_CRIT_TITLE          "Battery"            /**< The critical card's title. */
#define KGUI_CRIT_SUB            "Critical"           /**< ...and what is under it. */
#define KGUI_CRIT_PLUG           "Plug in the cable." /**< Its line, while the cable is out. */
#define KGUI_CRIT_PLUGGED        "The cable is in."   /**< ...and while it is in. */

/** @brief Whether the critical card has been raised for this entry into critical. */
typedef struct {
    bool shown;  /**< The card was raised and the battery has not recovered since. */
    bool armed;  /**< A reading above 0 % was seen: there is a cell to be critical. */
} kgui_crit_t;

/**
 * @brief Whether a battery reading is a new entry into critical.
 *
 * Once per entry: a reading that stays critical is due only the first time,
 * and a charge that wobbles around the line is not a new entry. The latch
 * opens again when the cell charges, or climbs KGUI_BAT_CRIT_REARM_PCT above
 * the line.
 *
 * Nothing is due until a reading above 0 % has been seen. A board whose gauge
 * reads no cell says 0 % on every reading, and nothing in a reading says the
 * cell is there, so a charge that has never been above 0 is no cell's.
 *
 * @param c        The latch.
 * @param critical Whether this reading is critical: not charging and at or
 *                 under KGUI_BAT_CRIT_PCT.
 * @param charging Whether the cell is charging.
 * @param soc_pct  The charge, in percent.
 * @return true when the card is due. Call kgui_crit_taken() once it is raised.
 */
bool kgui_crit_due(kgui_crit_t *c, bool critical, bool charging, uint8_t soc_pct);

/**
 * @brief The card was raised: it is not due again until the battery recovers.
 *
 * @param c The latch.
 */
void kgui_crit_taken(kgui_crit_t *c);

/**
 * @brief Whether a card that is due has to wait for the glass.
 *
 * A due card that waits stays due (kgui_crit_taken() is not called), so the
 * reading or the wake that finds the way clear raises it.
 *
 * @param locked  A locked card is up: a write is running.
 * @param system  Another system card is up: it would take a question away.
 * @param guided  A guided test has the glass.
 * @param parked  The board rests with the glass handed over: nothing would
 *                answer the card, and nobody would see it.
 * @return true when the card has to wait.
 */
bool kgui_crit_waits(bool locked, bool system, bool guided, bool parked);

/* ---- The factory reset ----------------------------------------------------- */

#define KGUI_RESET_ROW         "Factory reset"            /**< About's row that asks for it. */
#define KGUI_RESET_ROW_SUB     "Erase apps and settings"  /**< ...and the line under it. */
#define KGUI_RESET_ASK         "Factory reset?"           /**< The question's title. */
#define KGUI_RESET_ASK_SUB     "Apps and settings go"     /**< ...its second line. */
#define KGUI_RESET_ASK_DETAIL  "So do networks and paired\ndevices. The welcome app,\nthe name and files stay." /**< ...and what else goes and what stays. */
#define KGUI_RESET_BUTTON      "Reset"                    /**< The question's one button. */
#define KGUI_RESET_TITLE       "Factory reset"            /**< The wait's title, and the refusal's. */
#define KGUI_RESET_WAIT_SUB    "Erasing"                  /**< The wait's second line. */
#define KGUI_RESET_WAIT_DETAIL "The Kode restarts\nwhen it is done."   /**< ...and what comes after it. */
#define KGUI_RESET_REFUSED_SUB "Not done"                 /**< kgui_reset_sub(): nothing was erased. */
#define KGUI_RESET_PARTLY_SUB  "Partly done"              /**< kgui_reset_sub(): some of it was. */
#define KGUI_RESET_NO_WELCOME  "The welcome app is not on this Kode" /**< Why, when the welcome app is neither installed nor on the card. */
#define KGUI_RESET_BUSY        "Busy: try again in a moment"         /**< Why, when the board could not take the request. */
#define KGUI_RESET_NOTHING     "Nothing was erased.\nRestart the Kode,\nthen try again."               /**< Why, stopped before anything went. */
#define KGUI_RESET_NO_RADIO    "The radio did not answer.\nNothing was erased.\nRestart, then try again." /**< Why, stopped at the C5's link. */
#define KGUI_RESET_NO_INSTALL  "The welcome app did not\ngo in. Your data is here.\nTry again."        /**< Why, stopped writing the welcome app. */
#define KGUI_RESET_RADIO_QUIT  "The radio stopped.\nApps and settings stay.\nRestart, then try again." /**< Why, stopped at the C5's wipe. */
#define KGUI_RESET_APPS_STAY   "Networks are gone.\nApps and settings stay.\nRestart, then try again."  /**< Why, stopped removing the apps. */
#define KGUI_RESET_SOME_STAY   "Networks and apps gone.\nSome settings stay.\nRestart, then try again." /**< Why, stopped erasing the settings. */
#define KGUI_RESET_ALL_GONE    "Everything is erased.\nRestart the Kode."      /**< Why, when only the closing request failed. */
#define KGUI_RESET_STOPPED_AT  "Stopped at "              /**< Why, before the word of a step nobody named. */
#define KGUI_RESET_WHY_MAX     24U                        /**< Room for "Stopped at " and the longest step's word, "request", terminator included. */

/**
 * @brief The line the factory reset's card shows when the reset did not happen.
 *
 * The step and the status are FactoryResetRspEvt's. The string stays good
 * until the next call.
 *
 * @param step   The step that stopped the reset: a kboot_reset_step_t, as an integer.
 * @param status That step's kode_status_t, as an integer.
 * @return KGUI_RESET_NO_WELCOME for step FIND with KODE_DENIED,
 *         KGUI_RESET_BUSY for step FIND with KODE_NO_RESOURCES, a sentence that
 *         says what is gone and what stays for any other failure of a known
 *         step, KGUI_RESET_STOPPED_AT and the step's word
 *         (boot_reset_step_name()) for one nobody named, and "" for KODE_OK.
 *         Never NULL.
 */
const char *kgui_reset_why(uint8_t step, uint32_t status);

/**
 * @brief The line over kgui_reset_why(): whether the reset that stopped had begun
 *        to erase.
 *
 * The steps run what can fail first, so the step says it (kaos.h).
 *
 * @param step The step that stopped the reset: a kboot_reset_step_t, as an integer.
 * @return KGUI_RESET_REFUSED_SUB up to and including the install step,
 *         KGUI_RESET_PARTLY_SUB from the wipe on. Never NULL.
 */
const char *kgui_reset_sub(uint8_t step);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_SYSCARD_H */
