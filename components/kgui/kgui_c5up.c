/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_c5up.c
 * @brief The radio's own update (kgui_c5up.h).
 */
#include "kgui_c5up.h"

#include <string.h>

#define C5UP_HTTP_OK         200U  /**< The service answered with a release. */
#define C5UP_HTTP_NOT_FOUND  404U  /**< The service has no release for this kodeOS. */

kgui_c5up_stage_t kgui_c5up_judged(kgui_c5up_stage_t stage, bool old)
{
    kgui_c5up_stage_t next = stage;

    if ( old && (KGUI_C5UP_IDLE == stage) )
    {
        next = KGUI_C5UP_OWED;
    }
    return next;
}

bool kgui_c5up_may_ask(kgui_c5up_stage_t stage, bool linked, bool busy)
{
    return (KGUI_C5UP_OWED == stage) && linked && (!busy);
}

/**
 * @brief Read a 200's body against the rule.
 * @param reply The answer, a 200 that came whole.
 * @param rule  What it is read against.
 * @param rel   Receives the release when it is due.
 * @param why   Receives why.
 * @return KGUI_C5UP_DUE or KGUI_C5UP_NONE.
 */
static kgui_c5up_stage_t c5up_release(const kgui_c5up_reply_t *reply, const kgui_c5up_rule_t *rule,
                                      kgui_osrel_t *rel, kgui_c5up_why_t *why)
{
    kgui_c5up_stage_t next = KGUI_C5UP_NONE;
    kgui_osrel_t      got;

    (void)memset(&got, 0, sizeof(got));
    if ( (NULL == reply->body)
         || (!kgui_osrel_parse(reply->body, reply->len, rule->plain_ok, &got)) )
    {
        *why = KGUI_C5UP_WHY_UNREAD;
    }
    else if ( (NULL == rule->min) || kgui_osrel_newer(rule->min, got.version) )
    {
        /* The minimum is newer than the release named: installing it would
         * leave the C5 too old, and the board would ask again at every boot. */
        *why = KGUI_C5UP_WHY_TOO_OLD;
    }
    else if ( (NULL != rule->tried) && (strcmp(rule->tried, got.version) == 0) )
    {
        *why = KGUI_C5UP_WHY_TRIED;
    }
    else
    {
        *why = KGUI_C5UP_WHY_RELEASE;
        next = KGUI_C5UP_DUE;
        if ( NULL != rel )
        {
            *rel = got;
        }
    }
    return next;
}

kgui_c5up_stage_t kgui_c5up_answer(const kgui_c5up_reply_t *reply, const kgui_c5up_rule_t *rule,
                                   kgui_osrel_t *rel, kgui_c5up_why_t *why)
{
    kgui_c5up_stage_t next = KGUI_C5UP_NONE;
    kgui_c5up_why_t   said = KGUI_C5UP_WHY_UNREAD;
    uint8_t           asks = KGUI_OSREL_BOOT_ASKS;

    if ( NULL != rule )
    {
        asks = rule->asks;
    }
    if ( (NULL == reply) || (!reply->reached) )
    {
        said = KGUI_C5UP_WHY_GAVE_UP;
        if ( KGUI_OSREL_BOOT_ASKS > asks )
        {
            said = KGUI_C5UP_WHY_UNREACHED;
            next = KGUI_C5UP_AGAIN;
        }
    }
    else if ( C5UP_HTTP_NOT_FOUND == reply->status )
    {
        said = KGUI_C5UP_WHY_NO_RELEASE;
    }
    else if ( (C5UP_HTTP_OK == reply->status) && reply->whole && (NULL != rule) )
    {
        next = c5up_release(reply, rule, rel, &said);
    }
    else
    {
        /* Another status, a body cut short, or nothing to read it against. */
    }
    if ( NULL != why )
    {
        *why = said;
    }
    return next;
}

bool kgui_c5up_may_start(kgui_c5up_stage_t stage, bool power, const kgui_osrel_glass_t *glass)
{
    return (KGUI_C5UP_DUE == stage) && power && kgui_osrel_glass_free(glass);
}

kgui_c5up_stage_t kgui_c5up_unanswered(uint8_t starts)
{
    kgui_c5up_stage_t next = KGUI_C5UP_NONE;

    if ( KGUI_C5UP_STARTS > starts )
    {
        next = KGUI_C5UP_DUE;
    }
    return next;
}

bool kgui_c5up_holds_os(kgui_c5up_stage_t stage)
{
    return (KGUI_C5UP_OWED == stage) || (KGUI_C5UP_ASKING == stage)
           || (KGUI_C5UP_AGAIN == stage) || (KGUI_C5UP_DUE == stage)
           || (KGUI_C5UP_STARTED == stage);
}

const char *kgui_c5up_stage_word(kgui_c5up_stage_t stage)
{
    static const char *const WORDS[] = { "idle", "owed", "asking", "again", "due", "started",
                                         "none" };
    _Static_assert((sizeof(WORDS) / sizeof(WORDS[0])) == ((size_t)KGUI_C5UP_NONE + 1U),
                   "one word per kgui_c5up_stage_t, in the enum's order");
    const char *word = "?";

    if ( (size_t)stage < (sizeof(WORDS) / sizeof(WORDS[0])) )
    {
        word = WORDS[stage];
    }
    return word;
}

const char *kgui_c5up_why_words(kgui_c5up_why_t why)
{
    static const char *const WORDS[] = {
        "a release this firmware works with",
        "no release for this kodeOS",
        "an answer that did not read",
        "a release older than the minimum",
        "the release the radio took already",
        "the question did not reach the service",
        "the question did not reach the service in every try",
    };
    _Static_assert((sizeof(WORDS) / sizeof(WORDS[0])) == ((size_t)KGUI_C5UP_WHY_GAVE_UP + 1U),
                   "words per kgui_c5up_why_t, in the enum's order");
    const char *words = "?";

    if ( (size_t)why < (sizeof(WORDS) / sizeof(WORDS[0])) )
    {
        words = WORDS[why];
    }
    return words;
}
