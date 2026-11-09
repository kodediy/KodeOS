/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_fluid.h
 * @brief The battery's liquid: a small simulation of a fluid in a round glass.
 */
#ifndef KGUI_FLUID_H
#define KGUI_FLUID_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KGUI_FLUID_COLS 33U   /**< Points along the surface. */

/** @brief One glass of liquid. */
typedef struct {
    float r;                       /**< The glass's radius, px. */
    float level;                   /**< The surface over the centre, along "up", px. */
    float level_to;                /**< Where the volume puts it. */
    float angle;                   /**< Where "up" leans, radians from the glass's up, clockwise. */
    float spin;                    /**< How fast, radians a second. */
    float h[KGUI_FLUID_COLS];      /**< The ripples, px over the surface. */
    float v[KGUI_FLUID_COLS];      /**< Their speed, px a step. */
} kgui_fluid_t;

/**
 * @brief An empty glass, level.
 * @param f    The liquid.
 * @param side The glass's side, px.
 */
void kgui_fluid_init(kgui_fluid_t *f, int32_t side);

/**
 * @brief Say how full it is.
 * @param f    The liquid.
 * @param fill 0 to 1, of the disc's area.
 * @param now  true to be there at once, false to rise to it.
 */
void kgui_fluid_fill(kgui_fluid_t *f, float fill, bool now);

/**
 * @brief Stand it at an angle, at rest and with no ripples.
 * @param f     The liquid.
 * @param angle Where "up" leans, radians.
 */
void kgui_fluid_rest(kgui_fluid_t *f, float angle);

/**
 * @brief One step of KGUI_FLUID_STEP_MS.
 * @param f     The liquid.
 * @param angle Where gravity says "up" is, radians.
 * @param jolt  How hard the Dot was knocked, in g over its own weight; 0 for none.
 * @return true when anything moved: the picture is another one.
 */
bool kgui_fluid_step(kgui_fluid_t *f, float angle, float jolt);

/**
 * @brief Draw it: 255 under the surface, 0 over it, soft on the line.
 *
 * The disc's own round is its parent's to clip.
 *
 * @param f      The liquid.
 * @param a8     The picture.
 * @param stride Bytes a row.
 * @param side   Its side, px.
 */
void kgui_fluid_draw(kgui_fluid_t const *f, uint8_t *a8, uint32_t stride, int32_t side);

#define KGUI_FLUID_STEP_MS 33U   /**< One step: a frame at 30 fps. */

#ifdef __cplusplus
}
#endif

#endif /* KGUI_FLUID_H */
