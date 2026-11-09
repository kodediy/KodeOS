/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_group.c
 * @brief The launcher's groups (kgui_group.h).
 */
#include "kgui_group.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "kapp_cat.h"
#include "kui_tokens.h"

_Static_assert(KGUI_GROUP_NAME_MAX == KAPP_CATEGORY_ITEM_MAX,
               "a group's name is one category item");
_Static_assert(KGUI_GROUP_CATS_LEN == KAPP_CATEGORY_MAX,
               "an app's list is one category list");

#define GROUP_GENERAL    "general"  /**< Where an app with no category, or `other`, goes (Luismi, 5-nov). */

/** @brief A folder of the marketplace's closed list: its words, its glyph and its disc's colour. */
typedef struct {
    const char *key;    /**< The group's name as the model keeps it: lowercase, one word. */
    const char *alias;  /**< The other form of the same word, which files an app under the same group. */
    const char *title;  /**< The folder's name as the screen writes it. */
    const char *glyph;  /**< The glyph's name (kgui_list_group_glyph()). */
    uint32_t    rgb;    /**< The disc's colour, 0xRRGGBB, from the kit's palette. */
} group_art_t;

/*
 * The folders, in the order of the root: the marketplace's seven
 * (kodediy/ecosystem, packages/types/src/app-categories.ts), then the twelve
 * the launcher design of 5-nov-2026 drew for what a Dot is for, and Time
 * (kode-dot-os, informes/2026-11-05-lanzador/DISENO.md). kgui knows the
 * thirteen new words whenever an app's category says them, before the
 * marketplace takes them. The names come from here and never from a rule, so
 * no folder is given a plural nobody wrote (`ai` is AI, not Ais). Every folder
 * wears the same Medium Grey disc, the apocrifa's disc of an app with no art:
 * a folder is told by its drawing and its name, in Soft Black with a touch of
 * Bold Yellow (kgui_folder_art(), Luismi, 5-nov, over decision 0040). A word
 * that is not here has no drawing and its disc wears its letters
 * (kgui_group_mono()).
 */
static const group_art_t s_art[] = {
    /* General first, when it has apps (Luismi, 5-nov). */
    { "general",       "other",          "General",       "apps",    KUI_PALETTE_MEDIUM_GREY },
    { "games",         "game",           "Games",         "games",   KUI_PALETTE_MEDIUM_GREY },
    { "tools",         "tool",           "Tools",         "tool",    KUI_PALETTE_MEDIUM_GREY },
    { "communication", "communications", "Communication", "talk",    KUI_PALETTE_MEDIUM_GREY },
    { "wireless",      "wirelesses",     "Wireless",      "wifi",    KUI_PALETTE_MEDIUM_GREY },
    { "creative",      "creatives",      "Creative",      "brush",   KUI_PALETTE_MEDIUM_GREY },
    { "maker",         "makers",         "Maker",         "maker",   KUI_PALETTE_MEDIUM_GREY },
    { "ai",            "ais",            "AI",            "spark",   KUI_PALETTE_MEDIUM_GREY },
    { "sensors",       "sensor",         "Sensors",       "gauge",   KUI_PALETTE_MEDIUM_GREY },
    { "radio",         "sdr",            "Radio",         "antenna", KUI_PALETTE_MEDIUM_GREY },
    { "security",      "infosec",        "Security",      "shield",  KUI_PALETTE_MEDIUM_GREY },
    { "music",         "audio",          "Music",         "note",    KUI_PALETTE_MEDIUM_GREY },
    { "camera",        "photo",          "Camera",        "camera",  KUI_PALETTE_MEDIUM_GREY },
    { "robotics",      "robot",          "Robotics",      "robot",   KUI_PALETTE_MEDIUM_GREY },
    { "home",          "smarthome",      "Home",          "home",    KUI_PALETTE_MEDIUM_GREY },
    { "health",        "fitness",        "Health",        "heart",   KUI_PALETTE_MEDIUM_GREY },
    { "navigation",    "maps",           "Navigation",    "pin",     KUI_PALETTE_MEDIUM_GREY },
    { "time",          "clock",          "Time",          "clock",   KUI_PALETTE_MEDIUM_GREY },
    { "productivity",  "office",         "Productivity",  "list",    KUI_PALETTE_MEDIUM_GREY },
    { "education",     "learning",       "Education",     "book",    KUI_PALETTE_MEDIUM_GREY },
    { "developer",     "dev",            "Developer",     "code",    KUI_PALETTE_MEDIUM_GREY },
};

#define GROUP_ART_N (sizeof(s_art) / sizeof(s_art[0]))  /**< How many folders the table names. */

_Static_assert(GROUP_ART_N == KGUI_GROUP_ART_N, "the table names every folder kgui_group.h counts");

/**
 * @brief The table's row for a lowercase name, in either of its forms.
 *
 * @param name A lowercase group name.
 * @return Its row, or NULL for a word the table does not know.
 */
static const group_art_t *group_row(const char *name)
{
    const group_art_t *row = NULL;

    for ( size_t i = 0U; (GROUP_ART_N > i) && (NULL == row); ++i )
    {
        if ( (strcmp(name, s_art[i].key) == 0) || (strcmp(name, s_art[i].alias) == 0) )
        {
            row = &s_art[i];
        }
    }
    return row;
}

/**
 * @brief Take the first category of a list as the group it files an app under.
 *
 * The word is lowercased. A word of the table, in either of its forms, is
 * the table's key (`game` and `games` are the group `games`); any other word
 * stays as it is, with no plural made up for it.
 *
 * A list that is empty files its app under `general`.
 *
 * @param list An app's category list.
 * @param name Out: KGUI_GROUP_NAME_MAX bytes; `general` when there is none.
 */
static void group_first(const char *list, char *name)
{
    char item[KAPP_CATEGORY_ITEM_MAX];

    (void)snprintf(name, KGUI_GROUP_NAME_MAX, "%s", GROUP_GENERAL);
    if ( kapp_cat_next(list, item, sizeof(item)) != NULL )
    {
        size_t i = 0U;

        while ( (i < (KGUI_GROUP_NAME_MAX - 1U)) && ('\0' != item[i]) )
        {
            name[i] = (char)tolower((unsigned char)item[i]);
            ++i;
        }
        name[i] = '\0';
        if ( '\0' == name[0] )
        {
            (void)snprintf(name, KGUI_GROUP_NAME_MAX, "%s", GROUP_GENERAL);
        }
        else
        {
            const group_art_t * const row = group_row(name);

            if ( NULL != row )
            {
                (void)snprintf(name, KGUI_GROUP_NAME_MAX, "%s", row->key);
            }
        }
    }
}

/**
 * @brief Where a group stands among the folders: the table's place, and
 *        past the table for any other word.
 *
 * @param name A group's name as group_first() gives it.
 * @return Its place in the table, or the table's size.
 */
static size_t group_rank(const char *name)
{
    size_t rank = GROUP_ART_N;

    for ( size_t i = 0U; GROUP_ART_N > i; ++i )
    {
        if ( strcmp(name, s_art[i].key) == 0 )
        {
            rank = i;
        }
    }
    return rank;
}

/**
 * @brief Compare two groups in the root's order: the table's, then the
 *        other words alphabetically.
 *
 * @param a A group's name.
 * @param b Another's.
 * @return Negative when @p a comes first, positive when @p b does, 0 for the same.
 */
static int group_cmp(const char *a, const char *b)
{
    size_t const ra  = group_rank(a);
    size_t const rb  = group_rank(b);
    int          cmp = strcmp(a, b);

    if ( ra < rb )
    {
        cmp = -1;
    }
    else if ( ra > rb )
    {
        cmp = 1;
    }
    else
    {
        /* The same place in the table, or both past it: the words decide. */
    }
    return cmp;
}

/**
 * @brief Find a group by name.
 *
 * @param groups The groups made so far.
 * @param count  How many.
 * @param name   The name to look for.
 * @return Its place, or @p count when there is none.
 */
static uint8_t group_find(const kgui_group_t *groups, uint8_t count,
                          const char *name)
{
    uint8_t i = 0U;

    while ( (i < count) && (strcmp(groups[i].name, name) != 0) )
    {
        ++i;
    }
    return i;
}

uint8_t kgui_group_root(const char (*cats)[KGUI_GROUP_CATS_LEN], uint8_t n,
                        kgui_group_t *groups, uint8_t max,
                        kgui_group_row_t *rows, uint8_t rows_max)
{
    char    name[KGUI_GROUP_NAME_MAX];
    char    best[KGUI_GROUP_NAME_MAX];
    uint8_t cap = max;
    uint8_t ng  = 0U;
    uint8_t nr  = 0U;
    uint8_t i;
    uint8_t g;

    if ( (NULL == cats) || (NULL == groups) || (NULL == rows) )
    {
        return 0U;
    }
    if ( KGUI_GROUP_MAX < cap )
    {
        cap = KGUI_GROUP_MAX;
    }

    /* Pass 1: the groups, the next one in the root's order each time, so the
     * cap keeps the first folders of the table and not the first apps seen. */
    while ( ng < cap )
    {
        bool found = false;

        for ( i = 0U; i < n; ++i )
        {
            group_first(cats[i], name);
            if ( ((0U == ng) || (group_cmp(name, groups[ng - 1U].name) > 0))
                && ((!found) || (group_cmp(name, best) < 0)) )
            {
                (void)snprintf(best, sizeof(best), "%s", name);
                found = true;
            }
        }
        if ( !found )
        {
            break;
        }
        (void)snprintf(groups[ng].name, sizeof(groups[ng].name), "%s", best);
        groups[ng].count = 0U;
        ++ng;
    }
    for ( i = 0U; i < n; ++i )
    {
        group_first(cats[i], name);
        g = group_find(groups, ng, name);
        if ( g < ng )
        {
            ++groups[g].count;
        }
    }
    for ( g = 0U; (g < ng) && (nr < rows_max); ++g )
    {
        rows[nr].kind = KGUI_GROUP_ROW_GROUP;
        rows[nr].idx  = g;
        ++nr;
    }

    /* Pass 2: the apps that belong to no group. */
    for ( i = 0U; (i < n) && (nr < rows_max); ++i )
    {
        group_first(cats[i], name);
        if ( group_find(groups, ng, name) >= ng )
        {
            rows[nr].kind = KGUI_GROUP_ROW_APP;
            rows[nr].idx  = i;
            ++nr;
        }
    }
    return nr;
}

uint8_t kgui_group_members(const char (*cats)[KGUI_GROUP_CATS_LEN], uint8_t n,
                           const char *group, uint8_t *idx, uint8_t max)
{
    char    want[KGUI_GROUP_NAME_MAX];
    char    name[KGUI_GROUP_NAME_MAX];
    uint8_t out = 0U;
    uint8_t i;

    if ( (NULL == cats) || (NULL == group) || (NULL == idx) )
    {
        return 0U;
    }
    /* The wanted name goes through the same door as an app's category. */
    group_first(group, want);
    for ( i = 0U; (i < n) && (out < max); ++i )
    {
        group_first(cats[i], name);
        if ( strcmp(name, want) == 0 )
        {
            idx[out] = i;
            ++out;
        }
    }
    return out;
}

bool kgui_group_of(const char *cats, char *name)
{
    bool named = false;

    if ( (NULL != cats) && (NULL != name) )
    {
        group_first(cats, name);
        named = true;
    }
    return named;
}

const char *kgui_group_glyph(const char *group)
{
    char               name[KGUI_GROUP_NAME_MAX];
    const group_art_t *row   = NULL;
    const char        *glyph = NULL;

    if ( NULL != group )
    {
        group_first(group, name);
        row = group_row(name);
    }
    if ( NULL != row )
    {
        glyph = row->glyph;
    }
    return glyph;
}

/* cppcheck-suppress misra-c2012-8.7 ; public API of the group module, taken by its host test */
bool kgui_group_rgb(const char *group, uint32_t *rgb)
{
    char               name[KGUI_GROUP_NAME_MAX];
    const group_art_t *row = NULL;
    bool               own = false;

    if ( NULL != group )
    {
        group_first(group, name);
        row = group_row(name);
    }
    if ( (NULL != row) && (NULL != rgb) )
    {
        *rgb = row->rgb;
        own  = true;
    }
    return own;
}

void kgui_group_title(const char *group, char *out)
{
    char name[KGUI_GROUP_NAME_MAX];

    if ( NULL == out )
    {
        return;
    }
    out[0] = '\0';
    if ( NULL != group )
    {
        const group_art_t *row;

        group_first(group, name);
        row = group_row(name);
        if ( NULL != row )
        {
            (void)snprintf(out, KGUI_GROUP_NAME_MAX, "%s", row->title);
        }
        else
        {
            /* A word of nobody's table: its own letters, the first a capital. */
            (void)snprintf(out, KGUI_GROUP_NAME_MAX, "%s", name);
            out[0] = (char)toupper((unsigned char)out[0]);
        }
    }
}

/* cppcheck-suppress misra-c2012-8.7 ; public API of the group module, taken by its host test */
const char *kgui_group_art_key(uint8_t i)
{
    const char *key = NULL;

    if ( GROUP_ART_N > i )
    {
        key = s_art[i].key;
    }
    return key;
}

/* cppcheck-suppress misra-c2012-8.7 ; public API of the group module, taken by its host test */
const char *kgui_group_art_alias(uint8_t i)
{
    const char *alias = NULL;

    if ( GROUP_ART_N > i )
    {
        alias = s_art[i].alias;
    }
    return alias;
}

/**
 * @brief How many bytes the UTF-8 character at @p s takes.
 *
 * A lead byte and the continuation bytes (10xxxxxx) after it, at most four;
 * a stray continuation byte counts as one, so nothing is ever cut short of
 * the terminator.
 *
 * @param s A string, not empty.
 * @return 1 to 4.
 */
static size_t group_char_len(const char *s)
{
    size_t len = 1U;

    while ( (4U > len) && ((((unsigned char)s[len]) & 0xC0U) == 0x80U) )
    {
        ++len;
    }
    return len;
}

/**
 * @brief Whether two titles start with the same character: an ASCII letter
 *        in either case, any other character byte for byte.
 *
 * @param a A title, not empty.
 * @param b Another, not empty.
 * @return true when the first characters are the same.
 */
static bool group_same_first(const char *a, const char *b)
{
    size_t const la   = group_char_len(a);
    bool         same = (la == group_char_len(b));

    if ( same && (1U == la) )
    {
        same = (tolower((unsigned char)a[0]) == tolower((unsigned char)b[0]));
    }
    else if ( same )
    {
        same = (strncmp(a, b, la) == 0);
    }
    else
    {
        /* Characters of different lengths are different characters. */
    }
    return same;
}

/**
 * @brief Append the character at @p s to @p out, an ASCII letter in the case
 *        asked for and any other character as it is.
 *
 * @param out   The letters so far, terminated, with room for one more
 *              character and the terminator (KGUI_GROUP_MONO_MAX).
 * @param s     The character.
 * @param upper true for a capital, false for a small letter.
 */
static void group_mono_put(char *out, const char *s, bool upper)
{
    size_t const at  = strlen(out);
    size_t const len = group_char_len(s);

    (void)memcpy(&out[at], s, len);
    out[at + len] = '\0';
    if ( (1U == len) && isalpha((unsigned char)s[0]) )
    {
        out[at] = (char)(upper ? toupper((unsigned char)s[0]) : tolower((unsigned char)s[0]));
    }
}

void kgui_group_mono(const char *title, const char *const *artless, uint8_t n,
                     char out[KGUI_GROUP_MONO_MAX])
{
    bool twin = false;

    if ( NULL == out )
    {
        return;
    }
    out[0] = '\0';
    if ( (NULL == title) || ('\0' == title[0]) )
    {
        return;
    }
    for ( uint8_t i = 0U; (i < n) && (NULL != artless) && !twin; ++i )
    {
        const char * const other = artless[i];

        /* The folder itself may be in the list: it is no twin of its own. */
        twin = (NULL != other) && ('\0' != other[0]) && (strcmp(other, title) != 0)
               && group_same_first(title, other);
    }
    group_mono_put(out, title, true);
    if ( twin )
    {
        const char * const second = &title[group_char_len(title)];

        if ( '\0' != second[0] )
        {
            group_mono_put(out, second, false);
        }
    }
}
