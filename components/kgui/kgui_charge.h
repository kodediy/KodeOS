/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_charge.h
 * @brief What a board on a cable is doing with it, in words a person reads.
 */
#ifndef KGUI_CHARGE_H
#define KGUI_CHARGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What the cable is doing. */
typedef enum {
    KGUI_CHARGE_NO_CABLE = 0,  /**< No cable. */
    KGUI_CHARGE_ON,            /**< The cell is filling. */
    KGUI_CHARGE_FULL,          /**< The cell is full. */
    KGUI_CHARGE_WARM,          /**< Held: the pack is too warm. */
    KGUI_CHARGE_COLD,          /**< Held: the pack is too cold. */
    KGUI_CHARGE_IDLE           /**< A cable, no charge, and no reason known. */
} kgui_charge_t;

/**
 * @brief Read a battery reading.
 * @param vbus A good input is present.
 * @param chrg kdrv_bq25896_chrg_stat_t.
 * @param ntc  kdrv_bq25896's NTC_FAULT: 0 normal, 2 warm, 3 cool, 5 cold, 6 hot.
 * @param soc  The charge, percent.
 * @return What the cable is doing.
 */
kgui_charge_t kgui_charge_state(bool vbus, uint8_t chrg, uint8_t ntc, uint8_t soc);

/**
 * @brief Whether a state is a charge held for the pack's temperature.
 * @param state The state.
 * @return true for KGUI_CHARGE_WARM and KGUI_CHARGE_COLD.
 */
bool kgui_charge_held(kgui_charge_t state);

/**
 * @brief The few words a state is said in.
 * @param state The state.
 * @param soc   The charge, percent.
 * @param out   Receives them; empty for KGUI_CHARGE_NO_CABLE.
 * @param cap   Bytes in @p out.
 */
void kgui_charge_words(kgui_charge_t state, uint8_t soc, char *out, size_t cap);

/**
 * @brief Whether this reading has something to say, and what.
 *
 * A cable coming in owes a notice, which is said with the first reading that
 * knows what the cable is doing: the chip takes a moment to start the charge, so
 * a first look that sees nothing happening waits for one more. A charge that
 * comes to be held later is said when it does.
 *
 * @param was     The state of the reading before.
 * @param now     This reading's state.
 * @param plugged The cable has just come in.
 * @param owed    Whether a cable's notice is still owed, kept by the caller.
 * @param soc     The charge, percent.
 * @param out     Receives the words when true is returned.
 * @param cap     Bytes in @p out.
 * @return true when there is something to say.
 */
bool kgui_charge_notice(kgui_charge_t was, kgui_charge_t now, bool plugged, bool *owed,
                        uint8_t soc, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_CHARGE_H */
