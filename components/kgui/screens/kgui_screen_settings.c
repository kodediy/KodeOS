/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_screen_settings.c
 * @brief The settings screen: kshell's control centre of three circles, battery liquid and levels.
 */
#include "kgui_screens.h"
#include "kgui_fluid.h"
#include "kgui_lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "kgui_tilt.h"

#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(kgui_font_dmsans_b_34);

/* The volume's vibrate stop: what rounds to KGUI_VOL_VIBRATE_PCT. */
#define LEVEL_VIB_FROM_PM  25
#define LEVEL_VIB_TO_PM    75
LV_IMAGE_DECLARE(kgui_glyph_share_46);
LV_IMAGE_DECLARE(kgui_glyph_sun0_52);
LV_IMAGE_DECLARE(kgui_glyph_sun1_52);
LV_IMAGE_DECLARE(kgui_glyph_sun2_52);
LV_IMAGE_DECLARE(kgui_glyph_sun3_52);
LV_IMAGE_DECLARE(kgui_glyph_spk0_52);
LV_IMAGE_DECLARE(kgui_glyph_spk1_52);
LV_IMAGE_DECLARE(kgui_glyph_spk2_52);
LV_IMAGE_DECLARE(kgui_glyph_spk3_52);

/** The opacity of a row of the surface that is only partly liquid: the share of it that is. */

lv_obj_t *kgui_screen_settings = NULL;

/** One circle of the control centre: the name AO_Gui finds it by and where it stands. */
typedef struct {
    const char *name;  /**< The circle's object name. */
    int32_t     x;     /**< Its left edge. */
    int32_t     y;     /**< Its top. */
} circle_spec_t;

/** The circles, in the order of GuiSetCtl: Wi-Fi, Bluetooth, More, Turn off. */
static const circle_spec_t s_circles[] = {
    { "ctl_wifi", KGUI_CC_X0, KGUI_CC_Y0 },
    { "ctl_ble",  KGUI_CC_X1, KGUI_CC_Y0 },
    { "ctl_more", KGUI_CC_X2, KGUI_CC_Y0 },
    { "ctl_off",  KGUI_CC_X2, KGUI_CC_Y1 },
};

/** The battery's circle, which is not a control and so is in no GuiSetCtl: its name and its place. */
static const circle_spec_t s_battery = { "ctl_bat", KGUI_CC_X0, KGUI_CC_Y1 };

/**
 * @brief Build one circle: a disc of the off grey with its glyph in the middle.
 *
 * The glyph is AO_Gui's to write and to colour; the cursor's ring is the kit's
 * focus ring on the checked state.
 *
 * @param parent The screen.
 * @param spec   Which circle.
 */
static void circle_build(lv_obj_t *parent, circle_spec_t const * const spec)
{
    lv_obj_t * const c = lv_obj_create(parent);

    if ( NULL == c )
    {
        return;
    }
    lv_obj_set_name(c, spec->name);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, spec->x, spec->y);
    lv_obj_set_size(c, KGUI_CC_CIRCLE, KGUI_CC_CIRCLE);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(c, KUI_COLOR_OFF, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(c, false);
    lv_obj_set_clickable(c, true);
    lv_obj_add_style(c, &kui_style_ring, LV_STATE_CHECKED);

    lv_obj_t * const img = lv_image_create(c);

    if ( NULL != img )
    {
        lv_obj_set_name(img, "slot_img");
        lv_obj_set_align(img, LV_ALIGN_CENTER);
        lv_obj_set_clickable(img, false);
        lv_obj_set_hidden(img, true);
    }
}

/**
 * @brief Build one level: a track, its fill, the glyph and the figure.
 *
 * The fill is Light Grey over the off-grey track, 72 px wide at its least and
 * 374 at its fullest; the figure stands inside it in Soft Black once it is
 * wide enough, and on the track after its edge before that
 * (level_paint()). The brightness level and the volume level are this
 * one shape at two heights.
 *
 * @param parent The screen.
 * @param name   The track's object name.
 * @param y      The track's top.
 * @param glyph  What stands on it until the first paint says otherwise.
 */
static void level_build(lv_obj_t *parent, const char *name, int32_t y,
                        const lv_image_dsc_t *glyph)
{
    lv_obj_t * const track = lv_obj_create(parent);

    if ( NULL == track )
    {
        return;
    }
    lv_obj_set_name(track, name);
    lv_obj_remove_style_all(track);
    lv_obj_set_pos(track, KGUI_CC_LEVEL_X, y);
    lv_obj_set_size(track, KGUI_CC_LEVEL_W, KGUI_CC_LEVEL_H);
    lv_obj_set_style_radius(track, KGUI_CC_LEVEL_H / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(track, KUI_COLOR_OFF, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(track, true, LV_PART_MAIN);
    lv_obj_set_scrollable(track, false);
    lv_obj_set_clickable(track, true);
    lv_obj_add_style(track, &kui_style_ring, LV_STATE_CHECKED);

    lv_obj_t * const fill = lv_obj_create(track);

    if ( NULL != fill )
    {
        lv_obj_set_name(fill, "level_fill");
        lv_obj_remove_style_all(fill);
        lv_obj_set_pos(fill, 0, 0);
        lv_obj_set_size(fill, KGUI_CC_LEVEL_H, KGUI_CC_LEVEL_H);
        lv_obj_set_style_radius(fill, KGUI_CC_LEVEL_H / 2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(fill, KUI_COLOR_TEXT_ON_BG, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_clickable(fill, false);
    }

    lv_obj_t * const icon = lv_image_create(track);

    if ( NULL != icon )
    {
        lv_obj_set_name(icon, "level_img");
        lv_image_set_src(icon, glyph);
        lv_obj_set_pos(icon, KGUI_CC_LEVEL_GLYPH_X,
                       (KGUI_CC_LEVEL_H - KGUI_CC_LEVEL_GLYPH_PX) / 2);
        lv_obj_set_style_image_recolor(icon, KUI_COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_clickable(icon, false);
    }

    lv_obj_t * const fig = lv_label_create(track);

    if ( NULL != fig )
    {
        lv_obj_set_name(fig, "level_text");
        lv_obj_set_style_text_font(fig, &kgui_font_dmsans_b_34, LV_PART_MAIN);
        lv_label_set_text(fig, "");
        lv_obj_set_y(fig, (KGUI_CC_LEVEL_H - lv_font_get_line_height(&kgui_font_dmsans_b_34)) / 2);
        lv_obj_set_clickable(fig, false);
    }
}

#define LEVEL_PM_FULL 1000  /**< A level whole, in thousandths: a finger moves it by the pixel. */
#define LEVEL_PM_NEAR 20    /**< This close to the finger the fill is under it. */
#define LEVEL_PM_PER_PCT 10 /**< Thousandths in a percent: a level is kept in thousandths. */

/** @brief Where a level's fill stands. */
typedef struct {
    kgui_level_t which;  /**< Which level it is: its place in the table. */
    const char *name;    /**< Its track. */
    int32_t     floor;   /**< The least a finger can take it to, in thousandths. */
    int32_t     pm;      /**< Where it is drawn. */
    int32_t     target;  /**< Where it is going. */
    bool        held;    /**< A finger has it. */
    bool        known;   /**< It has been drawn. */
} level_state_t;

static level_state_t s_levels[KGUI_LEVEL_COUNT] = {
    [KGUI_LEVEL_BRIGHT] = { .which = KGUI_LEVEL_BRIGHT, .name = "ctl_level",
                            .floor = (int32_t)KGUI_CC_LEVEL_MIN_PCT * LEVEL_PM_PER_PCT },
    [KGUI_LEVEL_VOLUME] = { .which = KGUI_LEVEL_VOLUME, .name = "ctl_volume", .floor = 0 },
};

/**
 * @brief Draw one level at @p pct: the fill's width, the glyph and the figure.
 *
 * @param name  The track's object name.
 * @param pct   How full, 0 to 100.
 * @param glyph The glyph for that fill (the caller picks the step).
 */
static void level_paint(const char *name, int32_t pm, const lv_image_dsc_t *glyph,
                        const char *word)
{
    if ( NULL == kgui_screen_settings )
    {
        return;
    }
    lv_obj_t const * const track = lv_obj_find_by_name(kgui_screen_settings, name);

    if ( NULL == track )
    {
        return;
    }
    lv_obj_t * const fill = lv_obj_find_by_name(track, "level_fill");
    lv_obj_t * const icon = lv_obj_find_by_name(track, "level_img");
    lv_obj_t * const fig  = lv_obj_find_by_name(track, "level_text");
    int32_t const    fw   = KGUI_CC_LEVEL_H + ((pm * KGUI_CC_LEVEL_SPAN) / LEVEL_PM_FULL);

    if ( NULL != fill )
    {
        lv_obj_set_width(fill, fw);
    }
    if ( (NULL != icon) && (lv_image_get_src(icon) != glyph) )
    {
        lv_image_set_src(icon, glyph);
    }
    if ( NULL != fig )
    {
        char       text[16];
        lv_point_t said;

        const lv_font_t * const font = (NULL != word) ? &kui_font_name : &kgui_font_dmsans_b_34;

        if ( NULL != word )
        {
            (void)snprintf(text, sizeof(text), "%s", word);
        }
        else
        {
            (void)snprintf(text, sizeof(text), "%d %%", (int)((pm + 5) / 10));
        }
        lv_obj_set_style_text_font(fig, font, LV_PART_MAIN);
        lv_label_set_text(fig, text);
        kgui_text_size(&said, text, font);
        if ( fw >= (KGUI_CC_FIG_IN_X + said.x + KGUI_CC_FIG_OUT_GAP) )
        {
            lv_obj_set_x(fig, KGUI_CC_FIG_IN_X);
            lv_obj_set_style_text_color(fig, KUI_COLOR_TEXT, LV_PART_MAIN);
        }
        else
        {
            lv_obj_set_x(fig, fw + KGUI_CC_FIG_OUT_GAP);
            lv_obj_set_style_text_color(fig, KUI_COLOR_TEXT_ON_BG, LV_PART_MAIN);
        }
    }
}

/** @brief Whether a level at @p pm stands on the volume's vibrate stop. */
static bool level_vibrates(kgui_level_t which, int32_t pm)
{
    return (KGUI_LEVEL_VOLUME == which) && (LEVEL_VIB_FROM_PM <= pm) && (LEVEL_VIB_TO_PM > pm);
}

/** @brief The glyph for a level at @p pm. */
static const lv_image_dsc_t *level_glyph(kgui_level_t which, int32_t pm)
{
    static const lv_image_dsc_t *const SUNS[KGUI_CC_SUN_STEPS] = {
        &kgui_glyph_sun0_52, &kgui_glyph_sun1_52,
        &kgui_glyph_sun2_52, &kgui_glyph_sun3_52,
    };
    static const lv_image_dsc_t *const SPKS[KGUI_CC_SUN_STEPS] = {
        &kgui_glyph_spk0_52, &kgui_glyph_spk1_52,
        &kgui_glyph_spk2_52, &kgui_glyph_spk3_52,
    };
    int32_t const p = (pm + 5) / 10;
    int32_t       step;

    if ( KGUI_LEVEL_BRIGHT == which )
    {
        step = (p * KGUI_CC_SUN_STEPS) / (KGUI_CC_LEVEL_FULL + 1);
        return SUNS[(KGUI_CC_SUN_STEPS <= step) ? (KGUI_CC_SUN_STEPS - 1) : step];
    }
    /* Just above nothing the Dot vibrates and does not sound (Luismi, 5-nov). */
    if ( level_vibrates(which, pm) )
    {
        return &kui_glyph_vib_40;
    }
    /* The speaker is struck through at nothing and has one, two and three
     * waves over the three thirds above it. */
    step = (0 < p) ? (1 + (((p - 1) * (KGUI_CC_SUN_STEPS - 1)) / KGUI_CC_LEVEL_FULL)) : 0;
    return SPKS[step];
}

/** @brief Draw a level where it now stands. */
static void level_show(kgui_level_t which, int32_t pm)
{
    s_levels[which].pm = pm;
    level_paint(s_levels[which].name, pm, level_glyph(which, pm),
                level_vibrates(which, pm) ? "Vibrate" : NULL);
}

/** @brief An lv_anim's setter: a level settling. */
/* cppcheck-suppress constParameterCallback ; lv_anim_exec_xcb_t takes the animated var as void *, whatever a setter does with it */
static void level_step(void *var, int32_t v)
{
    /* cppcheck-suppress misra-c2012-11.5 ; lv_anim_set_var() took a level_state_t, which LVGL hands back as void * */
    level_state_t const * const l = (level_state_t const *)var;

    level_show(l->which, v);
}

/** @brief Take a level to @p pct: at once off the glass, softly on it. */
static void level_go(kgui_level_t which, uint8_t pct)
{
    level_state_t * const l = &s_levels[which];
    int32_t               p = (int32_t)pct;

    if ( l->held )
    {
        return;   /* A finger has it. */
    }
    if ( KGUI_CC_LEVEL_FULL < p )
    {
        p = KGUI_CC_LEVEL_FULL;
    }
    p *= 10;
    if ( l->known && (p == l->target) )
    {
        return;
    }
    l->target = p;
    (void)lv_anim_delete(l, level_step);
    if ( (!l->known) || (lv_screen_active() != kgui_screen_settings) )
    {
        l->known = true;
        level_show(which, p);
    }
    else
    {
        lv_anim_t a;

        lv_anim_init(&a);
        lv_anim_set_var(&a, l);
        lv_anim_set_exec_cb(&a, level_step);
        lv_anim_set_values(&a, l->pm, p);
        lv_anim_set_duration(&a, KGUI_LEVEL_SNAP_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        (void)lv_anim_start(&a);
    }
}

void kgui_screen_settings_level_drag(kgui_level_t which, int32_t pm)
{
    level_state_t * const l = &s_levels[which];
    int32_t               to = pm;

    if ( to < l->floor )
    {
        to = l->floor;
    }
    if ( LEVEL_PM_FULL < to )
    {
        to = LEVEL_PM_FULL;
    }
    (void)lv_anim_delete(l, level_step);
    l->held = true;
    /* Near the finger it is the finger; a long way off (the first touch, a
     * fast flick) it catches up over a few frames rather than leaping. */
    if ( ((to - l->pm) > LEVEL_PM_NEAR) || ((l->pm - to) > LEVEL_PM_NEAR) )
    {
        to = l->pm + ((to - l->pm) / 2);
    }
    level_show(which, to);
}

void kgui_screen_settings_level_release(kgui_level_t which, uint8_t pct)
{
    s_levels[which].held   = false;
    s_levels[which].target = -1;
    level_go(which, pct);
}

void kgui_screen_settings_level(uint8_t pct)
{
    level_go(KGUI_LEVEL_BRIGHT, pct);
}

void kgui_screen_settings_volume(uint8_t pct)
{
    level_go(KGUI_LEVEL_VOLUME, pct);
}

/** LVGL's LV_ANIM_REPEAT_INFINITE, spelled with its unsigned type. */
static kgui_fluid_t   s_fluid;       /**< The battery's liquid (kgui_fluid.h). */
static lv_draw_buf_t *s_fluid_buf;   /**< Its picture, an A8 mask the colour goes through. */
static lv_obj_t      *s_fluid_obj;   /**< What shows it. */
static lv_timer_t    *s_fluid_timer; /**< Its steps, while AO_Gui lets it move. */
static bool           s_fluid_homed; /**< It has stood at the Dot's angle since the tilt started. */

/** @brief Draw the liquid where it now stands. */
static void fluid_paint(void)
{
    if ( (NULL != s_fluid_buf) && (NULL != s_fluid_obj) )
    {
        kgui_fluid_draw(&s_fluid, s_fluid_buf->data, s_fluid_buf->header.stride, KGUI_CC_CIRCLE);
        lv_image_cache_drop(s_fluid_buf);
        (void)lv_obj_invalidate(s_fluid_obj);
    }
}

/**
 * @brief One step of the liquid: nothing is drawn while nothing moves.
 *
 * It starts at the Dot's own angle: opening the tab is no reason to slosh.
 *
 * @param t The timer.
 */
static void fluid_tick(lv_timer_t *t)
{
    float angle = 0.0F;
    float jolt  = 0.0F;

    (void)t;
    if ( kgui_tilt_read(&angle, &jolt) )
    {
        if ( !s_fluid_homed )
        {
            s_fluid_homed = true;
            kgui_fluid_rest(&s_fluid, angle);
            fluid_paint();
        }
        else if ( kgui_fluid_step(&s_fluid, angle, jolt) )
        {
            fluid_paint();
        }
        else
        {
            /* At rest: the panel has nothing to repaint here. */
        }
    }
}

/**
 * @brief Build the battery's circle: the off-grey disc the others are, with a
 *        liquid in it.
 *
 * Its child is the liquid (kgui_fluid.h), which moves while AO_Gui lets it
 * (kgui_screen_settings_wave()) and only when the Dot does. It is a control
 * like the circles: a tap opens the Battery page, and the cursor rings it.
 *
 * @param parent The screen.
 */
static void battery_build(lv_obj_t *parent)
{
    lv_obj_t * const c = lv_obj_create(parent);

    if ( NULL == c )
    {
        return;
    }
    lv_obj_set_name(c, s_battery.name);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, s_battery.x, s_battery.y);
    lv_obj_set_size(c, KGUI_CC_CIRCLE, KGUI_CC_CIRCLE);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(c, KUI_COLOR_OFF, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(c, true, LV_PART_MAIN);
    lv_obj_set_scrollable(c, false);
    lv_obj_set_clickable(c, true);
    lv_obj_add_style(c, &kui_style_ring, LV_STATE_CHECKED);

    /* The liquid: an A8 picture, the charge's colour through it, the disc's
     * clip for its round. */
    s_fluid_buf = lv_draw_buf_create(KGUI_CC_CIRCLE, KGUI_CC_CIRCLE, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
    s_fluid_obj = lv_canvas_create(c);
    kgui_fluid_init(&s_fluid, KGUI_CC_CIRCLE);
    if ( (NULL != s_fluid_buf) && (NULL != s_fluid_obj) )
    {
        lv_canvas_set_draw_buf(s_fluid_obj, s_fluid_buf);
        lv_obj_set_pos(s_fluid_obj, 0, 0);
        lv_obj_set_clickable(s_fluid_obj, false);
        lv_obj_set_style_image_recolor(s_fluid_obj, KUI_COLOR_OK, LV_PART_MAIN);
        lv_obj_set_style_image_recolor_opa(s_fluid_obj, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_image_opa(s_fluid_obj, KGUI_CC_LIQUID_OPA, LV_PART_MAIN);
        fluid_paint();
    }

    /* The charge as a figure, on a small dark chip of its own, so it reads
     * the same over the green and over the black (Luismi, 4-nov). */
    lv_obj_t * const chip = lv_obj_create(c);

    if ( NULL != chip )
    {
        lv_obj_remove_style_all(chip);
        lv_obj_set_name(chip, "ctl_bat_chip");
        lv_obj_set_size(chip, kgui_size_content(), kgui_size_content());
        /* The figure as it is, on the circle's grey: no chip behind it. */
        lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_clickable(chip, false);
        lv_obj_set_scrollable(chip, false);
        lv_obj_align(chip, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t * const pct = lv_label_create(chip);

        if ( NULL != pct )
        {
            lv_obj_set_name(pct, "ctl_bat_pct");
            /* 28: the figure sits inside the circle with air round it. */
            lv_obj_set_style_text_font(pct, &kui_font_name, LV_PART_MAIN);
            lv_obj_set_style_text_color(pct, KUI_COLOR_TEXT_ON_BG, LV_PART_MAIN);
            lv_label_set_text_static(pct, "");
            lv_obj_set_clickable(pct, false);
        }
    }
}

/**
 * @brief The share circle, between the battery and Turn off: there, and off,
 *        until sharing exists (Luismi, 4-nov). Not a control yet: it takes no
 *        press and the pad's cursor passes it by.
 *
 * @param parent The screen.
 */
static void share_build(lv_obj_t *parent)
{
    lv_obj_t * const c = lv_obj_create(parent);

    if ( NULL == c )
    {
        return;
    }
    lv_obj_set_name(c, "ctl_share");
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, KGUI_CC_X1, KGUI_CC_Y1);
    lv_obj_set_size(c, KGUI_CC_CIRCLE, KGUI_CC_CIRCLE);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(c, KUI_COLOR_OFF, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(c, false);
    lv_obj_set_clickable(c, false);

    lv_obj_t * const img = lv_image_create(c);

    if ( NULL != img )
    {
        lv_image_set_src(img, &kgui_glyph_share_46);
        lv_obj_set_align(img, LV_ALIGN_CENTER);
        lv_obj_set_style_image_recolor(img, KUI_COLOR_TEXT_MUTED, LV_PART_MAIN);
        lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, LV_PART_MAIN);
        /* Off: present, and plainly not ready. */
        lv_obj_set_style_opa(img, LV_OPA_40, LV_PART_MAIN);
        lv_obj_set_clickable(img, false);
    }
}

void kgui_screen_settings_battery(uint8_t pct, lv_color_t col)
{
    if ( NULL == kgui_screen_settings )
    {
        return;
    }
    lv_obj_t const * const c = lv_obj_find_by_name(kgui_screen_settings, s_battery.name);

    if ( NULL == c )
    {
        return;
    }
    lv_obj_t * const fig = lv_obj_find_by_name(c, "ctl_bat_pct");
    int32_t          p = (int32_t)pct;

    if ( KGUI_CC_LEVEL_FULL < p )
    {
        p = KGUI_CC_LEVEL_FULL;
    }
    if ( NULL != fig )
    {
        char words[8];

        (void)snprintf(words, sizeof(words), "%d %%", (int)p);
        if ( strcmp(lv_label_get_text(fig), words) != 0 )
        {
            lv_label_set_text(fig, words);
        }
        /* Light Grey while the charge is fine; the low colour AO_Gui gives
         * (the danger red) when it is not, so the state still shows. */
        lv_obj_set_style_text_color(fig, lv_color_eq(col, KUI_COLOR_DANGER) ? KUI_COLOR_DANGER
                                                                             : KUI_COLOR_TEXT_ON_BG,
                                    LV_PART_MAIN);
    }
    /* The same charge in the same colour is the same picture: the 2 s
     * announcement of the battery lands here whether or not it moved. */
    if ( NULL != s_fluid_obj )
    {
        static int32_t s_pct = -1;

        if ( !lv_color_eq(lv_obj_get_style_image_recolor(s_fluid_obj, LV_PART_MAIN), col) )
        {
            lv_obj_set_style_image_recolor(s_fluid_obj, col, LV_PART_MAIN);
        }
        if ( p != s_pct )
        {
            bool const first = (0 > s_pct);

            s_pct = p;
            kgui_fluid_fill(&s_fluid, ((float)p * KGUI_CC_LIQUID_FULL) / (float)KGUI_CC_LEVEL_FULL, first);
            if ( first || (NULL == s_fluid_timer) )
            {
                if ( !first )
                {
                    kgui_fluid_fill(&s_fluid, ((float)p * KGUI_CC_LIQUID_FULL) / (float)KGUI_CC_LEVEL_FULL, true);
                }
                fluid_paint();
            }
        }
    }
}

void kgui_screen_settings_wave(bool run)
{
    if ( NULL == kgui_screen_settings )
    {
        return;
    }
    kgui_tilt_run(run);
    if ( run && (NULL == s_fluid_timer) )
    {
        s_fluid_homed = false;
        s_fluid_timer = lv_timer_create(&fluid_tick, KGUI_FLUID_STEP_MS, NULL);
    }
    if ( (!run) && (NULL != s_fluid_timer) )
    {
        lv_timer_delete(s_fluid_timer);
        s_fluid_timer = NULL;
    }
}

/**
 * @brief The grab bar at the tab's foot: subtle, it says the tab can be taken
 *        from below (Luismi, 5-nov).
 * @param parent The settings screen.
 */
static void handle_build(lv_obj_t *parent)
{
    lv_obj_t * const h = lv_obj_create(parent);

    if ( NULL != h )
    {
        lv_obj_set_name(h, "ctl_handle");
        lv_obj_remove_style_all(h);
        lv_obj_set_pos(h, ((int32_t)KBSP_LCD_H_RES - KGUI_CC_HANDLE_W) / 2, KGUI_CC_HANDLE_Y);
        lv_obj_set_size(h, KGUI_CC_HANDLE_W, KGUI_CC_HANDLE_H);
        lv_obj_set_style_radius(h, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(h, KUI_COLOR_TEXT_MUTED, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(h, LV_OPA_50, LV_PART_MAIN);
        lv_obj_set_scrollable(h, false);
        lv_obj_set_clickable(h, false);
    }
}

void kgui_screen_settings_build(void)
{
    kgui_screen_settings = lv_obj_create(NULL);
    lv_obj_t * const scr = kgui_screen_settings;
    lv_obj_set_name_static(scr, "screen_settings_#");
    lv_obj_add_style(scr, &kui_style_screen, 0);
    lv_obj_set_scrollable(scr, false);

    for ( uint32_t i = 0U; i < (sizeof(s_circles) / sizeof(s_circles[0])); i++ )
    {
        circle_build(scr, &s_circles[i]);
    }
    battery_build(scr);
    handle_build(kgui_screen_settings);
    share_build(scr);
    level_build(scr, "ctl_level", KGUI_CC_LEVEL_Y, &kgui_glyph_sun3_52);
    level_build(scr, "ctl_volume", KGUI_CC_VOLUME_Y, &kgui_glyph_spk3_52);
    kgui_screen_settings_level(100U);
    kgui_screen_settings_volume(100U);
}
