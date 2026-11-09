/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_kody_rest.h
 * @brief Kody at Rest: the scene's falling asleep, its outline and its first press.
 */
#ifndef KGUI_KODY_REST_H
#define KGUI_KODY_REST_H

#include "kui_kody.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One scene's animations for Rest. */
typedef struct {
    const kui_kody_anim_t *sleep;  /**< <scene>_duerme: he falls asleep, in colour. */
    const kui_kody_anim_t *rest;   /**< <scene>_bn: asleep, white outline on black; NULL for the clock's. */
    const kui_kody_anim_t *peek;   /**< <scene>_pk: the same with his blue and face in colour; NULL likewise. */
} kgui_kody_rest_t;

/**
 * @brief A scene's animations for Rest.
 * @param scene The scene's name in the pack.
 * @return Its animations, or NULL for a scene the apocrifa does not have.
 */
const kgui_kody_rest_t *kgui_kody_rest_of(const char *scene);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_KODY_REST_H */
