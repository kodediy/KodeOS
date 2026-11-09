/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_welcome.c
 * @brief The landing after Welcome (kgui_welcome.h): the pure part.
 */
#include "kgui_welcome.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define PATH_GLASS_W   410.0F  /**< The glass's width; kgui_welcome_ring.c holds it to the board's. */
#define PATH_GLASS_H   502.0F  /**< Its height. */
#define PATH_GLASS_R   114.0F  /**< Its corners. */
#define PATH_IN        ((float)KGUI_WELCOME_RING_W / 2.0F)  /**< The stroke's middle, in from the glass. */
#define PATH_R         (PATH_GLASS_R - PATH_IN)             /**< The path's corners. */
#define PATH_X0        PATH_IN
#define PATH_X1        (PATH_GLASS_W - PATH_IN)
#define PATH_Y0        PATH_IN
#define PATH_Y1        (PATH_GLASS_H - PATH_IN)
#define PATH_CX0       PATH_GLASS_R                         /**< The centres of the corners. */
#define PATH_CX1       (PATH_GLASS_W - PATH_GLASS_R)
#define PATH_CY0       PATH_GLASS_R
#define PATH_CY1       (PATH_GLASS_H - PATH_GLASS_R)
#define PATH_PI        3.14159265F
#define PATH_QUARTER   ((PATH_PI * PATH_R) / 2.0F)          /**< A corner, along the path. */
#define PATH_HALF_TOP  ((PATH_GLASS_W / 2.0F) - PATH_GLASS_R)  /**< The top middle to a corner. */
#define PATH_SIDE      (PATH_GLASS_H - (2.0F * PATH_GLASS_R))  /**< A side. */
#define PATH_BOTTOM    (PATH_GLASS_W - (2.0F * PATH_GLASS_R))  /**< The bottom. */
#define PATH_EIGHTHS   8.0F
#define PATH_LEGS      4U

/** @brief One leg of the path: a straight stretch and the corner after it. */
typedef struct {
    float x;    /**< Where the stretch begins. */
    float y;
    float dx;   /**< Which way it runs. */
    float dy;
    float len;  /**< How long it is. */
    float cx;   /**< The centre of the corner after it. */
    float cy;
    float a0;   /**< The angle the corner starts at, in radians. */
} path_leg_t;

void kgui_welcome_load(kgui_welcome_t *w, const char *word)
{
    w->pending = (NULL != word) && (strcmp(word, KGUI_WELCOME_PENDING) == 0);
    w->armed   = false;
    w->woke_ms = 0U;
}

bool kgui_welcome_pending(const kgui_welcome_t *w)
{
    return w->pending;
}

void kgui_welcome_drop(kgui_welcome_t *w)
{
    w->pending = false;
}

bool kgui_welcome_due(kgui_welcome_t *w, bool woke, bool bare, uint32_t now_ms)
{
    bool due = false;

    if ( w->pending && woke )
    {
        if ( !w->armed )
        {
            w->armed   = true;
            w->woke_ms = now_ms;
        }
        if ( bare && ((now_ms - w->woke_ms) >= KGUI_WELCOME_GREET_MS) )
        {
            w->pending = false;
            due        = true;
        }
    }
    return due;
}

bool kgui_welcome_word_ok(const char *word)
{
    return (NULL != word)
           && ((strcmp(word, KGUI_WELCOME_PENDING) == 0) || (strcmp(word, KGUI_WELCOME_DONE) == 0));
}

float kgui_welcome_path_len(void)
{
    return (2.0F * PATH_HALF_TOP) + (2.0F * PATH_SIDE) + PATH_BOTTOM + (4.0F * PATH_QUARTER);
}

void kgui_welcome_path_at(float d, float *x, float *y)
{
    static const path_leg_t LEGS[PATH_LEGS] = {
        { PATH_GLASS_W / 2.0F, PATH_Y0, 1.0F, 0.0F, PATH_HALF_TOP, PATH_CX1, PATH_CY0, -PATH_PI / 2.0F },
        { PATH_X1, PATH_CY0, 0.0F, 1.0F, PATH_SIDE, PATH_CX1, PATH_CY1, 0.0F },
        { PATH_CX1, PATH_Y1, -1.0F, 0.0F, PATH_BOTTOM, PATH_CX0, PATH_CY1, PATH_PI / 2.0F },
        { PATH_X0, PATH_CY1, 0.0F, -1.0F, PATH_SIDE, PATH_CX0, PATH_CY0, PATH_PI },
    };
    float left = d;

    if ( 0.0F > left )
    {
        left = 0.0F;
    }
    for ( uint32_t i = 0U; PATH_LEGS > i; i++ )
    {
        path_leg_t const * const l = &LEGS[i];

        if ( left <= l->len )
        {
            *x = l->x + (l->dx * left);
            *y = l->y + (l->dy * left);
            return;
        }
        left -= l->len;
        if ( PATH_QUARTER >= left )
        {
            float const a = l->a0 + (left / PATH_R);

            *x = l->cx + (PATH_R * cosf(a));
            *y = l->cy + (PATH_R * sinf(a));
            return;
        }
        left -= PATH_QUARTER;
    }
    /* The last of the top, back to the middle where the path began. */
    if ( PATH_HALF_TOP < left )
    {
        left = PATH_HALF_TOP;
    }
    *x = PATH_CX0 + left;
    *y = PATH_Y0;
}

float kgui_welcome_fill_len(int32_t pm)
{
    float const part = kgui_welcome_path_len() / PATH_EIGHTHS;

    return (part * (PATH_EIGHTHS - 1.0F)) + ((part * (float)pm) / (float)KGUI_WELCOME_FULL_PM);
}
