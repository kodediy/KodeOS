/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_screen_loading.c
 * @brief The loading screen: Kody in his wheel and one line for the guided factory tests' prompts.
 */
#include "kgui_screens.h"
#include "kgui_assets.h"
#include "kui_kody.h"

lv_obj_t *kgui_screen_loading = NULL;

/** Dimmed text. */
static lv_style_t s_muted;

/** Kody's player, or NULL when the kit could not make one. */
static lv_obj_t *s_kody = NULL;

/** The loading role is on the player: it loops from here on. */
static bool s_kody_playing = false;

/**
 * @brief The screen is being loaded: start Kody's wheel, the first time.
 *
 * kui_kody_play() inflates and parses the animation and draws its first
 * frame in this step, so the screen lands with Kody already on it. A play
 * that fails is not tried again, until a new pack is read
 * (kgui_screen_loading_kody_stop()): the player says why, stays empty and
 * transparent, and the screen is its line alone.
 *
 * @param e The LV_EVENT_SCREEN_LOAD_START event.
 */
static void on_loading_load_start(lv_event_t *e)
{
    const kui_kody_anim_t *const wheel = kgui_assets_role(KUI_PACK_ROLE_LOADING);

    (void)e;
    if ( (NULL != s_kody) && !s_kody_playing && (NULL != wheel) )
    {
        s_kody_playing = true;
        /* kui_kody_play() says why when it cannot. */
        (void)kui_kody_play(s_kody, wheel, true);
    }
}

void kgui_screen_loading_build(void)
{
    lv_style_init(&s_muted);
    lv_style_set_text_opa(&s_muted, KUI_OPA_PCT(60));

    kgui_screen_loading = lv_obj_create(NULL);
    lv_obj_t *scr = kgui_screen_loading;
    lv_obj_set_name_static(scr, "screen_loading_#");
    lv_obj_add_style(scr, &kui_style_screen, 0);

    uint16_t w = 0U;
    uint16_t h = 0U;

    kui_pack_role_canvas(KUI_PACK_ROLE_LOADING, &w, &h);
    /* kui_kody_create() says why when it cannot make one. */
    s_kody = kui_kody_create(scr, w, h);
    if ( NULL != s_kody )
    {
        lv_obj_set_name(s_kody, "loading_kody");
        lv_obj_set_align(s_kody, LV_ALIGN_CENTER);
        (void)lv_obj_add_event_cb(scr, &on_loading_load_start,
                                  LV_EVENT_SCREEN_LOAD_START, NULL);
    }

    lv_obj_t *label = lv_label_create(scr);
    lv_obj_set_name(label, "loading_label");
    (void)lv_label_bind_text(label, kgui_subject_loading_text, NULL);
    lv_obj_set_align(label, LV_ALIGN_CENTER);
    lv_obj_set_y(label, 175);
    lv_obj_set_style_text_font(label, &kui_font_body, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(label, &s_muted, 0);
}

void kgui_screen_loading_kody_stop(void)
{
    kui_kody_stop(s_kody);
    s_kody_playing = false;
}
