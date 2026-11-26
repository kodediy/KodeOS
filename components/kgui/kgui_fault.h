/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_fault.h
 * @brief The fault notice: the record kept until a person answers, when its
 *        card rises, and whether it offers to send the report.
 *
 * Pure: no LVGL, no SDK type and no I/O. AO_Gui reads and writes the record's
 * byte in NVS, maps the boot's end and the network's state into these types,
 * and does what the answers here say.
 */
#ifndef KGUI_FAULT_H
#define KGUI_FAULT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The numbers of the notice ----------------------------------------------- */

#define KGUI_FAULT_NET_WAIT_MS   30000U  /**< The most the card waits for the channel since it became due, in milliseconds. */
#define KGUI_FAULT_HOLD_MS        1200U  /**< After the play, when Kody is paused: frame 36 of 72, up with the X, in milliseconds. */
#define KGUI_FAULT_KODY_WAIT_MS   5000U  /**< The most the card waits for the pack's fault animation to be read since it became due, in milliseconds. */

#define KGUI_FAULT_KEY           "fault" /**< The record's byte: a u8 in the kgui NVS namespace, kgui_fault_pack()'s. */

#define KGUI_FAULT_CLASS_MASK    0x03U   /**< The record's byte: bits 0 and 1 hold the class. */
#define KGUI_FAULT_DUMP_BIT      0x80U   /**< The record's byte: bit 7 says the fault left a dump. */

/* ---- The record ------------------------------------------------------------------ */

/** @brief What kind of fault the boot came back from. */
typedef enum {
    KGUI_FAULT_NONE = 0,  /**< No fault: a clean boot. */
    KGUI_FAULT_CRASHED,   /**< A panic. */
    KGUI_FAULT_HUNG       /**< A watchdog. */
} kgui_fault_class_t;

/** @brief The record kept until a person answers. */
typedef struct {
    uint8_t cls;   /**< A kgui_fault_class_t. */
    bool    dump;  /**< The fault left a dump. */
} kgui_fault_rec_t;

/**
 * @brief The record as the byte kept in NVS.
 * @param r The record.
 * @return The class in bits 0 and 1 and the dump in bit 7; 0, which is no
 *         record, for a class that is neither CRASHED nor HUNG.
 */
uint8_t kgui_fault_pack(kgui_fault_rec_t r);

/**
 * @brief Read the byte kept in NVS.
 * @param byte The byte.
 * @param out  Receives the record; left as it was on a refusal.
 * @return true when the class is CRASHED or HUNG and no bit is set beyond the
 *         class and the dump; false for any other byte or a NULL @p out.
 */
bool kgui_fault_unpack(uint8_t byte, kgui_fault_rec_t *out);

/**
 * @brief The card's one line of why: the Stopped card's own words.
 * @param cls A kgui_fault_class_t.
 * @return "It crashed", "It stopped responding", or "" for any other class.
 *         Never NULL.
 */
const char *kgui_fault_words(uint8_t cls);

/* ---- The network's verdict --------------------------------------------------------- */

/** @brief The Kode channel, as the card's wait reads it. */
typedef enum {
    KGUI_FAULT_CHAN_COMING = 0,  /**< Not up yet, and on its way. */
    KGUI_FAULT_CHAN_UP,          /**< Up. */
    KGUI_FAULT_CHAN_NEVER        /**< It will not come: off, refused, without identity, or not in the build. */
} kgui_fault_chan_t;

/** @brief What the card's verdict is decided from. */
typedef struct {
    bool     dump;       /**< The record says a dump, and one of a kodeOS image is in flash. */
    bool     built;      /**< The build can send: the channel and the upload are in it. */
    bool     wifi_on;    /**< Wi-Fi is switched on. */
    uint8_t  chan;       /**< A kgui_fault_chan_t. */
    bool     link;       /**< The session names a `dumps` link. */
    uint32_t waited_ms;  /**< Since the card became due, in milliseconds. */
} kgui_fault_net_t;

/** @brief Whether the card rises, and with what. */
typedef enum {
    KGUI_FAULT_WAIT = 0,   /**< Not yet: the channel is on its way. */
    KGUI_FAULT_RISE_OK,    /**< It rises with OK alone. */
    KGUI_FAULT_RISE_SEND   /**< It rises with OK and Send report. */
} kgui_fault_verdict_t;

/**
 * @brief Whether the card rises, and with what.
 *
 * No dump, a build that cannot send, Wi-Fi off or a channel that will not
 * come: OK alone at once. The channel up: Send report as well when the
 * session names a `dumps` link. The channel on its way: wait, up to
 * KGUI_FAULT_NET_WAIT_MS, then OK alone.
 *
 * @param n What is known; NULL answers KGUI_FAULT_RISE_OK.
 * @return The verdict.
 */
kgui_fault_verdict_t kgui_fault_verdict(const kgui_fault_net_t *n);

/* ---- The notice -------------------------------------------------------------------- */

/** @brief What the card shows. */
typedef enum {
    KGUI_FAULT_FACE_NOTICE = 0,  /**< The notice: OK, and Send report when offered. */
    KGUI_FAULT_FACE_SENDING,     /**< "Sending report...", the edge spinning. */
    KGUI_FAULT_FACE_SENT,        /**< "Report sent", the edge closed green. */
    KGUI_FAULT_FACE_FAILED       /**< "Couldn't send the report", Close and Retry. */
} kgui_fault_face_t;

/** @brief Where the notice stands. */
typedef struct {
    kgui_fault_rec_t rec;     /**< What is owed. */
    bool             owed;    /**< A record is kept. */
    bool             due;     /**< The card may rise once the rest allows. */
    uint32_t         due_ms;  /**< When it became due, in milliseconds. */
    bool             store;   /**< The record must be written to NVS. */
} kgui_fault_t;

/**
 * @brief Begin the notice at AO_Gui's start.
 *
 * A fault boot owes its own record, to be stored, whatever was kept. Otherwise
 * a kept byte that unpacks is owed. Otherwise nothing is. A record owed is due
 * at @p now_ms.
 *
 * @param f         The notice; NULL does nothing.
 * @param kept      A byte is kept in NVS.
 * @param kept_byte That byte.
 * @param boot      This boot's record; a class that is neither CRASHED nor
 *                  HUNG (KGUI_FAULT_NONE for no fault) owes nothing of its own.
 * @param now_ms    The time now, in milliseconds.
 */
void kgui_fault_begin(kgui_fault_t *f, bool kept, uint8_t kept_byte,
                      kgui_fault_rec_t boot, uint32_t now_ms);

/**
 * @brief Rest or the auto-lock took the card down: it is no longer due.
 * @param f The notice; NULL does nothing.
 */
void kgui_fault_taken_down(kgui_fault_t *f);

/**
 * @brief A button or a touch was taken: an owed notice that is not due
 *        becomes due at @p now_ms.
 * @param f      The notice; NULL does nothing.
 * @param now_ms The time now, in milliseconds.
 */
void kgui_fault_input(kgui_fault_t *f, uint32_t now_ms);

/**
 * @brief OK, Send report, BOT or HOME answered the card: nothing is owed and
 *        nothing is to be stored.
 * @param f The notice; NULL does nothing.
 */
void kgui_fault_answered(kgui_fault_t *f);

/**
 * @brief Whether Rest and the auto-lock may take the card down.
 * @param face A kgui_fault_face_t.
 * @return true for the notice, the sent card and the failure card; false
 *         while a send runs and for any other value.
 */
bool kgui_fault_yields(uint8_t face);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_FAULT_H */
