/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_curtain.c
 * @brief The curtain's pictures, screen and objects (kgui_curtain.h).
 */
#include "kgui_curtain.h"

#include <math.h>
#include <string.h>

#include "kbsp_board.h"
#include "kdrv_display.h"
#include "kgui_hint.h"
#include "khal_time.h"
#include "kode_log.h"
#include "kui.h"
#include "src/misc/cache/instance/lv_image_cache.h"

/** @brief Log tag: kgui's own. */
#define TAG "kgui"

#define CURTAIN_W           ((int32_t)KBSP_LCD_H_RES)          /**< The glass's width, in pixels. */
#define CURTAIN_H           ((int32_t)KBSP_LCD_V_RES)          /**< The glass's height, in pixels. */
#define CURTAIN_STRIDE      (KBSP_LCD_H_RES * 2U)              /**< A row of the framebuffer, in bytes: RGB565 is two a pixel. */
#define CURTAIN_BYTES       (CURTAIN_STRIDE * KBSP_LCD_V_RES)  /**< The framebuffer, in bytes. */
#define CURTAIN_RUBBER_DIV  2                                  /**< The rubber's share grows linearly to the whole finger over its length: the slope's integral is the distance squared over this many lengths. */

#define CURTAIN_PX_BYTES 2U   /**< RGB565. */

static lv_draw_buf_t *s_glass_buf;    /**< The glass, as it was copied. */
static lv_draw_buf_t *s_mix_buf;      /**< A circle's growth, written by hand: one opaque picture for LVGL to draw. */
static lv_draw_buf_t const *s_mix_base; /**< What the circle opens over. */
static lv_draw_buf_t const *s_mix_top;  /**< What it opens onto. */
static lv_draw_buf_t *s_other_buf;    /**< The other screen, as it was rendered. */
static lv_obj_t      *s_curtain_screen;       /**< The curtain's own screen: LVGL draws it and nothing under it. */
static lv_obj_t      *s_below;        /**< Its first child: the picture below, opaque and the size of the glass. */
static lv_obj_t      *s_dim;          /**< The black dim over it. */
static lv_obj_t      *s_panel;        /**< The panel's picture, over the dim. */
static lv_obj_t      *s_cue;          /**< The armed cue's handle, over the panel's own. */
static lv_obj_t      *s_cue_text;     /**< The line under the cue's handle. */
static lv_obj_t      *s_grow;         /**< A growing take's window, black and of the glass's shape, over the panel. */
static lv_obj_t      *s_mix;          /**< What shows s_mix_buf. */
static lv_obj_t      *s_grow_pic;     /**< The picture below, fixed to the glass inside the window. */
static int32_t        s_cue_x;        /**< The panel's handle: its left, from the panel's left. */
static int32_t        s_cue_y;        /**< The panel's handle: its top, from the panel's top. */
static int32_t        s_cue_h;        /**< The panel's handle: its height. */
static bool           s_cue_known;    /**< The take was given a handle, and the cue sits over it. */
static bool           s_cue_lit;      /**< The cue is drawn. */
static bool           s_on_glass;     /**< The curtain's screen is the one on the glass. */
static bool           s_dims;         /**< The picture below darkens as the panel comes down. */
static bool           s_sideways;     /**< The pictures slide along the glass: the panel in from the left, the picture below out to the right. */
static bool           s_mirrored;     /**< A sideways take runs the other way: the panel in from the right, the picture below out to the left. */
static bool           s_grows;        /**< The picture below grows over the panel: in place, or out of a circle. */
static bool           s_grow_from;    /**< A growing take starts from a circle (s_from_*), not in place. */
static lv_area_t      s_from_area;    /**< That circle's place on the glass. */
static lv_color_t     s_from_color;   /**< Its colour. */
static uint32_t       s_flushes;      /**< Chunks handed to the panel, as kgui_curtain_flushed() counts them. */

uint32_t kgui_place_left_by(uint32_t placed_ms, uint32_t now_ms, uint32_t least_ms)
{
    uint32_t const since = now_ms - placed_ms;
    uint32_t       left  = 0U;

    if ( since < least_ms )
    {
        left = least_ms - since;
    }
    return left;
}

uint32_t kgui_place_left(uint32_t placed_ms, uint32_t now_ms)
{
    return kgui_place_left_by(placed_ms, now_ms, KGUI_DRAG_PLACE_MS);
}

/**
 * @brief Make one picture buffer the size of the glass, if it is not there yet.
 *
 * @param buf The buffer's pointer; left alone when it holds a buffer.
 */
static void curtain_buf_make(lv_draw_buf_t **buf)
{
    if ( NULL == *buf )
    {
        *buf = lv_draw_buf_create((uint32_t)CURTAIN_W, (uint32_t)CURTAIN_H,
                                  LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    }
}

/**
 * @brief Delete the screen and everything on it, after one of them was refused.
 */
static void curtain_screen_drop(void)
{
    if ( NULL != s_curtain_screen )
    {
        lv_obj_delete(s_curtain_screen);
    }
    s_curtain_screen   = NULL;
    s_below    = NULL;
    s_dim      = NULL;
    s_panel    = NULL;
    s_cue      = NULL;
    s_cue_text = NULL;
    s_grow     = NULL;
    s_grow_pic = NULL;
    s_mix      = NULL;
}

/**
 * @brief Make the armed cue's two objects, hidden, over the panel.
 *
 * The handle is the kit's selection blue and as round as the glass's
 * corners; the line wears the hint's own look - the small face on a dark
 * capsule, so it reads over any picture - and is centred across the glass.
 * Neither is clickable.
 */
static void curtain_cue_make(void)
{
    lv_obj_remove_style_all(s_cue);
    /* What you do is yellow (ui-dot 02); blue is Kody's and the links'. */
    lv_obj_set_style_bg_color(s_cue, KUI_COLOR_ACTION, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_cue, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_cue, KUI_RADIUS_GLASS, LV_PART_MAIN);
    lv_obj_set_clickable(s_cue, false);
    lv_obj_set_hidden(s_cue, true);
    lv_obj_remove_style_all(s_cue_text);
    kgui_hint_dress(s_cue_text);
    lv_obj_set_style_align(s_cue_text, LV_ALIGN_TOP_MID, LV_PART_MAIN);
    lv_label_set_text_static(s_cue_text, KGUI_CURTAIN_CUE_TEXT);
    lv_obj_set_clickable(s_cue_text, false);
    lv_obj_set_hidden(s_cue_text, true);
}

/**
 * @brief Make a growing take's window and the picture inside it, hidden.
 *
 * The window clips the picture to itself, corners included, and the picture
 * keeps the glass's place whatever the window's (curtain_grow_place()): it
 * is revealed, not scaled. Neither is clickable.
 */
static void curtain_grow_make(void)
{
    lv_obj_remove_style_all(s_grow);
    lv_obj_remove_style_all(s_grow_pic);
    lv_obj_set_style_bg_color(s_grow, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_clip_corner(s_grow, true, LV_PART_MAIN);
    lv_obj_set_scrollable(s_grow, false);
    lv_obj_set_clickable(s_grow, false);
    lv_obj_set_size(s_grow_pic, CURTAIN_W, CURTAIN_H);
    lv_obj_set_hidden(s_grow, true);
}

/**
 * @brief Make the curtain's screen with the objects on it, in the order
 *        they are drawn: the picture below, the dim, the panel, the cue,
 *        the growing square.
 *
 * Nothing on it is clickable, so a press during the motion lands on nothing.
 *
 * @return true when the screen and all its objects exist.
 */
static bool curtain_screen_make(void)
{
    bool made = false;

    s_curtain_screen = lv_obj_create(NULL);
    if ( NULL != s_curtain_screen )
    {
        s_below    = lv_image_create(s_curtain_screen);
        s_dim      = lv_obj_create(s_curtain_screen);
        s_panel    = lv_image_create(s_curtain_screen);
        s_cue      = lv_obj_create(s_curtain_screen);
        s_cue_text = lv_label_create(s_curtain_screen);
        s_grow     = lv_obj_create(s_curtain_screen);
        s_mix      = lv_image_create(s_curtain_screen);
    }
    if ( NULL != s_grow )
    {
        s_grow_pic = lv_image_create(s_grow);
    }
    if ( (NULL != s_curtain_screen) && (NULL != s_below) && (NULL != s_dim)
        && (NULL != s_panel) && (NULL != s_cue) && (NULL != s_cue_text)
        && (NULL != s_grow) && (NULL != s_grow_pic) && (NULL != s_mix) )
    {
        /* Pictures and a dim, nothing the theme adds around them. */
        lv_obj_remove_style_all(s_curtain_screen);
        lv_obj_remove_style_all(s_below);
        lv_obj_remove_style_all(s_dim);
        lv_obj_remove_style_all(s_panel);
        lv_obj_set_style_bg_color(s_curtain_screen, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_curtain_screen, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_dim, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_dim, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_scrollable(s_curtain_screen, false);
        lv_obj_set_clickable(s_curtain_screen, false);
        lv_obj_set_clickable(s_dim, false);
        lv_obj_set_scrollable(s_dim, false);
        lv_obj_set_size(s_dim, CURTAIN_W, CURTAIN_H);
        lv_obj_set_pos(s_dim, 0, 0);
        lv_obj_set_size(s_below, CURTAIN_W, CURTAIN_H);
        lv_obj_set_pos(s_below, 0, 0);
        lv_obj_set_size(s_panel, CURTAIN_W, CURTAIN_H);
        lv_obj_set_pos(s_panel, 0, -CURTAIN_H);
        curtain_cue_make();
        curtain_grow_make();
        lv_obj_remove_style_all(s_mix);
        lv_obj_set_size(s_mix, CURTAIN_W, CURTAIN_H);
        lv_obj_set_pos(s_mix, 0, 0);
        lv_obj_set_clickable(s_mix, false);
        lv_obj_set_hidden(s_mix, true);
        made = true;
    }
    else
    {
        curtain_screen_drop();
    }
    return made;
}

/**
 * @brief Make whatever the pictures need and does not have yet.
 *
 * @return true when both buffers, the screen and all its objects exist.
 */
static bool curtain_build(void)
{
    bool ready;

    curtain_buf_make(&s_glass_buf);
    curtain_buf_make(&s_other_buf);
    curtain_buf_make(&s_mix_buf);    /* Without it a circle's growth is drawn the costly way. */
    ready = (NULL != s_glass_buf) && (NULL != s_other_buf);
    if ( ready && (NULL == s_curtain_screen) )
    {
        ready = curtain_screen_make();
    }
    return ready;
}

/**
 * @brief Make the glass picture match the glass.
 *
 * The front framebuffer is 410 x 502 RGB565 with a stride of 820 bytes; it
 * is wrapped as a draw buffer of that stride and LVGL copies it row by row,
 * whatever stride the picture's buffer has. The copy is skipped when no chunk
 * has gone to the panel since the last one and the active screen is the same:
 * the glass has not changed.
 *
 * @return false when the framebuffer could not be wrapped.
 */
static bool curtain_glass_copy(void)
{
    static const lv_obj_t *copy_screen;  /* The active screen at the last copy. */
    static uint32_t  copy_flushes;       /* s_flushes at the last copy. */
    static bool      copy_done;          /* There has been a copy. */
    const lv_obj_t * const screen = lv_screen_active();
    bool ok = true;

    if ( !copy_done || (copy_screen != screen) || (copy_flushes != s_flushes) )
    {
        lv_draw_buf_t front;

        if ( lv_draw_buf_init(&front, KBSP_LCD_H_RES, KBSP_LCD_V_RES,
                             LV_COLOR_FORMAT_RGB565, CURTAIN_STRIDE,
                             kdrv_display_front_buffer(), CURTAIN_BYTES)
            == LV_RESULT_OK )
        {
            lv_draw_buf_copy(s_glass_buf, NULL, &front, NULL);
            copy_done    = true;
            copy_screen  = screen;
            copy_flushes = s_flushes;
        }
        else
        {
            ok = false;
        }
    }
    return ok;
}

/**
 * @brief Point the two image objects at the two pictures.
 *
 * @param other_is_panel true when the other screen is the panel.
 */
static void curtain_pictures_set(bool other_is_panel)
{
    if ( other_is_panel )
    {
        lv_image_set_src(s_below, s_glass_buf);
        lv_image_set_src(s_grow_pic, s_glass_buf);
        lv_image_set_src(s_panel, s_other_buf);
        s_mix_base = s_other_buf;
        s_mix_top  = s_glass_buf;
    }
    else
    {
        lv_image_set_src(s_below, s_other_buf);
        lv_image_set_src(s_grow_pic, s_other_buf);
        lv_image_set_src(s_panel, s_glass_buf);
        s_mix_base = s_glass_buf;
        s_mix_top  = s_other_buf;
    }
}

/**
 * @brief Put the cue's handle over the panel's own, or take it away.
 *
 * Read from the settings screen's handle as it stands: its area from the
 * screen's corner, which is where it sits in the panel's picture.
 *
 * @param handle The settings screen's handle; NULL leaves the cue unknown.
 */
static void curtain_cue_read(lv_obj_t const *handle)
{
    s_cue_known = false;
    s_cue_lit   = false;
    lv_obj_set_hidden(s_cue, true);
    lv_obj_set_hidden(s_cue_text, true);
    if ( NULL != handle )
    {
        lv_obj_t const * const screen = lv_obj_get_screen(handle);
        lv_area_t              area;
        lv_area_t              corner;

        lv_obj_update_layout(screen);
        lv_obj_get_coords(handle, &area);
        lv_obj_get_coords(screen, &corner);
        s_cue_x = area.x1 - corner.x1;
        s_cue_y = area.y1 - corner.y1;
        s_cue_h = lv_area_get_height(&area);
        lv_obj_set_size(s_cue, lv_area_get_width(&area), s_cue_h);
        s_cue_known = true;
    }
}

void kgui_curtain_flushed(void)
{
    ++s_flushes;
}

int32_t kgui_curtain_rubber(int32_t travel)
{
    int32_t fed = travel;

    if ( 0 < travel )
    {
        if ( KGUI_LOCK_PULL_RUBBER_PX > travel )
        {
            fed = (travel * travel) / (CURTAIN_RUBBER_DIV * KGUI_LOCK_PULL_RUBBER_PX);
        }
        else
        {
            fed = travel - (KGUI_LOCK_PULL_RUBBER_PX / CURTAIN_RUBBER_DIV);
        }
    }
    return fed;
}

bool kgui_curtain_take(kgui_curtain_spec_t const *spec)
{
    uint64_t const t0 = khal_time_us();
    bool           ok = false;

    if ( (NULL != spec) && (NULL != spec->other)
        && (kdrv_display_front_buffer() != NULL) && curtain_build()
        && curtain_glass_copy() )
    {
        uint64_t const t_glass = khal_time_us();

        if ( lv_snapshot_take_to_draw_buf(spec->other, LV_COLOR_FORMAT_RGB565,
                                         s_other_buf) == LV_RESULT_OK )
        {
            const char *name = "below";
            uint64_t const t_end = khal_time_us();

            curtain_pictures_set(spec->other_is_panel);
            curtain_cue_read(spec->handle);
            s_dims     = spec->dims;
            s_sideways = spec->sideways;
            s_mirrored = spec->sideways && spec->mirrored;
            s_grows    = spec->grows;
            s_grow_from = spec->grows && spec->from;
            s_from_area   = spec->from_area;
            s_from_color  = spec->from_color;
            lv_obj_set_hidden(s_grow, !s_grows);
            lv_obj_set_hidden(s_mix, true);
            if ( spec->other_is_panel )
            {
                name = "panel";
            }
            KLOG_D(TAG, "curtain: pictures in %u ms (glass %u ms, %s %u ms)",
                   (unsigned)((t_end - t0) / KGUI_US_PER_MS),
                   (unsigned)((t_glass - t0) / KGUI_US_PER_MS), name,
                   (unsigned)((t_end - t_glass) / KGUI_US_PER_MS));
            ok = true;
        }
        else
        {
            KLOG_W(TAG, "curtain: the snapshot of the other screen failed");
        }
    }
    return ok;
}

/**
 * @brief Draw the armed cue where the panel is, or take it away.
 *
 * The handle and the line under it move with the panel: the handle sits
 * where the panel's own is, the line KGUI_CURTAIN_LINE_GAP_PX under it.
 * Nothing is moved or drawn while the cue is not lit.
 *
 * @param off   The panel's offset from closed, already clamped.
 * @param armed true to draw the cue; it is drawn only if the take was given
 *              a handle.
 */
static void curtain_cue_show(int32_t off, bool armed)
{
    bool const lit = armed && s_cue_known;

    if ( lit )
    {
        lv_obj_set_pos(s_cue, s_cue_x, (s_cue_y + off) - CURTAIN_H);
        lv_obj_set_pos(s_cue_text, 0,
                       ((s_cue_y + s_cue_h + KGUI_CURTAIN_LINE_GAP_PX) + off)
                       - CURTAIN_H);
    }
    if ( lit != s_cue_lit )
    {
        lv_obj_set_hidden(s_cue, !lit);
        lv_obj_set_hidden(s_cue_text, !lit);
        s_cue_lit = lit;
    }
}

/**
 * @brief How far the panel travels: the glass's height down it, or its
 *        width along it.
 *
 * @return The travel, in pixels.
 */
static int32_t curtain_extent(void)
{
    int32_t extent = CURTAIN_H;

    if ( s_sideways || s_grows )
    {
        extent = CURTAIN_W;
    }
    return extent;
}

/**
 * @brief A straight step from one value to another.
 *
 * @param a The value at the start.
 * @param b The value at the end.
 * @param k The way along, 0 to KGUI_CURTAIN_GROW_K.
 * @return The value at @p k.
 */
static int32_t curtain_lerp(int32_t a, int32_t b, int32_t k)
{
    return a + (((b - a) * k) / KGUI_CURTAIN_GROW_K);
}

/** @brief The whole square root of @p v. */
static int32_t curtain_isqrt(int32_t v)
{
    int32_t r = 0;

    while ( ((r + 1) * (r + 1)) <= v )
    {
        r++;
    }
    return r;
}

/**
 * @brief A circle's growth by hand: the two pictures row by row, into one.
 *
 * A window with clip_corner makes LVGL compose a layer of the whole glass for
 * every frame, which is what made the growth drop frames. This copies each row
 * in up to three runs of the two opaque pictures (Luismi, 5-nov), and LVGL draws
 * one opaque picture, and only the part that changed.
 *
 * @param cx The circle's centre.
 * @param cy The circle's centre.
 * @param r  Its radius.
 */
static void curtain_mix(int32_t cx, int32_t cy, int32_t r)
{
    static int32_t s_mix_r;   /**< The circle's radius in the picture as it stands. */
    uint32_t const out_s  = s_mix_buf->header.stride;
    uint32_t const base_s = s_mix_base->header.stride;
    uint32_t const top_s  = s_mix_top->header.stride;
    uint32_t const px     = CURTAIN_PX_BYTES;
    int32_t const  most   = (r > s_mix_r) ? r : s_mix_r;

    for ( int32_t y = 0; CURTAIN_H > y; ++y )
    {
        uint8_t * const       out  = &s_mix_buf->data[(uint32_t)y * out_s];
        uint8_t const * const base = &s_mix_base->data[(uint32_t)y * base_s];
        uint8_t const * const top  = &s_mix_top->data[(uint32_t)y * top_s];
        int32_t const         dy   = y - cy;
        int32_t               x1   = 0;
        int32_t               x2   = 0;

        if ( (dy * dy) < (r * r) )
        {
            int32_t const chord = (r * r) - (dy * dy);
            int32_t const half  = (int32_t)sqrtf((float)chord);

            x1 = ((cx - half) < 0) ? 0 : (cx - half);
            x2 = ((cx + half) > CURTAIN_W) ? CURTAIN_W : (cx + half);
        }
        if ( x2 > x1 )
        {
            int32_t const run  = x2 - x1;
            int32_t const rest = CURTAIN_W - x2;

            (void)memcpy(out, base, (uint32_t)x1 * px);
            (void)memcpy(&out[(uint32_t)x1 * px], &top[(uint32_t)x1 * px], (uint32_t)run * px);
            (void)memcpy(&out[(uint32_t)x2 * px], &base[(uint32_t)x2 * px], (uint32_t)rest * px);
        }
        else
        {
            (void)memcpy(out, base, (uint32_t)CURTAIN_W * px);
        }
    }
    s_mix_r = r;
    lv_image_cache_drop(s_mix_buf);
    if ( lv_obj_is_hidden(s_mix) )
    {
        lv_image_set_src(s_mix, s_mix_buf);
        lv_obj_set_hidden(s_mix, false);
        (void)lv_obj_invalidate(s_mix);
    }
    else
    {
        lv_area_t box = { cx - most, cy - most, cx + most, cy + most };

        (void)lv_obj_invalidate_area(s_mix, &box);
    }
}

/**
 * @brief Put a growing take's window where its growth puts it.
 *
 * In place, the window is the whole glass with no ground of its own and the
 * picture inside it fades in straight over the panel: a crossfade, nothing
 * moves (the apocrifa's tap into Apps). Out of a circle it is kshell's level
 * zoom: the window goes from the circle's place, colour and radius to the
 * whole glass, its ground darkening from the circle's colour to black, and
 * the picture fades in over the last two thirds of the way; going back, the
 * same the other way, ending in the circle it came from. The picture keeps
 * its place on the glass: nothing is scaled, it is revealed and faded.
 *
 * @param off The panel's offset from closed, already clamped: the growth is
 *            what is left of the glass's width.
 */
static void curtain_grow_place(int32_t off)
{
    int32_t const k = ((CURTAIN_W - off) * KGUI_CURTAIN_GROW_K) / CURTAIN_W;
    int32_t       x = 0;
    int32_t       y = 0;
    int32_t       w = CURTAIN_W;
    int32_t       h = CURTAIN_H;
    int32_t const shade = (k * (int32_t)LV_OPA_COVER) / KGUI_CURTAIN_GROW_K;
    int32_t       ink = shade;

    if ( s_grow_from )
    {
        /* A circle all the way (Luismi, 5-nov): it grows from the one it
         * stands on, about its centre, until it covers the glass's farthest
         * corner. */
        int32_t const cx = s_from_area.x1 + (lv_area_get_width(&s_from_area) / 2);
        int32_t const cy = s_from_area.y1 + (lv_area_get_height(&s_from_area) / 2);
        int32_t const dx = (cx > (CURTAIN_W - cx)) ? cx : (CURTAIN_W - cx);
        int32_t const dy = (cy > (CURTAIN_H - cy)) ? cy : (CURTAIN_H - cy);
        int32_t const r  = curtain_lerp(lv_area_get_width(&s_from_area) / 2,
                                        curtain_isqrt((dx * dx) + (dy * dy)) + 1, k);

        if ( (NULL != s_mix_buf) && (NULL != s_mix_base) && (NULL != s_mix_top) )
        {
            lv_obj_set_hidden(s_grow, true);
            curtain_mix(cx, cy, r);
            return;
        }
        x = cx - r;
        y = cy - r;
        w = 2 * r;
        h = 2 * r;
        ink = 0;
        if ( KGUI_CURTAIN_GROW_INK_FROM < k )
        {
            ink = ((k - KGUI_CURTAIN_GROW_INK_FROM) * (int32_t)LV_OPA_COVER)
                  / (KGUI_CURTAIN_GROW_K - KGUI_CURTAIN_GROW_INK_FROM);
        }
        lv_obj_set_style_radius(s_grow, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_grow,
                                  lv_color_mix(lv_color_black(), s_from_color,
                                               (uint8_t)shade),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_grow, LV_OPA_COVER, LV_PART_MAIN);
    }
    else
    {
        lv_obj_set_style_radius(s_grow, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_grow, LV_OPA_TRANSP, LV_PART_MAIN);
    }
    lv_obj_set_pos(s_grow, x, y);
    lv_obj_set_size(s_grow, w, h);
    lv_obj_set_pos(s_grow_pic, -x, -y);
    lv_obj_set_style_opa(s_grow_pic, (lv_opa_t)ink, LV_PART_MAIN);
    lv_obj_set_hidden(s_grow, 0 == k);
}

/**
 * @brief Put the two pictures where an offset puts them.
 *
 * Down the glass the panel's foot is at the offset and the picture below
 * stays; along it the panel's right edge is at the offset and the picture
 * below is pushed out by the same distance, so the two move as one page (a
 * mirrored take: the panel's left edge, at the glass's width less it). A
 * growing take leaves both pictures where they are and moves its window.
 *
 * @param off The panel's offset from closed, already clamped.
 */
static void curtain_pictures_place(int32_t off)
{
    if ( s_grows )
    {
        lv_obj_set_pos(s_panel, 0, 0);
        lv_obj_set_pos(s_below, 0, 0);
        curtain_grow_place(off);
    }
    else if ( s_sideways && s_mirrored )
    {
        lv_obj_set_pos(s_panel, CURTAIN_W - off, 0);
        lv_obj_set_pos(s_below, -off, 0);
    }
    else if ( s_sideways )
    {
        lv_obj_set_pos(s_panel, off - CURTAIN_W, 0);
        lv_obj_set_pos(s_below, off, 0);
    }
    else
    {
        /* Whole: a fade way before this take leaves the panel clear. */
        lv_obj_set_style_opa(s_panel, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_pos(s_panel, 0, off - CURTAIN_H);
        lv_obj_set_pos(s_below, 0, 0);
    }
}

void kgui_curtain_show(int32_t offset, bool armed)
{
    int32_t       off    = offset;
    int32_t const extent = curtain_extent();
    int32_t       dim;

    if ( 0 > off )
    {
        off = 0;
    }
    if ( off > extent )
    {
        off = extent;
    }
    dim = 0;
    if ( s_dims )
    {
        dim = (off * (int32_t)KGUI_CURTAIN_DIM_OPA) / extent;
    }
    if ( NULL != s_panel )
    {
        curtain_pictures_place(off);
        lv_obj_set_style_bg_opa(s_dim, (lv_opa_t)dim, LV_PART_MAIN);
        curtain_cue_show(off, armed);
        if ( !s_on_glass )
        {
            s_on_glass = true;
            lv_screen_load(s_curtain_screen);
        }
    }
}

void kgui_curtain_hide(lv_obj_t *back)
{
    if ( s_on_glass )
    {
        s_on_glass = false;
        if ( lv_screen_active() == s_curtain_screen )
        {
            lv_screen_load(back);
        }
    }
}
