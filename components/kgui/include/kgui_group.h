/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_group.h
 * @brief The launcher's groups: the apps filed by their first category.
 */
#ifndef KGUI_GROUP_H
#define KGUI_GROUP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ---- */

#define KGUI_GROUP_MAX        21U  /**< Most groups at the root: every folder of the table; the apps of any more are loose. */
#define KGUI_GROUP_NAME_MAX   16U  /**< A group's name, terminator included (KAPP_CATEGORY_ITEM_MAX). */
#define KGUI_GROUP_CATS_LEN   64U  /**< One app's category list, terminator included (KAPP_CATEGORY_MAX). */
#define KGUI_GROUP_ART_N      21U  /**< The folders the table draws: each has its glyph. */
#define KGUI_GROUP_MONO_MAX   9U   /**< A folder's letters: two UTF-8 characters and the terminator. */

/* ---- Rows ---- */

#define KGUI_GROUP_ROW_GROUP  0U   /**< A row that is a group: idx is its place in the groups array. */
#define KGUI_GROUP_ROW_APP    1U   /**< A row that is a loose app: idx is its place in the apps given. */

/** @brief One group at the root. */
typedef struct {
    char    name[KGUI_GROUP_NAME_MAX];  /**< The group's key: the first category, lowercased (the table's word for those it has). */
    uint8_t count;                      /**< How many apps it holds. */
} kgui_group_t;

/** @brief One row of the root: a group, or an app with none. */
typedef struct {
    uint8_t kind;  /**< KGUI_GROUP_ROW_GROUP or KGUI_GROUP_ROW_APP. */
    uint8_t idx;   /**< Into the groups array or into the apps, by kind. */
} kgui_group_row_t;

/* ---- The model ---- */

/**
 * @brief Build the root: the groups, then the loose apps.
 *
 * Groups come in the table's order (games, tools, communication, wireless,
 * creative, maker, ai, sensors, radio, security, music, camera, robotics,
 * home, health, navigation, time, productivity, education, developer), then
 * any other word alphabetically, whatever the
 * order of the apps. At most KGUI_GROUP_MAX groups (and @p max) are made; an
 * app whose group would be one more (the last in that order) is a loose row.
 * A group of a single app is still a group.
 *
 * @param cats     Each app's category list, in display order (installed
 *                 first, then files). Lists are normalised (kapp_cat.h).
 * @param n        How many apps.
 * @param groups   Out: the groups made, in order. All of them are written even
 *                 when @p rows_max cuts the group rows short, so a group row's
 *                 idx always points at a group that is there; the number of
 *                 groups is the number of group rows returned in @p rows,
 *                 which @p rows_max may leave short of the groups made.
 * @param max      Room in @p groups.
 * @param rows     Out: the groups, then the loose apps.
 * @param rows_max Room in @p rows; rows past it are dropped.
 * @return The number of rows written.
 */
uint8_t kgui_group_root(const char (*cats)[KGUI_GROUP_CATS_LEN], uint8_t n,
                        kgui_group_t *groups, uint8_t max,
                        kgui_group_row_t *rows, uint8_t rows_max);

/**
 * @brief List the apps of one group, in order.
 *
 * A name that holds no app (the group is gone, or never was) gives 0, and
 * the caller falls back to the root. The empty name and `other` never name
 * a group.
 *
 * The answer is by name alone and knows nothing of the cap on groups
 * (KGUI_GROUP_MAX): a group the cap made loose at the root still lists its
 * apps here, and those apps are also loose rows of kgui_group_root().
 *
 * @param cats  Each app's category list, as for kgui_group_root().
 * @param n     How many apps.
 * @param group The group's name; the case is folded and a word of the table
 *              may come in either form, so `game` asks for `games`.
 * @param idx   Out: the place of each app of the group in @p cats.
 * @param max   Room in @p idx; apps past it are dropped.
 * @return The number of apps written.
 */
uint8_t kgui_group_members(const char (*cats)[KGUI_GROUP_CATS_LEN], uint8_t n,
                           const char *group, uint8_t *idx, uint8_t max);

/**
 * @brief The group one app's category list files it under.
 *
 * The same rule kgui_group_root() uses: the first item of the list, lowercased;
 * an empty list, or one that starts with `other`, files the app nowhere.
 *
 * @param cats One app's category list.
 * @param name Out: the group's name, KGUI_GROUP_NAME_MAX bytes; empty for none.
 * @return true when the list names a group.
 */
bool kgui_group_of(const char *cats, char *name);

/**
 * @brief The standard glyph of a group: the table's (`games`, `tool`, `talk`,
 *        `wifi`, `brush`, `maker`, `spark`, `gauge`, `antenna`, `shield`,
 *        `note`, `camera`, `robot`, `home`, `heart`, `pin`, `clock`, `list`,
 *        `book`, `code`), and none for any other name.
 *
 * A word of the table may come in either form (`tool` is `tools`). The
 * returned string is static. A group with no glyph wears its letters
 * (kgui_group_mono()).
 *
 * @param group A group's name; NULL has none.
 * @return The glyph's name, or NULL for a group the table does not draw.
 */
const char *kgui_group_glyph(const char *group);

/**
 * @brief The colour of a group's disc, for the groups of the table.
 *
 * Each is a colour of the kit's palette, never the focus ring's yellow. A
 * word the table does not have has no colour of its own and its disc stays
 * the kit's Medium Grey, as an app's with no colour does.
 *
 * @param group A group's name; NULL has no colour.
 * @param rgb   Out: the colour as 0xRRGGBB, written only when true is returned.
 * @return true when the group has a colour of its own.
 */
bool kgui_group_rgb(const char *group, uint32_t *rgb);

/**
 * @brief A group's name as the screen writes it.
 *
 * The table's title for a word it has (`ai` is `AI`, `games` is `Games`),
 * any other word with its first letter a capital and nothing else changed.
 *
 * @param group A group's name in any case and either form; NULL, "" and
 *              `other` name none.
 * @param out   Out: KGUI_GROUP_NAME_MAX bytes; empty for none.
 */
void kgui_group_title(const char *group, char *out);

/**
 * @brief The letters on the disc of a folder that has no glyph.
 *
 * One: the title's first character, an ASCII letter in capitals. Two, the
 * second a small letter, only when another folder with no glyph at the same
 * root starts with the same character (Farming and Finance are `Fa` and
 * `Fi`); two that still agree are let be (Weather and Wearables are `We` and
 * `We`): the name stands beside the disc. A first character that is not an
 * ASCII letter is taken whole, as UTF-8, and not changed.
 *
 * @param title   The folder's title (kgui_group_title()).
 * @param artless The titles of the root's folders with no glyph; @p title
 *                itself may be among them. NULL for none.
 * @param n       How many.
 * @param out     Out: the letters, terminated; empty for an empty title.
 */
void kgui_group_mono(const char *title, const char *const *artless, uint8_t n,
                     char out[KGUI_GROUP_MONO_MAX]);

/**
 * @brief The table's key at a place, for the tests that walk it.
 *
 * @param i The place, 0 to KGUI_GROUP_ART_N - 1.
 * @return The key, or NULL past the table.
 */
const char *kgui_group_art_key(uint8_t i);

/**
 * @brief The table's other form of the word at a place.
 *
 * @param i The place, 0 to KGUI_GROUP_ART_N - 1.
 * @return The alias, or NULL past the table.
 */
const char *kgui_group_art_alias(uint8_t i);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_GROUP_H */
