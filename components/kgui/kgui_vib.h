/**
 * @file kgui_vib.h
 * @brief What the motor does for where the volume stands and what the
 *        Vibration row chose, and what that makes of the buzzes of apps.
 *
 * Pure: no LVGL, no board. The interface plays its own buzzes at the level
 * kgui_vib_level() gives, and AO_Haptic keeps kgui_vib_scale() of it as
 * `haptic`, the scale it puts on every buzz an app asks for.
 */
#ifndef KGUI_VIB_H
#define KGUI_VIB_H

#include <stdint.h>

#include "kgui_haptic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KGUI_VIB_SCALE_NONE 0U   /**< `haptic` with the motor off: a buzz an app asks for plays nothing. */
#define KGUI_VIB_SCALE_FULL 100U /**< `haptic` with the motor on: a buzz an app asks for plays at its own strength. */

/** @brief Where the volume stands, for the motor. */
typedef enum {
    KGUI_VIB_VOL_SILENT = 0, /**< At silence: no sound and no buzz. */
    KGUI_VIB_VOL_VIBRATE,    /**< On the vibrate stop: no sound, and the motor on. */
    KGUI_VIB_VOL_SOUND       /**< Above them: the Vibration row decides. */
} kgui_vib_vol_t;

/**
 * @brief How much of the interface buzzes, for the volume's stop and the row.
 *
 * @param vol Where the volume stands.
 * @param row What the Vibration row chose.
 * @return KGUI_HAPTIC_OFF at silence; at the vibrate stop @p row, and
 *         KGUI_HAPTIC_LIGHT when @p row is KGUI_HAPTIC_OFF; @p row above them.
 */
kgui_haptic_level_t kgui_vib_level(kgui_vib_vol_t vol, kgui_haptic_level_t row);

/**
 * @brief The scale AO_Haptic keeps for the level in force.
 *
 * Light and Normal differ in which of the interface's effects play, not in
 * strength: an app's buzz is whole at both.
 *
 * @param level The level in force (kgui_vib_level()).
 * @return KGUI_VIB_SCALE_NONE for KGUI_HAPTIC_OFF, KGUI_VIB_SCALE_FULL for
 *         any other level.
 */
uint8_t kgui_vib_scale(kgui_haptic_level_t level);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_VIB_H */
