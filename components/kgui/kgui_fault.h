/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_fault.h
 * @brief The fault notice: the record kept until a person answers, when its
 *        card rises, and whether it offers to send the report.
 *
 * Pure: no LVGL, no SDK type and no I/O. AO_Gui reads and writes the record's
 * byte in NVS, maps the boot's end and the network's state into these types,
 * and does what the answers here say. The send's part - its faces' clock, its
 * words and what a press does on the pair - is built only with
 * CONFIG_KODEOS_DUMP_HTTP.
 */
#ifndef KGUI_FAULT_H
#define KGUI_FAULT_H

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h" /* CONFIG_KODEOS_DUMP_HTTP */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The numbers of the notice ----------------------------------------------- */

#define KGUI_FAULT_NET_WAIT_MS   30000U  /**< The most the card waits for the channel since it became due, in milliseconds. */
#define KGUI_FAULT_HOLD_MS        1200U  /**< After the play, when Kody is paused: frame 36 of 72, up with the X, in milliseconds. */
#define KGUI_FAULT_KODY_WAIT_MS   5000U  /**< The most the card waits for the pack's fault animation to be read since it became due, in milliseconds. */
#if CONFIG_KODEOS_DUMP_HTTP
#define KGUI_FAULT_SENT_MS        1500U  /**< How long "Report sent" stays before the card leaves, in milliseconds. */
#define KGUI_FAULT_SEND_GUARD_MS 75000U  /**< The most the card waits for AO_Net's answer to a send, in milliseconds: longer than KODE_NET_GUARD_MS, within which AO_Net answers every request it took, so it covers only an answer lost on its way. */
#endif

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
    bool     offered;    /**< This build offers the report (CONFIG_KGUI_FAULT_REPORT). */
    bool     dump;       /**< The record says the fault left a dump, and a dump of a kodeOS image is in flash now. */
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
    KGUI_FAULT_RISE_SEND   /**< It rises with OK and Send. */
} kgui_fault_verdict_t;

/**
 * @brief Whether the card rises, and with what.
 *
 * A build that offers no report, no dump, a build that cannot send, Wi-Fi
 * off or a channel that will not come: OK alone at once. The channel up: Send as well when the
 * session names a `dumps` link. The channel on its way: wait, up to
 * KGUI_FAULT_NET_WAIT_MS, then OK alone.
 *
 * @param n What is known; NULL answers KGUI_FAULT_RISE_OK.
 * @return The verdict.
 */
kgui_fault_verdict_t kgui_fault_verdict(const kgui_fault_net_t *n);

#if CONFIG_KODEOS_DUMP_HTTP
/**
 * @brief Read a radio not heard yet this boot as one on its way.
 *
 * Before its first state arrives a field reads its zero: Wi-Fi off and the
 * channel KGUI_FAULT_CHAN_NEVER. Unknown is not never: until it is heard,
 * Wi-Fi counts as on and the channel as coming, so the card waits for the
 * network's verdict up to KGUI_FAULT_NET_WAIT_MS.
 *
 * @param n          The inputs, as the fields read; NULL does nothing.
 * @param wifi_heard Wi-Fi's state was heard this boot.
 * @param chan_heard The channel's state was heard this boot.
 */
void kgui_fault_net_unheard(kgui_fault_net_t *n, bool wifi_heard, bool chan_heard);
#endif

/* ---- The notice -------------------------------------------------------------------- */

/** @brief What the card shows. */
typedef enum {
    KGUI_FAULT_FACE_NOTICE = 0,  /**< The notice: OK, and Send when offered. */
    KGUI_FAULT_FACE_SENDING,     /**< "Sending report...", the edge spinning. */
    KGUI_FAULT_FACE_SENT,        /**< "Report sent", the edge closed green. */
    KGUI_FAULT_FACE_FAILED       /**< "Couldn't send the report", Close and Retry. */
} kgui_fault_face_t;

#if CONFIG_KODEOS_DUMP_HTTP
/** @brief How a send ended, as the card reads AO_Net's answer. */
typedef enum {
    KGUI_FAULT_SEND_OK = 0,     /**< The service took the report. */
    KGUI_FAULT_SEND_REFUSED,    /**< The service answered something else. */
    KGUI_FAULT_SEND_NO_ANSWER,  /**< No answer came: TLS, socket, timeout, or the card's guard. */
    KGUI_FAULT_SEND_BUSY,       /**< The network was taken by other work. */
    KGUI_FAULT_SEND_CHANNEL,    /**< The channel is down or names no `dumps` link. */
    KGUI_FAULT_SEND_NONE        /**< No dump of a kodeOS image is in flash. */
} kgui_fault_send_t;

/** @brief What the card's clock asks of a face. */
typedef enum {
    KGUI_FAULT_STEP_STAY = 0,   /**< Nothing: the face stays. */
    KGUI_FAULT_STEP_FAIL,       /**< The send had no answer in time: the failure face. */
    KGUI_FAULT_STEP_CLOSE       /**< "Report sent" has been read: the card leaves. */
} kgui_fault_step_t;

/** @brief Where on the card a press landed. */
typedef enum {
    KGUI_FAULT_PILL_LEFT = 0,   /**< The left pill of a pair: OK beside Send, Close beside Retry. */
    KGUI_FAULT_PILL_ANSWER,     /**< The answer pill: the notice's one OK, Send, or Retry. */
    KGUI_FAULT_PILL_NONE        /**< No pill: the glass, the pad, or TOP with no pill standing. */
} kgui_fault_pill_t;

/** @brief What a press does on the card. */
typedef enum {
    KGUI_FAULT_DO_NOTHING = 0,  /**< Nothing: no pill stands where it landed. */
    KGUI_FAULT_DO_ANSWER,       /**< OK: the notice is answered and the card closes. */
    KGUI_FAULT_DO_SEND,         /**< Send: the notice is answered and the report goes. */
    KGUI_FAULT_DO_RETRY,        /**< Retry: the report goes again. */
    KGUI_FAULT_DO_CLOSE,        /**< Close, or any press on "Report sent": the card closes. */
    KGUI_FAULT_DO_REFUSED,      /**< Send or Retry pressed by a remote line: the card stays. */
    KGUI_FAULT_DO_FOCUS,        /**< A pad press: the focus moves between the pair as on any card. */
    KGUI_FAULT_DO_STILL         /**< A pad press by a remote line: no focus moves and no ring shows. */
} kgui_fault_do_t;
#endif

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
 * @brief OK, Send, BOT or HOME answered the card: nothing is owed and
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

#if CONFIG_KODEOS_DUMP_HTTP
/**
 * @brief The card's words for how a send ended.
 * @param result A kgui_fault_send_t.
 * @return "Report sent" for OK; the one line of why for the rest: "The service
 *         refused it", "No answer", "Busy. Try again", "No connection",
 *         "Nothing to send"; "No answer" for any other value. Never NULL.
 */
const char *kgui_fault_send_words(uint8_t result);

/**
 * @brief What the card does with AO_Net's answer to a send.
 *
 * Only the card on the glass with the sending face takes it: OK turns it to
 * the sent face, anything else to the failure face. Any other face, or no
 * card, leaves @p face as it was.
 *
 * @param face    The card's face, a kgui_fault_face_t; receives the new one.
 * @param card_up The fault notice is the card on the glass.
 * @param result  A kgui_fault_send_t.
 * @return true when the answer was taken: the card is up with the sending
 *         face; false for a NULL @p face, no card on the glass, or any other
 *         face.
 */
bool kgui_fault_on_answer(uint8_t *face, bool card_up, uint8_t result);

/**
 * @brief What a press on the card does.
 *
 * Nothing leaves the board without a person's press: Send and Retry pressed
 * by a remote line (a console line from the Kode channel or a scheduled
 * entry) are refused, and the card stays as it is. OK and Close answer any
 * press. On the notice the left pill is OK and the answer pill Send when the
 * send is offered, OK alone when not; on the failure they are Close and
 * Retry. "Report sent" closes at any press; while the report goes no pill
 * stands.
 *
 * @param face   A kgui_fault_face_t.
 * @param offer  The notice offers the send (a pair, OK and Send).
 * @param pill   A kgui_fault_pill_t.
 * @param remote A remote line made the press.
 * @return What the press does; KGUI_FAULT_DO_NOTHING for any other face or
 *         pill.
 */
kgui_fault_do_t kgui_fault_press(uint8_t face, bool offer, uint8_t pill, bool remote);

/**
 * @brief What the clock asks of a face that has stood @p since_ms.
 *
 * The sending face fails at KGUI_FAULT_SEND_GUARD_MS; the sent face closes
 * at KGUI_FAULT_SENT_MS; the notice and the failure wait for a person.
 *
 * @param face     A kgui_fault_face_t.
 * @param since_ms Since the face was taken, in milliseconds.
 * @return The step.
 */
kgui_fault_step_t kgui_fault_face_step(uint8_t face, uint32_t since_ms);

/**
 * @brief What a press of the pad (up, down, left or right) does on the card.
 *
 * A remote line moves no focus: otherwise it could put the focus on Send, and
 * the person's next TOP, meant as OK, would send the report. "Report sent"
 * closes at any press; while the report goes the pad does nothing.
 *
 * @param face   A kgui_fault_face_t.
 * @param remote A remote line made the press.
 * @return KGUI_FAULT_DO_FOCUS or, for a remote press, KGUI_FAULT_DO_STILL on
 *         the notice and the failure; KGUI_FAULT_DO_CLOSE on the sent card;
 *         KGUI_FAULT_DO_NOTHING while the report goes and for any other face.
 */
kgui_fault_do_t kgui_fault_pad(uint8_t face, bool remote);

/**
 * @brief The word a pill shows, for the log.
 * @param face  A kgui_fault_face_t.
 * @param offer The notice offers the send (a pair, OK and Send).
 * @param pill  A kgui_fault_pill_t.
 * @return "OK", "Send", "Close" or "Retry"; NULL where no pill stands.
 */
const char *kgui_fault_pill_word(uint8_t face, bool offer, uint8_t pill);
#endif

#ifdef __cplusplus
}
#endif

#endif /* KGUI_FAULT_H */
