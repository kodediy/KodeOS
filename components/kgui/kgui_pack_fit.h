/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_pack_fit.h
 * @brief Whether a pack with no word on its canvas was drawn for this face at all.
 */
#ifndef KGUI_PACK_FIT_H
#define KGUI_PACK_FIT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* More than one: a good pack can carry one stale scene. */
/** @brief Wake-ups of different scenes refused for their canvas that give a pack up. */
#define KGUI_PACK_FIT_WAKES  2U

/** @brief What the face's files have said about the active pack. */
typedef struct {
    uint8_t wakes_refused;  /**< Wake-ups refused for their canvas. */
    bool    passed;         /**< A file on the face's canvas passed. */
} kgui_pack_fit_t;

/**
 * @brief A new pack: nothing is known of it.
 * @param fit The record.
 */
void kgui_pack_fit_reset(kgui_pack_fit_t *fit);

/**
 * @brief A file on the face's canvas passed: the pack is for this face.
 * @param fit The record.
 */
void kgui_pack_fit_passed(kgui_pack_fit_t *fit);

/**
 * @brief A scene's wake-up was refused for its canvas.
 * @param fit The record.
 * @return true once, at the refusal that gives the pack up.
 */
bool kgui_pack_fit_wake_refused(kgui_pack_fit_t *fit);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_PACK_FIT_H */
