/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_scene_label.h
 * @brief The words the interface uses for one of Kody's scenes.
 */
#ifndef KGUI_SCENE_LABEL_H
#define KGUI_SCENE_LABEL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A scene's name as the interface shows it.
 *
 * The table covers the official pack's six scenes: `tren` is "Train",
 * `reloj` "Clock", `azotea` "Rooftop", `camping` "Camping", `madrugada`
 * "Late night", and the hour's own choice `auto` is "Auto".
 * A name the table does not have, a pack's own scene, comes back as it was
 * given.
 *
 * @param name The scene's name as the pack (or the store) gives it.
 * @return The English words; @p name itself when it has none; "" for NULL.
 */
const char *kgui_scene_label(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_SCENE_LABEL_H */
