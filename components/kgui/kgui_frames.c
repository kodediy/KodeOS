/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_frames.c
 * @brief Timing a motion by the frames AO_Gui hands to the panel (kgui_frames.h).
 */
#include "kgui_frames.h"

#include <stdio.h>
#include <string.h>

#include "kdrv_display.h"
#include "kode_log.h"

/** @brief Log tag: kgui's own. */
#define TAG "kgui"

#define FRAMES_BINS             4U     /**< Refresh counts told apart: 1, 2, 3 and 4 or more. */
#define FRAMES_US_PER_TENTH_MS  100U   /**< Microseconds in a tenth of a millisecond. */
#define FRAMES_HALF_REFRESH_US  (KDRV_DISPLAY_FRAME_US / 2U)  /**< Half a refresh: rounds an interval to the nearest one. */
#define FRAMES_TAIL_US          (KGUI_FRAMES_TAIL_REFRESHES * KDRV_DISPLAY_FRAME_US) /**< How long a window waits for its last frame. */

/** @brief The window being counted. */
typedef struct {
    bool     running;                        /**< A window is open. */
    bool     end_next;                       /**< It closes on the next frame it counts. */
    bool     tail_on;                        /**< kgui_frames_tick() has begun to wait for that frame. */
    uint32_t tail_us;                        /**< When it began to. */
    char     name[KGUI_FRAMES_NAME_MAX];     /**< The motion it times. */
    uint32_t begin_us;                       /**< When it opened. */
    uint32_t last_us;                        /**< When its last frame came. */
    uint32_t frames;                         /**< Frames counted. */
    uint32_t sum_us;                         /**< The intervals between frames, added up. */
    uint32_t longest_us;                     /**< The longest interval. */
    uint32_t refreshes[FRAMES_BINS];         /**< Intervals by refreshes: 1, 2, 3, 4+. */
} frames_window_t;

static frames_window_t s_win;  /**< The one window being counted. */

void kgui_frames_begin(const char *name, uint32_t now_us)
{
    const char *label = name;

    if ( s_win.running )
    {
        kgui_frames_end(now_us);
    }
    if ( NULL == label )
    {
        label = "";
    }
    (void)memset(&s_win, 0, sizeof(s_win));
    (void)snprintf(s_win.name, sizeof(s_win.name), "%s", label);
    s_win.running  = true;
    s_win.begin_us = now_us;
}

void kgui_frames_frame(uint32_t now_us)
{
    if ( !s_win.running )
    {
        return;
    }
    if ( 0U == s_win.frames )
    {
        /* The first frame has no frame before it: it starts the cadence. */
        s_win.last_us = now_us;
        s_win.frames  = 1U;
    }
    else
    {
        uint32_t const interval  = now_us - s_win.last_us;
        uint32_t       refreshes = (interval + FRAMES_HALF_REFRESH_US) / KDRV_DISPLAY_FRAME_US;

        if ( 1U > refreshes )
        {
            refreshes = 1U;
        }
        if ( FRAMES_BINS < refreshes )
        {
            refreshes = FRAMES_BINS;
        }
        ++s_win.refreshes[refreshes - 1U];
        s_win.sum_us += interval;
        if ( interval > s_win.longest_us )
        {
            s_win.longest_us = interval;
        }
        s_win.last_us = now_us;
        ++s_win.frames;
    }
    if ( s_win.end_next )
    {
        kgui_frames_end(now_us);
    }
}

void kgui_frames_end(uint32_t now_us)
{
    uint32_t average = 0U;

    if ( !s_win.running )
    {
        return;
    }
    s_win.running  = false;
    s_win.end_next = false;
    /* n frames have n - 1 intervals; a window of one frame or none has
     * none. */
    if ( 1U < s_win.frames )
    {
        average = s_win.sum_us / (s_win.frames - 1U);
    }
    KLOG_D(TAG, "motion: %s %u ms, %u frames, %u.%u ms average, "
                "%u.%u ms longest, refreshes 1:%u 2:%u 3:%u 4+:%u",
           s_win.name,
           (unsigned)((now_us - s_win.begin_us) / KGUI_US_PER_MS),
           (unsigned)s_win.frames,
           (unsigned)(average / KGUI_US_PER_MS),
           (unsigned)((average % KGUI_US_PER_MS) / FRAMES_US_PER_TENTH_MS),
           (unsigned)(s_win.longest_us / KGUI_US_PER_MS),
           (unsigned)((s_win.longest_us % KGUI_US_PER_MS) / FRAMES_US_PER_TENTH_MS),
           (unsigned)s_win.refreshes[0],
           (unsigned)s_win.refreshes[1],
           (unsigned)s_win.refreshes[2],
           (unsigned)s_win.refreshes[3]);
}

void kgui_frames_end_next(void)
{
    if ( s_win.running )
    {
        s_win.end_next = true;
    }
}

void kgui_frames_tick(uint32_t now_us)
{
    if ( s_win.running && s_win.end_next )
    {
        if ( !s_win.tail_on )
        {
            s_win.tail_on = true;
            s_win.tail_us = now_us;
        }
        else if ( (now_us - s_win.tail_us) >= FRAMES_TAIL_US )
        {
            uint32_t end_us = now_us;

            if ( 0U < s_win.frames )
            {
                end_us = s_win.last_us;
            }
            kgui_frames_end(end_us);
        }
        else
        {
            /* The frame may still come. */
        }
    }
}

bool kgui_frames_running(void)
{
    return s_win.running;
}

bool kgui_frames_is(const char *name)
{
    bool same = false;

    if ( s_win.running && (NULL != name) )
    {
        same = (strncmp(s_win.name, name, KGUI_FRAMES_NAME_MAX - 1U) == 0);
    }
    return same;
}
