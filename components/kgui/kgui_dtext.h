/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_dtext.h
 * @brief The words of an app's details: its size, its build date and the line under its name.
 */
#ifndef KGUI_DTEXT_H
#define KGUI_DTEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KGUI_DTEXT_SIZE_MAX 12U  /**< A size's words, terminator included: "4096.0 MB". */
#define KGUI_DTEXT_DATE_MAX 16U  /**< A date's words, terminator included: "28 Oct 2026". */
#define KGUI_DTEXT_META_MAX 40U  /**< The line under the name, terminator included: "v", a 15 character version, the 4 bytes of " · " and a 15 character category. */

/**
 * @brief Say a size: whole KB under 1 MB, MB with one decimal from there.
 *
 * A size that rounds to 1024 KB is said as 1.0 MB, and a size under half a KB
 * as 1 KB: no size reads 0. A size of 0 is not known and gives "".
 *
 * @param bytes The size.
 * @param out   Receives the words, terminated.
 * @param cap   Its size.
 */
void kgui_dtext_size(uint32_t bytes, char *out, size_t cap);

/**
 * @brief Say a build date the way the compiler wrote it, day first.
 *
 * `Nov  2 2026` (the compiler's __DATE__: three letters, a day padded with a
 * space, four digits) becomes `2 Nov 2026`. Anything else, and an empty
 * string, gives "".
 *
 * @param raw The date as AppEntryEvt carries it; may be NULL.
 * @param out Receives the words, terminated.
 * @param cap Its size.
 */
void kgui_dtext_date(const char *raw, char *out, size_t cap);

/**
 * @brief The line under the app's name: `v<version> · <Category>`.
 *
 * Each part is left out when it is not known, with its dot. A version
 * that already begins with a `v` or a `V` is not given a second one, and the
 * category's first letter is made a capital.
 *
 * @param version  The version as the system has it; may be NULL or "".
 * @param category The group's title (kgui_group_title()); may be NULL or "".
 * @param out      Receives the words, terminated.
 * @param cap      Its size.
 */
void kgui_dtext_meta(const char *version, const char *category, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_DTEXT_H */
