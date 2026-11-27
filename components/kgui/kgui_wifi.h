/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_wifi.h
 * @brief The words of the radios' rows: what Wi-Fi and Bluetooth are doing, why a join
 *        failed, and what a press on the Kode ecosystem row asks for.
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
 * @brief The line under Pair new device on the Bluetooth page.
 *
 * The board has one link: with a device on it nothing is on the air, so the
 * row never says "visible" then, whatever was asked.
 *
 * @param open   The time to pair is running.
 * @param wait   It was asked with a device connected and starts when that
 *               one leaves.
 * @param linked A device is connected now.
 * @return "Visible to new devices", "When the other leaves", or NULL
 *         for no line.
 */
const char *kgui_ble_pair_words(bool open, bool wait, bool linked);

/** @brief Under this many dBm a network is heard too weakly to say a handshake
 *         that timed out was the password's doing. */
#define KGUI_WIFI_WEAK_DBM   (-75)

/** @brief A network nobody heard before its join: a hidden one, whose name is typed. */
#define KGUI_WIFI_RSSI_NONE  0

/**
 * @brief Why a join failed, in the hint's words.
 *
 * A handshake that timed out is a wrong password or a signal that went during
 * it, and the stack gives one reason for both. How the network was heard when
 * it was chosen decides: weaker than KGUI_WIFI_WEAK_DBM it is the signal,
 * otherwise the password. Each answer fits one line of the hint.
 *
 * @param fail kdrv_wifi_fail_t, as WifiStateEvt carries it.
 * @param rssi How the network was heard when it was chosen, dBm, or
 *             KGUI_WIFI_RSSI_NONE.
 * @return "Wrong password", "Signal lost", "Network not found", or "Could not join".
 */
const char *kgui_wifi_fail_words(uint8_t fail, int8_t rssi);

/**
 * @brief Whether a failed join is the password's doing.
 *
 * True for the failures kgui_wifi_fail_words() words as "Wrong password": the
 * ones a new password can mend.
 *
 * @param fail kdrv_wifi_fail_t, as WifiStateEvt carries it.
 * @param rssi How the network was heard when it was chosen, dBm, or
 *             KGUI_WIFI_RSSI_NONE.
 * @return true when the password is what failed.
 */
bool kgui_wifi_fail_is_password(uint8_t fail, int8_t rssi);

/** @brief What a state of the radio says about a join that was asked for
 *         (kgui_wifi_join_read()). */
typedef enum {
    KGUI_JOIN_OUT = 0,   /**< Nothing yet: the join is still out, or the state is another network's. */
    KGUI_JOIN_MADE,      /**< The network gave an address.                                          */
    KGUI_JOIN_TURNED,    /**< The network turned the board away; the state's `fail` says why.       */
    KGUI_JOIN_NOTHING    /**< It ended on no network: a timeout, or a request the radio did not take. */
} kgui_join_t;

/**
 * @brief What a state of the radio says about a join that was asked for.
 *
 * The answer to a join is the first state AO_Radio publishes once the join is
 * no longer out. An address on the network asked for is the join, and an
 * address on any other says nothing about it: the board was on that one
 * before. A state published while the join is out - a refresh of the
 * strength, a round's end - is not its answer. Off a network the radio names
 * none, so a network that turned the board away is known by the state alone.
 *
 * @param asked   The network the join was asked for.
 * @param state   kdrv_wifi_state_t, as WifiStateEvt carries it.
 * @param ssid    The network the state is about, as WifiStateEvt carries it.
 * @param joining The join has yet to end (WifiStateEvt.joining).
 * @return What the state says about the join.
 */
kgui_join_t kgui_wifi_join_read(const char *asked, uint8_t state, const char *ssid,
                                bool joining);

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

/** @brief When the Wi-Fi page asks for its next sweep (kgui_wifi_scan_next()). */
typedef enum {
    KGUI_SCAN_NEXT_CYCLE = 0,  /**< At the page's own period.                */
    KGUI_SCAN_NEXT_SOON        /**< Soon: the last question taught nothing.  */
} kgui_scan_next_t;

/**
 * @brief When the Wi-Fi page asks for its next sweep, from how the last one ended.
 *
 * A sweep that reached its end is followed at the page's period. So is one
 * somebody else's newer Wi-Fi request ended early - a scan, a join, Wi-Fi
 * switched: whoever ended it has the radio, and a sweep asked for at once
 * would end theirs in turn. A refusal or a failure taught nothing and took
 * nobody's place, so the page asks again soon. So it does after a sweep the
 * page's own join ended: the list is wanted back once the join is made, and
 * a sweep waits for a join instead of ending it.
 *
 * @param ok       The sweep reached its end (WifiScanEndEvt.ok).
 * @param cut      It was ended early for a newer request (WifiScanEndEvt.cut).
 * @param own_join The page asked for a join while the sweep was out.
 * @return KGUI_SCAN_NEXT_CYCLE or KGUI_SCAN_NEXT_SOON.
 */
kgui_scan_next_t kgui_wifi_scan_next(bool ok, bool cut, bool own_join);

/** @brief What a press on the Kode ecosystem row asks for (kgui_wifi_chan_press()). */
typedef enum {
    KGUI_CHAN_PRESS_ON = 0,  /**< `chan=on`: join, or try again now.           */
    KGUI_CHAN_PRESS_OFF,     /**< `chan=off`: leave.                           */
    KGUI_CHAN_PRESS_NO_NET   /**< Nothing: the channel has no network to join. */
} kgui_chan_press_t;

/**
 * @brief What a press on the Kode ecosystem row asks for, from the channel's state.
 *
 * A channel with nothing under way - off, waiting out a failed try, refused,
 * with no identity - is asked `chan=on`: AO_Net joins, or tries again now. A
 * channel joining or up is asked to leave. With no network there is nothing a
 * press can start.
 *
 * @param state chan_state_t, as ChannelStateEvt carries it.
 * @return What the press asks for; a state this build does not know leaves.
 */
kgui_chan_press_t kgui_wifi_chan_press(uint8_t state);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_WIFI_H */
