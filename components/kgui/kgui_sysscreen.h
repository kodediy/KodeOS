/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_sysscreen.h
 * @brief The system screen's frame: protagonist, figure, three lines and a pill on a system sheet.
 */
#ifndef KGUI_SYSSCREEN_H
#define KGUI_SYSSCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "kgui_icon.h"
#include "kui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The 128 face of the write's figure: the one declaration of it, which kgui_ao.c's deep digit shares. */
LV_FONT_DECLARE(kgui_font_figure_128);

/* ---- Where things stand, on the 410 x 502 glass ---- */

#define KGUI_SYS_GLASS_H      502   /**< The glass's height. */
#define KGUI_SYS_CENTRE_X     205   /**< The glass's middle, across. */
#define KGUI_SYS_GLYPH_MID_RISE 40  /**< A large glyph as the whole message stands this far above the glass's middle (Luismi, 5-nov). */
#define KGUI_SYS_PROT_Y       104   /**< The protagonist's middle, down. */
#define KGUI_SYS_DISC         104    /**< An app's disc. */
#define KGUI_SYS_GLYPH        96    /**< A glyph, scaled to this side. */
#define KGUI_SYS_BLANK_SCALE  208   /**< The apps glyph on the blank disc: 44 px out of its 64, in 1/256ths. */
#define KGUI_SYS_FIGURE_BASE  288   /**< The baseline of a write's figure and its "%". */
#define KGUI_SYS_FIGURE_Y     157   /**< The top of a write's figure. */
#define KGUI_SYS_UNIT_GAP     6     /**< Between the figure and its "%". */
#define KGUI_SYS_NAME_Y       334   /**< The top of the name under a write's figure. */
#define KGUI_SYS_SECOND_Y     384   /**< The top of the second line under it. */
#define KGUI_SYS_TEXT_X       30    /**< The left of every line of words. */
#define KGUI_SYS_TEXT_W       350   /**< Their width: they are centred in it. */
#define KGUI_SYS_NOTE_Y       431   /**< The top of a write's small print, under its second line: high enough that its widest line stays out of the corners' edge band. */
#define KGUI_SYS_TEXT_Y       160   /**< The top of a card's name when it is not centred in the room. */
#define KGUI_SYS_ROOM_TOP     (KGUI_SYS_PROT_Y + (KGUI_SYS_GLYPH / 2)) /**< Where the room for a card's words begins: under the protagonist's largest form. */
#define KGUI_SYS_ROOM_END     KGUI_SYS_PILL_Y /**< ...and where it ends: the answer pill's top. */
#define KGUI_SYS_ROOM_NONE    0     /**< As the room's end: the words are not centred, they begin at the top given. */
#define KGUI_SYS_TEXT_STEP_NAME   46 /**< From a card's name to its second line. */
#define KGUI_SYS_TEXT_STEP_SECOND 40 /**< ...and from the second line to the detail. */
#define KGUI_SYS_CODE_Y       160   /**< The top of a pairing's code. */
#define KGUI_SYS_PILE_W       150   /**< The red battery's width. */
#define KGUI_SYS_PILE_H       88    /**< ...and its height. */
#define KGUI_SYS_PILE_BORDER  7     /**< ...and its line. */
#define KGUI_SYS_PILE_R       18    /**< ...and its corners. */
#define KGUI_SYS_PILE_NUB_W   8     /**< The terminal's width. */
#define KGUI_SYS_PILE_NUB_H   22    /**< ...and its height. */
#define KGUI_SYS_PILE_NUB_Y   26    /**< ...and where it stands in the battery. */
#define KGUI_SYS_PILE_LEVEL_AT 6    /**< ...and where its fill stands in it: inside the line. */
#define KGUI_SYS_PILE_LEVEL_W 14    /**< What is left in the cell: the sliver of its fill. */
#define KGUI_SYS_PILE_LEVEL_H 62    /**< ...and its height. */
#define KGUI_SYS_PILE_LEVEL_R 6     /**< ...and its corners. */
#define KGUI_SYS_PILL_Y       370   /**< The top of the answer pill. */
#define KGUI_SYS_PILL_H       72    /**< ...and its height. */
#define KGUI_SYS_PILL_W_MIN   173   /**< ...and the least of its width. */
#define KGUI_SYS_PILL_RING    (KUI_RING_W + KUI_RING_GAP) /**< ...and the room its focus ring takes outside it. */
#define KGUI_SYS_PILL_PAD     72    /**< ...what its word is given across, both sides in all. */
#define KGUI_SYS_PILL_ROW_W   374   /**< The row the pill stands in: the widest it may be. */
#define KGUI_SYS_KIT_ROW_Y    395   /**< Where the kit stands its answer row (kui_sheet_create()). */
#define KGUI_SYS_FIGURE_NONE  255U  /**< The share the figure shows when it shows none. */

/** @brief The frame's objects. */
typedef struct {
    kgui_icon_t icon;     /**< An app's disc. */
    lv_obj_t   *blank;    /**< The disc of an app with no look. */
    lv_obj_t   *blank_img;/**< The apps glyph on it. */
    lv_obj_t   *glyph;    /**< A glyph, 96 px. */
    lv_obj_t   *word;     /**< A word as the protagonist. */
    lv_obj_t   *pile;     /**< The red battery. */
    lv_obj_t   *code;     /**< A pairing's code. */
    lv_obj_t   *figure;   /**< A write's share. */
    lv_obj_t   *unit;     /**< Its "%". */
    lv_obj_t   *name;     /**< The first line. */
    lv_obj_t   *second;   /**< The second. */
    lv_obj_t   *detail;   /**< The lines under them, three at most, the last one dotted when there is more. */
    lv_obj_t   *note;     /**< A write's small print, under its second line. */
    lv_obj_t   *message;  /**< A card that is only words: large, centred over its answer, no protagonist. */
    uint8_t     shown;    /**< The share the figure says, or KGUI_SYS_FIGURE_NONE. */
    bool        made;     /**< Every object stands. */
} kgui_sys_t;

/**
 * @brief Make the frame's objects on a card, hidden.
 *
 * @param s    Receives them.
 * @param card The card of a system sheet.
 * @return true when all of them stand.
 */
bool kgui_sys_create(kgui_sys_t *s, lv_obj_t *card);

/**
 * @brief Say one message as the whole card: Outfit ExtraBold 40, centred
 *        across the glass and in the room above the answer pill, with no
 *        protagonist (Luismi, 4-nov). NULL takes it away.
 *
 * @param s    The frame.
 * @param text The words; up to three lines, wrapped.
 */
void kgui_sys_message(kgui_sys_t *s, const char *text);

/**
 * @brief A large glyph as the card's whole message: at its own size, centred
 *        across and KGUI_SYS_GLYPH_MID_RISE above the middle of the glass.
 *
 * @param s    The frame.
 * @param img  The glyph, A8.
 * @param ink  Its colour.
 */
void kgui_sys_glyph_mid(kgui_sys_t *s, const lv_image_dsc_t *img, lv_color_t ink);

/**
 * @brief Move a write's screen down as a whole, to centre what it shows.
 *
 * kgui_sys_clear() puts it back.
 *
 * @param s  The frame.
 * @param dy Pixels down.
 */
void kgui_sys_lift(kgui_sys_t *s, int32_t dy);

/**
 * @brief Take everything off the glass and forget what was said.
 *
 * @param s The frame.
 */
void kgui_sys_clear(kgui_sys_t *s);

/**
 * @brief The protagonist is an app: its disc, as the launcher draws it.
 *
 * @param s    The frame.
 * @param look The app's icon; NULL for an app that is not known: the apps
 *             glyph on a grey disc.
 */
void kgui_sys_app(kgui_sys_t *s, const kgui_icon_spec_t *look);

/**
 * @brief The protagonist is a glyph, KGUI_SYS_GLYPH px.
 *
 * @param s   The frame.
 * @param img The glyph, a line glyph of the kit (A8) or a 40 px one.
 * @param ink What it is drawn in.
 */
void kgui_sys_glyph(kgui_sys_t *s, const lv_image_dsc_t *img, lv_color_t ink);

/**
 * @brief The protagonist is a word, in the display face.
 *
 * @param s    The frame.
 * @param text The word.
 */
void kgui_sys_word(kgui_sys_t *s, const char *text);

/**
 * @brief The protagonist is the red battery, nearly empty.
 *
 * @param s The frame.
 */
void kgui_sys_pile(kgui_sys_t *s);

/**
 * @brief The code of a pairing, in the 80 face.
 *
 * @param s    The frame.
 * @param text The digits, as they are read out.
 */
void kgui_sys_code(kgui_sys_t *s, const char *text);

/**
 * @brief A write's share, big, with its "%".
 *
 * Drawn again only when it moved.
 *
 * @param s   The frame.
 * @param pct 0 to 100.
 */
void kgui_sys_figure(kgui_sys_t *s, uint8_t pct);

/**
 * @brief A write's name and second line under its figure.
 *
 * @param s      The frame.
 * @param name   The name; NULL for none, and the second line takes its place.
 * @param second The line under it; NULL for none.
 */
void kgui_sys_hero(kgui_sys_t *s, const char *name, const char *second);

/**
 * @brief A write's small print, one line in the caption face under the second line.
 *
 * @param s    The frame.
 * @param text What it says; NULL for none.
 */
void kgui_sys_note(kgui_sys_t *s, const char *text);

/**
 * @brief A card's words: a name, a second line and the detail.
 *
 * With a room (@p bottom past @p top) the block is centred in it, the same
 * air above and below; with KGUI_SYS_ROOM_NONE the first line stands at
 * @p top.
 *
 * @param s      The frame.
 * @param top    Where the room begins, or where the first line stands.
 * @param bottom Where the room ends, or KGUI_SYS_ROOM_NONE.
 * @param name   The name; NULL for none.
 * @param second The second line; NULL for none.
 * @param detail Up to three lines under them, one string with its breaks; NULL for none.
 */
void kgui_sys_text(kgui_sys_t *s, int32_t top, int32_t bottom, const char *name,
                   const char *second, const char *detail);

/**
 * @brief Where the app's disc stands, for a launch that flies out of it.
 *
 * @param cx   Receives its middle, across.
 * @param cy   Receives its middle, down.
 * @param size Receives its side.
 */
void kgui_sys_origin(int32_t *cx, int32_t *cy, int32_t *size);

/**
 * @brief Dress the sheet's answer button as the frame's pill, or as the kit's.
 *
 * The button is the kit's (kui_sheet_create()): the pill is its size, place
 * and word. The kit's own size comes back for every other sheet.
 *
 * @param card   The card of the sheet.
 * @param framed true for the frame's pill; false for the kit's button.
 * @param word   What the button says, for the pill's width; NULL keeps it.
 */
void kgui_sys_pill(const lv_obj_t *card, bool framed, const char *word);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_SYSSCREEN_H */
