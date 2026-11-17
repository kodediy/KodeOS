/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_selftest.h
 * @brief What the Self-test page says about the last run: its verdict and
 *        the line under its rows.
 *
 * Pure: no LVGL and no QP. AO_Gui counts the rows of the kept report and
 * everything said about the count is decided here.
 */
#ifndef KGUI_SELFTEST_H
#define KGUI_SELFTEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The words ---------------------------------------------------------------- */

#define KGUI_SELFTEST_VERDICT_MAX 16U                           /**< A verdict's bytes, terminator included: "255 not checked" is the longest. */
#define KGUI_SELFTEST_NEVER       "Never run"                   /**< No run kept: the Settings row and the page say it alike. */
#define KGUI_SELFTEST_ALL_GOOD    "All good"                    /**< Every row of the run passed. */
#define KGUI_SELFTEST_FOOT_FIRST  "Takes a few minutes"         /**< Under the page before any run. */
#define KGUI_SELFTEST_FOOT_AGAIN  "Tap a row to test it again"  /**< Under a run with no failure. */
/** Under a run with a failure: what to do next, a line each. */
#define KGUI_SELFTEST_FOOT_FAILED "Tap a row to test it again.\nStill failing?\nContact Kode support."

/** @brief How the rows of a run ended, counted. */
typedef struct {
    uint8_t rows;    /**< Rows in the run; 0: no run kept. */
    uint8_t passed;  /**< Rows whose owner said pass. */
    uint8_t failed;  /**< Rows whose owner said fail. */
} kgui_selftest_tally_t;

/**
 * @brief Whether the run can be called good.
 *
 * @param t The tally.
 * @return true only when there was a run and every row of it passed.
 */
bool kgui_selftest_all_good(const kgui_selftest_tally_t *t);

/**
 * @brief The rows nobody judged: no answer came, what a row needs was not
 *        there, or a state this build does not know.
 *
 * @param t The tally.
 * @return The rows that neither passed nor failed; 0 when the two outnumber the rows.
 */
uint8_t kgui_selftest_unchecked(const kgui_selftest_tally_t *t);

/**
 * @brief The verdict on the run: "Never run", "All good", "N problems" when
 *        a row failed, else "N not checked".
 *
 * @param t   The tally.
 * @param out Receives the words, always terminated; left as it was when NULL or when @p cap is 0.
 * @param cap Bytes @p out holds, terminator included; KGUI_SELFTEST_VERDICT_MAX holds every verdict.
 */
void kgui_selftest_verdict(const kgui_selftest_tally_t *t, char *out, size_t cap);

/**
 * @brief The line under the page's rows.
 *
 * @param t The tally.
 * @return KGUI_SELFTEST_FOOT_FIRST, _AGAIN or _FAILED.
 */
const char *kgui_selftest_footer(const kgui_selftest_tally_t *t);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_SELFTEST_H */
