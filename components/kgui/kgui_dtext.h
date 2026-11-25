/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_dtext.h
 * @brief The words of an app's details: its size and the line under its name.
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
#define KGUI_DTEXT_META_MAX 64U  /**< The line under the name, terminator included: a 15 character category, a 15 character version and a size, with two " · " of 4 bytes each between. */
#define KGUI_DTEXT_DELETE_BODY "It's deleted with its files." /**< The body of the question before Delete. */

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
 * @brief Say a version without its `v` and without the leading zeros of each
 *        group: `00.07.05` is `0.7.5`.
 *
 * @param version The version as the system has it; may be NULL.
 * @param out     Receives the words, terminated.
 * @param cap     Its size.
 */
void kgui_dtext_version(const char *version, char *out, size_t cap);

/**
 * @brief The line under the app's name: `<Category> · <version> · <size>`.
 *
 * Each part is left out when it is not known, with its dot. The version
 * loses a leading `v` or `V` and the leading zeros of each of its groups
 * (`00.07.05` is `0.7.5`), and the category's first letter is made a capital.
 * `Communication` is said `Comms`, so the line fits its column.
 *
 * @param category The group's title (kgui_group_title()); may be NULL or "".
 * @param version  The version as the system has it; may be NULL or "".
 * @param size     The size's words (kgui_dtext_size()); may be NULL or "".
 * @param out      Receives the words, terminated.
 * @param cap      Its size.
 */
void kgui_dtext_meta(const char *category, const char *version, const char *size, char *out,
                     size_t cap);

/**
 * @brief What the launcher says when a Remove did not take everything.
 *
 * Reads the status the erase ended with (AppEraseDoneEvt.status, a
 * kode_status_t): something of the app would not go, the owner was busy,
 * there was nothing of that name, or no answer came.
 *
 * @param status How the erase ended.
 * @return The hint's words; NULL for an erase that left nothing, which the
 *         notice that names the app says instead.
 */
const char *kgui_dtext_remove_end(uint8_t status);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_DTEXT_H */
