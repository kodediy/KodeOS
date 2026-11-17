/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_osrel.h
 * @brief The kodeOS release the update service names: reading its answer,
 *        telling a newer version, what About's row says about it, and the
 *        card that offers it at boot.
 *
 * Pure: no LVGL and no network. AO_Gui asks the service once Wi-Fi has its
 * first address after the boot (again, a few times, when that question never
 * reached the service) and when About opens, and feeds what it hears;
 * everything decided about the answer is decided here.
 */
#ifndef KGUI_OSREL_H
#define KGUI_OSREL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The numbers of an answer ----------------------------------------------- */

#define KGUI_OSREL_VERSION_MAX  16U        /**< A version, terminator included: three groups of up to four digits and two dots. */
#define KGUI_OSREL_GROUPS       3U         /**< Groups of digits in a version. */
#define KGUI_OSREL_GROUP_DIGITS 4U         /**< The most digits one group has. */
#define KGUI_OSREL_URL_MAX      128U       /**< The link of a release's file, terminator included. */
#define KGUI_OSREL_ANSWER_MAX   512U       /**< The most of the service's answer that is read, in bytes. */
#define KGUI_OSREL_TEXT_MAX     48U        /**< The row's words, or a line of the card's, terminator included. */
#define KGUI_OSREL_MIN_PCT      20U        /**< Under this charge, with no cable, an update is not started. */

/** @brief The release the service names. */
typedef struct {
    char version[KGUI_OSREL_VERSION_MAX];  /**< Its version, as the image says it: no `v`. */
    char url[KGUI_OSREL_URL_MAX];          /**< Where its `.kfw` is. */
} kgui_osrel_t;

/** @brief Where the check stands. */
typedef enum {
    KGUI_OSREL_NEVER = 0,   /**< The service has not been asked since the boot: opening About asks. */
    KGUI_OSREL_ASKING,      /**< A question is on its way. */
    KGUI_OSREL_CURRENT,     /**< The service names nothing newer than what runs. */
    KGUI_OSREL_AVAILABLE,   /**< The service names a newer version. */
    KGUI_OSREL_FAILED,      /**< The last question got no usable answer. */
    KGUI_OSREL_UNREACHED    /**< The last question never reached the service: AO_Net refused it (its one worker busy) or did not take it. */
} kgui_osrel_check_t;

/** @brief What About's "Software update" row says. */
typedef enum {
    KGUI_OSREL_ROW_NONE = 0,   /**< No card image and no service in this build. */
    KGUI_OSREL_ROW_CARD,       /**< The card carries kodeos.bin. */
    KGUI_OSREL_ROW_AVAILABLE,  /**< A newer version can be installed now. */
    KGUI_OSREL_ROW_CHARGE,     /**< A newer version waits for more battery or a cable. */
    KGUI_OSREL_ROW_CURRENT,    /**< Nothing newer. */
    KGUI_OSREL_ROW_CHECKING,   /**< The answer is on its way. */
    KGUI_OSREL_ROW_OFFLINE,    /**< No network to ask with. */
    KGUI_OSREL_ROW_FAILED      /**< The service did not answer. */
} kgui_osrel_row_t;

/** @brief What the row is decided from. */
typedef struct {
    bool               service;  /**< This build has a service to ask. */
    bool               card;     /**< The card carries kodeos.bin. */
    bool               linked;   /**< Wi-Fi has an address. */
    bool               power;    /**< kgui_osrel_power_ok() said yes. */
    kgui_osrel_check_t check;    /**< Where the check stands. */
} kgui_osrel_view_t;

/**
 * @brief Read the service's answer.
 *
 * The answer is one JSON object; `version` and `url` are taken from its top
 * level and everything else is skipped. The version must be three groups of
 * digits and the link `https://`, printable, with no quote, backslash or
 * space in it.
 *
 * @param json     The answer; it need not end in a terminator.
 * @param len      Its bytes.
 * @param plain_ok Take an `http://` link too: only a build whose service is
 *                 itself asked over `http://`, on a bench.
 * @param out      Receives the release; left as it was on a refusal.
 * @return true when both fields were there and well formed.
 */
bool kgui_osrel_parse(const char *json, size_t len, bool plain_ok, kgui_osrel_t *out);

/**
 * @brief Whether @p latest is a newer version than @p running.
 *
 * Group by group, as numbers: `0.1.0` is newer than `00.00.24`, and
 * `00.01.00` is `0.1.0`. A version that is not three groups of digits is
 * never newer and nothing is newer than it.
 *
 * @param latest  The version the service names.
 * @param running The version that runs.
 * @return true only when both read and @p latest is the greater.
 */
bool kgui_osrel_newer(const char *latest, const char *running);

/**
 * @brief Whether the board has the power to start an update.
 * @param soc  The charge left, in percent.
 * @param vbus A cable is in.
 * @return true on a cable, or from KGUI_OSREL_MIN_PCT up.
 */
bool kgui_osrel_power_ok(uint8_t soc, bool vbus);

/**
 * @brief What the row says.
 * @param view What is known; NULL answers KGUI_OSREL_ROW_NONE.
 * @return The row's state.
 */
kgui_osrel_row_t kgui_osrel_row(const kgui_osrel_view_t *view);

/**
 * @brief Whether the top button on the row installs something.
 * @param row The row's state.
 * @return true for the card's image and for a version that can be installed now.
 */
bool kgui_osrel_row_installs(kgui_osrel_row_t row);

/**
 * @brief Whether the top button on the row asks the service again.
 * @param row The row's state.
 * @return true when the row shows an answer or a failure, and none is on its way.
 */
bool kgui_osrel_row_asks(kgui_osrel_row_t row);

/**
 * @brief Write the row's words.
 * @param row     The row's state.
 * @param version The newer version, for the two states that name it; NULL reads as "".
 * @param out     Receives the words and a terminator.
 * @param size    Bytes @p out holds; KGUI_OSREL_TEXT_MAX fits every state.
 */
void kgui_osrel_row_text(kgui_osrel_row_t row, const char *version, char *out, size_t size);

/* ---- The card at boot -------------------------------------------------------- */

#define KGUI_OSREL_CARD_WORD    "kodeOS"                 /**< The card's protagonist, as the write that follows wears it. */
#define KGUI_OSREL_CARD_TITLE   "New release available"  /**< The card's first line. */
#define KGUI_OSREL_CARD_BUTTON  "Update"                 /**< Its one button. */

#define KGUI_OSREL_BOOT_ASKS     4U     /**< The most times the boot's question goes out: the first, and again while it never reaches the service. */
#define KGUI_OSREL_BOOT_AGAIN_MS 5000U  /**< The wait before the boot's question goes out again, in milliseconds. */

/** @brief Where the boot's offer stands: one per boot. */
typedef enum {
    KGUI_OSREL_OFFER_UNASKED = 0,  /**< Wi-Fi has had no address since the boot: nothing asked yet. */
    KGUI_OSREL_OFFER_ASKED,        /**< The boot's question is out: its answer decides. */
    KGUI_OSREL_OFFER_DUE,          /**< A newer version: the card waits for the glass. */
    KGUI_OSREL_OFFER_SHOWN,        /**< The card rose: not again until the next boot. */
    KGUI_OSREL_OFFER_NONE,         /**< Nothing to offer this boot. */
    KGUI_OSREL_OFFER_AGAIN         /**< The boot's question never reached the service: it goes out again after KGUI_OSREL_BOOT_AGAIN_MS. */
} kgui_osrel_offer_t;

/** @brief The screen state AO_Gui is in, as the card's rule reads it. */
typedef enum {
    KGUI_OSREL_SCREEN_OTHER = 0,  /**< None of the three: the boot's wait, the way back from an app, a test, asleep. */
    KGUI_OSREL_SCREEN_FACE,       /**< The face, awake or at Rest. */
    KGUI_OSREL_SCREEN_APPS,       /**< Apps. */
    KGUI_OSREL_SCREEN_SETTINGS    /**< The settings screen. */
} kgui_osrel_screen_t;

/** @brief What holds the glass, for the card's rule. */
typedef struct {
    kgui_osrel_screen_t screen;     /**< The screen state AO_Gui is in. */
    bool                rest;       /**< The face is at Rest. */
    bool                card;       /**< A card is up, or one is still leaving the glass. */
    bool                prompt;     /**< The face's yellow pill asked for its page, which has not risen yet. */
    bool                locked;     /**< A locked card is up. */
    bool                writing;    /**< A flash write runs. */
    bool                deciding;   /**< An install from the card waits for AO_AppMgr's word on what it would evict. */
    bool                resetting;  /**< The factory reset was asked and has not ended. */
    bool                parked;     /**< The glass is handed over: the board rests. */
    bool                moving;     /**< A curtain, a door or the list moves. */
    bool                hand;       /**< A finger is on the glass or a button is down. */
    bool                greeting;   /**< The welcome's greeting waits for the face, or its line is not through. */
} kgui_osrel_glass_t;

/** @brief What the card's button can do now. */
typedef enum {
    KGUI_OSREL_CARD_READY = 0,  /**< It installs. */
    KGUI_OSREL_CARD_OFFLINE,    /**< Grey: no network to fetch the release with. */
    KGUI_OSREL_CARD_CHARGE      /**< Grey: under KGUI_OSREL_MIN_PCT with no cable. */
} kgui_osrel_card_t;

/**
 * @brief Where the offer stands once the check has moved.
 *
 * Called after the boot's question goes out and on every answer. The first
 * answer that reached the service decides: a newer version is due, anything
 * else is no offer. A question that never reached it is asked again while
 * fewer than KGUI_OSREL_BOOT_ASKS have gone out. A due offer goes only when
 * a later answer says nothing is newer.
 *
 * @param offer Where it stood.
 * @param check Where the check stands now.
 * @param asks  How many times the boot's question has gone out.
 * @return Where it stands.
 */
kgui_osrel_offer_t kgui_osrel_offer_follow(kgui_osrel_offer_t offer, kgui_osrel_check_t check,
                                           uint8_t asks);

/**
 * @brief The card is on the glass: a due offer is spent for this boot.
 * @param offer Where it stood.
 * @return KGUI_OSREL_OFFER_SHOWN from DUE or SHOWN; @p offer otherwise.
 */
kgui_osrel_offer_t kgui_osrel_offer_shown(kgui_osrel_offer_t offer);

/**
 * @brief The offer in one word, for the log.
 * @param offer Where it stands.
 * @return "unasked", "asked", "due", "shown", "none" or "again"; "?" outside the enum.
 */
const char *kgui_osrel_offer_word(kgui_osrel_offer_t offer);

/**
 * @brief Whether the card may rise now.
 *
 * Only for a due offer, and only on the face awake, Apps or the settings
 * screen with nothing else on the glass: no card and none asked for by the
 * face's pill, no locked card, no write and no install being decided, no
 * factory reset, no parked glass, nothing moving, no hand on the board and
 * no greeting waiting.
 *
 * @param offer Where the offer stands.
 * @param glass What holds the glass; NULL never lets it rise.
 * @return true when the card may rise.
 */
bool kgui_osrel_offer_may_rise(kgui_osrel_offer_t offer, const kgui_osrel_glass_t *glass);

/**
 * @brief What the card's button can do.
 * @param linked Wi-Fi has an address.
 * @param power  kgui_osrel_power_ok() said yes.
 * @return READY, or why it is grey: the network first, as About's row decides.
 */
kgui_osrel_card_t kgui_osrel_card(bool linked, bool power);

/**
 * @brief Write the card's second line: the version it offers.
 * @param version The newer version; NULL reads as "".
 * @param out     Receives the words and a terminator.
 * @param size    Bytes @p out holds; KGUI_OSREL_TEXT_MAX fits the longest version.
 */
void kgui_osrel_card_version(const char *version, char *out, size_t size);

/**
 * @brief Write why the card's button is grey.
 * @param card What the button can do.
 * @param out  Receives the words and a terminator; "" when it installs.
 * @param size Bytes @p out holds; KGUI_OSREL_TEXT_MAX fits every state.
 */
void kgui_osrel_card_why(kgui_osrel_card_t card, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_OSREL_H */
