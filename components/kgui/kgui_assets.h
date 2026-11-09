/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_assets.h
 * @brief Kody's pack on the board: reading it, choosing it, installing it.
 */
#ifndef KGUI_ASSETS_H
#define KGUI_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kode_sys.h"
#include "kaos.h"
#include "kui_kody.h"
#include "kui_pack.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The built-in scene's id, valid whatever the source. */
#define KGUI_ASSETS_BUILTIN  0xFFFEU

/** @brief AO_Gui's task name, which `tasks` and a core dump show. */
#define KGUI_TASK_NAME  "gui"

/** @brief What kgui_assets tells AO_Gui. */
typedef enum {
    KGUI_ASSETS_EV_RETIRE = 0,  /**< The active pack is about to be freed: stop every player NOW. */
    KGUI_ASSETS_EV_RESOLVED,    /**< A source is active (built-in included); scenes renumbered. */
    KGUI_ASSETS_EV_SCENE,       /**< arg's wake-up and loop are ready, or the scene died. */
    KGUI_ASSETS_EV_REACTION,    /**< The reaction asked for on scene arg is ready, or failed. */
    KGUI_ASSETS_EV_TRY,         /**< assets_try's file is ready (arg 1) or was refused (arg 0). */
    KGUI_ASSETS_EV_INSTALL      /**< An install moved on a file, or ended. */
} kgui_assets_ev_t;

/**
 * @brief How kgui_assets tells AO_Gui what happened.
 * @param ev  What.
 * @param arg A scene id, or 0/1, as the event says.
 */
typedef void (*kgui_assets_notify_fn)(kgui_assets_ev_t ev, uint16_t arg);

/**
 * @brief Set the module up. Once, from AO_Gui's constructor.
 * @param ao     The AO every answer is posted to: AO_Gui.
 * @param notify Called on AO_Gui's thread, from inside kgui_assets_* calls
 *               and kgui_assets_on_event().
 */
void kgui_assets_init(QActive *ao, kgui_assets_notify_fn notify);

/**
 * @brief Finish an interrupted install, then resolve the source.
 *
 * Once, when the panel is up and the face is about to be shown. Ends in
 * KGUI_ASSETS_EV_RESOLVED.
 */
void kgui_assets_start(void);

/**
 * @brief Take an answer from AO_Storage or AO_Net.
 *
 * STORAGE_OPEN_RSP_SIG, STORAGE_DATA_SIG, NET_STREAM_OPEN_RSP_SIG,
 * NET_STREAM_DATA_SIG, STORAGE_PATH_RSP_SIG and STORAGE_WRITE_RSP_SIG are
 * consumed when the module waits for that kind of answer and ignored
 * otherwise. STORAGE_INFO_SIG is read and never consumed: kgui reads it too.
 *
 * @param e The event.
 * @return true when the event was the module's.
 */
bool kgui_assets_on_event(QEvt const *e);

/**
 * @brief Drive retries, the operation timeout and the queued loads.
 * @param now_ms khal_time_ms(), from every render tick.
 */
void kgui_assets_poll(uint32_t now_ms);

/**
 * @brief Whether the first resolution has ended.
 * @return true from the first KGUI_ASSETS_EV_RESOLVED on.
 */
bool kgui_assets_ready(void);

/**
 * @brief Where the active pack comes from.
 * @return KUI_PACK_SRC_BUILTIN when there is none.
 */
kui_pack_src_t kgui_assets_source(void);

/**
 * @brief The active pack's name.
 * @return The name; "" for none.
 */
const char *kgui_assets_pack_name(void);

/**
 * @brief The active pack's version.
 * @return The version; "" for none.
 */
const char *kgui_assets_pack_version(void);

/**
 * @brief How many scenes the active pack has.
 * @return 0 under the built-in source: KGUI_ASSETS_BUILTIN is the only scene.
 */
uint16_t kgui_assets_scene_count(void);

/**
 * @brief A scene's name.
 * @param s A scene id.
 * @return Its name; "reloj" for KGUI_ASSETS_BUILTIN; "-" for an id that is none.
 */
const char *kgui_assets_scene_name(uint16_t s);

/**
 * @brief A scene's id by name.
 * @param name The name.
 * @return Its id; KGUI_ASSETS_BUILTIN for "reloj" under the built-in source;
 *         KUI_PACK_NONE when the active source has no such scene.
 */
uint16_t kgui_assets_scene_find(const char *name);

/**
 * @brief The scene the hour picks.
 * @param hour The local hour; negative while the time is unknown.
 * @return A scene id; KGUI_ASSETS_BUILTIN under the built-in source.
 */
uint16_t kgui_assets_scene_for_hour(int8_t hour);

/**
 * @brief Make a scene's wake-up and loop ready.
 * @param s A scene id.
 * @return true when both are ready now (always for KGUI_ASSETS_BUILTIN);
 *         false when they are loading - KGUI_ASSETS_EV_SCENE with @p s follows
 *         - or the scene is dead.
 */
bool kgui_assets_scene_load(uint16_t s);

/**
 * @brief Whether a scene cannot play: its wake-up or its loop was refused.
 * @param s A scene id.
 * @return true when it is dead; never for KGUI_ASSETS_BUILTIN.
 */
bool kgui_assets_scene_dead(uint16_t s);

/**
 * @brief A scene's wake-up.
 * @param s A scene id whose kgui_assets_scene_load() answered true.
 * @return The descriptor; the built-in one for an id that is not ready.
 */
const kui_kody_anim_t *kgui_assets_wake(uint16_t s);

/**
 * @brief A scene's loop.
 * @param s A scene id whose kgui_assets_scene_load() answered true.
 * @return The descriptor; the built-in one for an id that is not ready.
 */
const kui_kody_anim_t *kgui_assets_loop(uint16_t s);

/**
 * @brief Whether a scene reacts to a trigger at all.
 * @param s A scene id.
 * @param t The trigger.
 * @return true when the scene has anchors and at least one file for @p t.
 */
bool kgui_assets_has_trigger(uint16_t s, kui_pack_trigger_t t);

/**
 * @brief One of a trigger's reactions, picked at random.
 * @param s A scene id.
 * @param t The trigger.
 * @return The descriptor; NULL when it is loading (KGUI_ASSETS_EV_REACTION
 *         follows, then kgui_assets_react_pending() gives it), when the one
 *         picked is dead, or when the scene has none.
 */
const kui_kody_anim_t *kgui_assets_react(uint16_t s, kui_pack_trigger_t t);

/**
 * @brief The reaction a KGUI_ASSETS_EV_REACTION announced.
 * @return The descriptor, once; NULL when it failed or was already taken.
 */
const kui_kody_anim_t *kgui_assets_react_pending(void);

/**
 * @brief Free every face file's block but scene @p s's.
 *
 * Call it only once the face player holds @p s's descriptors (after a
 * kui_kody_play() of its wake-up and a kui_kody_then() of its loop): the
 * player keeps pointers into the blocks it was given.
 *
 * @param s The scene on the face.
 */
void kgui_assets_scene_keep(uint16_t s);

/**
 * @brief A role's animation, ready since the source was resolved.
 * @param r The role.
 * @return The descriptor; NULL when the active pack has none for @p r.
 */
const kui_kody_anim_t *kgui_assets_role(kui_pack_role_t r);

/**
 * @brief Stage an install of @p bytes: the cable's and BLE's first step.
 *
 * Creates /storage/kode.new/, removes a stale `.ok`, and checks the internal
 * FAT has room for @p bytes. The outcome is a log line: `assets: begin
 * ready`, or `assets: begin refused why=room`.
 *
 * @param bytes What the host is about to send.
 * @return KODE_OK when the job started; KODE_DENIED while another runs.
 */
kode_status_t kgui_assets_begin(uint32_t bytes);

/**
 * @brief Check the staged pack and make it the internal FAT's.
 *
 * Every file /storage/kode.new/kode.json names is read, its digest checked
 * and judged; then the files move into /storage/kode/ one by one and the
 * source is resolved again. Logged file by file, then `assets: commit done`
 * or `assets: commit refused why=<word>`.
 *
 * @return KODE_OK when the job started; KODE_DENIED while another runs.
 */
kode_status_t kgui_assets_commit(void);

/**
 * @brief Fetch a pack over the network, stage it, and commit it.
 *
 * Files whose digest the internal FAT's pack already carries are copied
 * locally; the rest are fetched from the manifest's folder. The outcome is
 * logged as the commit's, or `assets: update refused why=<word>`.
 *
 * @param url    The manifest's URL; NULL for CONFIG_KGUI_ASSETS_URL.
 * @param online Wi-Fi has an address.
 * @return KODE_OK when the job started or was refused with a reason logged;
 *         KODE_DENIED while another runs.
 */
kode_status_t kgui_assets_update(const char *url, bool online);

/**
 * @brief Whether an install runs.
 * @return true from its start to its end.
 */
bool kgui_assets_installing(void);

/**
 * @brief Files the running install has handled.
 * @return The count.
 */
uint16_t kgui_assets_install_done(void);

/**
 * @brief Files the running install will handle.
 * @return The count; 0 until the manifest is read.
 */
uint16_t kgui_assets_install_of(void);

/**
 * @brief Why the last install failed.
 * @return The word; "" while one runs or after one succeeded.
 */
const char *kgui_assets_install_why(void);

/**
 * @brief Say the active pack: its source, every scene, every role.
 *
 * `assets: source=… name=… version=… scenes=<n>` once, then one
 * `assets: scene="…" anchor=<n> look=<n> dead=yes|no` per scene and one
 * `assets: role=<word> file="…"|-` per role - in the log, and as `>|assets`
 * records while a console function waits (kgui_assets_ask()).
 */
void kgui_assets_info(void);

/**
 * @brief Check every file of a medium's pack; activate nothing.
 * @param src KUI_PACK_SRC_SD or KUI_PACK_SRC_FS.
 * @return KODE_OK when the job started; KODE_DENIED while another runs or
 *         for another source.
 */
kode_status_t kgui_assets_check(kui_pack_src_t src);

/**
 * @brief Resolve the source again (spec section 6.1).
 * @return KODE_OK when the job started; KODE_DENIED while another runs.
 */
kode_status_t kgui_assets_reload(void);

/**
 * @brief Read one file of the active pack and check it for the face's canvas.
 *
 * KGUI_ASSETS_EV_TRY follows: arg 1 when kgui_assets_try_anim() is ready to
 * play, 0 when it was refused (said). The face may still decline an arg 1 by
 * calling kgui_assets_try_refuse() inside the notify.
 *
 * @param file A file name of the active pack's folder.
 * @return KODE_OK when the job started; KODE_DENIED while another runs, under
 *         the built-in source, or for a name that cannot be a pack's file.
 */
kode_status_t kgui_assets_try(const char *file);

/**
 * @brief The file kgui_assets_try() read, until a scene is kept again.
 * @return The descriptor; NULL when there is none.
 */
const kui_kody_anim_t *kgui_assets_try_anim(void);

/**
 * @brief The face cannot play the file kgui_assets_try() read: say why.
 *
 * Called from the KGUI_ASSETS_EV_TRY notify; the try is answered as refused
 * with this word instead of as playing.
 *
 * @param why One word.
 */
void kgui_assets_try_refuse(const char *why);

/* ---- Answering a console function ----------------------------------------
 * The `assets_*`, `face_scene` and `time_zone` functions are rows with
 * answer_ms set (kode_functions.h): the console keeps the line open until
 * the owner has sent its records and ended the function. A log line never
 * leaves the board over the Kode channel; a record does.
 */

/**
 * @brief The command just accepted waits for the words of the next job.
 *
 * Until the job ends (or kgui_assets_ask_end()), every line the job says as
 * `assets: key=value ...` is sent as the record `>|assets key=value ...`
 * (and logged at debug level only, unless it is a change of state), and the
 * job's end closes the console's line: `>ok`, or `>err` when the job
 * refused or failed.
 *
 * @param ticket The function's ticket (function_ticket()), echoed by every
 *               record and by the end.
 */
void kgui_assets_ask(uint32_t ticket);

/**
 * @brief A command was refused before any job started: say so, and end the
 *        function as a failure.
 * @param op  The command's word: info, check, reload, begin, commit,
 *            update or try.
 * @param why One word: busy, builtin...
 */
void kgui_assets_refuse(const char *op, const char *why);

/**
 * @brief End the function being answered, unless a job already has.
 * @param status How it went; KODE_OK is `>ok`.
 */
void kgui_assets_ask_end(kode_status_t status);

/**
 * @brief Send one record of a function.
 * @param ticket The function's ticket.
 * @param record The record without its `>|`.
 * @return true when the console has it; false, with one warning, when it
 *         took none, and always false in a firmware without the console.
 */
bool kgui_answer(uint32_t ticket, const char *record);

/**
 * @brief End a function.
 *
 * An end the console refuses stays owed and is posted again at the next step
 * of the module (a bounded number of times), so the line closes promptly
 * instead of at the row's `answer_ms`.
 *
 * @param ticket The function's ticket.
 * @param status KODE_OK: `>ok`; KODE_DENIED: a bad argument; else code 5.
 * @param msg    The word after `msg=` of a code 5; NULL for the function's
 *               own name.
 */
void kgui_answer_end(uint32_t ticket, kode_status_t status, const char *msg);

/**
 * @brief A text as one value of a record.
 * @param dst Room for FUNCTION_QUOTED_MAX() of the text.
 * @param cap Its size.
 * @param s   The text.
 * @return The text quoted; `-` when there is none.
 */
const char *kgui_answer_quote(char *dst, size_t cap, const char *s);

#ifdef __cplusplus
}
#endif

#endif /* KGUI_ASSETS_H */
