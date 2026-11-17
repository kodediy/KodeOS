/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_welcome.c
 * @brief The landing after Welcome (kgui_welcome.h): the pure part.
 */
#include "kgui_welcome.h"

#include <stddef.h>
#include <string.h>

void kgui_welcome_load(kgui_welcome_t *w, const char *word)
{
    w->pending = (NULL != word) && (strcmp(word, KGUI_WELCOME_PENDING) == 0);
    w->armed   = false;
    w->woke_ms = 0U;
    w->said    = false;
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
            w->said    = true;
            due        = true;
        }
    }
    return due;
}

void kgui_welcome_notice(kgui_welcome_t *w, bool notice)
{
    if ( !notice )
    {
        w->said = false;
    }
}

bool kgui_welcome_greeting(const kgui_welcome_t *w, bool notice)
{
    return w->pending || (w->said && notice);
}

bool kgui_welcome_word_ok(const char *word)
{
    return (NULL != word)
           && ((strcmp(word, KGUI_WELCOME_PENDING) == 0) || (strcmp(word, KGUI_WELCOME_DONE) == 0));
}
