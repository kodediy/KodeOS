/**
 * @file kgui_vib.c
 * @brief What the motor does for the volume's stop and the Vibration row.
 */
#include "kgui_vib.h"

kgui_haptic_level_t kgui_vib_level(kgui_vib_vol_t vol, kgui_haptic_level_t row)
{
    kgui_haptic_level_t level = row;

    if ( KGUI_VIB_VOL_SILENT == vol )
    {
        level = KGUI_HAPTIC_OFF;
    }
    else if ( (KGUI_VIB_VOL_VIBRATE == vol) && (KGUI_HAPTIC_OFF == row) )
    {
        level = KGUI_HAPTIC_LIGHT;
    }
    else
    {
        /* The row's. */
    }
    return level;
}

uint8_t kgui_vib_scale(kgui_haptic_level_t level)
{
    uint8_t scale = (uint8_t)KGUI_VIB_SCALE_FULL;

    if ( KGUI_HAPTIC_OFF == level )
    {
        scale = (uint8_t)KGUI_VIB_SCALE_NONE;
    }
    return scale;
}
