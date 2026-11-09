/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_dtext.c
 * @brief The words of an app's details (kgui_dtext.h).
 */
#include "kgui_dtext.h"

#include <stdio.h>
#include <string.h>

#define DT_BYTES_KB    1024U                  /**< Bytes in a KB. */
#define DT_BYTES_MB    (1024U * 1024U)        /**< Bytes in a MB. */
#define DT_TENTHS      10U                    /**< Tenths in a whole. */
#define DT_MONTH_LEN   3U                     /**< Letters of a month in the compiler's date. */
#define DT_DAY_AT      4U                     /**< Where the day begins in `Nov  2 2026`. */
#define DT_DAY_LEN     2U                     /**< Characters the day takes, a space padding a single digit. */
#define DT_YEAR_AT     7U                     /**< Where the year begins. */
#define DT_YEAR_LEN    4U                     /**< Digits of the year. */
#define DT_DATE_LEN    11U                    /**< The length of the compiler's date. */
#define DT_SEP         " \xC2\xB7 "           /**< Between the version and the category: a middle dot, one line in the caption face. */

/**
 * @brief Whether a character is a decimal digit.
 *
 * @param c The character.
 * @return true for 0 to 9.
 */
static bool dtx_digit(char c)
{
    return ('0' <= c) && ('9' >= c);
}

/**
 * @brief Whether a character is a letter.
 *
 * @param c The character.
 * @return true for A to Z and a to z.
 */
static bool dtx_alpha(char c)
{
    return (('A' <= c) && ('Z' >= c)) || (('a' <= c) && ('z' >= c));
}

/**
 * @brief Whether a string says something.
 *
 * @param text A string; may be NULL.
 * @return true when it has at least one character.
 */
static bool dtx_known(const char *text)
{
    return (NULL != text) && ('\0' != text[0]);
}

void kgui_dtext_size(uint32_t bytes, char *out, size_t cap)
{
    if ( (NULL == out) || (0U == cap) )
    {
        return;
    }
    out[0] = '\0';
    if ( 0U == bytes )
    {
        return;
    }
    uint32_t kb = bytes / DT_BYTES_KB;

    if ( (bytes % DT_BYTES_KB) >= (DT_BYTES_KB / 2U) )
    {
        ++kb;
    }
    if ( 0U == kb )
    {
        kb = 1U;
    }
    if ( DT_BYTES_KB > kb )
    {
        (void)snprintf(out, cap, "%u KB", (unsigned)kb);
    }
    else
    {
        uint32_t whole = bytes / DT_BYTES_MB;
        uint32_t tenth = (((bytes % DT_BYTES_MB) * DT_TENTHS) + (DT_BYTES_MB / 2U)) / DT_BYTES_MB;

        if ( DT_TENTHS <= tenth )
        {
            ++whole;
            tenth = 0U;
        }
        (void)snprintf(out, cap, "%u.%u MB", (unsigned)whole, (unsigned)tenth);
    }
}

void kgui_dtext_date(const char *raw, char *out, size_t cap)
{
    if ( (NULL == out) || (0U == cap) )
    {
        return;
    }
    out[0] = '\0';
    if ( (NULL == raw) || (strlen(raw) != DT_DATE_LEN) )
    {
        return;
    }
    bool const day_ok = ((' ' == raw[DT_DAY_AT]) || dtx_digit(raw[DT_DAY_AT]))
                        && dtx_digit(raw[DT_DAY_AT + 1U]);
    bool const year_ok = dtx_digit(raw[DT_YEAR_AT]) && dtx_digit(raw[DT_YEAR_AT + 1U])
                         && dtx_digit(raw[DT_YEAR_AT + 2U]) && dtx_digit(raw[DT_YEAR_AT + 3U]);
    bool const seps_ok = (' ' == raw[DT_MONTH_LEN]) && (' ' == raw[DT_YEAR_AT - 1U])
                         && dtx_alpha(raw[0]) && dtx_alpha(raw[1]) && dtx_alpha(raw[2]);

    if ( day_ok && year_ok && seps_ok )
    {
        const char *day     = &raw[DT_DAY_AT];
        size_t      day_len = DT_DAY_LEN;

        if ( ' ' == day[0] )
        {
            ++day;
            day_len = 1U;
        }
        (void)snprintf(out, cap, "%.*s %.*s %.*s",
                       (int)day_len, day,
                       (int)DT_MONTH_LEN, raw,
                       (int)DT_YEAR_LEN, &raw[DT_YEAR_AT]);
    }
}

void kgui_dtext_meta(const char *version, const char *category, char *out, size_t cap)
{
    if ( (NULL == out) || (0U == cap) )
    {
        return;
    }
    out[0] = '\0';
    if ( dtx_known(version) )
    {
        const char *prefix = "v";

        if ( ('v' == version[0]) || ('V' == version[0]) )
        {
            prefix = "";
        }
        (void)snprintf(out, cap, "%s%s", prefix, version);
    }
    if ( dtx_known(category) )
    {
        size_t const used = strlen(out);
        const char  *sep  = "";
        char         first = category[0];

        if ( 0U < used )
        {
            sep = DT_SEP;
        }
        if ( ('a' <= first) && ('z' >= first) )
        {
            first = (char)(first - ('a' - 'A'));
        }
        if ( used < cap )
        {
            (void)snprintf(&out[used], cap - used, "%s%c%s", sep, first, &category[1]);
        }
    }
}
