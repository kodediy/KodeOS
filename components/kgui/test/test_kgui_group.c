/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_kgui_group.c
 * @brief Host tests of the group model (kgui_group.h).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "kgui_group.h"
#include "kui_tokens.h"

#define APPS_MAX     32U  /**< Most apps one case builds. */
#define ROWS_MAX     32U  /**< Room for the rows of any case. */
#define NAME_LEN     8U   /**< A generated category: "c" and its number. */
#define TABLE_N      21U  /**< The folders the table draws: the marketplace's seven, then the design's thirteen. */

/** @brief One folder of the table, as the tests expect it. */
typedef struct {
    const char *key;     /**< The key the model keeps. */
    const char *plural;  /**< The other form of the word. */
    const char *title;   /**< How the screen writes it. */
    const char *glyph;   /**< The glyph's name. */
    uint32_t    rgb;     /**< The disc's colour: a colour of the kit's palette. */
} want_t;

/** @brief The table, in the marketplace's order. */
static const want_t s_want[TABLE_N] = {
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

/** @brief The glyphs kgui_list_group_glyph() has a picture for: the kit's seven, then kgui's own. */
static const char *const s_glyphs[] = {
    "apps", "games", "tool", "talk", "wifi", "brush", "maker", "spark",
    "gauge", "antenna", "shield", "note", "camera", "robot", "home", "heart", "pin",
    "clock", "list", "book", "code",
};

/** @brief The apps' category lists the case under test is given. */
static char s_cats[APPS_MAX][KGUI_GROUP_CATS_LEN];
/** @brief The groups the root made. */
static kgui_group_t s_groups[KGUI_GROUP_MAX];
/** @brief The rows the root made. */
static kgui_group_row_t s_rows[ROWS_MAX];

/**
 * @brief Set app @p i's category list.
 *
 * @param i    The app.
 * @param list Its category list.
 */
static void app(uint8_t i, const char *list)
{
    (void)snprintf(s_cats[i], sizeof(s_cats[i]), "%s", list);
}

/**
 * @brief Build the root of the first @p n apps.
 *
 * @param n How many apps.
 * @return The number of rows.
 */
static uint8_t root(uint8_t n)
{
    return kgui_group_root(s_cats, n, s_groups, KGUI_GROUP_MAX,
                           s_rows, ROWS_MAX);
}

/**
 * @brief Check one row.
 *
 * @param r    The row's place.
 * @param kind The kind it must have.
 * @param idx  The index it must carry.
 */
static void row_is(uint8_t r, uint8_t kind, uint8_t idx)
{
    TEST_ASSERT_EQUAL_UINT8(kind, s_rows[r].kind);
    TEST_ASSERT_EQUAL_UINT8(idx, s_rows[r].idx);
}

void setUp(void)
{
    (void)memset(s_cats, 0, sizeof(s_cats));
    (void)memset(s_groups, 0, sizeof(s_groups));
    (void)memset(s_rows, 0, sizeof(s_rows));
}

void tearDown(void)
{
}

void test_group_first_category_only(void)
{
    uint8_t idx[APPS_MAX];

    app(0U, "games,tools");
    app(1U, "tools");
    app(2U, "games");

    TEST_ASSERT_EQUAL_UINT8(2U, root(3U));
    TEST_ASSERT_EQUAL_STRING("games", s_groups[0].name);
    TEST_ASSERT_EQUAL_UINT8(2U, s_groups[0].count);
    TEST_ASSERT_EQUAL_STRING("tools", s_groups[1].name);
    TEST_ASSERT_EQUAL_UINT8(1U, s_groups[1].count);

    /* The app with two categories is in games alone. */
    TEST_ASSERT_EQUAL_UINT8(2U, kgui_group_members(s_cats, 3U, "games", idx, APPS_MAX));
    TEST_ASSERT_EQUAL_UINT8(0U, idx[0]);
    TEST_ASSERT_EQUAL_UINT8(2U, idx[1]);
    TEST_ASSERT_EQUAL_UINT8(1U, kgui_group_members(s_cats, 3U, "tools", idx, APPS_MAX));
    TEST_ASSERT_EQUAL_UINT8(1U, idx[0]);
}

void test_group_no_category_goes_to_general(void)
{
    app(0U, "");
    app(1U, "other");
    app(2U, "games");
    app(3U, "other,games");

    TEST_ASSERT_EQUAL_UINT8(2U, root(4U));
    row_is(0U, KGUI_GROUP_ROW_GROUP, 0U);
    row_is(1U, KGUI_GROUP_ROW_GROUP, 1U);
    TEST_ASSERT_EQUAL_STRING("general", s_groups[0].name);
    TEST_ASSERT_EQUAL_UINT8(3U, s_groups[0].count);
    TEST_ASSERT_EQUAL_STRING("games", s_groups[1].name);
    TEST_ASSERT_EQUAL_UINT8(1U, s_groups[1].count);
}

void test_group_order_follows_the_table(void)
{
    /* The apps come in any order; the root does not follow it. */
    app(0U, "ai");
    app(1U, "zeta");
    app(2U, "tools");
    app(3U, "alpha");
    app(4U, "games");
    app(5U, "tools");
    app(6U, "maker");
    app(7U, "wireless");
    app(8U, "creative");
    app(9U, "communication");

    TEST_ASSERT_EQUAL_UINT8(9U, root(10U));
    TEST_ASSERT_EQUAL_STRING("games", s_groups[0].name);
    TEST_ASSERT_EQUAL_STRING("tools", s_groups[1].name);
    TEST_ASSERT_EQUAL_UINT8(2U, s_groups[1].count);
    TEST_ASSERT_EQUAL_STRING("communication", s_groups[2].name);
    TEST_ASSERT_EQUAL_STRING("wireless", s_groups[3].name);
    TEST_ASSERT_EQUAL_STRING("creative", s_groups[4].name);
    TEST_ASSERT_EQUAL_STRING("maker", s_groups[5].name);
    TEST_ASSERT_EQUAL_STRING("ai", s_groups[6].name);
    /* Then the words the table does not know, alphabetically. */
    TEST_ASSERT_EQUAL_STRING("alpha", s_groups[7].name);
    TEST_ASSERT_EQUAL_STRING("zeta", s_groups[8].name);
    for ( uint8_t i = 0U; 9U > i; ++i )
    {
        row_is(i, KGUI_GROUP_ROW_GROUP, i);
    }
}

void test_group_single_app_is_a_group(void)
{
    app(0U, "wireless");

    TEST_ASSERT_EQUAL_UINT8(1U, root(1U));
    row_is(0U, KGUI_GROUP_ROW_GROUP, 0U);
    TEST_ASSERT_EQUAL_STRING("wireless", s_groups[0].name);
    TEST_ASSERT_EQUAL_UINT8(1U, s_groups[0].count);
}

void test_group_cap(void)
{
    char    name[NAME_LEN];
    uint8_t i;

    for ( i = 0U; i < (KGUI_GROUP_MAX + 1U); ++i )
    {
        (void)snprintf(name, sizeof(name), "c%02u", (unsigned)i);
        app(i, name);
    }

    TEST_ASSERT_EQUAL_UINT8(KGUI_GROUP_MAX + 1U, root(KGUI_GROUP_MAX + 1U));
    for ( i = 0U; KGUI_GROUP_MAX > i; ++i )
    {
        row_is(i, KGUI_GROUP_ROW_GROUP, i);
    }
    /* The category past the cap is a loose row, whole. */
    row_is(KGUI_GROUP_MAX, KGUI_GROUP_ROW_APP, KGUI_GROUP_MAX);
    {
        /* The cap is the root's: by name, that category still lists its app. */
        uint8_t idx[APPS_MAX];

        TEST_ASSERT_EQUAL_UINT8(1U, kgui_group_members(s_cats, KGUI_GROUP_MAX + 1U,
                                                       "c21", idx, APPS_MAX));
        TEST_ASSERT_EQUAL_UINT8(KGUI_GROUP_MAX, idx[0]);
    }
}

void test_group_cap_keeps_the_table_first(void)
{
    char    name[NAME_LEN];
    uint8_t idx[APPS_MAX];
    uint8_t i;

    /* Ten unknown words, then a table folder: the cap cuts by the root's
     * order, so the table's folder is kept and the last word is loose. */
    for ( i = 0U; KGUI_GROUP_MAX > i; ++i )
    {
        (void)snprintf(name, sizeof(name), "c%02u", (unsigned)i);
        app(i, name);
    }
    app(KGUI_GROUP_MAX, "games");

    TEST_ASSERT_EQUAL_UINT8(KGUI_GROUP_MAX + 1U, root(KGUI_GROUP_MAX + 1U));
    TEST_ASSERT_EQUAL_STRING("games", s_groups[0].name);
    TEST_ASSERT_EQUAL_STRING("c19", s_groups[KGUI_GROUP_MAX - 1U].name);
    row_is(KGUI_GROUP_MAX, KGUI_GROUP_ROW_APP, KGUI_GROUP_MAX - 1U);
    TEST_ASSERT_EQUAL_UINT8(1U, kgui_group_members(s_cats, KGUI_GROUP_MAX + 1U,
                                                   "c09", idx, APPS_MAX));
}

void test_group_cap_follows_room_given(void)
{
    app(0U, "a");
    app(1U, "b");
    app(2U, "c");

    TEST_ASSERT_EQUAL_UINT8(3U, kgui_group_root(s_cats, 3U, s_groups, 2U,
                                                s_rows, ROWS_MAX));
    row_is(0U, KGUI_GROUP_ROW_GROUP, 0U);
    row_is(1U, KGUI_GROUP_ROW_GROUP, 1U);
    row_is(2U, KGUI_GROUP_ROW_APP, 2U);
}

void test_group_vanishes_falls_to_root(void)
{
    uint8_t idx[APPS_MAX];

    app(0U, "games");
    app(1U, "tools");

    /* The control: a group that is there is found by the same call. */
    TEST_ASSERT_EQUAL_UINT8(1U, kgui_group_members(s_cats, 2U, "games", idx, APPS_MAX));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_group_members(s_cats, 2U, "weather", idx, APPS_MAX));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_group_members(s_cats, 0U, "games", idx, APPS_MAX));
    TEST_ASSERT_EQUAL_UINT8(0U, kgui_group_members(s_cats, 2U, NULL, idx, APPS_MAX));
}

void test_group_of_is_the_first_item(void)
{
    char name[KGUI_GROUP_NAME_MAX];

    TEST_ASSERT_TRUE(kgui_group_of("Games,tools", name));
    TEST_ASSERT_EQUAL_STRING("games", name);
    TEST_ASSERT_TRUE(kgui_group_of("", name));
    TEST_ASSERT_EQUAL_STRING("general", name);
    TEST_ASSERT_TRUE(kgui_group_of("other,games", name));
    TEST_ASSERT_EQUAL_STRING("general", name);
    TEST_ASSERT_FALSE(kgui_group_of(NULL, name));
    TEST_ASSERT_FALSE(kgui_group_of("games", NULL));
}

void test_group_plural_merges(void)
{
    uint8_t idx[APPS_MAX];
    char    name[KGUI_GROUP_NAME_MAX];

    app(0U, "game");
    app(1U, "games");
    app(2U, "tool");
    app(3U, "Tool,games");

    TEST_ASSERT_EQUAL_UINT8(2U, root(4U));
    TEST_ASSERT_EQUAL_STRING("games", s_groups[0].name);
    TEST_ASSERT_EQUAL_UINT8(2U, s_groups[0].count);
    TEST_ASSERT_EQUAL_STRING("tools", s_groups[1].name);
    TEST_ASSERT_EQUAL_UINT8(2U, s_groups[1].count);
    TEST_ASSERT_EQUAL_UINT8(2U, kgui_group_members(s_cats, 4U, "games", idx, APPS_MAX));
    TEST_ASSERT_EQUAL_UINT8(2U, kgui_group_members(s_cats, 4U, "game", idx, APPS_MAX));
    TEST_ASSERT_EQUAL_UINT8(1U, idx[1]);
    TEST_ASSERT_EQUAL_UINT8(2U, kgui_group_members(s_cats, 4U, "tool", idx, APPS_MAX));
    TEST_ASSERT_TRUE(kgui_group_of("game", name));
    TEST_ASSERT_EQUAL_STRING("games", name);
}

void test_group_unknown_word_is_not_pluralised(void)
{
    char    name[KGUI_GROUP_NAME_MAX];
    uint8_t idx[APPS_MAX];

    TEST_ASSERT_TRUE(kgui_group_of("weather", name));
    TEST_ASSERT_EQUAL_STRING("weather", name);
    TEST_ASSERT_TRUE(kgui_group_of("Farming", name));
    TEST_ASSERT_EQUAL_STRING("farming", name);
    /* A name that fills the field is kept whole, as it always was. */
    TEST_ASSERT_TRUE(kgui_group_of("abcdefghijklmno", name));
    TEST_ASSERT_EQUAL_STRING("abcdefghijklmno", name);

    /* Two forms of an unknown word are two groups: nobody wrote a plural for it. */
    app(0U, "weather");
    app(1U, "weathers");
    TEST_ASSERT_EQUAL_UINT8(2U, root(2U));
    TEST_ASSERT_EQUAL_UINT8(1U, kgui_group_members(s_cats, 2U, "weather", idx, APPS_MAX));
    TEST_ASSERT_EQUAL_UINT8(0U, idx[0]);
}

void test_group_table_keys_and_plurals(void)
{
    char    name[KGUI_GROUP_NAME_MAX];
    char    upper[KGUI_GROUP_NAME_MAX];
    uint8_t idx[APPS_MAX];

    for ( uint8_t t = 0U; TABLE_N > t; ++t )
    {
        /* The key and its other form are one group, whatever the case. */
        TEST_ASSERT_TRUE(kgui_group_of(s_want[t].key, name));
        TEST_ASSERT_EQUAL_STRING(s_want[t].key, name);
        TEST_ASSERT_TRUE(kgui_group_of(s_want[t].plural, name));
        TEST_ASSERT_EQUAL_STRING(s_want[t].key, name);
        (void)snprintf(upper, sizeof(upper), "%s", s_want[t].plural);
        upper[0] = (char)(upper[0] - ('a' - 'A'));
        TEST_ASSERT_TRUE(kgui_group_of(upper, name));
        TEST_ASSERT_EQUAL_STRING(s_want[t].key, name);

        (void)memset(s_cats, 0, sizeof(s_cats));
        app(0U, s_want[t].key);
        app(1U, s_want[t].plural);
        TEST_ASSERT_EQUAL_UINT8(1U, root(2U));
        TEST_ASSERT_EQUAL_STRING(s_want[t].key, s_groups[0].name);
        TEST_ASSERT_EQUAL_UINT8(2U, s_groups[0].count);
        TEST_ASSERT_EQUAL_UINT8(2U, kgui_group_members(s_cats, 2U, s_want[t].key, idx, APPS_MAX));
        TEST_ASSERT_EQUAL_UINT8(2U, kgui_group_members(s_cats, 2U, s_want[t].plural, idx, APPS_MAX));
    }
}

void test_group_title_by_the_table(void)
{
    char title[KGUI_GROUP_NAME_MAX];

    for ( uint8_t t = 0U; TABLE_N > t; ++t )
    {
        kgui_group_title(s_want[t].key, title);
        TEST_ASSERT_EQUAL_STRING(s_want[t].title, title);
        kgui_group_title(s_want[t].plural, title);
        TEST_ASSERT_EQUAL_STRING(s_want[t].title, title);
    }
    /* AI is two capitals, not "Ai", and never "Ais". */
    kgui_group_title("ai", title);
    TEST_ASSERT_EQUAL_STRING("AI", title);
    kgui_group_title("AI", title);
    TEST_ASSERT_EQUAL_STRING("AI", title);
    /* Any other word: its own letters, the first a capital, no plural. */
    kgui_group_title("weather", title);
    TEST_ASSERT_EQUAL_STRING("Weather", title);
    kgui_group_title("WEATHER", title);
    TEST_ASSERT_EQUAL_STRING("Weather", title);
    kgui_group_title("farming,games", title);
    TEST_ASSERT_EQUAL_STRING("Farming", title);
    /* No category is General. */
    kgui_group_title("other", title);
    TEST_ASSERT_EQUAL_STRING("General", title);
    kgui_group_title("", title);
    TEST_ASSERT_EQUAL_STRING("General", title);
    kgui_group_title(NULL, title);
    TEST_ASSERT_EQUAL_STRING("", title);
    kgui_group_title("games", NULL);
}

void test_group_other_and_empty_are_general(void)
{
    char    name[KGUI_GROUP_NAME_MAX];
    uint8_t idx[APPS_MAX];

    app(0U, "other");
    app(1U, "");
    app(2U, "Other");
    app(3U, "other,ai");

    TEST_ASSERT_EQUAL_UINT8(1U, root(4U));
    row_is(0U, KGUI_GROUP_ROW_GROUP, 0U);
    TEST_ASSERT_EQUAL_UINT8(4U, s_groups[0].count);
    TEST_ASSERT_TRUE(kgui_group_of("Other", name));
    TEST_ASSERT_EQUAL_STRING("general", name);
    TEST_ASSERT_EQUAL_UINT8(4U, kgui_group_members(s_cats, 4U, "other", idx, APPS_MAX));
    TEST_ASSERT_EQUAL_UINT8(4U, kgui_group_members(s_cats, 4U, "", idx, APPS_MAX));
    TEST_ASSERT_EQUAL_STRING("apps", kgui_group_glyph("other"));
}

void test_group_case_is_folded(void)
{
    uint8_t idx[APPS_MAX];

    app(0U, "Games");
    app(1U, "games");

    TEST_ASSERT_EQUAL_UINT8(1U, root(2U));
    TEST_ASSERT_EQUAL_STRING("games", s_groups[0].name);
    TEST_ASSERT_EQUAL_UINT8(2U, s_groups[0].count);
    TEST_ASSERT_EQUAL_UINT8(2U, kgui_group_members(s_cats, 2U, "GAMES", idx, APPS_MAX));
}

void test_group_rows_max_truncates(void)
{
    app(0U, "a");
    app(1U, "b");
    app(2U, "");

    TEST_ASSERT_EQUAL_UINT8(2U, kgui_group_root(s_cats, 3U, s_groups, KGUI_GROUP_MAX,
                                                s_rows, 2U));
    row_is(0U, KGUI_GROUP_ROW_GROUP, 0U);
    row_is(1U, KGUI_GROUP_ROW_GROUP, 1U);
}

void test_group_members_max_truncates(void)
{
    uint8_t idx[APPS_MAX];

    app(0U, "games");
    app(1U, "games");
    app(2U, "games");

    TEST_ASSERT_EQUAL_UINT8(2U, kgui_group_members(s_cats, 3U, "games", idx, 2U));
    TEST_ASSERT_EQUAL_UINT8(0U, idx[0]);
    TEST_ASSERT_EQUAL_UINT8(1U, idx[1]);
}

void test_group_glyph_known_and_default(void)
{
    for ( uint8_t t = 0U; TABLE_N > t; ++t )
    {
        TEST_ASSERT_EQUAL_STRING(s_want[t].glyph, kgui_group_glyph(s_want[t].key));
        TEST_ASSERT_EQUAL_STRING(s_want[t].glyph, kgui_group_glyph(s_want[t].plural));
    }
    /* A word the table does not draw has no glyph: its disc wears letters. */
    TEST_ASSERT_NULL(kgui_group_glyph("weather"));
    TEST_ASSERT_NULL(kgui_group_glyph("farming"));
    TEST_ASSERT_EQUAL_STRING("apps", kgui_group_glyph(""));
    TEST_ASSERT_NULL(kgui_group_glyph(NULL));
    TEST_ASSERT_EQUAL_STRING("antenna", kgui_group_glyph("radio"));
    TEST_ASSERT_EQUAL_STRING("clock", kgui_group_glyph("Clock"));
}

void test_group_rgb_is_the_kit_palette(void)
{
    uint32_t rgb = 0U;

    for ( uint8_t t = 0U; TABLE_N > t; ++t )
    {
        TEST_ASSERT_TRUE(kgui_group_rgb(s_want[t].key, &rgb));
        TEST_ASSERT_EQUAL_HEX32(s_want[t].rgb, rgb);
        TEST_ASSERT_TRUE(kgui_group_rgb(s_want[t].plural, &rgb));
        TEST_ASSERT_EQUAL_HEX32(s_want[t].rgb, rgb);
        /* Yellow is the focus ring's and never a folder's. */
        TEST_ASSERT_NOT_EQUAL_UINT32(KUI_PALETTE_BOLD_YELLOW, rgb);
    }
    /* One disc for every folder: no folder is told by a colour (ui-dot 0040). */
    for ( uint8_t t = 0U; TABLE_N > t; ++t )
    {
        TEST_ASSERT_TRUE(kgui_group_rgb(s_want[t].key, &rgb));
        TEST_ASSERT_EQUAL_HEX32(KUI_PALETTE_MEDIUM_GREY, rgb);
    }
    rgb = 0x123456U;
    TEST_ASSERT_FALSE(kgui_group_rgb("weather", &rgb));
    TEST_ASSERT_FALSE(kgui_group_rgb(NULL, &rgb));
    TEST_ASSERT_FALSE(kgui_group_rgb("games", NULL));
    TEST_ASSERT_EQUAL_HEX32(0x123456U, rgb);
}

void test_group_table_has_twenty_drawn_folders(void)
{
    const char *words[2U * TABLE_N];

    TEST_ASSERT_EQUAL_UINT(TABLE_N, KGUI_GROUP_ART_N);
    TEST_ASSERT_NULL(kgui_group_art_key((uint8_t)KGUI_GROUP_ART_N));
    TEST_ASSERT_NULL(kgui_group_art_alias((uint8_t)KGUI_GROUP_ART_N));
    for ( uint8_t t = 0U; TABLE_N > t; ++t )
    {
        const char * const glyph = kgui_group_glyph(kgui_group_art_key(t));
        bool               drawn = false;

        /* The table is the one the tests expect, in its order. */
        TEST_ASSERT_EQUAL_STRING(s_want[t].key, kgui_group_art_key(t));
        TEST_ASSERT_EQUAL_STRING(s_want[t].plural, kgui_group_art_alias(t));
        /* Every folder of the table has a glyph there is a picture of. */
        TEST_ASSERT_NOT_NULL(glyph);
        for ( size_t g = 0U; g < (sizeof(s_glyphs) / sizeof(s_glyphs[0])); ++g )
        {
            drawn = drawn || (strcmp(glyph, s_glyphs[g]) == 0);
        }
        TEST_ASSERT_TRUE_MESSAGE(drawn, glyph);
        /* A name fits the model's field. */
        TEST_ASSERT_TRUE(strlen(kgui_group_art_key(t)) < KGUI_GROUP_NAME_MAX);
        TEST_ASSERT_TRUE(strlen(kgui_group_art_alias(t)) < KGUI_GROUP_NAME_MAX);
        words[2U * t]        = kgui_group_art_key(t);
        words[(2U * t) + 1U] = kgui_group_art_alias(t);
    }
    /* No word names two folders: no key or alias is said twice. */
    for ( size_t a = 0U; a < (2U * TABLE_N); ++a )
    {
        for ( size_t b = a + 1U; b < (2U * TABLE_N); ++b )
        {
            TEST_ASSERT_TRUE_MESSAGE(strcmp(words[a], words[b]) != 0, words[a]);
        }
    }
    /* No two folders wear the same glyph. */
    for ( uint8_t a = 0U; TABLE_N > a; ++a )
    {
        for ( uint8_t b = (uint8_t)(a + 1U); TABLE_N > b; ++b )
        {
            TEST_ASSERT_TRUE(strcmp(s_want[a].glyph, s_want[b].glyph) != 0);
        }
    }
}

void test_group_order_of_the_new_folders(void)
{
    app(0U, "zeta");
    app(1U, "developer");
    app(2U, "time");
    app(3U, "sensors");
    app(4U, "games");
    app(5U, "alpha");

    TEST_ASSERT_EQUAL_UINT8(6U, root(6U));
    TEST_ASSERT_EQUAL_STRING("games", s_groups[0].name);
    TEST_ASSERT_EQUAL_STRING("sensors", s_groups[1].name);
    TEST_ASSERT_EQUAL_STRING("time", s_groups[2].name);
    TEST_ASSERT_EQUAL_STRING("developer", s_groups[3].name);
    /* Any other word after the table, alphabetically, as it always was. */
    TEST_ASSERT_EQUAL_STRING("alpha", s_groups[4].name);
    TEST_ASSERT_EQUAL_STRING("zeta", s_groups[5].name);
}

void test_group_mono_one_letter(void)
{
    char        m[KGUI_GROUP_MONO_MAX];
    const char *alone[] = { "Weather" };

    /* Weather alone: W. */
    kgui_group_mono("Weather", alone, 1U, m);
    TEST_ASSERT_EQUAL_STRING("W", m);
    kgui_group_mono("Weather", NULL, 0U, m);
    TEST_ASSERT_EQUAL_STRING("W", m);
    /* A small first letter is a capital on the disc. */
    kgui_group_mono("weather", NULL, 0U, m);
    TEST_ASSERT_EQUAL_STRING("W", m);
    kgui_group_mono("", NULL, 0U, m);
    TEST_ASSERT_EQUAL_STRING("", m);
    kgui_group_mono(NULL, NULL, 0U, m);
    TEST_ASSERT_EQUAL_STRING("", m);
    kgui_group_mono("Weather", NULL, 0U, NULL);
}

void test_group_mono_two_letters_only_for_a_twin(void)
{
    char        m[KGUI_GROUP_MONO_MAX];
    const char *fafi[]  = { "Farming", "Finance" };
    const char *wewe[]  = { "Weather", "Wearables" };
    const char *other[] = { "Weather", "Zebra" };

    /* Farming and Finance: Fa and Fi. */
    kgui_group_mono("Farming", fafi, 2U, m);
    TEST_ASSERT_EQUAL_STRING("Fa", m);
    kgui_group_mono("Finance", fafi, 2U, m);
    TEST_ASSERT_EQUAL_STRING("Fi", m);
    /* Weather and Wearables still agree, and are let be. */
    kgui_group_mono("Weather", wewe, 2U, m);
    TEST_ASSERT_EQUAL_STRING("We", m);
    kgui_group_mono("Wearables", wewe, 2U, m);
    TEST_ASSERT_EQUAL_STRING("We", m);
    /* Weather beside Wireless, which has its glyph: Wireless is not in the
     * list of folders with none, so Weather is W. */
    kgui_group_mono("Weather", other, 2U, m);
    TEST_ASSERT_EQUAL_STRING("W", m);
    /* The second letter is a small one whatever the title says. */
    {
        const char *caps[] = { "FARMING", "FINANCE" };

        kgui_group_mono("FARMING", caps, 2U, m);
        TEST_ASSERT_EQUAL_STRING("Fa", m);
    }
}

void test_group_mono_takes_a_character_whole(void)
{
    char        m[KGUI_GROUP_MONO_MAX];
    const char *two[] = { "\xC3\x89" "cole", "\xC3\x89t\xC3\xA9" };  /* École, Été */

    /* A character that is not an ASCII letter is taken whole and as it is. */
    kgui_group_mono("\xC3\x89" "cole", NULL, 0U, m);
    TEST_ASSERT_EQUAL_STRING("\xC3\x89", m);
    kgui_group_mono("\xC3\x89" "cole", two, 2U, m);
    TEST_ASSERT_EQUAL_STRING("\xC3\x89" "c", m);
    kgui_group_mono("\xC3\x89t\xC3\xA9", two, 2U, m);
    TEST_ASSERT_EQUAL_STRING("\xC3\x89" "t", m);
    /* A digit stays a digit: the font's fallback draws it in DM Sans. */
    kgui_group_mono("3d", NULL, 0U, m);
    TEST_ASSERT_EQUAL_STRING("3", m);
    /* A one-letter title with a twin has no second letter to give. */
    {
        const char *xs[] = { "X", "Xy" };

        kgui_group_mono("X", xs, 2U, m);
        TEST_ASSERT_EQUAL_STRING("X", m);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_group_first_category_only);
    RUN_TEST(test_group_no_category_goes_to_general);
    RUN_TEST(test_group_order_follows_the_table);
    RUN_TEST(test_group_single_app_is_a_group);
    RUN_TEST(test_group_cap);
    RUN_TEST(test_group_cap_keeps_the_table_first);
    RUN_TEST(test_group_cap_follows_room_given);
    RUN_TEST(test_group_vanishes_falls_to_root);
    RUN_TEST(test_group_of_is_the_first_item);
    RUN_TEST(test_group_plural_merges);
    RUN_TEST(test_group_unknown_word_is_not_pluralised);
    RUN_TEST(test_group_table_keys_and_plurals);
    RUN_TEST(test_group_title_by_the_table);
    RUN_TEST(test_group_other_and_empty_are_general);
    RUN_TEST(test_group_case_is_folded);
    RUN_TEST(test_group_rows_max_truncates);
    RUN_TEST(test_group_members_max_truncates);
    RUN_TEST(test_group_glyph_known_and_default);
    RUN_TEST(test_group_rgb_is_the_kit_palette);
    RUN_TEST(test_group_table_has_twenty_drawn_folders);
    RUN_TEST(test_group_order_of_the_new_folders);
    RUN_TEST(test_group_mono_one_letter);
    RUN_TEST(test_group_mono_two_letters_only_for_a_twin);
    RUN_TEST(test_group_mono_takes_a_character_whole);
    return UNITY_END();
}
