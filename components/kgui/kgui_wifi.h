/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_wifi.h
 * @brief The words of the radios' rows: what Wi-Fi and Bluetooth are doing, and why a join failed.
 */
#ifndef KGUI_WIFI_H
#define KGUI_WIFI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What the radios' rows know, from AO_Radio's two state events. */
typedef struct {
    bool on;            /**< The person's switch.                            */
    bool linked;        /**< On a network, with an address (Wi-Fi only).     */
    bool reconnecting;  /**< Off the network and getting back on one.        */
    bool relinking;     /**< The link to the radio chip is being built again. */
    bool gave_up;       /**< The radio chip stopped and nothing more is tried. */
} kgui_radio_view_t;

/**
 * @brief The words a radio's row says under its name.
 *
 * @param v The view.
 * @return "Off", "Not responding", "Reconnecting...", "On", or NULL when the
 *         row says the network it is on.
 */
const char *kgui_radio_words(kgui_radio_view_t const *v);

/**
 * @brief Why a join failed, in the hint's words.
 * @param fail kdrv_wifi_fail_t, as WifiStateEvt carries it.
 * @return "Wrong password", "Network not found", or "Could not join".
 */
const char *kgui_wifi_fail_words(uint8_t fail);

/**
 * @brief Whether the Kode channel's wait means the network reaches nothing.
 *
 * The channel's join is a request to Kode over HTTPS. When it ends with no
 * answer at all - nothing came back, or the broker cannot be reached - while
 * the network gave an address, the network goes nowhere: a sign-in page of a
 * hotel, a router with no line. A refusal, a busy radio or a full memory is
 * not that. It is one probe of one place, so an outage of Kode's own reads the
 * same; there is no other probe to tell the two apart.
 *
 * @param why The channel's word for its wait, as ChannelStateEvt carries it.
 * @return true for a wait that says the network goes nowhere.
 */
bool kgui_wifi_goes_nowhere(const char *why);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_WIFI_H */
