/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_clock.c
 * @brief The Time page's arithmetic (kgui_clock.h).
 */
#include "kgui_clock.h"

#include <stddef.h>
#include <string.h>

#include "kode_log.h"

/** February, as struct tm counts the months. */
#define CLOCK_FEB          1
/** February's days in a leap year. */
#define CLOCK_FEB_LEAP     29U
/** A year divisible by this is a leap year... */
#define CLOCK_LEAP_EVERY   4
/** ...unless it is divisible by this... */
#define CLOCK_LEAP_CENTURY 100
/** ...unless it is divisible by this as well. */
#define CLOCK_LEAP_400     400
/** Microseconds in a second. */
#define CLOCK_US_PER_S     1000000ULL
/** Microseconds in a millisecond. */
#define CLOCK_US_PER_MS    1000ULL

/** @brief One zone: the words its row shows and the POSIX TZ string. */
typedef struct {
    const char *label;  /**< The row's words and the log's. */
    const char *tz;     /**< The POSIX TZ string the clock runs in there. */
} clock_zone_t;

/*
 * West to east. Summer time where the place keeps it and left out where it
 * does not: Mexico City keeps none, so it is not Chicago, and neither do
 * Bogota and Lima, so they are not New York.
 */
static const clock_zone_t s_zones[] = {
    { "Honolulu",         "HST10"                        },
    { "Los Angeles",      "PST8PDT,M3.2.0,M11.1.0"       },
    { "Denver",           "MST7MDT,M3.2.0,M11.1.0"       },
    { "Chicago",          "CST6CDT,M3.2.0,M11.1.0"       },
    { "Mexico City",      "CST6"                         },
    { "New York",         "EST5EDT,M3.2.0,M11.1.0"       },
    { "Bogota, Lima",     "<-05>5"                       },
    { "Sao Paulo",        "<-03>3"                       },
    { "UTC",              KGUI_CLOCK_ZONE_UTC            },
    { "London, Lisbon",   "GMT0BST,M3.5.0/1,M10.5.0"     },
    { "Madrid, Paris",    "CET-1CEST,M3.5.0,M10.5.0/3"   },
    { "Athens",           "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Johannesburg",     "SAST-2"                       },
    { "Istanbul, Moscow", "<+03>-3"                      },
    { "Dubai",            "<+04>-4"                      },
    { "Mumbai",           "IST-5:30"                     },
    { "Bangkok",          "<+07>-7"                      },
    { "Beijing, Perth",   "<+08>-8"                      },
    { "Tokyo, Seoul",     "JST-9"                        },
    { "Sydney",           "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Auckland",         "NZST-12NZDT,M9.5.0,M4.1.0/3"  },
};

/** How many zones s_zones holds. */
#define CLOCK_ZONE_COUNT  ((uint8_t)(sizeof(s_zones) / sizeof(s_zones[0])))
_Static_assert(KGUI_CLOCK_ZONES == CLOCK_ZONE_COUNT, "KGUI_CLOCK_ZONES counts s_zones");

/** @brief A value one step round 0 to @p n - 1. */
static int32_t clock_round(int32_t v, int32_t n, int8_t dir)
{
    int32_t step = 1;

    if ( 0 > dir )
    {
        step = n - 1;
    }
    return (v + step) % n;
}

/** @brief Bring the day down to the last its month has. */
static void clock_trim_day(struct tm *t)
{
    int32_t const last = (int32_t)kgui_clock_month_days((int32_t)t->tm_year + KGUI_CLOCK_TM_YEAR0,
                                                         (int32_t)t->tm_mon);

    if ( last < (int32_t)t->tm_mday )
    {
        t->tm_mday = (int)last;
    }
}

uint8_t kgui_clock_month_days(int32_t year, int32_t mon)
{
    static const uint8_t DAYS[KGUI_CLOCK_MONTHS] = {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
    };
    uint8_t days = 0U;

    if ( (0 <= mon) && (KGUI_CLOCK_MONTHS > mon) )
    {
        days = DAYS[mon];
        if ( (CLOCK_FEB == mon)
            && ((0 == (year % CLOCK_LEAP_400))
                || ((0 == (year % CLOCK_LEAP_EVERY)) && (0 != (year % CLOCK_LEAP_CENTURY)))) )
        {
            days = CLOCK_FEB_LEAP;
        }
    }
    return days;
}

bool kgui_clock_step(struct tm *t, kgui_clock_field_t field, int8_t dir)
{
    bool moved = false;

    if ( NULL == t )
    {
        return false;
    }
    switch ( field )
    {
        case KGUI_CLOCK_HOUR: {
            t->tm_hour = (int)clock_round((int32_t)t->tm_hour, KGUI_CLOCK_HOURS, dir);
            moved      = true;
            break;
        }
        case KGUI_CLOCK_MINUTE: {
            t->tm_min = (int)clock_round((int32_t)t->tm_min, KGUI_CLOCK_MINUTES, dir);
            moved     = true;
            break;
        }
        case KGUI_CLOCK_DAY: {
            int32_t const last = (int32_t)kgui_clock_month_days((int32_t)t->tm_year + KGUI_CLOCK_TM_YEAR0,
                                                                (int32_t)t->tm_mon);

            if ( 0 < last )
            {
                /* Days count from 1: round the day less one. */
                t->tm_mday = (int)(clock_round((int32_t)t->tm_mday - 1, last, dir) + 1);
                moved      = true;
            }
            break;
        }
        case KGUI_CLOCK_MONTH: {
            t->tm_mon = (int)clock_round((int32_t)t->tm_mon, KGUI_CLOCK_MONTHS, dir);
            clock_trim_day(t);
            moved = true;
            break;
        }
        case KGUI_CLOCK_YEAR: {
            int32_t year = (int32_t)t->tm_year + KGUI_CLOCK_TM_YEAR0;

            if ( 0 > dir )
            {
                year -= 1;
            }
            else
            {
                year += 1;
            }
            if ( (KGUI_CLOCK_YEAR_MIN <= year) && (KGUI_CLOCK_YEAR_MAX >= year) )
            {
                t->tm_year = (int)(year - KGUI_CLOCK_TM_YEAR0);
                clock_trim_day(t);
                moved = true;
            }
            break;
        }
        default: {
            /* No row steps anything else. */
            break;
        }
    }
    return moved;
}

bool kgui_clock_utc_of(struct tm const *local, uint32_t later_s, int64_t *utc_s)
{
    struct tm t;
    int64_t   when;
    bool      ok = false;

    if ( (NULL == local) || (NULL == utc_s) )
    {
        return false;
    }
    t          = *local;
    t.tm_isdst = -1;
    /* mktime()'s -1 for a time it cannot convert is below the floor too.
     * The floor is khal_time_wall_known()'s: a second the clock counts as
     * known is after it, never on it. */
    when       = (int64_t)mktime(&t) + (int64_t)later_s;
    if ( ((int64_t)KODE_CLOCK_KNOWN_AFTER_S < when) && (KGUI_CLOCK_UTC_END_S > when) )
    {
        *utc_s = when;
        ok     = true;
    }
    return ok;
}

bool kgui_clock_step_to(struct tm const *from, kgui_clock_field_t field, int8_t dir,
                        int64_t *utc_s)
{
    struct tm t;
    int64_t   was  = 0;
    int64_t   when = 0;
    bool      ok   = false;
    bool      known;

    if ( (NULL == from) || (NULL == utc_s) )
    {
        return false;
    }
    known = kgui_clock_utc_of(from, 0U, &was);
    t     = *from;
    if ( kgui_clock_step(&t, field, dir) )
    {
        ok = kgui_clock_utc_of(&t, 0U, &when);
    }
    if ( ok && known && (was == when) && (KGUI_CLOCK_HOUR == field) )
    {
        /* The hour stepped into the spring gap and the C library put it back
         * where it was: one more step crosses the gap. */
        ok = false;
        if ( kgui_clock_step(&t, field, dir) )
        {
            ok = kgui_clock_utc_of(&t, 0U, &when) && (was != when);
        }
    }
    if ( ok )
    {
        *utc_s = when;
    }
    return ok;
}

bool kgui_clock_hand_holds(int64_t set_s, uint32_t since_ms, uint64_t wall_us)
{
    uint64_t expect;
    uint64_t off;

    if ( 0 > set_s )
    {
        return false;
    }
    expect = ((uint64_t)set_s * CLOCK_US_PER_S) + ((uint64_t)since_ms * CLOCK_US_PER_MS);
    if ( wall_us >= expect )
    {
        off = wall_us - expect;
    }
    else
    {
        off = expect - wall_us;
    }
    return KGUI_CLOCK_HAND_SLACK_US >= off;
}

uint8_t kgui_clock_zone_find(const char *tz)
{
    const char *want  = tz;
    uint8_t     found = CLOCK_ZONE_COUNT;

    if ( NULL == want )
    {
        return CLOCK_ZONE_COUNT;
    }
    if ( '\0' == want[0] )
    {
        want = KGUI_CLOCK_ZONE_UTC;
    }
    for ( uint8_t i = 0U; (CLOCK_ZONE_COUNT > i) && (CLOCK_ZONE_COUNT == found); ++i )
    {
        if ( strcmp(s_zones[i].tz, want) == 0 )
        {
            found = i;
        }
    }
    return found;
}

const char *kgui_clock_zone_label(const char *tz)
{
    uint8_t const at    = kgui_clock_zone_find(tz);
    const char   *label = "Custom";

    if ( CLOCK_ZONE_COUNT > at )
    {
        label = s_zones[at].label;
    }
    return label;
}

const char *kgui_clock_zone_tz(uint8_t at)
{
    const char *tz = KGUI_CLOCK_ZONE_UTC;

    if ( CLOCK_ZONE_COUNT > at )
    {
        tz = s_zones[at].tz;
    }
    return tz;
}

uint8_t kgui_clock_zone_step(const char *tz, int8_t dir)
{
    uint8_t at = kgui_clock_zone_find(tz);

    if ( CLOCK_ZONE_COUNT <= at )
    {
        at = kgui_clock_zone_find(KGUI_CLOCK_ZONE_UTC);
    }
    return (uint8_t)clock_round((int32_t)at, (int32_t)CLOCK_ZONE_COUNT, dir);
}

kgui_clock_ask_t kgui_clock_ask(bool known, bool zone_heard, bool zone_stored, bool at_rest)
{
    kgui_clock_ask_t ask = KGUI_CLOCK_ASK_NONE;

    if ( at_rest )
    {
        /* At Rest nothing asks. */
    }
    else if ( !known )
    {
        ask = KGUI_CLOCK_ASK_TIME;
    }
    else if ( zone_heard && (!zone_stored) )
    {
        ask = KGUI_CLOCK_ASK_ZONE;
    }
    else
    {
        /* The clock and its zone are known. */
    }
    return ask;
}
