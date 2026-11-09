/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_screen_launcher.c
 * @brief The launcher: the window of the rail of rows, and the empty state behind it.
 */
#include "kgui_screens.h"

#include <string.h>

lv_obj_t *kgui_screen_launcher = NULL;

/**
 * @brief The empty state shows only while the list is empty.
 * @param observer The observer, bound to the empty box.
 * @param subject  kgui_subject_app_count.
 */
static void empty_box_follow(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *box = lv_observer_get_target_obj(observer);
    lv_obj_set_hidden(box, lv_subject_get_int(subject) > 0);
}

/**
 * @brief The title is the title once the lists have answered, and a Medium
 *        caption standing lower while they have not (kshell's "Looking for apps").
 * @param observer The observer, bound to the title.
 * @param subject  kgui_subject_empty_text.
 */
static void empty_title_follow(lv_observer_t *observer, lv_subject_t *subject)
{
    lv_obj_t *title = lv_observer_get_target_obj(observer);
    const char *text = lv_subject_get_string(subject);

    if ( strncmp(text, KGUI_EMPTY_WAIT_TEXT, strlen(KGUI_EMPTY_WAIT_TEXT)) == 0 )
    {
        lv_obj_set_style_text_font(title, &kui_font_body, 0);
        lv_obj_set_style_text_color(title, KUI_COLOR_TEXT_MUTED, 0);
        lv_obj_set_style_translate_y(title, KGUI_EMPTY_WAIT_Y - KGUI_EMPTY_TITLE_Y, 0);
    }
    else
    {
        lv_obj_set_style_text_font(title, &kui_font_display_40, 0);
        lv_obj_set_style_text_color(title, KUI_COLOR_TEXT_ON_BG, 0);
        lv_obj_set_style_translate_y(title, 0, 0);
    }
}

void kgui_screen_launcher_build(void)
{
    kgui_screen_launcher = lv_obj_create(NULL);
    lv_obj_t *scr = kgui_screen_launcher;
    lv_obj_set_name_static(scr, "screen_launcher_#");
    lv_obj_add_style(scr, &kui_style_screen, 0);

    /* The list is a window on the glass, all of it, with nothing of its own
     * to scroll. AO_Gui puts the box of rows and the focus ring in it and
     * moves them (kgui_list.h). */
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_name(list, "app_list");
    lv_obj_set_pos(list, 0, 0);
    lv_obj_set_size(list, KGUI_WINDOW_W, KGUI_WINDOW_H);
    lv_obj_set_scrollable(list, false);

    lv_obj_t *empty = kui_flex_create(scr, LV_FLEX_FLOW_COLUMN, KUI_SPACE_MD, 0U,
                                      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_name(empty, "empty_box");
    lv_obj_set_width(empty, lv_pct(100));
    lv_obj_set_height(empty, lv_pct(100));
    (void)lv_subject_add_observer_obj(kgui_subject_app_count, empty_box_follow, empty, NULL);

    lv_obj_t *title = lv_label_create(empty);
    lv_obj_set_name(title, "empty_title");
    (void)lv_label_bind_text(title, kgui_subject_empty_text, NULL);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(title, &kui_font_display_40, 0);
    lv_obj_set_style_text_color(title, KUI_COLOR_TEXT_ON_BG, 0);
    (void)lv_subject_add_observer_obj(kgui_subject_empty_text, empty_title_follow, title, NULL);

    /* Under the title while Kody stands below it: he is what it points at,
     * and the apps are why - on the face a tap on him is only a look. */
    lv_obj_t *tap = lv_label_create(empty);
    lv_obj_set_name(tap, "empty_tap");
    lv_label_set_text(tap, "Tap Kody to find apps");
    lv_obj_set_style_text_font(tap, &kui_font_body, 0);
    lv_obj_set_style_text_color(tap, KUI_COLOR_TEXT_MUTED, 0);

    lv_obj_t *cta = kui_flex_create(empty, LV_FLEX_FLOW_COLUMN, KUI_SPACE_MD, 0U,
                                    LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_name(cta, "empty_cta");

    /* The card the code stands on: Light Grey, the code in Soft Black. */
    lv_obj_t *panel = lv_obj_create(cta);
    lv_obj_set_name(panel, "empty_panel");
    lv_obj_set_size(panel, KGUI_EMPTY_CARD, KGUI_EMPTY_CARD);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_radius(panel, KGUI_EMPTY_CARD_R, 0);
    lv_obj_set_style_bg_color(panel, KUI_COLOR_TEXT_ON_BG, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_scrollable(panel, false);

    lv_obj_t *qr = lv_obj_create(panel);
    lv_obj_set_name(qr, "empty_qr");
    lv_obj_set_align(qr, LV_ALIGN_CENTER);
    lv_obj_set_size(qr, KGUI_EMPTY_QR, KGUI_EMPTY_QR);
    lv_obj_set_style_bg_opa(qr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(qr, 0, 0);
    lv_obj_set_style_pad_all(qr, 0, 0);

    lv_obj_t *url = lv_label_create(cta);
    lv_obj_set_name(url, "empty_url");
    lv_label_set_text(url, "apps.kode.diy");
    lv_obj_set_style_text_font(url, &kui_font_title, 0);
    lv_obj_set_style_text_color(url, KUI_COLOR_TEXT_ON_BG, 0);

    lv_obj_t *hint = lv_label_create(cta);
    lv_obj_set_name(hint, "empty_hint");
    lv_label_set_text(hint, "Copy apps to the microSD card");
    lv_obj_set_style_text_font(hint, &kui_font_body, 0);
    lv_obj_set_style_text_color(hint, KUI_COLOR_TEXT_MUTED, 0);
}
