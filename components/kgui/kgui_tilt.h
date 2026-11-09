/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_tilt.h
 * @brief How the Dot leans, so the battery liquid's surface stays level with the world.
 */
#ifndef KGUI_TILT_H
#define KGUI_TILT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KGUI_TILT_PERIOD_MS  33U   /**< How often the IMU is read while the tilt runs. */
#define KGUI_TILT_FLAT_G     0.35F /**< Under this much gravity in the glass's plane the Dot is lying flat: "up" stays where it was. */

/**
 * @brief Start or stop reading the IMU. The task is made the first time.
 * @param on true while the battery's circle is on the glass.
 */
void kgui_tilt_run(bool on);

/**
 * @brief Where "up" is on the glass, smoothed, and whether the Dot was knocked.
 *
 * @param[out] angle Radians from the glass's up, clockwise.
 * @param[out] jolt  g over the Dot's own weight since the last call, 0 for none.
 * @return false until the IMU has been read once.
 */
bool kgui_tilt_read(float *angle, float *jolt);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_TILT_H */
