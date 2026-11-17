/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_span_label.c
 * @brief The interface's words for a length of time, and the step round a
 *        setting's words (kgui_span_label.h).
 */
#include "kgui_span_label.h"

#include <stddef.h>
#include <string.h>

/** @brief One length of time: the word its owner says and the words shown. */
typedef struct {
    const char *word;   /**< The value as the setting's owner says it. */
    const char *label;  /**< The interface's English words for it. */
} span_label_t;

const char *kgui_span_label(const char *word)
{
    /* The words `off_after` offers, shortest first. */
    static const span_label_t SPANS[] = {
        { "20m",   "20 minutes" },
        { "1h",    "1 hour"     },
        { "6h",    "6 hours"    },
        { "24h",   "1 day"      },
        { "7d",    "7 days"     },
        { "never", "Never"      },
    };
    const char *label = "";

    if ( NULL != word )
    {
        label = word;
        for ( size_t i = 0U; (sizeof(SPANS) / sizeof(SPANS[0])) > i; ++i )
        {
            if ( strcmp(word, SPANS[i].word) == 0 )
            {
                label = SPANS[i].label;
                break;
            }
        }
    }
    return label;
}

uint8_t kgui_span_next(const char *const *words, uint8_t n, const char *now, int8_t dir)
{
    uint8_t next = n;

    if ( (NULL != words) && (0U < n) )
    {
        uint8_t at = n;

        for ( uint8_t i = 0U; (i < n) && (NULL != now); ++i )
        {
            if ( (NULL != words[i]) && (strcmp(now, words[i]) == 0) )
            {
                at = i;
            }
        }
        if ( 0 > dir )
        {
            /* Back from a word the list lacks is the last one. */
            next = (uint8_t)((at + (n - 1U)) % n);
        }
        else if ( at < n )
        {
            next = (uint8_t)((at + 1U) % n);
        }
        else
        {
            next = 0U;
        }
    }
    return next;
}
