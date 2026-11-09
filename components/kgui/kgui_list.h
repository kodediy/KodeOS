/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_list.h
 * @brief The launcher's rail objects: the box its rows move in, the focus ring and the one row.
 */
#ifndef KGUI_LIST_H
#define KGUI_LIST_H

#include <stdbool.h>
#include <stdint.h>

#include "kui.h"
#include "kgui_motion.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The row --------------------------------------------------------------- */

#define KGUI_ROW_ICON_SRC   64  /**< The icon's own size, in pixels: what a scale up to a disc is a share of. */

/** @brief The objects of the list: what the page holds of it. */
typedef struct {
    lv_obj_t *box;   /**< Moves with the motions; the ring and the rows are its children. */
    lv_obj_t *ring;  /**< The focus ring, under the rows; placed with kgui_list_ring_place(). */
} kgui_list_t;

/**
 * @brief Make the box and the ring in a viewport.
 *
 * The viewport is cut to its own edges and takes no scroll of its own.
 * Nothing here can be clicked, and the rows that go in the box are clickable.
 * The ring starts hidden.
 *
 * @param list     Receives the objects.
 * @param viewport The window on the page.
 * @return true when the box and the ring exist; false when LVGL's heap could
 *         not make them.
 */
bool kgui_list_build(kgui_list_t *list, lv_obj_t *viewport);

/**
 * @brief Fit the box to the rows.
 *
 * @param list      The objects.
 * @param content_h The rows' height, in pixels.
 */
void kgui_list_fit(kgui_list_t const *list, int32_t content_h);

/**
 * @brief Put the ring where the list says, or hide it.
 *
 * @param list  The objects.
 * @param y     The ring's top in the box, in pixels (kgui_rows_light_y()).
 * @param shown false when there is no row to stand on, or the pad is not in use.
 * @return true when the ring's place changed: showing or hiding it is not a move.
 */
bool kgui_list_ring_place(kgui_list_t const *list, int32_t y, bool shown);

/**
 * @brief Make a row in the box: the disc, its picture and glyph, and the name.
 *
 * The row is the box's last child, KGUI_ROW_H tall and as wide as the glass,
 * with nothing of its own to draw. Its children are named `slot` (the disc),
 * `slot_img` (the app's picture), `slot_glyph` and `name`. The caller puts it
 * at its pitch and gives it its click.
 *
 * @param list The objects.
 * @return The row, or NULL when LVGL's heap could not make it.
 */
lv_obj_t *kgui_list_row_make(kgui_list_t const *list);

/**
 * @brief Make a disc: a round box that clips, with a picture and a glyph in it.
 *
 * The children are named `slot_img`, `slot_glyph` and `slot_mono`, all
 * hidden. The row
 * (kgui_list_row_make()) and the details make their discs the same way.
 *
 * @param parent Its parent.
 * @param x      Its left.
 * @param y      Its top.
 * @param side   Its side, in pixels.
 * @return The disc, or NULL when LVGL's heap could not make it.
 */
lv_obj_t *kgui_list_disc_make(lv_obj_t *parent, int32_t x, int32_t y, int32_t side);

/**
 * @brief Put what a disc wears on it.
 *
 * @param disc   A disc from kgui_list_disc_make().
 * @param side   Its side, in pixels: the picture is scaled to it.
 * @param ground The disc's colour.
 * @param pic    The app's picture, KGUI_ROW_ICON_SRC square; NULL for the
 *               letters or the glyph.
 * @param mono   The letters drawn when there is no picture; NULL for the glyph.
 * @param mono_font The letters' face: NULL for an app's (kui_font_name,
 *               Outfit Bold 28); a folder's is kui_font_title (Outfit Bold
 *               34), and two letters in it stand a pixel closer.
 * @param glyph  The glyph drawn when there is neither; NULL for none.
 * @param scale  The glyph's scale, LV_SCALE_NONE for its own size.
 * @param ink    The colour of the letters or the glyph.
 */
void kgui_list_disc_dress(lv_obj_t *disc, int32_t side, lv_color_t ground,
                          const lv_image_dsc_t *pic, const char *mono,
                          const lv_font_t *mono_font,
                          const lv_image_dsc_t *glyph, uint32_t scale, lv_color_t ink);

/**
 * @brief Put what a row's disc wears on it.
 *
 * @param row    A row from kgui_list_row_make().
 * @param ground The disc's colour.
 * @param pic    The app's picture, KGUI_ROW_ICON_SRC square and drawn at the
 *               disc's size; NULL for the letters or the glyph.
 * @param mono   The letters drawn when there is no picture; NULL for the glyph.
 * @param mono_font The letters' face, as kgui_list_disc_dress() takes it.
 * @param glyph  The glyph drawn when there is neither; NULL for none.
 * @param scale  The glyph's scale, LV_SCALE_NONE for its own size.
 * @param ink    The colour of the letters or the glyph.
 */
void kgui_list_row_disc(lv_obj_t const *row, lv_color_t ground, const lv_image_dsc_t *pic,
                        const char *mono, const lv_font_t *mono_font,
                        const lv_image_dsc_t *glyph, uint32_t scale, lv_color_t ink);

/**
 * @brief Put a folder's art on a row's disc, in the art's own colours.
 *
 * The art is the ink alone on transparent, at its own 40 px (kgui_folder_art()):
 * a glyph's tint would flatten its two inks into one.
 *
 * @param row    The row.
 * @param ground The disc's colour.
 * @param art    The image.
 */
void kgui_list_row_disc_art(lv_obj_t const *row, lv_color_t ground, const lv_image_dsc_t *art);

/**
 * @brief Write a row's name and how it stands: focused or not.
 *
 * Focused it is Light Grey; otherwise it is Medium Grey; both on one line,
 * dotted when the name is longer. A row pushed in along the glass's edge keeps its name inside
 * the glass: its width is the name's less twice @p narrow, once for the push
 * and once for the corner on the right.
 *
 * @param row     A row from kgui_list_row_make().
 * @param text    The name.
 * @param focused true for the row the focus is on.
 * @param narrow  How far the row is pushed in, in pixels (kgui_rail_t::dx).
 */
void kgui_list_row_name(lv_obj_t const *row, const char *text, bool focused, int32_t narrow);

/**
 * @brief Stand a row against the glass's edge: pushed in, faded, or gone.
 *
 * What is already so is not written again. The name's width follows the push.
 *
 * @param row  A row from kgui_list_row_make().
 * @param edge How it stands (kgui_rail_edge()).
 */
void kgui_list_row_edge(lv_obj_t *row, kgui_rail_t const *edge);

/**
 * @brief A group's glyph, by the standard name kgui_group_glyph() gives.
 *
 * @param name  A name kgui_group_glyph() gives (`games`, `tool`, `talk`,
 *              `wifi`, `brush`, `maker`, `spark`, `gauge`, `antenna`,
 *              `shield`, `note`, `camera`, `robot`, `home`, `heart`, `pin`,
 *              `clock`, `list`, `book`, `code`), or NULL.
 * @param scale Receives the scale to draw it at, LV_SCALE_NONE for its own size.
 * @return The kit's or kgui's glyph for it; the apps glyph for NULL or a
 *         name it has none for.
 */
const lv_image_dsc_t *kgui_list_group_glyph(const char *name, uint32_t *scale);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_LIST_H */
