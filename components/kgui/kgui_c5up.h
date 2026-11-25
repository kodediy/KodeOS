/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_c5up.h
 * @brief The radio's own update: whether the C5 needs one, what the update
 *        service's answer says, and when it starts.
 *
 * Pure: no LVGL and no network. AO_Radio judges the C5 at every link-up
 * (RadioStateEvt.c5_fit); when it is older than the oldest kode_c5 this
 * firmware works with, AO_Gui asks the service which release its kodeOS
 * carries, and installs it by itself once the power and the glass allow.
 * AO_Gui feeds what it hears; everything decided about it is decided here.
 */
#ifndef KGUI_C5UP_H
#define KGUI_C5UP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kgui_osrel.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KGUI_C5UP_QUERY        "?kodeos="  /**< What joins the service's link and the running kodeOS's version. */
#define KGUI_C5UP_STARTS       3U          /**< The most times a boot asks AO_Radio for the update when no report comes back. */
#define KGUI_C5UP_REPORT_MS    15000U      /**< How long a start waits for AO_Radio's first report before it counts as refused, in milliseconds. */

/** @brief Where the radio's update stands: one per boot. */
typedef enum {
    KGUI_C5UP_IDLE = 0,  /**< The C5 has not been judged too old since the boot: nothing to do. */
    KGUI_C5UP_OWED,      /**< Too old: the question waits for a Wi-Fi address and for its turn. */
    KGUI_C5UP_ASKING,    /**< The question is out. */
    KGUI_C5UP_AGAIN,     /**< It never reached the service: it goes out again after KGUI_OSREL_BOOT_AGAIN_MS. */
    KGUI_C5UP_DUE,       /**< A release to install: it starts once the power and the glass allow. */
    KGUI_C5UP_STARTED,   /**< AO_Radio was asked for the update. */
    KGUI_C5UP_NONE       /**< Nothing to install this boot. */
} kgui_c5up_stage_t;

/** @brief Why an answer left the stage where it did, for the log. */
typedef enum {
    KGUI_C5UP_WHY_RELEASE = 0, /**< A release that meets the minimum: due. */
    KGUI_C5UP_WHY_NO_RELEASE,  /**< 404: the service has none for this kodeOS. */
    KGUI_C5UP_WHY_UNREAD,      /**< Another status, a body cut short, or one that did not read. */
    KGUI_C5UP_WHY_TOO_OLD,     /**< The release named is older than the minimum. */
    KGUI_C5UP_WHY_TRIED,       /**< The C5 took that release already and still reads older. */
    KGUI_C5UP_WHY_UNREACHED,   /**< The question did not reach the service: it is asked again. */
    KGUI_C5UP_WHY_GAVE_UP      /**< It did not reach it in KGUI_OSREL_BOOT_ASKS tries. */
} kgui_c5up_why_t;

/** @brief The service's answer, as AO_Net gave it. */
typedef struct {
    bool        reached;  /**< The question reached the service: AO_Net's status was KODE_OK. */
    uint16_t    status;   /**< The HTTP status; 0 when nothing answered. */
    bool        whole;    /**< The body came whole: not cut short, and within KGUI_OSREL_ANSWER_MAX. */
    const char *body;     /**< The body; it need not end in a terminator. NULL only with @c len 0. */
    size_t      len;      /**< Its bytes. */
} kgui_c5up_reply_t;

/** @brief What the answer is read against. */
typedef struct {
    const char *min;       /**< The oldest kode_c5 this firmware works with (KDRV_C5_RELEASE_MIN). */
    const char *tried;     /**< The release the C5 took last (kgui/c5_tried); "" or NULL for none. */
    bool        plain_ok;  /**< Take an `http://` link: only when the service itself is asked so, on a bench. */
    uint8_t     asks;      /**< How many times the question has gone out this boot, this one included. */
} kgui_c5up_rule_t;

/**
 * @brief AO_Radio judged the C5.
 *
 * Only the first judgement that it is too old moves anything: from IDLE the
 * question is owed. A judgement while the link is down (unknown) or a later
 * one leaves the stage alone, so the update's own link loss changes nothing.
 *
 * @param stage Where it stood.
 * @param old   The C5 is older than the minimum (KRADIO_C5_FIT_OLD).
 * @return Where it stands.
 */
kgui_c5up_stage_t kgui_c5up_judged(kgui_c5up_stage_t stage, bool old);

/**
 * @brief Whether the question may go out now.
 *
 * @param stage  Where it stands.
 * @param linked Wi-Fi has an address.
 * @param busy   Another question of AO_Gui's is out: one at a time.
 * @return true only when it is owed, there is an address and nothing else is out.
 */
bool kgui_c5up_may_ask(kgui_c5up_stage_t stage, bool linked, bool busy);

/**
 * @brief Read the service's answer.
 *
 * A 200 that reads (kgui_osrel_parse()) and names a release that meets
 * @c rule->min and is not @c rule->tried is due. A 404 and anything else
 * that reached the service end the boot's update; a question that did not
 * reach it is asked again while fewer than KGUI_OSREL_BOOT_ASKS went out.
 *
 * @param reply The answer; NULL reads as one that never reached the service.
 * @param rule  What it is read against; NULL refuses every release.
 * @param rel   Receives the release when it is due; left as it was otherwise.
 * @param why   Receives why, for the log; NULL for none.
 * @return KGUI_C5UP_DUE, KGUI_C5UP_AGAIN or KGUI_C5UP_NONE.
 */
kgui_c5up_stage_t kgui_c5up_answer(const kgui_c5up_reply_t *reply, const kgui_c5up_rule_t *rule,
                                   kgui_osrel_t *rel, kgui_c5up_why_t *why);

/**
 * @brief Whether the update may start now.
 *
 * By itself, with no question: a due release, the power for it
 * (kgui_osrel_power_ok()) and a free glass (kgui_osrel_glass_free()).
 *
 * @param stage Where it stands.
 * @param power A cable, or charge enough.
 * @param glass What holds the glass; NULL never lets it start.
 * @return true when AO_Radio may be asked now.
 */
bool kgui_c5up_may_start(kgui_c5up_stage_t stage, bool power, const kgui_osrel_glass_t *glass);

/**
 * @brief A start had no report from AO_Radio within KGUI_C5UP_REPORT_MS.
 *
 * AO_Radio refuses some requests in its log alone (a job in flight, the
 * stream that would not open), and an update the glass never saw start
 * owes nothing. The release is due again while fewer than KGUI_C5UP_STARTS
 * starts went out.
 *
 * @param starts How many times AO_Radio was asked this boot.
 * @return KGUI_C5UP_DUE, or KGUI_C5UP_NONE once the starts are spent.
 */
kgui_c5up_stage_t kgui_c5up_unanswered(uint8_t starts);

/**
 * @brief Whether the boot's kodeOS question waits for the radio's.
 *
 * While the radio's question is owed or out, while a release is due and
 * while the update runs: an update of kodeOS over a radio that is too old
 * changes nothing it needs, and a restart ends the radio's anyway.
 *
 * @param stage Where it stands.
 * @return true from OWED to STARTED.
 */
bool kgui_c5up_holds_os(kgui_c5up_stage_t stage);

/**
 * @brief The stage in one word, for the log.
 * @param stage Where it stands.
 * @return "idle", "owed", "asking", "again", "due", "started" or "none"; "?" outside the enum.
 */
const char *kgui_c5up_stage_word(kgui_c5up_stage_t stage);

/**
 * @brief Why, in a few words, for the log.
 * @param why Why.
 * @return The words; "?" outside the enum.
 */
const char *kgui_c5up_why_words(kgui_c5up_why_t why);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_C5UP_H */
