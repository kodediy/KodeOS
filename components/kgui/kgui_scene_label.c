/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_scene_label.c
 * @brief The interface's words for Kody's scenes (kgui_scene_label.h).
 */
#include "kgui_scene_label.h"

#include <stddef.h>
#include <string.h>

/** @brief One scene: the name the pack gives and the words shown. */
typedef struct {
    const char *name;   /**< The pack's name for the scene. */
    const char *label;  /**< The interface's English words for it. */
} scene_label_t;

const char *kgui_scene_label(const char *name)
{
    /* The official pack's scenes, in the pack's order, and the hour's own
     * choice, which reads like the other rows' values. */
    static const scene_label_t SCENES[] = {
        { "auto",      "Auto"     },
        { "tren",      "Train"    },
        { "reloj",     "Clock"    },
        { "azotea",    "Rooftop"  },
        { "camping",   "Camping"  },
        { "madrugada", "Late night" },
        { "parque",    "Park"     },
        { "desayuno",  "Breakfast" },
        { "taller",    "Workshop" },
        { "lluvia",    "Rain"     },
        { "playa",     "Beach"    },
    };
    const char *label = "";

    if ( NULL != name )
    {
        label = name;
        for ( size_t i = 0U; (sizeof(SCENES) / sizeof(SCENES[0])) > i; ++i )
        {
            if ( strcmp(name, SCENES[i].name) == 0 )
            {
                label = SCENES[i].label;
                break;
            }
        }
    }
    return label;
}
