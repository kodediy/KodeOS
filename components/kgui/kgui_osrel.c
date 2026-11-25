/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_osrel.c
 * @brief The kodeOS release the update service names (kgui_osrel.h).
 */
#include "kgui_osrel.h"

#include <stdio.h>
#include <string.h>

#define OSREL_KEY_VERSION   "version"   /**< The answer's field that holds the version. */
#define OSREL_KEY_URL       "url"       /**< The answer's field that holds the file's link. */
#define OSREL_SCHEME_TLS    "https://"  /**< What a file's link starts with. */
#define OSREL_SCHEME_PLAIN  "http://"   /**< What a bench's link may start with. */
#define OSREL_PRINT_FIRST   0x21        /**< The first character a link may hold: past the space. */
#define OSREL_PRINT_LAST    0x7E        /**< The last one: the end of printable ASCII. */
#define OSREL_TOP_LEVEL     1U          /**< How deep the answer's own fields are. */
#define OSREL_DECIMAL       10U         /**< The base a version's groups are written in. */

/** @brief A walk over the answer's bytes. */
typedef struct {
    const char *json;  /**< The answer. */
    size_t      len;   /**< Its bytes. */
    size_t      at;    /**< The next byte to read. */
} osrel_walk_t;

/**
 * @brief Step over spaces, tabs and line ends.
 * @param w The walk.
 */
static void osrel_skip_space(osrel_walk_t *w)
{
    while ( (w->at < w->len)
            && ((' ' == w->json[w->at]) || ('\t' == w->json[w->at])
                || ('\n' == w->json[w->at]) || ('\r' == w->json[w->at])) )
    {
        ++w->at;
    }
}

/**
 * @brief Read the string that opens at the walk's place.
 *
 * The walk stands on the opening quote and is left past the closing one. An
 * escape is stepped over with the character it escapes, and counted, since
 * neither field this file wants may hold one.
 *
 * @param w       The walk.
 * @param start   Receives where the string's content starts.
 * @param n       Receives the content's bytes.
 * @param escaped Receives whether the content held an escape.
 * @return true when the closing quote was there.
 */
static bool osrel_string(osrel_walk_t *w, size_t *start, size_t *n, bool *escaped)
{
    bool closed = false;

    *escaped = false;
    ++w->at;
    *start = w->at;
    while ( (w->at < w->len) && ('\0' != w->json[w->at]) && (!closed) )
    {
        if ( '"' == w->json[w->at] )
        {
            closed = true;
        }
        else if ( '\\' == w->json[w->at] )
        {
            *escaped = true;
            w->at += 2U;
        }
        else
        {
            ++w->at;
        }
    }
    if ( closed )
    {
        *n = w->at - *start;
        ++w->at;
    }
    return closed;
}

/**
 * @brief Copy the string value of one top-level field.
 *
 * @param json The answer.
 * @param len  Its bytes.
 * @param key  The field's name.
 * @param out  Receives the value and a terminator.
 * @param size Bytes @p out holds.
 * @return true when the field is there, at the top level, holds a string
 *         with no escape in it, and the string fits @p out whole.
 */
static bool osrel_field(const char *json, size_t len, const char *key, char *out, size_t size)
{
    osrel_walk_t w     = { json, len, 0U };
    size_t const key_n = strlen(key);
    uint32_t     depth = 0U;
    bool         found = false;
    bool         sound = true;

    while ( (w.at < w.len) && ('\0' != w.json[w.at]) && sound && (!found) )
    {
        char const c = w.json[w.at];

        if ( ('{' == c) || ('[' == c) )
        {
            ++depth;
            ++w.at;
        }
        else if ( ('}' == c) || (']' == c) )
        {
            if ( 0U < depth )
            {
                --depth;
            }
            ++w.at;
        }
        else if ( '"' != c )
        {
            ++w.at;
        }
        else
        {
            size_t start   = 0U;
            size_t n       = 0U;
            bool   escaped = false;

            sound = osrel_string(&w, &start, &n, &escaped);
            osrel_skip_space(&w);
            /* A string followed by a colon is a field's name; any other is a value. */
            if ( sound && (w.at < w.len) && (':' == w.json[w.at])
                && ((uint32_t)OSREL_TOP_LEVEL == depth) && (!escaped)
                && (key_n == n) && (strncmp(&w.json[start], key, n) == 0) )
            {
                ++w.at;
                osrel_skip_space(&w);
                sound = (w.at < w.len) && ('"' == w.json[w.at])
                        && osrel_string(&w, &start, &n, &escaped)
                        && (!escaped) && (n < size);
                if ( sound )
                {
                    (void)memcpy(out, &w.json[start], n);
                    out[n] = '\0';
                    found  = true;
                }
            }
        }
    }
    return found;
}

/**
 * @brief Read a version's three groups.
 * @param text   The version; NULL does not read.
 * @param groups Receives the groups, the first one the most significant.
 * @return true when @p text is three groups of one to four digits with a dot
 *         between them and nothing else.
 */
static bool osrel_version(const char *text, uint16_t groups[KGUI_OSREL_GROUPS])
{
    size_t   at    = 0U;
    uint32_t group = 0U;
    bool     reads = (NULL != text);

    while ( reads && ((uint32_t)KGUI_OSREL_GROUPS > group) )
    {
        uint32_t value  = 0U;
        uint32_t digits = 0U;

        while ( ('0' <= text[at]) && ('9' >= text[at]) )
        {
            uint32_t const digit = (uint32_t)text[at] - (uint32_t)'0';

            value = (value * (uint32_t)OSREL_DECIMAL) + digit;
            ++digits;
            ++at;
            if ( (uint32_t)KGUI_OSREL_GROUP_DIGITS < digits )
            {
                break;
            }
        }
        reads = (0U < digits) && ((uint32_t)KGUI_OSREL_GROUP_DIGITS >= digits);
        if ( reads )
        {
            groups[group] = (uint16_t)value;
            ++group;
            if ( (uint32_t)KGUI_OSREL_GROUPS > group )
            {
                reads = ('.' == text[at]);
                ++at;
            }
            else
            {
                reads = ('\0' == text[at]);
            }
        }
    }
    return reads;
}

/**
 * @brief Whether a link is one the board follows.
 * @param url      The link.
 * @param plain_ok Take `http://` as well as `https://`.
 * @return true when it has a scheme this build takes, something after it,
 *         and no character outside printable ASCII, no quote and no backslash.
 */
static bool osrel_url_ok(const char *url, bool plain_ok)
{
    size_t skip = 0U;
    bool   ok   = false;

    if ( strncmp(url, OSREL_SCHEME_TLS, strlen(OSREL_SCHEME_TLS)) == 0 )
    {
        skip = strlen(OSREL_SCHEME_TLS);
    }
    else if ( plain_ok && (strncmp(url, OSREL_SCHEME_PLAIN, strlen(OSREL_SCHEME_PLAIN)) == 0) )
    {
        skip = strlen(OSREL_SCHEME_PLAIN);
    }
    else
    {
        /* No scheme this build follows. */
    }
    if ( (0U < skip) && ('\0' != url[skip]) )
    {
        ok = true;
        for ( size_t i = skip; ('\0' != url[i]) && ok; ++i )
        {
            ok = (OSREL_PRINT_FIRST <= (int)url[i]) && (OSREL_PRINT_LAST >= (int)url[i])
                 && ('"' != url[i]) && ('\\' != url[i]);
        }
    }
    return ok;
}

bool kgui_osrel_parse(const char *json, size_t len, bool plain_ok, kgui_osrel_t *out)
{
    bool ok = false;

    if ( (NULL != json) && (NULL != out) )
    {
        kgui_osrel_t rel;
        uint16_t     groups[KGUI_OSREL_GROUPS];

        ok = osrel_field(json, len, OSREL_KEY_VERSION, rel.version, sizeof(rel.version))
             && osrel_field(json, len, OSREL_KEY_URL, rel.url, sizeof(rel.url))
             && osrel_version(rel.version, groups)
             && osrel_url_ok(rel.url, plain_ok);
        if ( ok )
        {
            *out = rel;
        }
    }
    return ok;
}

bool kgui_osrel_newer(const char *latest, const char *running)
{
    uint16_t theirs[KGUI_OSREL_GROUPS];
    uint16_t ours[KGUI_OSREL_GROUPS];
    bool     newer = false;

    if ( osrel_version(latest, theirs) && osrel_version(running, ours) )
    {
        bool decided = false;

        for ( uint32_t i = 0U; ((uint32_t)KGUI_OSREL_GROUPS > i) && (!decided); ++i )
        {
            if ( theirs[i] != ours[i] )
            {
                newer   = (theirs[i] > ours[i]);
                decided = true;
            }
        }
    }
    return newer;
}

bool kgui_osrel_power_ok(uint8_t soc, bool vbus)
{
    return vbus || ((uint8_t)KGUI_OSREL_MIN_PCT <= soc);
}

kgui_osrel_row_t kgui_osrel_row(const kgui_osrel_view_t *view)
{
    kgui_osrel_row_t row = KGUI_OSREL_ROW_NONE;

    if ( NULL == view )
    {
        /* Nothing is known. */
    }
    else if ( view->card )
    {
        row = KGUI_OSREL_ROW_CARD;
    }
    else if ( !view->service )
    {
        /* The card was this build's only source. */
    }
    else if ( !view->linked )
    {
        row = KGUI_OSREL_ROW_OFFLINE;
    }
    else if ( KGUI_OSREL_AVAILABLE == view->check )
    {
        row = KGUI_OSREL_ROW_CHARGE;
        if ( view->power )
        {
            row = KGUI_OSREL_ROW_AVAILABLE;
        }
    }
    else if ( KGUI_OSREL_CURRENT == view->check )
    {
        row = KGUI_OSREL_ROW_CURRENT;
    }
    else if ( (KGUI_OSREL_FAILED == view->check) || (KGUI_OSREL_UNREACHED == view->check) )
    {
        row = KGUI_OSREL_ROW_FAILED;
    }
    else
    {
        /* Asked, or about to be: opening the page asks. */
        row = KGUI_OSREL_ROW_CHECKING;
    }
    return row;
}

bool kgui_osrel_row_installs(kgui_osrel_row_t row)
{
    return (KGUI_OSREL_ROW_CARD == row) || (KGUI_OSREL_ROW_AVAILABLE == row);
}

bool kgui_osrel_row_asks(kgui_osrel_row_t row)
{
    return (KGUI_OSREL_ROW_CURRENT == row) || (KGUI_OSREL_ROW_FAILED == row);
}

void kgui_osrel_row_text(kgui_osrel_row_t row, const char *version, char *out, size_t size)
{
    const char *ver = "";

    if ( (NULL == out) || (0U == size) )
    {
        return;
    }
    if ( NULL != version )
    {
        ver = version;
    }
    switch ( row )
    {
        case KGUI_OSREL_ROW_CARD: {
            (void)snprintf(out, size, "kodeos.bin on the card");
            break;
        }
        case KGUI_OSREL_ROW_AVAILABLE: {
            (void)snprintf(out, size, "kodeOS %s available", ver);
            break;
        }
        case KGUI_OSREL_ROW_CHARGE: {
            (void)snprintf(out, size, "Charge to install kodeOS %s", ver);
            break;
        }
        case KGUI_OSREL_ROW_CURRENT: {
            (void)snprintf(out, size, "kodeOS is up to date");
            break;
        }
        case KGUI_OSREL_ROW_CHECKING: {
            (void)snprintf(out, size, "Checking...");
            break;
        }
        case KGUI_OSREL_ROW_OFFLINE: {
            (void)snprintf(out, size, "Join Wi-Fi to check");
            break;
        }
        case KGUI_OSREL_ROW_FAILED: {
            (void)snprintf(out, size, "Could not check");
            break;
        }
        case KGUI_OSREL_ROW_NONE:
        default: {
            (void)snprintf(out, size, "No image on the card");
            break;
        }
    }
}

kgui_osrel_offer_t kgui_osrel_offer_follow(kgui_osrel_offer_t offer, kgui_osrel_check_t check,
                                           uint8_t asks)
{
    kgui_osrel_offer_t next = offer;

    if ( (KGUI_OSREL_OFFER_UNASKED == offer) || (KGUI_OSREL_OFFER_ASKED == offer)
        || (KGUI_OSREL_OFFER_AGAIN == offer) )
    {
        if ( KGUI_OSREL_ASKING == check )
        {
            next = KGUI_OSREL_OFFER_ASKED;
        }
        else if ( KGUI_OSREL_AVAILABLE == check )
        {
            next = KGUI_OSREL_OFFER_DUE;
        }
        else if ( (KGUI_OSREL_UNREACHED == check) && ((uint8_t)KGUI_OSREL_BOOT_ASKS > asks) )
        {
            /* AO_Net never took it to the service: it goes out again. */
            next = KGUI_OSREL_OFFER_AGAIN;
        }
        else
        {
            /* Nothing newer, no usable answer, the asks spent, or nobody to
             * ask: once per boot. */
            next = KGUI_OSREL_OFFER_NONE;
        }
    }
    else if ( (KGUI_OSREL_OFFER_DUE == offer) && (KGUI_OSREL_CURRENT == check) )
    {
        /* The release was taken back since. */
        next = KGUI_OSREL_OFFER_NONE;
    }
    else
    {
        /* Due stays due on any other answer; shown and none stay until the next boot. */
    }
    return next;
}

kgui_osrel_offer_t kgui_osrel_offer_shown(kgui_osrel_offer_t offer)
{
    kgui_osrel_offer_t next = offer;

    if ( KGUI_OSREL_OFFER_DUE == offer )
    {
        next = KGUI_OSREL_OFFER_SHOWN;
    }
    return next;
}

const char *kgui_osrel_offer_word(kgui_osrel_offer_t offer)
{
    static const char *const WORDS[] = { "unasked", "asked", "due", "shown", "none", "again" };
    _Static_assert((sizeof(WORDS) / sizeof(WORDS[0])) == ((size_t)KGUI_OSREL_OFFER_AGAIN + 1U),
                   "one word per kgui_osrel_offer_t, in the enum's order");
    const char *word = "?";

    if ( (size_t)offer < (sizeof(WORDS) / sizeof(WORDS[0])) )
    {
        word = WORDS[offer];
    }
    return word;
}

bool kgui_osrel_glass_free(const kgui_osrel_glass_t *glass)
{
    bool free_glass = false;

    if ( NULL != glass )
    {
        bool const free_screen = (KGUI_OSREL_SCREEN_FACE == glass->screen)
                                 || (KGUI_OSREL_SCREEN_APPS == glass->screen)
                                 || (KGUI_OSREL_SCREEN_SETTINGS == glass->screen);
        bool const held = glass->rest || glass->card || glass->prompt || glass->locked
                          || glass->writing || glass->deciding || glass->resetting
                          || glass->parked || glass->moving || glass->hand
                          || glass->greeting;

        free_glass = free_screen && (!held);
    }
    return free_glass;
}

bool kgui_osrel_offer_may_rise(kgui_osrel_offer_t offer, const kgui_osrel_glass_t *glass)
{
    return (KGUI_OSREL_OFFER_DUE == offer) && kgui_osrel_glass_free(glass);
}

kgui_osrel_card_t kgui_osrel_card(bool linked, bool power)
{
    kgui_osrel_card_t card = KGUI_OSREL_CARD_READY;

    if ( !linked )
    {
        card = KGUI_OSREL_CARD_OFFLINE;
    }
    else if ( !power )
    {
        card = KGUI_OSREL_CARD_CHARGE;
    }
    else
    {
        /* It installs. */
    }
    return card;
}

void kgui_osrel_card_title(const char *version, char *out, size_t size)
{
    if ( (NULL == out) || (0U == size) )
    {
        return;
    }
    if ( (NULL != version) && ('\0' != version[0]) )
    {
        (void)snprintf(out, size, KGUI_OSREL_CARD_WORD " %s", version);
    }
    else
    {
        (void)snprintf(out, size, "%s", KGUI_OSREL_CARD_WORD);
    }
}

void kgui_osrel_card_body(kgui_osrel_card_t card, char *out, size_t size)
{
    if ( (NULL == out) || (0U == size) )
    {
        return;
    }
    if ( KGUI_OSREL_CARD_OFFLINE == card )
    {
        (void)snprintf(out, size, "Join Wi-Fi to update.");
    }
    else if ( KGUI_OSREL_CARD_CHARGE == card )
    {
        (void)snprintf(out, size, "Charge to %u%% or plug in.", (unsigned)KGUI_OSREL_MIN_PCT);
    }
    else
    {
        (void)snprintf(out, size, "%s", KGUI_OSREL_CARD_READY_BODY);
    }
}
