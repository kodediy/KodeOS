/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_clock.h
 * @brief The Time page's arithmetic: the zones it offers, the step of each of
 *        its rows, the local time kept across a change of zone, and what the
 *        face asks for.
 */
#ifndef KGUI_CLOCK_H
#define KGUI_CLOCK_H

#include <stdbool.h>
#include <stdint.h>
/* cppcheck-suppress misra-c2012-21.10 ; the page reads and sets the C library's local time, in its struct tm */
#include <time.h>

#include "kdrv_clocks.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ---- */

/** The first year the wall clock counts as known: KODE_CLOCK_KNOWN_AFTER_S is
 *  its first second, UTC, and a host case holds the two together. */
#define KGUI_CLOCK_YEAR_MIN  2026
/** The last year the clock accepts, `time set`'s own. */
#define KGUI_CLOCK_YEAR_MAX  ((int32_t)KDRV_CLOCKS_WALL_YEAR_MAX)
/** The first second past KGUI_CLOCK_YEAR_MAX, UTC (2106-01-01T00:00:00Z):
 *  a host case holds it to the year. */
#define KGUI_CLOCK_UTC_END_S 4291747200LL
/** How far the clock may stand from where the Time page left it and still be
 *  the page's: one second either way, in microseconds. */
#define KGUI_CLOCK_HAND_SLACK_US 1000000ULL
/** What struct tm's tm_year counts from. */
#define KGUI_CLOCK_TM_YEAR0  1900
/** Months in a year. */
#define KGUI_CLOCK_MONTHS    12
/** Hours in a day. */
#define KGUI_CLOCK_HOURS     24
/** Minutes in an hour. */
#define KGUI_CLOCK_MINUTES   60
/** How many zones the Time page offers, west to east. */
#define KGUI_CLOCK_ZONES     21U

/** The zone a board with nothing stored runs in: the board's own UTC. */
#define KGUI_CLOCK_ZONE_UTC  "UTC0"

/* ---- The Time page's rows ---- */

/** The rows of the Time page that step a part of the date or the time. */
typedef enum {
    KGUI_CLOCK_HOUR = 0,   /**< 0 to 23, round, the day stays. */
    KGUI_CLOCK_MINUTE,     /**< 0 to 59, round, the hour stays. */
    KGUI_CLOCK_DAY,        /**< 1 to the month's last, round, the month stays. */
    KGUI_CLOCK_MONTH,      /**< January to December, round, the year stays. */
    KGUI_CLOCK_YEAR,       /**< KGUI_CLOCK_YEAR_MIN to KGUI_CLOCK_YEAR_MAX, no round. */
} kgui_clock_field_t;

/**
 * @brief How many days a month has.
 * @param year The year, as a person writes it (2028).
 * @param mon  The month, 0 for January as struct tm counts it.
 * @return 28 to 31; 0 for a month out of range.
 */
uint8_t kgui_clock_month_days(int32_t year, int32_t mon);

/**
 * @brief Step one part of a local time by one, as a press on its row does.
 *
 * A day the new month or year lacks becomes that month's last. Only the
 * fields named are written: tm_wday and tm_yday are mktime()'s to fix.
 *
 * @param t     The local time; tm_year, tm_mon, tm_mday, tm_hour and tm_min
 *              are read and written.
 * @param field The row.
 * @param dir   -1 for one less, anything else for one more.
 * @return false when nothing moved: the year at either end, or a bad field.
 */
bool kgui_clock_step(struct tm *t, kgui_clock_field_t field, int8_t dir);

/**
 * @brief The UTC second a local time names, in the zone in force, later by
 *        @p later_s.
 *
 * mktime() decides summer time (tm_isdst -1), so a reading inside a spring
 * gap moves forward by the gap. The seconds are @p local's own.
 *
 * @param local   The local time; not written.
 * @param later_s Seconds to add: how long ago @p local was read.
 * @param utc_s   Receives the second, on true.
 * @return false when the C library cannot convert it, or when the result
 *         is not after KODE_CLOCK_KNOWN_AFTER_S (the clock would not count it
 *         as known, khal_time_wall_known()) or not before KGUI_CLOCK_UTC_END_S.
 */
bool kgui_clock_utc_of(struct tm const *local, uint32_t later_s, int64_t *utc_s);

/**
 * @brief Step one row of a local time, as a press does, and say which UTC
 *        second the new reading is.
 *
 * kgui_clock_step() and then kgui_clock_utc_of(). On the night summer time
 * starts, an hour stepped into the gap names no reading the clock can show,
 * and the C library may put it back where it was: then the hour steps once
 * more, so a press always moves the clock the way it was asked. The night
 * summer time ends, the hour that comes twice is read as the C library
 * decides (tm_isdst -1), and the page cannot ask for the other one.
 *
 * @param from  The local time the row shows; not written.
 * @param field The row.
 * @param dir   -1 for one less, anything else for one more.
 * @param utc_s Receives the new UTC second, on true.
 * @return false when the step does nothing: the year at either end, a bad
 *         field, or a reading kgui_clock_utc_of() refuses.
 */
bool kgui_clock_step_to(struct tm const *from, kgui_clock_field_t field, int8_t dir,
                        int64_t *utc_s);

/**
 * @brief Whether the wall clock still stands where the Time page left it.
 *
 * The page notes the second it set and when (khal_time_ms()); a clock that
 * has run on from there, give or take KGUI_CLOCK_HAND_SLACK_US, is still the
 * page's. One the console, the Kode channel or a time server wrote since is
 * not, and a change of zone moves its hour instead of keeping its reading.
 *
 * @param set_s    The UTC second the page set.
 * @param since_ms Milliseconds since it set it.
 * @param wall_us  The wall clock now, UTC microseconds (khal_time_wall_us()).
 * @return true while the clock is where the page left it.
 */
bool kgui_clock_hand_holds(int64_t set_s, uint32_t since_ms, uint64_t wall_us);

/* ---- The zones ---- */

/**
 * @brief Where @p tz stands among the zones, west to east.
 * @param tz A POSIX TZ string; empty is UTC, a board with nothing set.
 * @return Its place, or KGUI_CLOCK_ZONES when the table lacks it.
 */
uint8_t kgui_clock_zone_find(const char *tz);

/**
 * @brief The words the Time zone row and the log give @p tz.
 * @param tz A POSIX TZ string, or empty for UTC.
 * @return The city, "Custom" for one the table lacks; never NULL.
 */
const char *kgui_clock_zone_label(const char *tz);

/**
 * @brief The POSIX TZ string of one zone.
 * @param at A place below KGUI_CLOCK_ZONES.
 * @return The string; UTC's for a place out of range.
 */
const char *kgui_clock_zone_tz(uint8_t at);

/**
 * @brief The zone a press of the Time zone row takes the clock to.
 * @param tz  The zone in force; one the table lacks steps from UTC's place.
 * @param dir -1 for the one to the west, anything else for the one to the east.
 * @return The new zone's place, round both ways.
 */
uint8_t kgui_clock_zone_step(const char *tz, int8_t dir);

/* ---- The face ---- */

/** What the face's yellow pill asks for. */
typedef enum {
    KGUI_CLOCK_ASK_NONE = 0,  /**< Nothing: the clock and the zone are known. */
    KGUI_CLOCK_ASK_TIME,      /**< "Set the time": the clock was never told. */
    KGUI_CLOCK_ASK_ZONE,      /**< "Choose your time zone": nobody chose one. */
} kgui_clock_ask_t;

/**
 * @brief What the face asks for; either pill opens the Time page.
 * @param known       The wall clock has been told the time.
 * @param zone_heard  AO_Boot has said which zone is in force.
 * @param zone_stored Somebody chose that zone.
 * @param at_rest     The face is at Rest, where nothing asks.
 * @return The one thing asked, or KGUI_CLOCK_ASK_NONE.
 */
kgui_clock_ask_t kgui_clock_ask(bool known, bool zone_heard, bool zone_stored, bool at_rest);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_CLOCK_H */
