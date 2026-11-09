/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_assets.c
 * @brief Kody's pack on the board (kgui_assets.h).
 */
#include "kgui_assets.h"

#include <stdio.h>
#include <string.h>

#include "kdrv_kfw.h"
#include "khal_zlib.h"
#include "khal_system.h"
#include "khal_time.h"
#include "kode_log.h"
#include "kgui_pack_fit.h"
#include "kui_kody_anims.h"

/** @brief Log tag: kgui's own. */
#define TAG "kgui"

#define ASSETS_SD_DIR        "/sdcard/kode"       /**< The card's pack. */
#define ASSETS_FS_DIR        "/storage/kode"      /**< The internal FAT's pack. */
#define ASSETS_NEW_DIR       "/storage/kode.new"  /**< Where an install is staged. */
#define ASSETS_MANIFEST      "kode.json"          /**< The manifest's name. */
#define ASSETS_OK_FILE       ".ok"                /**< Written once a staged pack is checked. */
#define ASSETS_ROOM_MIN_KB   256U    /**< Free room an update over the network needs to start. */
#define ASSETS_RETRY_MS      250U    /**< The reader slot was taken: ask again after this. */
#define ASSETS_RESOLVE_LATER_MS 1000U /**< A busy reader: choose the source again after this. */
#define ASSETS_DUE_HALF      0x80000000UL /**< Half the ms clock's range: a later deadline reads past it. */
#define ASSETS_RETRIES       20U     /**< Five seconds of a taken slot is a failure. */
#define ASSETS_OP_TIMEOUT_MS 20000U  /**< An answer that never comes ends the job. */
#define ASSETS_WANT_MAX      8U      /**< Loads the face may queue. */
#define ASSETS_PATH_MAX      KODE_APP_FILE_MAX  /**< The longest name AO_Storage's open and write take. */
#define ASSETS_END_TRIES     50U    /**< Posts of an end the console refuses, before it is given up. */
#define ASSETS_WHY_MAX       8U     /**< Room for the word the face refuses an assets_try with. */
#define ASSETS_REC_PREFIX_MAX 8U    /**< `assets ` and the terminator, ahead of a record's words. */

/** @brief Where a file's record stands. */
typedef enum {
    FILE_UNLOADED = 0,  /**< Not read, or freed again. */
    FILE_LOADING,       /**< Queued or being read. */
    FILE_READY,         /**< blk and anim are valid. */
    FILE_DEAD           /**< Refused or missing: never played. */
} file_state_t;

/** @brief One file of the active pack, on the board. */
typedef struct {
    uint8_t         *blk;    /**< The file as read; the descriptor points into it. */
    kui_kody_anim_t  anim;   /**< Valid when state is FILE_READY. */
    file_state_t     state;  /**< Where it stands. */
} file_rt_t;

/** @brief The operation in flight. */
typedef enum {
    OP_NONE = 0,  /**< None. */
    OP_GATHER,    /**< A file read whole: STORAGE_OPEN/DATA or NET_STREAM_OPEN/DATA. */
    OP_PATH,      /**< STORAGE_PATH_RSP_SIG: a stat, mkdir, unlink, move, copy, text. */
    OP_WRITE,     /**< STORAGE_WRITE_RSP_SIG. */
    OP_INFO       /**< STORAGE_INFO_SIG for the internal FAT. */
} op_t;

/** @brief The job running. */
typedef enum {
    JOB_IDLE = 0,  /**< None. */
    JOB_RESOLVE,   /**< Choosing the source and loading its roles. */
    JOB_LOAD,      /**< One face file the face asked for. */
    JOB_CHECK,     /**< assets_check. */
    JOB_BEGIN,     /**< assets_begin. */
    JOB_COMMIT,    /**< assets_commit, and the boot's recovery. */
    JOB_UPDATE,    /**< assets_update. */
    JOB_TRY,       /**< assets_try. */
    JOB_PROBE      /**< Is the card's manifest still there? (a card file was missing) */
} job_t;

/** @brief Everything the module knows. */
static struct {
    QActive               *ao;            /**< Where every answer is posted. */
    kgui_assets_notify_fn  notify;        /**< AO_Gui's ear. */
    kui_pack_src_t         src;           /**< The active source. */
    kui_pack_t             pack;          /**< Its manifest; empty for the built-in source. */
    file_rt_t             *rt;            /**< pack.n_files records, PSRAM. */
    bool                   ready;         /**< The first resolution has ended. */
    kgui_pack_fit_t        fit;           /**< Whether the pack was drawn for this face. */
    /* The one operation in flight. */
    op_t      op;
    uint32_t  op_t0_ms;
    bool      g_net;
    char      g_where[KODE_FLASH_SRC_MAX];
    uint8_t  *g_buf;
    uint32_t  g_len;
    uint32_t  g_cap;
    uint32_t  g_limit;
    uint32_t  g_size;
    uint8_t   g_tries;
    bool      g_probing;            /* a stat asks why an open was refused */
    bool      retry_armed;
    uint32_t  retry_at_ms;
    uint32_t  fs_free_kb;
    /* The job. */
    job_t          job;
    uint16_t       step;
    uint16_t       i;
    kui_pack_t     cand;
    kui_pack_src_t cand_src;
    bool           resolve_owed;
    bool           resolve_due;     /* a busy reader: resolve at resolve_at_ms */
    uint32_t       resolve_at_ms;
    bool           sd_probe;        /* a card file was missing: is the card's pack? */
    bool           cand_busy;       /* the last manifest read found storage busy */
    /* The loads the face asked for. */
    uint16_t  want[ASSETS_WANT_MAX];
    uint8_t   n_want;
    uint16_t  load_file;
    uint16_t  scene_waiting;
    uint16_t  react_scene;
    uint16_t  react_file;
    /* The installs. */
    kui_pack_t old;                 /* the internal FAT's manifest, for its names and digests */
    bool       recovering;          /* the commit finishes an interrupted one */
    bool       installing;
    uint32_t   begin_bytes;
    uint8_t   *wblk;                /* a block being written: kept until the answer */
    /* Answers still owed to operations given up on at their timeout. Only
     * this module asks AO_Storage for paths and writes from AO_Gui, and the
     * answers come in order, so the next that many are those, not the
     * operation in flight's. */
    uint8_t    p_stale;             /* path answers owed (a stat, a move...) */
    uint8_t    w_stale;             /* write answers owed */
    uint8_t   *w_orphan;            /* the oldest abandoned write's block */
    uint16_t   inst_done;
    uint16_t   inst_of;
    char       why[16];
    char       base[KODE_FLASH_SRC_MAX];
    char       url[KODE_FLASH_SRC_MAX];
    /* assets_check and assets_try. */
    kui_pack_src_t chk_src;
    uint16_t       chk_ok;
    uint16_t       chk_bad;
    uint8_t       *try_blk;
    kui_kody_anim_t try_anim;
    char           try_name[KUI_PACK_FILE_NAME_MAX + 1U];
    /* The console function waiting for this job's words. */
    bool           answering;   /**< A function waits: say() sends records. */
    uint32_t       ticket;      /**< Its ticket (function_ticket()).        */
    bool           end_owed;    /**< An end the console has not taken yet.  */
    uint32_t       end_ticket;  /**< ...whose function.                     */
    kode_status_t  end_rc;      /**< ...how it went.                        */
    char           end_msg[KODE_FUNCTION_MSG_MAX]; /**< ...its code 5 word.  */
    uint8_t        end_tries;   /**< Posts the console has refused.         */
    kode_status_t  ans_rc;      /**< How the job ends, once it does.        */
    char           try_why[ASSETS_WHY_MAX]; /**< Why the face would not play the file. */
    /* The end of the last operation, waiting for job_run(). */
    bool           res_ready;
    kode_status_t  res_rc;
    bool           res_exists;
} s_a;

static void job_step(kode_status_t rc, bool exists);
static void answer_flush(void);

/**
 * @brief Record how an operation ended, for job_run() to hand to the job.
 *
 * NO STEP CALLS ANOTHER. A job moves on only from job_run(), called at the
 * end of every public entry point, so a step that ends another step's work
 * at once - a refused open, a skipped file, a job that starts the next -
 * records it here instead of calling back in: the module never recurses.
 *
 * @param rc     How it ended.
 * @param exists What a stat said, or whether a usage figure is known.
 */
static void post_result(kode_status_t rc, bool exists)
{
    s_a.res_rc     = rc;
    s_a.res_exists = exists;
    s_a.res_ready  = true;
}

/** @brief Hand every recorded end to the running job, until none is left. */
static void job_run(void)
{
    answer_flush();
    while ( s_a.res_ready )
    {
        s_a.res_ready = false;
        job_step(s_a.res_rc, s_a.res_exists);
    }
}

/* ---- Small words ----------------------------------------------------------- */

/**
 * @brief The word a source is logged with.
 * @param src A kui_pack_src_t.
 * @return "builtin", "sd" or "fat".
 */
static const char *src_word(uint32_t src)
{
    const char *w = "builtin";

    if ( (uint32_t)KUI_PACK_SRC_SD == src )
    {
        w = "sd";
    }
    else if ( (uint32_t)KUI_PACK_SRC_FS == src )
    {
        w = "fat";
    }
    else
    {
        /* Built-in, as w already says. */
    }
    return w;
}

/**
 * @brief The folder a source's pack lives in.
 * @param src The source.
 * @return Its folder; "" for the built-in source.
 */
static const char *src_dir(kui_pack_src_t src)
{
    const char *d = "";

    if ( KUI_PACK_SRC_SD == src )
    {
        d = ASSETS_SD_DIR;
    }
    else if ( KUI_PACK_SRC_FS == src )
    {
        d = ASSETS_FS_DIR;
    }
    else
    {
        /* The built-in source has no folder. */
    }
    return d;
}

/**
 * @brief A file's path in a folder.
 * @param out  ASSETS_PATH_MAX bytes.
 * @param dir  The folder.
 * @param name The file.
 * @return true when the path fits.
 */
static bool path_of(char *out, const char *dir, const char *name)
{
    int const n = snprintf(out, ASSETS_PATH_MAX, "%s/%s", dir, name);

    return (0 < n) && ((size_t)n < ASSETS_PATH_MAX);
}

/**
 * @brief Whether a file is stored deflated.
 * @param name Its name.
 * @return true for a name ending ".z".
 */
static bool ends_z(const char *name)
{
    size_t const n = strlen(name);

    return (2U < n) && (strcmp(&name[n - 2U], ".z") == 0);
}

/* ---- Answering the console ---------------------------------------------------
 * `assets_*` are functions that answer (kode_functions.h): the console keeps
 * the line open while the job they started says its words, and the words
 * are the NOTICE lines this module has always logged, as records. One call
 * does both (job_say()), so a record and its log line cannot disagree; the
 * job's end (job_end()) is the end of the line.
 */

bool kgui_answer(uint32_t ticket, const char *record)
{
    bool sent = false;

#if CONFIG_KODEOS_CONSOLE
    sent = function_record(ticket, record);
    if ( !sent )
    {
        KLOG_W(TAG, "assets: the console took no record; the answer is short");
    }
#else
    (void)ticket;
    (void)record;
#endif
    return sent;
}

/**
 * @brief Post the end of the function owed one, once the console takes it.
 *
 * A refused end (a dry pool, a full queue) would leave the console's line
 * open for the row's whole `answer_ms`, so it stays owed and is posted again
 * at the next step of the module (job_run()), a bounded number of times.
 */
static void answer_flush(void)
{
    if ( s_a.end_owed )
    {
        bool sent = false;

#if CONFIG_KODEOS_CONSOLE
        sent = function_end(s_a.end_ticket, s_a.end_rc, s_a.end_msg);
#endif
        if ( sent )
        {
            s_a.end_owed = false;
        }
        else
        {
            if ( 0U == s_a.end_tries )
            {
                KLOG_W(TAG, "assets: the console took no end; it is posted again");
            }
            s_a.end_tries++;
            if ( ASSETS_END_TRIES <= s_a.end_tries )
            {
                s_a.end_owed = false;
                KLOG_W(TAG, "assets: the end was given up; the console's watchdog closes the line");
            }
        }
    }
}

void kgui_answer_end(uint32_t ticket, kode_status_t status, const char *msg)
{
    s_a.end_owed   = true;
    s_a.end_ticket = ticket;
    s_a.end_rc     = status;
    s_a.end_tries  = 0U;
    s_a.end_msg[0] = '\0';
    if ( NULL != msg )
    {
        (void)snprintf(s_a.end_msg, sizeof(s_a.end_msg), "%s", msg);
    }
    answer_flush();
}

const char *kgui_answer_quote(char *dst, size_t cap, const char *s)
{
    const char *q = "-";

    if ( (NULL != s) && ('\0' != s[0]) )
    {
#if CONFIG_KODEOS_CONSOLE
        q = function_quote(dst, cap, s);
#else
        (void)snprintf(dst, cap, "\"%s\"", s);
        q = dst;
#endif
    }
    return q;
}

void kgui_assets_ask(uint32_t ticket)
{
    s_a.answering = true;
    s_a.ticket    = ticket;
    s_a.ans_rc    = KODE_OK;
}

void kgui_assets_ask_end(kode_status_t status)
{
    if ( s_a.answering )
    {
        s_a.answering = false;
        kgui_answer_end(s_a.ticket, status, NULL);
    }
}

/**
 * @brief Log one line of a job and, while a function waits, send it as a
 *        record: `assets: op=check result=done` is `>|assets op=check
 *        result=done`.
 *
 * The record is the answer, so while a function waits the line is logged at
 * debug level only, and the cable and BLE do not read the same thing twice.
 * A line that is a real change of state (@p state) is a notice either way.
 *
 * @param warn  true to log it as a warning when nobody waits.
 * @param state true for a change of state: a notice, answered or not.
 * @param body  The words after `assets: `: `key=value` pairs.
 */
static void job_say_text(bool warn, bool state, const char *body)
{
    if ( s_a.answering && (!state) )
    {
        KLOG_D(TAG, "assets: %s", body);
    }
    else if ( warn )
    {
        KLOG_W(TAG, "assets: %s", body);
    }
    else
    {
        KLOG_N(TAG, "assets: %s", body);
    }
    if ( s_a.answering )
    {
        char rec[KODE_FUNCTION_REC_MAX];

        (void)snprintf(rec, sizeof(rec), "assets %s", body);
        (void)kgui_answer(s_a.ticket, rec);
    }
}

/** @brief Room for the words of one line of a job: a record is `assets `
 *         and these, in KODE_FUNCTION_REC_MAX. */
#define JOB_SAY_MAX  (KODE_FUNCTION_REC_MAX - ASSETS_REC_PREFIX_MAX)

/**
 * @brief job_say_text() over a format: the words are `snprintf`ed first, so
 *        the variadic part stays out of the module (MISRA 17.1).
 */
#define job_say(warn, ...) \
    do { \
        char say_buf_[JOB_SAY_MAX]; \
        (void)snprintf(say_buf_, sizeof(say_buf_), __VA_ARGS__); \
        job_say_text((warn), false, say_buf_); \
    } while (false)

/** @brief job_say() for a line that is a change of state, not an answer to a
 *         query: it stays a notice while a function waits. */
#define job_state(...) \
    do { \
        char say_buf_[JOB_SAY_MAX]; \
        (void)snprintf(say_buf_, sizeof(say_buf_), __VA_ARGS__); \
        job_say_text(false, true, say_buf_); \
    } while (false)

/**
 * @brief A command's job was refused or failed: say why, and the line ends
 *        as a failure.
 * @param op  The command's word: begin, commit, update, check or try.
 * @param why The reason, one word.
 */
static void job_refused(const char *op, const char *why)
{
    job_say(true, "op=%s result=refused why=%s", op, why);
    s_a.ans_rc = KODE_ERROR;
}

void kgui_assets_refuse(const char *op, const char *why)
{
    job_refused(op, why);
    kgui_assets_ask_end(s_a.ans_rc);
}

/**
 * @brief Say which pack is active: where it comes from, its name and
 *        version, and how many scenes.
 * @param state true when the pack has just been chosen (a change of state,
 *              a notice even while a function waits); false for the answer
 *              to `assets_info`.
 */
static void say_source(bool state)
{
    char qn[FUNCTION_QUOTED_MAX(KUI_PACK_TEXT_MAX)];
    char qv[FUNCTION_QUOTED_MAX(KUI_PACK_TEXT_MAX)];
    char body[JOB_SAY_MAX];

    (void)snprintf(body, sizeof(body), "source=%s name=%s version=%s scenes=%u",
                   src_word((uint32_t)s_a.src), kgui_answer_quote(qn, sizeof(qn), s_a.pack.name),
                   kgui_answer_quote(qv, sizeof(qv), s_a.pack.version),
                   (unsigned)s_a.pack.n_scenes);
    job_say_text(false, state, body);
}

/* ---- The gatherer: one file, read whole, into one PSRAM block --------------- */

/**
 * @brief Start reading a whole file from a path or a URL.
 * @param net   true for a URL (AO_Net's stream), false for a path.
 * @param where The path or the URL.
 * @param limit The largest file accepted.
 */
static void gather(bool net, const char *where, uint32_t limit)
{
    s_a.op       = OP_GATHER;
    s_a.op_t0_ms = khal_time_ms();
    s_a.g_net    = net;
    (void)snprintf(s_a.g_where, sizeof(s_a.g_where), "%s", where);
    s_a.g_buf    = NULL;
    s_a.g_len    = 0U;
    s_a.g_cap    = 0U;
    s_a.g_limit  = limit;
    s_a.g_size   = 0U;
    s_a.g_tries  = 0U;
    s_a.g_probing = false;
    if ( net )
    {
        net_stream_open_request(s_a.g_where, s_a.ao);
    }
    else
    {
        storage_open_request(s_a.g_where, s_a.ao);
    }
}

/** @brief Close the gather's file or stream; buffers on loan go back with it. */
static void gather_close(void)
{
    if ( s_a.g_net )
    {
        net_stream_close_request();
    }
    else
    {
        storage_close_request();
    }
}

/**
 * @brief End a gather that failed.
 * @param rc Why.
 */
static void gather_fail(kode_status_t rc)
{
    khal_free(s_a.g_buf);
    s_a.g_buf = NULL;
    s_a.g_len = 0U;
    s_a.op    = OP_NONE;
    post_result(rc, false);
}

/**
 * @brief The open was answered.
 * @param r The answer.
 */
static void gather_opened(StorageOpenRspEvt const * const r)
{
    if ( (uint8_t)KODE_OK != r->status )
    {
        /* AO_Storage refuses an empty file and a taken reader slot (another
         * open, a write, a copy) the same way, KODE_DENIED. A stat, which it
         * answers while busy, says which (gather_probed()). */
        if ( (!s_a.g_net) && ((uint8_t)KODE_DENIED == r->status) )
        {
            s_a.g_probing = true;
            s_a.op_t0_ms  = khal_time_ms();
            storage_path_request(s_a.ao, (uint8_t)STORAGE_PATH_STAT, s_a.g_where);
        }
        else
        {
            gather_fail((kode_status_t)r->status);
        }
        return;
    }
    s_a.g_size = r->size;
    if ( r->size > s_a.g_limit )
    {
        gather_close();
        gather_fail(KODE_NO_RESOURCES);   /* the job reads g_size: "size" */
        return;
    }
    s_a.g_cap = s_a.g_limit;
    if ( 0U < r->size )
    {
        s_a.g_cap = r->size;              /* chunked HTTP says 0: the limit is the room */
    }
    /* cppcheck-suppress misra-c2012-11.5 ; khal_alloc_psram() answers void *: PSRAM is taken by name (rule 8) */
    s_a.g_buf = (uint8_t *)khal_alloc_psram(sizeof(uint32_t), (size_t)s_a.g_cap);
    if ( NULL == s_a.g_buf )
    {
        gather_close();
        gather_fail(KODE_NO_RESOURCES);
        return;
    }
    if ( s_a.g_net )
    {
        net_stream_read_request();
    }
    else
    {
        storage_read_request();
    }
}

/**
 * @brief Why the open was refused, as the stat tells it.
 *
 * A file that is there with bytes in it was refused because the reader slot
 * is taken: asked again on the tick, a few times, then given up as busy
 * (KODE_DENIED), which the jobs take as "later", never as "absent". A file
 * of no bytes is KODE_NO_RESOURCES with g_size 0 ("size"); one that is not
 * there is KODE_ERROR ("missing").
 *
 * @param r The stat's answer.
 */
static void gather_probed(StoragePathRspEvt const * const r)
{
    if ( ((uint8_t)KODE_OK == r->rc) && r->exists && (0U < r->size) )
    {
        if ( ASSETS_RETRIES > s_a.g_tries )
        {
            s_a.g_tries++;
            s_a.retry_armed = true;
            s_a.retry_at_ms = khal_time_ms() + ASSETS_RETRY_MS;
        }
        else
        {
            gather_fail(KODE_DENIED);
        }
    }
    else if ( ((uint8_t)KODE_OK == r->rc) && r->exists )
    {
        s_a.g_size = 0U;
        gather_fail(KODE_NO_RESOURCES);
    }
    else
    {
        gather_fail(KODE_ERROR);
    }
}

/**
 * @brief A chunk arrived.
 * @param d The chunk's event.
 */
static void gather_data(StorageDataEvt const * const d)
{
    const uint8_t *src = NULL;

    if ( s_a.g_net )
    {
        src = net_stream_buf(d->idx);
    }
    else
    {
        src = storage_buf(d->idx);
    }
    if ( (uint8_t)KODE_OK != d->status )
    {
        gather_close();
        gather_fail((kode_status_t)d->status);
        return;
    }
    if ( ((uint32_t)d->len > (s_a.g_cap - s_a.g_len)) || ((0U < d->len) && (NULL == src)) )
    {
        gather_close();
        gather_fail(KODE_NO_RESOURCES);
        return;
    }
    if ( 0U < d->len )
    {
        (void)memcpy(&s_a.g_buf[s_a.g_len], src, (size_t)d->len);
        s_a.g_len += (uint32_t)d->len;
    }
    if ( d->eof )
    {
        gather_close();                    /* releases every buffer on loan */
        s_a.op = OP_NONE;
        post_result(KODE_OK, true);
    }
    else if ( s_a.g_net )
    {
        net_stream_return(d->idx);         /* a return IS the next read */
    }
    else
    {
        storage_next_request(d->idx);
    }
}

/**
 * @brief The block the last gather filled, handed over: the caller owns it.
 * @param len Receives its length.
 * @return The block, or NULL when there is none.
 */
static uint8_t *gather_take(uint32_t *len)
{
    uint8_t *const b = s_a.g_buf;

    *len = s_a.g_len;
    s_a.g_buf = NULL;
    s_a.g_len = 0U;
    return b;
}

/* ---- The other operations ------------------------------------------------------ */

/**
 * @brief Ask AO_Storage for a path operation: stat, mkdir, unlink.
 * @param op   A storage_path_op_t.
 * @param path The path.
 */
static void path_op(uint8_t op, const char *path)
{
    s_a.op       = OP_PATH;
    s_a.op_t0_ms = khal_time_ms();
    storage_path_request(s_a.ao, op, path);
}

/**
 * @brief Ask AO_Storage to move or copy a file.
 * @param op  A storage_pair_op_t.
 * @param src From.
 * @param dst To.
 */
static void pair_op(uint8_t op, const char *src, const char *dst)
{
    s_a.op       = OP_PATH;
    s_a.op_t0_ms = khal_time_ms();
    storage_pair_request(s_a.ao, op, src, dst);
}

/**
 * @brief Ask AO_Storage to write a short text file whole.
 * @param path The file.
 * @param text Its text.
 */
static void text_op(const char *path, const char *text)
{
    s_a.op       = OP_PATH;
    s_a.op_t0_ms = khal_time_ms();
    storage_text_request(s_a.ao, path, text, false, 0U);
}

/**
 * @brief Ask AO_Storage to write a block whole; the block is kept until the answer.
 * @param path The file.
 * @param blk  The block, owned by the module, freed when the write is answered.
 * @param len  Its length.
 */
static void assets_write_op(const char *path, uint8_t *blk, uint32_t len)
{
    s_a.op       = OP_WRITE;
    s_a.op_t0_ms = khal_time_ms();
    s_a.wblk     = blk;
    storage_write_all_request(path, blk, len, s_a.ao);
}

/** @brief Ask AO_Storage how much room the internal FAT has. */
static void info_op(void)
{
    s_a.op       = OP_INFO;
    s_a.op_t0_ms = khal_time_ms();
    storage_request(KDRV_MEDIUM_FS, STORAGE_OP_INFO);
}

/* ---- Jobs -------------------------------------------------------------------- */

/**
 * @brief Start a job at its first step.
 * @param j The job.
 */
static void job_start(job_t j)
{
    s_a.job  = j;
    s_a.step = 0U;
    s_a.i    = 0U;
    post_result(KODE_OK, false);
}

/** @brief The job is over: the function waiting for it is answered, and a
 *         resolution owed meanwhile runs now. */
static void job_end(void)
{
    s_a.job = JOB_IDLE;
    kgui_assets_ask_end(s_a.ans_rc);
    if ( s_a.resolve_owed )
    {
        s_a.resolve_owed = false;
        job_start(JOB_RESOLVE);
    }
}

/** @brief Resolve again once ASSETS_RESOLVE_LATER_MS has gone by. */
static void resolve_later(void)
{
    s_a.resolve_due = true;
    s_a.resolve_at_ms = khal_time_ms() + ASSETS_RESOLVE_LATER_MS;
}

/** @brief Resolve again now, or when the running job ends. */
static void resolve_soon(void)
{
    if ( JOB_IDLE == s_a.job )
    {
        job_start(JOB_RESOLVE);
    }
    else
    {
        s_a.resolve_owed = true;
    }
}

/* ---- Judging a file ------------------------------------------------------------ */

/**
 * @brief The JSON a file holds: itself, or inflated into a scratch block.
 * @param name    The file's name.
 * @param blk     The file as read.
 * @param len     Its length.
 * @param json    Receives the JSON.
 * @param jlen    Receives its length.
 * @param scratch Receives the scratch block to free, or NULL.
 * @return KUI_PACK_OK, KUI_PACK_E_SIZE (inflates past the limit),
 *         KUI_PACK_E_JSON (not a stream) or KUI_PACK_E_MEMORY.
 */
static kui_pack_err_t file_json(const char *name, const uint8_t *blk, uint32_t len,
                                const char **json, size_t *jlen, uint8_t **scratch)
{
    kui_pack_err_t err = KUI_PACK_OK;

    *json    = (const char *)blk;
    *jlen    = (size_t)len;
    *scratch = NULL;
    if ( ends_z(name) )
    {
        size_t out = 0U;
        /* cppcheck-suppress misra-c2012-11.5 ; khal_alloc_psram() answers void *: PSRAM is taken by name (rule 8) */
        uint8_t *const z = (uint8_t *)khal_alloc_psram(sizeof(uint32_t), KUI_PACK_JSON_MAX);

        if ( NULL == z )
        {
            err = KUI_PACK_E_MEMORY;
        }
        else
        {
            kode_status_t const rc = khal_zlib_decompress(blk, (size_t)len, z, KUI_PACK_JSON_MAX, &out);

            if ( KODE_OK == rc )
            {
                *json    = (const char *)z;
                *jlen    = out;
                *scratch = z;
            }
            else
            {
                khal_free(z);
                if ( KODE_DENIED == rc )
                {
                    err = KUI_PACK_E_SIZE;     /* inflates past 96 KB */
                }
                else if ( KODE_NO_RESOURCES == rc )
                {
                    err = KUI_PACK_E_MEMORY;
                }
                else
                {
                    err = KUI_PACK_E_JSON;
                }
            }
        }
    }
    return err;
}

/**
 * @brief Judge a file of pack @p p and, when it passes, describe it.
 * @param p    The pack that names the file.
 * @param f    Its index.
 * @param blk  The file as read; the caller's either way.
 * @param len  Its length.
 * @param anim Filled, pointing into @p blk, on success.
 * @param info Filled as far as the checks get.
 * @return KUI_PACK_OK or the refusal.
 */
static kui_pack_err_t file_judge(const kui_pack_t *p, uint16_t f, const uint8_t *blk, uint32_t len,
                                 kui_kody_anim_t *anim, kui_pack_anim_t *info)
{
    uint16_t w = 0U;
    uint16_t h = 0U;
    uint16_t anchor = 0U;
    const char *json = NULL;
    size_t jlen = 0U;
    uint8_t *scratch = NULL;
    kui_pack_err_t err = KUI_PACK_OK;

    (void)memset(info, 0, sizeof(*info));
    if ( !kui_pack_file_canvas(p, f, &w, &h, &anchor) )
    {
        err = KUI_PACK_E_CANVAS;
    }
    if ( KUI_PACK_OK == err )
    {
        err = file_json(p->files[f].name, blk, len, &json, &jlen, &scratch);
    }
    if ( KUI_PACK_OK == err )
    {
        err = kui_pack_check_anim(json, jlen, w, h, anchor, info);
    }
    khal_free(scratch);
    if ( KUI_PACK_OK == err )
    {
        (void)memset(anim, 0, sizeof(*anim));
        anim->zlib     = blk;
        anim->zlib_len = len;
        anim->json_len = (uint32_t)jlen;
        anim->w        = w;
        anim->h        = h;
        anim->frames   = info->frames;
        anim->fps      = info->fps;
        anim->anchor   = (uint8_t)anchor;
        anim->deflated = ends_z(p->files[f].name);
    }
    return err;
}

/**
 * @brief Whether a file of the active pack plays on the face's canvas.
 * @param f Its index.
 * @return true for a scene's file; false for a role's own canvas.
 */
static bool file_on_face(uint16_t f)
{
    uint16_t w = 0U;
    uint16_t h = 0U;
    uint16_t anchor = 0U;

    return kui_pack_file_canvas(&s_a.pack, f, &w, &h, &anchor) && (KUI_PACK_SCENE_W == w)
           && (KUI_PACK_SCENE_H == h);
}

/**
 * @brief Whether a file of the active pack is a scene's wake-up.
 * @param f Its index.
 * @return true when one scene wakes with it.
 */
static bool file_is_wake(uint16_t f)
{
    bool wake = false;

    for ( uint16_t s = 0U; (!wake) && (s < s_a.pack.n_scenes); ++s )
    {
        wake = (s_a.pack.scenes[s].wake == f);
    }
    return wake;
}

/**
 * @brief The pack was not drawn for this face: every scene of it is dead at once.
 *
 * Instead of reading and parsing the rest to learn the same, a file at a time,
 * with the glass black at boot (kgui_pack_fit.h). The roles keep their files:
 * they have canvases of their own.
 */
static void pack_refuse_canvas(void)
{
    job_say(true, "source=%s result=refused why=canvas", src_word((uint32_t)s_a.src));
    s_a.n_want = 0U;
    for ( uint16_t f = 0U; f < s_a.pack.n_files; ++f )
    {
        if ( file_on_face(f) )
        {
            s_a.rt[f].state = FILE_DEAD;
        }
    }
}

/**
 * @brief Settle a file of the active pack from what a load gave.
 * @param f   Its index.
 * @param rc  How the read ended.
 * @param blk The file as read when @p rc is KODE_OK; owned from here.
 * @param len Its length.
 */
static void file_settle(uint16_t f, kode_status_t rc, uint8_t *blk, uint32_t len)
{
    kui_pack_anim_t info;
    const char *const name = s_a.pack.files[f].name;

    if ( KODE_DENIED == rc )
    {
        /* Busy: the reader slot stayed taken. Not refused - read later. */
        khal_free(blk);
        s_a.rt[f].state = FILE_UNLOADED;
        KLOG_D(TAG, "assets: file=\"%s\" waits: storage is busy", name);
        return;
    }
    if ( KODE_OK != rc )
    {
        khal_free(blk);
        s_a.rt[f].state = FILE_DEAD;
        if ( KODE_NO_RESOURCES == rc )
        {
            job_say(true, "file=\"%s\" result=refused why=size", name);
        }
        else
        {
            job_say(true, "file=\"%s\" result=missing", name);
        }
        if ( (KODE_ERROR == rc) && (KUI_PACK_SRC_SD == s_a.src) )
        {
            /* A card file is not there. The card may be gone, or the pack
             * names a file it does not carry: a stat of the manifest tells
             * which, once the job is over (JOB_PROBE). */
            s_a.sd_probe = true;
        }
        return;
    }
    kui_pack_err_t const err = file_judge(&s_a.pack, f, blk, len, &s_a.rt[f].anim, &info);
    if ( KUI_PACK_OK == err )
    {
        s_a.rt[f].blk   = blk;
        s_a.rt[f].state = FILE_READY;
        KLOG_D(TAG, "assets: file=\"%s\" result=ok depth=%u", name, (unsigned)info.depth);
        if ( file_on_face(f) )
        {
            kgui_pack_fit_passed(&s_a.fit);
        }
    }
    else
    {
        khal_free(blk);
        s_a.rt[f].state = FILE_DEAD;
        job_say(true, "file=\"%s\" result=refused why=%s depth=%u", name,
                kui_pack_err_word(err), (unsigned)info.depth);
        if ( (KUI_PACK_E_CANVAS == err) && file_on_face(f) && file_is_wake(f)
            && kgui_pack_fit_wake_refused(&s_a.fit) )
        {
            pack_refuse_canvas();
        }
    }
}

/** @brief Free every block and record of the active pack. */
static void pack_retire(void)
{
    khal_free(s_a.try_blk);
    s_a.try_blk = NULL;
    if ( NULL != s_a.rt )
    {
        for ( uint16_t f = 0U; f < s_a.pack.n_files; ++f )
        {
            khal_free(s_a.rt[f].blk);
        }
        khal_free(s_a.rt);
        s_a.rt = NULL;
    }
    kui_pack_free(&s_a.pack);
}

/* ---- Resolution ------------------------------------------------------------------ */

/**
 * @brief Judge the manifest a gather brought as a candidate source.
 * @param rc  How the read ended.
 * @param src The source it came from.
 * @return true when it is usable: parsed and passed.
 */
static bool cand_take(kode_status_t rc, kui_pack_src_t src)
{
    bool usable = false;

    s_a.cand_busy = (KODE_DENIED == rc);

    if ( KODE_OK == rc )
    {
        uint32_t len = 0U;
        uint8_t *const blk = gather_take(&len);
        kui_pack_err_t const err = kui_pack_parse((const char *)blk, (size_t)len, &s_a.cand);

        khal_free(blk);
        if ( KUI_PACK_OK != err )
        {
            job_say(true, "source=%s result=refused why=%s", src_word((uint32_t)src),
                    kui_pack_err_word(err));
        }
        else
        {
            s_a.cand_src = src;
            usable = true;
        }
    }
    else if ( KODE_NO_RESOURCES == rc )
    {
        job_say(true, "source=%s result=refused why=size", src_word((uint32_t)src));
    }
    else if ( KODE_DENIED == rc )
    {
        KLOG_N(TAG, "assets: the %s is busy; the pack is read again shortly",
               src_word((uint32_t)src));
    }
    else
    {
        KLOG_D(TAG, "assets: no pack on the %s", src_word((uint32_t)src));
    }
    return usable;
}

/**
 * @brief Whether a role has a player in this build.
 *
 * The writing and goodbye roles belonged to the Opening card's Kody, which is
 * gone: the pack still carries them (its format does not change), and nothing
 * reads them, so their files are not loaded.
 *
 * @param role The role, an index of the pack's role table.
 * @return true for the roles something plays.
 */
static bool role_wanted(uint16_t role)
{
    return ((uint16_t)KUI_PACK_ROLE_EMPTY == role) || ((uint16_t)KUI_PACK_ROLE_LOADING == role);
}

/**
 * @brief Read the next role's file of the active pack, or finish.
 * @return true when a read started; false when every role is settled.
 */
static bool role_next(void)
{
    bool started = false;

    while ( (!started) && ((uint16_t)KUI_PACK_ROLE_COUNT > s_a.i) )
    {
        uint16_t const f = s_a.pack.role[s_a.i];
        char path[ASSETS_PATH_MAX];

        if ( role_wanted(s_a.i) && (KUI_PACK_NONE != f) && (FILE_UNLOADED == s_a.rt[f].state)
            && path_of(path, src_dir(s_a.src), s_a.pack.files[f].name) )
        {
            s_a.rt[f].state = FILE_LOADING;
            gather(false, path, KUI_PACK_FILE_MAX);
            started = true;
        }
        else
        {
            s_a.i++;
        }
    }
    return started;
}

/** @brief Make the candidate the active pack. */
static void activate(void)
{
    if ( NULL != s_a.notify )
    {
        s_a.notify(KGUI_ASSETS_EV_RETIRE, 0U);
    }
    pack_retire();
    s_a.src = s_a.cand_src;
    if ( KUI_PACK_SRC_BUILTIN != s_a.src )
    {
        s_a.pack = s_a.cand;
        (void)memset(&s_a.cand, 0, sizeof(s_a.cand));
        /* cppcheck-suppress misra-c2012-11.5 ; khal_alloc_psram() answers void *: PSRAM is taken by name (rule 8) */
        s_a.rt = (file_rt_t *)khal_alloc_psram(sizeof(uint32_t),
                                               (size_t)s_a.pack.n_files * sizeof(file_rt_t));
        if ( NULL == s_a.rt )
        {
            KLOG_E(TAG, "assets: no memory for the pack's %u files; the built-in scene plays",
                   (unsigned)s_a.pack.n_files);
            kui_pack_free(&s_a.pack);
            s_a.src = KUI_PACK_SRC_BUILTIN;
        }
    }
    kui_pack_free(&s_a.cand);
    s_a.n_want        = 0U;
    s_a.scene_waiting = KUI_PACK_NONE;
    s_a.react_scene   = KUI_PACK_NONE;
    s_a.react_file    = KUI_PACK_NONE;
    kgui_pack_fit_reset(&s_a.fit);
}

/** @brief The resolution is over: say so. */
static void resolve_done(void)
{
    s_a.ready = true;
    say_source(true);
    if ( NULL != s_a.notify )
    {
        s_a.notify(KGUI_ASSETS_EV_RESOLVED, 0U);
    }
    job_end();
}

/**
 * @brief The resolution's steps (spec section 6.1).
 * @param rc     How the last operation ended.
 * @param exists Unused here.
 */
static void resolve_step(kode_status_t rc, bool exists)
{
    (void)exists;
    switch ( s_a.step )
    {
        case 0U: {
            s_a.step = 1U;
            gather(false, ASSETS_SD_DIR "/" ASSETS_MANIFEST, KUI_PACK_MANIFEST_MAX);
            break;
        }
        case 1U: {
            if ( cand_take(rc, KUI_PACK_SRC_SD) )
            {
                s_a.step = 3U;
            }
            else if ( s_a.cand_busy && s_a.ready )
            {
                /* Busy is not absent: the active pack stays, read again later. */
                resolve_later();
                job_end();
            }
            else
            {
                s_a.step = 2U;
                gather(false, ASSETS_FS_DIR "/" ASSETS_MANIFEST, KUI_PACK_MANIFEST_MAX);
            }
            break;
        }
        case 2U: {
            if ( cand_take(rc, KUI_PACK_SRC_FS) )
            {
                s_a.step = 3U;
            }
            else if ( s_a.cand_busy )
            {
                /* Busy is not absent. Before the first resolution the face
                 * has the built-in scene meanwhile; after it, the active pack
                 * stays. Either way the pack is read again later. */
                resolve_later();
                if ( s_a.ready )
                {
                    job_end();
                }
                else
                {
                    s_a.cand_src = KUI_PACK_SRC_BUILTIN;
                    s_a.step = 3U;
                }
            }
            else
            {
                s_a.cand_src = KUI_PACK_SRC_BUILTIN;
                s_a.step = 3U;
            }
            break;
        }
        case 4U: {
            /* A role's file answered. */
            uint32_t len = 0U;
            uint8_t *const blk = gather_take(&len);

            file_settle(s_a.pack.role[s_a.i], rc, blk, len);
            s_a.i++;
            if ( !role_next() )
            {
                resolve_done();
            }
            break;
        }
        default: {
            break;
        }
    }
    if ( 3U == s_a.step )
    {
        s_a.step = 4U;
        activate();
        s_a.i = 0U;
        if ( (KUI_PACK_SRC_BUILTIN == s_a.src) || (!role_next()) )
        {
            resolve_done();
        }
    }
}

/* ---- Loads the face asks for ------------------------------------------------------ */

/**
 * @brief Queue a face file of the active pack for reading.
 * @param f Its index.
 */
static void want_file(uint16_t f)
{
    bool queued = false;

    for ( uint8_t i = 0U; i < s_a.n_want; ++i )
    {
        if ( s_a.want[i] == f )
        {
            queued = true;
        }
    }
    if ( (!queued) && (FILE_UNLOADED == s_a.rt[f].state) && (ASSETS_WANT_MAX > s_a.n_want) )
    {
        s_a.want[s_a.n_want] = f;
        s_a.n_want++;
        s_a.rt[f].state = FILE_LOADING;
    }
}

/**
 * @brief Withdraw the queued loads that scene @p sc does not use.
 *
 * The queue is short and the face shows one scene: a swipe through every
 * scene while the reader is busy would fill it with scenes already left, and
 * the one now chosen could not queue its pair.
 *
 * @param sc The scene the face is waiting for.
 */
static void want_only(uint16_t sc)
{
    kui_pack_scene_t const * const scene = &s_a.pack.scenes[sc];
    uint8_t kept = 0U;

    for ( uint8_t i = 0U; i < s_a.n_want; ++i )
    {
        uint16_t const f = s_a.want[i];
        bool uses = (f == scene->wake) || (f == scene->loop);

        for ( uint32_t t = 0U; (!uses) && ((uint32_t)KUI_PACK_TRIGGER_COUNT > t); ++t )
        {
            for ( uint16_t k = 0U; (!uses) && (k < scene->on_count[t]); ++k )
            {
                uses = (s_a.pack.react[scene->on_first[t] + k] == f);
            }
        }
        if ( uses )
        {
            s_a.want[kept] = f;
            kept++;
        }
        else
        {
            s_a.rt[f].state = FILE_UNLOADED;   /* asked again when it is seen */
        }
    }
    s_a.n_want = kept;
}

/**
 * @brief Withdraw a dead scene's files from the queue.
 * @param sc The scene.
 */
static void want_drop_scene(uint16_t sc)
{
    kui_pack_scene_t const * const scene = &s_a.pack.scenes[sc];
    uint8_t kept = 0U;

    for ( uint8_t i = 0U; i < s_a.n_want; ++i )
    {
        uint16_t const f = s_a.want[i];

        if ( (f == scene->wake) || (f == scene->loop) )
        {
            s_a.rt[f].state = FILE_UNLOADED;   /* asked again if it is ever seen */
        }
        else
        {
            s_a.want[kept] = f;
            kept++;
        }
    }
    s_a.n_want = kept;
}

/** @brief Start reading the first queued face file. */
static void load_next(void)
{
    uint16_t const f = s_a.want[0];
    char path[ASSETS_PATH_MAX];

    for ( uint8_t i = 1U; i < s_a.n_want; ++i )
    {
        s_a.want[i - 1U] = s_a.want[i];
    }
    s_a.n_want--;
    s_a.job       = JOB_LOAD;
    s_a.step      = 0U;
    s_a.load_file = f;
    if ( path_of(path, src_dir(s_a.src), s_a.pack.files[f].name) )
    {
        gather(false, path, KUI_PACK_FILE_MAX);
    }
    else
    {
        s_a.rt[f].state = FILE_DEAD;
        job_end();
    }
}

/**
 * @brief Whether a scene's wake-up and loop are settled: both ready, or one dead.
 *
 * One dead is enough: the scene cannot play, and its other file is not worth the
 * read, least of all at boot with the glass black.
 *
 * @param s A scene of the active pack.
 * @return true when nothing it could still play with is waiting for a read.
 */
static bool scene_settled(uint16_t s)
{
    file_state_t const w = s_a.rt[s_a.pack.scenes[s].wake].state;
    file_state_t const l = s_a.rt[s_a.pack.scenes[s].loop].state;

    return (FILE_DEAD == w) || (FILE_DEAD == l)
           || ((FILE_READY == w) && (FILE_READY == l));
}

/**
 * @brief A queued face file answered.
 * @param rc How the read ended.
 */
static void load_step(kode_status_t rc)
{
    uint16_t const f = s_a.load_file;
    uint32_t len = 0U;
    uint8_t *const blk = gather_take(&len);

    file_settle(f, rc, blk, len);
    job_end();
    if ( (FILE_UNLOADED == s_a.rt[f].state)
        && ((s_a.react_file == f) || ((KUI_PACK_NONE != s_a.scene_waiting)
            && ((s_a.pack.scenes[s_a.scene_waiting].wake == f)
                || (s_a.pack.scenes[s_a.scene_waiting].loop == f)))) )
    {
        /* Storage was busy: still wanted, so asked again. */
        want_file(f);
    }
    if ( (KUI_PACK_NONE != s_a.scene_waiting) && scene_settled(s_a.scene_waiting) )
    {
        uint16_t const s = s_a.scene_waiting;

        s_a.scene_waiting = KUI_PACK_NONE;
        if ( kgui_assets_scene_dead(s) )
        {
            /* Its other file may still be queued: what plays it is gone. */
            want_drop_scene(s);
        }
        if ( NULL != s_a.notify )
        {
            s_a.notify(KGUI_ASSETS_EV_SCENE, s);
        }
    }
    if ( (s_a.react_file == f) && (NULL != s_a.notify) )
    {
        s_a.notify(KGUI_ASSETS_EV_REACTION, s_a.react_scene);
    }
}

/* ---- Installs: stage, then commit ---------------------------------------------------- */

/**
 * @brief The name of file @p i of the staged pack, its manifest last.
 * @param i 0..cand.n_files-1 for its files, cand.n_files for kode.json.
 * @return The name.
 */
static const char *stage_name(uint16_t i)
{
    const char *name = ASSETS_MANIFEST;

    if ( i < s_a.cand.n_files )
    {
        name = s_a.cand.files[i].name;
    }
    return name;
}

/**
 * @brief An install ended: say so, and let AO_Gui know.
 * @param why "" for success, otherwise the word it failed for.
 */
static void install_end(const char *why)
{
    (void)snprintf(s_a.why, sizeof(s_a.why), "%s", why);
    s_a.installing = false;
    khal_free(s_a.wblk);
    s_a.wblk = NULL;
    kui_pack_free(&s_a.cand);
    kui_pack_free(&s_a.old);
    if ( NULL != s_a.notify )
    {
        s_a.notify(KGUI_ASSETS_EV_INSTALL, 0U);
    }
}

/**
 * @brief The commit failed: the active pack is untouched.
 * @param why The word.
 */
static void commit_fail(const char *why)
{
    if ( s_a.recovering )
    {
        KLOG_W(TAG, "assets: the interrupted install could not be finished, why=%s", why);
    }
    else
    {
        job_refused("commit", why);
    }
    s_a.recovering = false;
    install_end(why);
    job_end();
    resolve_soon();
}

/**
 * @brief Read the next staged file to check it, or write `.ok` when all are.
 */
static void commit_check_next(void)
{
    if ( s_a.i < s_a.cand.n_files )
    {
        char path[ASSETS_PATH_MAX];

        if ( path_of(path, ASSETS_NEW_DIR, s_a.cand.files[s_a.i].name) )
        {
            s_a.step = 2U;
            gather(false, path, KUI_PACK_FILE_MAX);
        }
        else
        {
            commit_fail("file");
        }
    }
    else
    {
        s_a.step = 3U;
        text_op(ASSETS_NEW_DIR "/" ASSETS_OK_FILE, "ok");
    }
}

/** @brief Remove the next file the internal FAT's old manifest names, then the manifest. */
static void commit_old_next(void)
{
    char path[ASSETS_PATH_MAX];
    bool asked = false;

    /* A name with no path to it (never, under the 40-character rule) is
     * skipped in place. */
    while ( (!asked) && (s_a.i < s_a.old.n_files) )
    {
        if ( path_of(path, ASSETS_FS_DIR, s_a.old.files[s_a.i].name) )
        {
            s_a.step = 6U;
            path_op((uint8_t)STORAGE_PATH_UNLINK, path);
            asked = true;
        }
        else
        {
            s_a.i++;
        }
    }
    if ( !asked )
    {
        s_a.step = 7U;
        path_op((uint8_t)STORAGE_PATH_UNLINK, ASSETS_FS_DIR "/" ASSETS_MANIFEST);
    }
}

/** @brief Ask whether the next staged file is still to move, or finish. */
static void commit_move_next(void)
{
    if ( s_a.i <= s_a.cand.n_files )
    {
        char path[ASSETS_PATH_MAX];

        s_a.step = 9U;
        (void)path_of(path, ASSETS_NEW_DIR, stage_name(s_a.i));
        path_op((uint8_t)STORAGE_PATH_STAT, path);
    }
    else
    {
        s_a.step = 12U;
        path_op((uint8_t)STORAGE_PATH_UNLINK, ASSETS_NEW_DIR "/" ASSETS_OK_FILE);
    }
}

/**
 * @brief The commit's steps (spec section 8.1), and the boot's recovery.
 * @param rc     How the last operation ended.
 * @param exists What a stat said.
 */
static void commit_step(kode_status_t rc, bool exists)
{
    switch ( s_a.step )
    {
        case 0U: {
            if ( s_a.recovering )
            {
                s_a.step = 100U;
                path_op((uint8_t)STORAGE_PATH_STAT, ASSETS_NEW_DIR "/" ASSETS_OK_FILE);
            }
            else
            {
                s_a.installing = true;
                s_a.why[0]     = '\0';
                s_a.inst_done  = 0U;
                s_a.inst_of    = 0U;
                s_a.step       = 1U;
                gather(false, ASSETS_NEW_DIR "/" ASSETS_MANIFEST, KUI_PACK_MANIFEST_MAX);
            }
            break;
        }
        case 100U: {
            if ( (KODE_OK != rc) || (!exists) )
            {
                /* Nothing was interrupted. */
                s_a.recovering = false;
                job_end();
                job_start(JOB_RESOLVE);
            }
            else
            {
                KLOG_N(TAG, "assets: finishing an interrupted install");
                s_a.step = 1U;
                gather(false, ASSETS_NEW_DIR "/" ASSETS_MANIFEST, KUI_PACK_MANIFEST_MAX);
            }
            break;
        }
        case 1U: {
            /* The staged manifest. */
            uint32_t len = 0U;
            uint8_t *const blk = gather_take(&len);

            if ( KODE_OK != rc )
            {
                khal_free(blk);
                if ( s_a.recovering && (KODE_ERROR == rc) )
                {
                    /* Moved already: only the `.ok` is left. */
                    s_a.i = s_a.cand.n_files + 1U;
                    commit_move_next();
                }
                else if ( (KODE_DENIED == rc) || (KODE_TIMEOUT == rc) )
                {
                    /* The reader is busy or slow, which says nothing about the
                     * file: stop, and keep `.ok` for the next try. */
                    commit_fail("busy");
                }
                else if ( (KODE_NO_RESOURCES == rc) && (KUI_PACK_MANIFEST_MAX < s_a.g_size) )
                {
                    commit_fail("size");
                }
                else
                {
                    commit_fail("missing");
                }
                break;
            }
            kui_pack_err_t const err = kui_pack_parse((const char *)blk, (size_t)len, &s_a.cand);
            khal_free(blk);
            if ( KUI_PACK_OK != err )
            {
                commit_fail(kui_pack_err_word(err));
            }
            else if ( s_a.recovering )
            {
                /* Checked before the `.ok` was written: straight to the moves. */
                s_a.step = 4U;
                path_op((uint8_t)STORAGE_PATH_MKDIR, ASSETS_FS_DIR);
            }
            else if ( !kui_pack_hashed(&s_a.cand) )
            {
                commit_fail("hash");
            }
            else
            {
                s_a.inst_of = s_a.cand.n_files;
                s_a.i       = 0U;
                commit_check_next();
            }
            break;
        }
        case 2U: {
            /* One staged file, read whole: its digest, then the checks. */
            uint32_t len = 0U;
            uint8_t *const blk = gather_take(&len);
            const char *const name = s_a.cand.files[s_a.i].name;

            if ( KODE_OK != rc )
            {
                khal_free(blk);
                job_say(true, "file=\"%s\" result=missing", name);
                commit_fail("missing");
                break;
            }
            uint8_t sha[KFW_SHA_LEN];
            kfw_sha256(blk, (size_t)len, sha);
            if ( memcmp(sha, s_a.cand.files[s_a.i].sha256, sizeof(sha)) != 0 )
            {
                khal_free(blk);
                job_say(true, "file=\"%s\" result=bad_hash", name);
                commit_fail("hash");
                break;
            }
            kui_kody_anim_t anim;
            kui_pack_anim_t info;
            kui_pack_err_t const err = file_judge(&s_a.cand, s_a.i, blk, len, &anim, &info);
            khal_free(blk);
            if ( KUI_PACK_OK != err )
            {
                job_say(true, "file=\"%s\" result=refused why=%s depth=%u", name,
                        kui_pack_err_word(err), (unsigned)info.depth);
                commit_fail(kui_pack_err_word(err));
                break;
            }
            job_say(false, "file=\"%s\" result=ok", name);
            s_a.inst_done++;
            if ( NULL != s_a.notify )
            {
                s_a.notify(KGUI_ASSETS_EV_INSTALL, 0U);
            }
            s_a.i++;
            commit_check_next();
            break;
        }
        case 3U: {
            if ( KODE_OK != rc )
            {
                commit_fail("write");
            }
            else
            {
                s_a.step = 4U;
                path_op((uint8_t)STORAGE_PATH_MKDIR, ASSETS_FS_DIR);
            }
            break;
        }
        case 4U: {
            /* An existing folder answers an error too: either way it is there. */
            s_a.step = 5U;
            gather(false, ASSETS_FS_DIR "/" ASSETS_MANIFEST, KUI_PACK_MANIFEST_MAX);
            break;
        }
        case 5U: {
            uint32_t len = 0U;
            uint8_t *const blk = gather_take(&len);

            if ( KODE_OK == rc )
            {
                if ( kui_pack_parse((const char *)blk, (size_t)len, &s_a.old) != KUI_PACK_OK )
                {
                    KLOG_D(TAG, "assets: the FAT's old manifest does not read; its files stay");
                }
            }
            khal_free(blk);
            s_a.i = 0U;
            commit_old_next();
            break;
        }
        case 6U: {
            s_a.i++;
            commit_old_next();
            break;
        }
        case 7U: {
            kui_pack_free(&s_a.old);
            s_a.i = 0U;
            commit_move_next();
            break;
        }
        case 9U: {
            if ( KODE_OK != rc )
            {
                /* The stat was refused, so where the file is is not known:
                 * stop with `.ok` kept, and the next boot finishes the move. */
                commit_fail("busy");
            }
            else if ( exists )
            {
                char to[ASSETS_PATH_MAX];

                s_a.step = 10U;
                (void)path_of(to, ASSETS_FS_DIR, stage_name(s_a.i));
                path_op((uint8_t)STORAGE_PATH_UNLINK, to);
            }
            else
            {
                /* Moved already, by the run a power cut stopped. */
                s_a.i++;
                commit_move_next();
            }
            break;
        }
        case 10U: {
            char from[ASSETS_PATH_MAX];
            char to[ASSETS_PATH_MAX];

            s_a.step = 11U;
            (void)path_of(from, ASSETS_NEW_DIR, stage_name(s_a.i));
            (void)path_of(to, ASSETS_FS_DIR, stage_name(s_a.i));
            pair_op((uint8_t)STORAGE_PAIR_MOVE, from, to);
            break;
        }
        case 11U: {
            if ( KODE_OK != rc )
            {
                /* The `.ok` stays: the next boot finishes the moves. */
                commit_fail("move");
            }
            else
            {
                s_a.i++;
                commit_move_next();
            }
            break;
        }
        case 12U: {
            if ( s_a.recovering )
            {
                KLOG_N(TAG, "assets: the interrupted install is finished");
            }
            else
            {
                job_state("op=commit result=done");
            }
            s_a.recovering = false;
            install_end("");
            job_end();
            resolve_soon();
            break;
        }
        default: {
            break;
        }
    }
}

/**
 * @brief The begin's steps: the staging folder and the room.
 * @param rc     How the last operation ended.
 * @param exists For the room: whether the FAT's usage is known.
 */
static void begin_step(kode_status_t rc, bool exists)
{
    (void)rc;
    switch ( s_a.step )
    {
        case 0U: {
            s_a.step = 1U;
            path_op((uint8_t)STORAGE_PATH_MKDIR, ASSETS_NEW_DIR);
            break;
        }
        case 1U: {
            s_a.step = 2U;
            path_op((uint8_t)STORAGE_PATH_UNLINK, ASSETS_NEW_DIR "/" ASSETS_OK_FILE);
            break;
        }
        case 2U: {
            s_a.step = 3U;
            info_op();
            break;
        }
        case 3U: {
            if ( exists && (((uint64_t)s_a.fs_free_kb * 1024U) >= (uint64_t)s_a.begin_bytes) )
            {
                job_say(false, "op=begin result=ready");
            }
            else
            {
                job_refused("begin", "room");
            }
            job_end();
            break;
        }
        default: {
            break;
        }
    }
}

/**
 * @brief The update failed: nothing was activated.
 * @param why The word.
 */
static void update_fail(const char *why)
{
    job_refused("update", why);
    install_end(why);
    job_end();
}

/** @brief Stage the next file of the downloaded manifest: kept, or fetched. */
static void update_next(void)
{
    char from[ASSETS_PATH_MAX];
    char to[ASSETS_PATH_MAX];

    if ( s_a.i >= s_a.cand.n_files )
    {
        /* All staged: the commit judges everything again, from the folder. */
        kui_pack_free(&s_a.cand);
        kui_pack_free(&s_a.old);
        s_a.recovering = false;
        job_start(JOB_COMMIT);
        return;
    }
    const kui_pack_file_t *const nf = &s_a.cand.files[s_a.i];
    uint16_t const of = kui_pack_file_find(&s_a.old, nf->name);

    if ( (KUI_PACK_NONE != of) && s_a.old.files[of].hashed
        && (memcmp(s_a.old.files[of].sha256, nf->sha256, KUI_PACK_SHA256_LEN) == 0)
        && path_of(from, ASSETS_FS_DIR, nf->name) && path_of(to, ASSETS_NEW_DIR, nf->name) )
    {
        s_a.step = 7U;
        pair_op((uint8_t)STORAGE_PAIR_COPY, from, to);
    }
    else
    {
        char url[KODE_FLASH_SRC_MAX];
        int const n = snprintf(url, sizeof(url), "%s%s", s_a.base, nf->name);

        if ( (0 >= n) || ((size_t)n >= sizeof(url)) )
        {
            update_fail("url");
        }
        else
        {
            s_a.step = 8U;
            gather(true, url, KUI_PACK_FILE_MAX);
        }
    }
}

/**
 * @brief The update's steps (spec section 8.2).
 * @param rc     How the last operation ended.
 * @param exists For the room: whether the FAT's usage is known.
 */
static void update_step(kode_status_t rc, bool exists)
{
    char path[ASSETS_PATH_MAX];

    switch ( s_a.step )
    {
        case 0U: {
            s_a.installing = true;
            s_a.why[0]     = '\0';
            s_a.inst_done  = 0U;
            s_a.inst_of    = 0U;
            s_a.step       = 1U;
            path_op((uint8_t)STORAGE_PATH_MKDIR, ASSETS_NEW_DIR);
            break;
        }
        case 1U: {
            s_a.step = 2U;
            path_op((uint8_t)STORAGE_PATH_UNLINK, ASSETS_NEW_DIR "/" ASSETS_OK_FILE);
            break;
        }
        case 2U: {
            s_a.step = 3U;
            info_op();
            break;
        }
        case 3U: {
            if ( (!exists) || (ASSETS_ROOM_MIN_KB > s_a.fs_free_kb) )
            {
                update_fail("room");
            }
            else
            {
                s_a.step = 4U;
                gather(true, s_a.url, KUI_PACK_MANIFEST_MAX);
            }
            break;
        }
        case 4U: {
            uint32_t len = 0U;
            uint8_t *const blk = gather_take(&len);

            if ( KODE_OK != rc )
            {
                khal_free(blk);
                if ( (KODE_NO_RESOURCES == rc) && (KUI_PACK_MANIFEST_MAX < s_a.g_size) )
                {
                    update_fail("size");
                }
                else
                {
                    update_fail("download");
                }
                break;
            }
            kui_pack_err_t const err = kui_pack_parse((const char *)blk, (size_t)len, &s_a.cand);
            if ( KUI_PACK_OK != err )
            {
                khal_free(blk);
                update_fail(kui_pack_err_word(err));
            }
            else if ( !kui_pack_hashed(&s_a.cand) )
            {
                khal_free(blk);
                update_fail("hash");
            }
            else
            {
                s_a.inst_of = s_a.cand.n_files;
                s_a.step    = 5U;
                assets_write_op(ASSETS_NEW_DIR "/" ASSETS_MANIFEST, blk, len);
            }
            break;
        }
        case 5U: {
            khal_free(s_a.wblk);
            s_a.wblk = NULL;
            if ( KODE_OK != rc )
            {
                update_fail("write");
            }
            else
            {
                s_a.step = 6U;
                gather(false, ASSETS_FS_DIR "/" ASSETS_MANIFEST, KUI_PACK_MANIFEST_MAX);
            }
            break;
        }
        case 6U: {
            uint32_t len = 0U;
            uint8_t *const blk = gather_take(&len);

            if ( KODE_OK == rc )
            {
                (void)kui_pack_parse((const char *)blk, (size_t)len, &s_a.old);
            }
            khal_free(blk);
            s_a.i = 0U;
            update_next();
            break;
        }
        case 7U: {
            /* A local copy of a file whose digest is unchanged. */
            if ( KODE_OK == rc )
            {
                job_say(false, "file=\"%s\" result=kept", s_a.cand.files[s_a.i].name);
                s_a.inst_done++;
                if ( NULL != s_a.notify )
                {
                    s_a.notify(KGUI_ASSETS_EV_INSTALL, 0U);
                }
                s_a.i++;
                update_next();
            }
            else
            {
                char url[KODE_FLASH_SRC_MAX];
                int const n = snprintf(url, sizeof(url), "%s%s", s_a.base,
                                       s_a.cand.files[s_a.i].name);

                if ( (0 >= n) || ((size_t)n >= sizeof(url)) )
                {
                    update_fail("url");
                }
                else
                {
                    s_a.step = 8U;
                    gather(true, url, KUI_PACK_FILE_MAX);
                }
            }
            break;
        }
        case 8U: {
            uint32_t len = 0U;
            uint8_t *const blk = gather_take(&len);
            const kui_pack_file_t *const nf = &s_a.cand.files[s_a.i];
            uint8_t sha[KFW_SHA_LEN];

            if ( KODE_OK != rc )
            {
                khal_free(blk);
                update_fail("download");
                break;
            }
            kfw_sha256(blk, (size_t)len, sha);
            if ( memcmp(sha, nf->sha256, sizeof(sha)) != 0 )
            {
                khal_free(blk);
                job_say(true, "file=\"%s\" result=bad_hash", nf->name);
                update_fail("hash");
            }
            else if ( !path_of(path, ASSETS_NEW_DIR, nf->name) )
            {
                khal_free(blk);
                update_fail("file");
            }
            else
            {
                s_a.step = 9U;
                assets_write_op(path, blk, len);
            }
            break;
        }
        case 9U: {
            khal_free(s_a.wblk);
            s_a.wblk = NULL;
            if ( KODE_OK != rc )
            {
                update_fail("write");
            }
            else
            {
                job_say(false, "file=\"%s\" result=fetched", s_a.cand.files[s_a.i].name);
                s_a.inst_done++;
                if ( NULL != s_a.notify )
                {
                    s_a.notify(KGUI_ASSETS_EV_INSTALL, 0U);
                }
                s_a.i++;
                update_next();
            }
            break;
        }
        default: {
            break;
        }
    }
}

/* ---- assets_check and assets_try ------------------------------------------------- */

/** @brief Read the next file of the pack being checked, or close the check. */
static void check_next(void)
{
    char path[ASSETS_PATH_MAX];
    bool asked = false;

    while ( (!asked) && (s_a.i < s_a.cand.n_files) )
    {
        if ( path_of(path, src_dir(s_a.chk_src), s_a.cand.files[s_a.i].name) )
        {
            s_a.step = 2U;
            gather(false, path, KUI_PACK_FILE_MAX);
            asked = true;
        }
        else
        {
            s_a.chk_bad++;
            s_a.i++;
        }
    }
    if ( !asked )
    {
        job_say(false, "op=check result=done ok=%u refused=%u", (unsigned)s_a.chk_ok,
                (unsigned)s_a.chk_bad);
        kui_pack_free(&s_a.cand);
        job_end();
    }
}

/**
 * @brief assets_check's steps: the manifest, then every file it names.
 * @param rc How the last operation ended.
 */
static void check_step(kode_status_t rc)
{
    switch ( s_a.step )
    {
        case 0U: {
            char path[ASSETS_PATH_MAX];

            s_a.chk_ok  = 0U;
            s_a.chk_bad = 0U;
            s_a.step    = 1U;
            (void)path_of(path, src_dir(s_a.chk_src), ASSETS_MANIFEST);
            gather(false, path, KUI_PACK_MANIFEST_MAX);
            break;
        }
        case 1U: {
            uint32_t len = 0U;
            uint8_t *const blk = gather_take(&len);
            kui_pack_err_t err = KUI_PACK_E_FILE;

            if ( KODE_OK == rc )
            {
                err = kui_pack_parse((const char *)blk, (size_t)len, &s_a.cand);
            }
            else if ( (KODE_NO_RESOURCES == rc) && (KUI_PACK_MANIFEST_MAX < s_a.g_size) )
            {
                err = KUI_PACK_E_SIZE;
            }
            else
            {
                err = KUI_PACK_E_FILE;
            }
            khal_free(blk);
            if ( KUI_PACK_OK != err )
            {
                job_refused("check", kui_pack_err_word(err));
                job_end();
            }
            else
            {
                s_a.i = 0U;
                check_next();
            }
            break;
        }
        case 2U: {
            uint32_t len = 0U;
            uint8_t *const blk = gather_take(&len);
            const kui_pack_file_t *const f = &s_a.cand.files[s_a.i];
            kui_kody_anim_t anim;
            kui_pack_anim_t info;

            (void)memset(&info, 0, sizeof(info));
            if ( KODE_OK != rc )
            {
                job_say(true, "file=\"%s\" result=missing", f->name);
                s_a.chk_bad++;
            }
            else
            {
                bool bad_hash = false;

                if ( f->hashed )
                {
                    uint8_t sha[KFW_SHA_LEN];

                    kfw_sha256(blk, (size_t)len, sha);
                    bad_hash = (memcmp(sha, f->sha256, sizeof(sha)) != 0);
                }
                if ( bad_hash )
                {
                    job_say(true, "file=\"%s\" result=bad_hash", f->name);
                    s_a.chk_bad++;
                }
                else
                {
                    kui_pack_err_t const err = file_judge(&s_a.cand, s_a.i, blk, len, &anim, &info);

                    if ( KUI_PACK_OK == err )
                    {
                        job_say(false, "file=\"%s\" result=ok depth=%u", f->name,
                                (unsigned)info.depth);
                        s_a.chk_ok++;
                    }
                    else
                    {
                        job_say(true, "file=\"%s\" result=refused why=%s depth=%u",
                                f->name, kui_pack_err_word(err), (unsigned)info.depth);
                        s_a.chk_bad++;
                    }
                }
            }
            khal_free(blk);
            s_a.i++;
            check_next();
            break;
        }
        default: {
            break;
        }
    }
}

/**
 * @brief assets_try's steps: read the file, check it for the face.
 * @param rc How the read ended.
 */
static void try_step(kode_status_t rc)
{
    if ( 0U == s_a.step )
    {
        char path[ASSETS_PATH_MAX];

        s_a.step = 1U;
        if ( path_of(path, src_dir(s_a.src), s_a.try_name) )
        {
            gather(false, path, KUI_PACK_FILE_MAX);
        }
        else
        {
            job_refused("try", "file");
            job_end();
            if ( NULL != s_a.notify )
            {
                s_a.notify(KGUI_ASSETS_EV_TRY, 0U);
            }
        }
        return;
    }
    uint32_t len = 0U;
    uint8_t *const blk = gather_take(&len);
    kui_pack_err_t err = KUI_PACK_E_FILE;
    kui_pack_anim_t info;
    const char *json = NULL;
    size_t jlen = 0U;
    uint8_t *scratch = NULL;

    (void)memset(&info, 0, sizeof(info));
    if ( KODE_OK == rc )
    {
        err = file_json(s_a.try_name, blk, len, &json, &jlen, &scratch);
        if ( KUI_PACK_OK == err )
        {
            err = kui_pack_check_anim(json, jlen, KUI_PACK_SCENE_W, KUI_PACK_SCENE_H, 0U, &info);
        }
        khal_free(scratch);
    }
    if ( KUI_PACK_OK == err )
    {
        khal_free(s_a.try_blk);
        s_a.try_blk = blk;
        (void)memset(&s_a.try_anim, 0, sizeof(s_a.try_anim));
        s_a.try_anim.zlib     = blk;
        s_a.try_anim.zlib_len = len;
        s_a.try_anim.json_len = (uint32_t)jlen;
        s_a.try_anim.w        = KUI_PACK_SCENE_W;
        s_a.try_anim.h        = KUI_PACK_SCENE_H;
        s_a.try_anim.frames   = info.frames;
        s_a.try_anim.fps      = info.fps;
        s_a.try_anim.anchor   = 0U;
        s_a.try_anim.deflated = ends_z(s_a.try_name);
        /* The face is asked before the answer is given: it may not be able
         * to play the file (kgui_assets_try_refuse()). */
        s_a.try_why[0] = '\0';
        if ( NULL != s_a.notify )
        {
            s_a.notify(KGUI_ASSETS_EV_TRY, 1U);
        }
        if ( '\0' != s_a.try_why[0] )
        {
            job_refused("try", s_a.try_why);
        }
        else
        {
            job_say(false, "op=try file=\"%s\" result=playing", s_a.try_name);
        }
    }
    else
    {
        khal_free(blk);
        job_say(true, "op=try result=refused why=%s depth=%u", kui_pack_err_word(err),
                (unsigned)info.depth);
        s_a.ans_rc = KODE_ERROR;
        if ( NULL != s_a.notify )
        {
            s_a.notify(KGUI_ASSETS_EV_TRY, 0U);
        }
    }
    job_end();
}

/**
 * @brief The card's probe: a card file was missing - is its manifest still
 *        there?
 *
 * Gone (the card came out, or its pack did): resolve again. There: the pack
 * names a file the card does not carry, which stays dead until the pack is
 * read again - no loop of resolutions over one missing file.
 *
 * @param rc     How the stat ended.
 * @param exists Whether the manifest is there.
 */
static void probe_step(kode_status_t rc, bool exists)
{
    if ( 0U == s_a.step )
    {
        s_a.step = 1U;
        path_op((uint8_t)STORAGE_PATH_STAT, ASSETS_SD_DIR "/" ASSETS_MANIFEST);
    }
    else
    {
        job_end();
        if ( (KODE_OK != rc) || (!exists) )
        {
            KLOG_N(TAG, "assets: the card's pack is gone; resolving again");
            resolve_soon();
        }
    }
}

/**
 * @brief Route the end of an operation to the job that asked for it.
 * @param rc     How it ended.
 * @param exists What a stat said, or whether a usage figure is known.
 */
static void job_step(kode_status_t rc, bool exists)
{
    switch ( s_a.job )
    {
        case JOB_RESOLVE: {
            resolve_step(rc, exists);
            break;
        }
        case JOB_LOAD: {
            load_step(rc);
            break;
        }
        case JOB_COMMIT: {
            commit_step(rc, exists);
            break;
        }
        case JOB_BEGIN: {
            begin_step(rc, exists);
            break;
        }
        case JOB_UPDATE: {
            update_step(rc, exists);
            break;
        }
        case JOB_CHECK: {
            check_step(rc);
            break;
        }
        case JOB_TRY: {
            try_step(rc);
            break;
        }
        case JOB_PROBE: {
            probe_step(rc, exists);
            break;
        }
        default: {
            /* A stray answer, or the end of a job already over. */
            break;
        }
    }
}

/* ---- The public half ----------------------------------------------------------------- */

void kgui_assets_init(QActive *ao, kgui_assets_notify_fn notify)
{
    (void)memset(&s_a, 0, sizeof(s_a));
    s_a.ao            = ao;
    s_a.notify        = notify;
    s_a.src           = KUI_PACK_SRC_BUILTIN;
    s_a.scene_waiting = KUI_PACK_NONE;
    s_a.react_scene   = KUI_PACK_NONE;
    s_a.react_file    = KUI_PACK_NONE;
    s_a.load_file     = KUI_PACK_NONE;
    for ( uint32_t r = 0U; (uint32_t)KUI_PACK_ROLE_COUNT > r; ++r )
    {
        s_a.pack.role[r] = KUI_PACK_NONE;
    }
}

void kgui_assets_start(void)
{
    /* An install a power cut stopped is finished first; then the source is
     * resolved (commit_step()). */
    s_a.recovering = true;
    job_start(JOB_COMMIT);
    job_run();
}

bool kgui_assets_on_event(QEvt const * const e)
{
    bool mine = false;

    switch ( e->sig )
    {
        case STORAGE_OPEN_RSP_SIG:
        case NET_STREAM_OPEN_RSP_SIG: {
            bool const net = ((QSignal)NET_STREAM_OPEN_RSP_SIG == e->sig);

            if ( (OP_GATHER == s_a.op) && (NULL == s_a.g_buf) && (s_a.g_net == net) )
            {
                gather_opened(Q_EVT_CAST(StorageOpenRspEvt));
                mine = true;
            }
            break;
        }
        case STORAGE_DATA_SIG:
        case NET_STREAM_DATA_SIG: {
            bool const net = ((QSignal)NET_STREAM_DATA_SIG == e->sig);

            if ( (OP_GATHER == s_a.op) && (NULL != s_a.g_buf) && (s_a.g_net == net) )
            {
                gather_data(Q_EVT_CAST(StorageDataEvt));
                mine = true;
            }
            break;
        }
        case STORAGE_PATH_RSP_SIG: {
            if ( 0U < s_a.p_stale )
            {
                /* The late answer of a path given up on. */
                --s_a.p_stale;
                mine = true;
            }
            else if ( (OP_GATHER == s_a.op) && s_a.g_probing )
            {
                s_a.g_probing = false;
                gather_probed(Q_EVT_CAST(StoragePathRspEvt));
                mine = true;
            }
            else if ( OP_PATH == s_a.op )
            {
                StoragePathRspEvt const * const r = Q_EVT_CAST(StoragePathRspEvt);

                s_a.op = OP_NONE;
                post_result((kode_status_t)r->rc, r->exists);
                mine = true;
            }
            else
            {
                /* Somebody else's answer. */
            }
            break;
        }
        case STORAGE_WRITE_RSP_SIG: {
            if ( 0U < s_a.w_stale )
            {
                /* The late answer of a write given up on: its block is free. */
                --s_a.w_stale;
                khal_free(s_a.w_orphan);
                s_a.w_orphan = NULL;
                mine = true;
            }
            else if ( OP_WRITE == s_a.op )
            {
                s_a.op = OP_NONE;
                post_result((kode_status_t)Q_EVT_CAST(StorageWriteRspEvt)->status, true);
                mine = true;
            }
            else
            {
                /* Somebody else's answer. */
            }
            break;
        }
        case STORAGE_INFO_SIG: {
            StorageInfoEvt const * const i = Q_EVT_CAST(StorageInfoEvt);

            if ( (uint8_t)KDRV_MEDIUM_FS == i->medium )
            {
                s_a.fs_free_kb = i->free_kb;
                if ( OP_INFO == s_a.op )
                {
                    s_a.op = OP_NONE;
                    post_result(KODE_OK, i->usage_known);
                }
            }
            break;
        }
        default: {
            break;
        }
    }
    job_run();
    return mine;
}

void kgui_assets_poll(uint32_t now_ms)
{
    if ( s_a.retry_armed )
    {
        uint32_t const since = now_ms - s_a.retry_at_ms;

        /* Due once the deadline has passed, wrap or not: a deadline ahead
         * reads as a difference past half the range. */
        if ( ASSETS_DUE_HALF > since )
        {
            s_a.retry_armed = false;
            s_a.op_t0_ms    = now_ms;
            storage_open_request(s_a.g_where, s_a.ao);
        }
    }
    else if ( (OP_NONE != s_a.op) && ((now_ms - s_a.op_t0_ms) > ASSETS_OP_TIMEOUT_MS) )
    {
        job_say(true, "result=timeout ms=%u", (unsigned)ASSETS_OP_TIMEOUT_MS);
        s_a.ans_rc = KODE_TIMEOUT;
        if ( OP_GATHER == s_a.op )
        {
            if ( s_a.g_probing && (UINT8_MAX > s_a.p_stale) )
            {
                ++s_a.p_stale;   /* the stat that asked why the open was refused */
            }
            gather_close();
            gather_fail(KODE_TIMEOUT);
        }
        else
        {
            if ( (OP_PATH == s_a.op) && (UINT8_MAX > s_a.p_stale) )
            {
                ++s_a.p_stale;
            }
            else if ( (OP_WRITE == s_a.op) && (UINT8_MAX > s_a.w_stale) )
            {
                /* AO_Storage may still be reading the block: it stays until
                 * that write answers. A second one at once is left to the
                 * heap. */
                ++s_a.w_stale;
                if ( NULL == s_a.w_orphan )
                {
                    s_a.w_orphan = s_a.wblk;
                }
                s_a.wblk = NULL;
            }
            else
            {
                /* OP_INFO: published, nobody's answer in particular. */
            }
            s_a.op = OP_NONE;
            post_result(KODE_TIMEOUT, false);
        }
    }
    else if ( (JOB_IDLE == s_a.job) && s_a.resolve_due
             && ((now_ms - s_a.resolve_at_ms) < ASSETS_DUE_HALF) )
    {
        s_a.resolve_due = false;
        job_start(JOB_RESOLVE);
    }
    else if ( (JOB_IDLE == s_a.job) && s_a.resolve_owed )
    {
        s_a.resolve_owed = false;
        job_start(JOB_RESOLVE);
    }
    else if ( (JOB_IDLE == s_a.job) && s_a.sd_probe )
    {
        s_a.sd_probe = false;
        job_start(JOB_PROBE);
    }
    else if ( (JOB_IDLE == s_a.job) && (0U < s_a.n_want) && (NULL != s_a.rt) )
    {
        load_next();
    }
    else
    {
        /* Nothing due. */
    }
    job_run();
}

bool kgui_assets_ready(void)
{
    return s_a.ready;
}

kui_pack_src_t kgui_assets_source(void)
{
    return s_a.src;
}

const char *kgui_assets_pack_name(void)
{
    return s_a.pack.name;
}

const char *kgui_assets_pack_version(void)
{
    return s_a.pack.version;
}

uint16_t kgui_assets_scene_count(void)
{
    return s_a.pack.n_scenes;
}

const char *kgui_assets_scene_name(uint16_t s)
{
    const char *name = "-";

    if ( KGUI_ASSETS_BUILTIN == s )
    {
        name = "reloj";
    }
    else if ( s < s_a.pack.n_scenes )
    {
        name = s_a.pack.scenes[s].name;
    }
    else
    {
        /* No such scene: "-", as name already says. */
    }
    return name;
}

uint16_t kgui_assets_scene_find(const char *name)
{
    uint16_t found = KUI_PACK_NONE;

    if ( KUI_PACK_SRC_BUILTIN == s_a.src )
    {
        if ( (NULL != name) && (strcmp(name, "reloj") == 0) )
        {
            found = KGUI_ASSETS_BUILTIN;
        }
    }
    else
    {
        found = kui_pack_scene_find(&s_a.pack, name);
    }
    return found;
}

uint16_t kgui_assets_scene_for_hour(int8_t hour)
{
    uint16_t s = KGUI_ASSETS_BUILTIN;

    if ( KUI_PACK_SRC_BUILTIN != s_a.src )
    {
        s = kui_pack_scene_for_hour(&s_a.pack, hour);
    }
    return s;
}

bool kgui_assets_scene_dead(uint16_t s)
{
    bool dead = false;

    if ( (KGUI_ASSETS_BUILTIN != s) && (s < s_a.pack.n_scenes) && (NULL != s_a.rt) )
    {
        dead = (FILE_DEAD == s_a.rt[s_a.pack.scenes[s].wake].state)
               || (FILE_DEAD == s_a.rt[s_a.pack.scenes[s].loop].state);
    }
    return dead;
}

bool kgui_assets_scene_load(uint16_t s)
{
    bool ready = true;

    if ( KGUI_ASSETS_BUILTIN != s )
    {
        if ( (KUI_PACK_SRC_BUILTIN == s_a.src) || (s >= s_a.pack.n_scenes) || (NULL == s_a.rt) )
        {
            ready = false;
        }
        else
        {
            uint16_t const w = s_a.pack.scenes[s].wake;
            uint16_t const l = s_a.pack.scenes[s].loop;

            ready = (FILE_READY == s_a.rt[w].state)
                    && (FILE_READY == s_a.rt[l].state);
            if ( (!ready) && (!kgui_assets_scene_dead(s)) )
            {
                want_only(s);
                want_file(w);
                want_file(l);
                s_a.scene_waiting = s;
            }
        }
    }
    return ready;
}

const kui_kody_anim_t *kgui_assets_wake(uint16_t s)
{
    const kui_kody_anim_t *a = &kody_despierta_reloj;

    if ( (KGUI_ASSETS_BUILTIN != s) && (s < s_a.pack.n_scenes) && (NULL != s_a.rt) )
    {
        uint16_t const f = s_a.pack.scenes[s].wake;

        if ( FILE_READY == s_a.rt[f].state )
        {
            a = &s_a.rt[f].anim;
        }
    }
    return a;
}

const kui_kody_anim_t *kgui_assets_loop(uint16_t s)
{
    const kui_kody_anim_t *a = &kody_reloj;

    if ( (KGUI_ASSETS_BUILTIN != s) && (s < s_a.pack.n_scenes) && (NULL != s_a.rt) )
    {
        uint16_t const f = s_a.pack.scenes[s].loop;

        if ( FILE_READY == s_a.rt[f].state )
        {
            a = &s_a.rt[f].anim;
        }
    }
    return a;
}

bool kgui_assets_has_trigger(uint16_t s, kui_pack_trigger_t t)
{
    bool has = false;

    if ( KGUI_ASSETS_BUILTIN == s )
    {
        has = (KUI_PACK_TRIGGER_LOOK == t);
    }
    else if ( (s < s_a.pack.n_scenes) && ((uint32_t)t < (uint32_t)KUI_PACK_TRIGGER_COUNT) )
    {
        has = (0U < s_a.pack.scenes[s].anchor) && (0U < s_a.pack.scenes[s].on_count[t]);
    }
    else
    {
        has = false;
    }
    return has;
}

const kui_kody_anim_t *kgui_assets_react(uint16_t s, kui_pack_trigger_t t)
{
    const kui_kody_anim_t *a = NULL;

    if ( KGUI_ASSETS_BUILTIN == s )
    {
        if ( KUI_PACK_TRIGGER_LOOK == t )
        {
            a = &kody_reloj_te_mira;
        }
    }
    else if ( kgui_assets_has_trigger(s, t) && (NULL != s_a.rt) )
    {
        const kui_pack_scene_t *const sc = &s_a.pack.scenes[s];
        uint16_t const k = (uint16_t)(khal_random() % (uint32_t)sc->on_count[t]);
        uint16_t const f = s_a.pack.react[sc->on_first[t] + k];

        if ( FILE_READY == s_a.rt[f].state )
        {
            a = &s_a.rt[f].anim;
        }
        else if ( FILE_DEAD == s_a.rt[f].state )
        {
            KLOG_D(TAG, "assets: the reaction picked, %s, is dead", s_a.pack.files[f].name);
        }
        else
        {
            want_file(f);
            s_a.react_scene = s;
            s_a.react_file  = f;
        }
    }
    else
    {
        a = NULL;
    }
    return a;
}

const kui_kody_anim_t *kgui_assets_react_pending(void)
{
    const kui_kody_anim_t *a = NULL;
    uint16_t const f = s_a.react_file;

    if ( (KUI_PACK_NONE != f) && (NULL != s_a.rt) && (f < s_a.pack.n_files)
        && (FILE_READY == s_a.rt[f].state) )
    {
        a = &s_a.rt[f].anim;
    }
    s_a.react_scene = KUI_PACK_NONE;
    s_a.react_file  = KUI_PACK_NONE;
    return a;
}

/**
 * @brief Whether a file belongs to scene @p s or to a role.
 * @param s A scene id.
 * @param f A file of the active pack.
 * @return true when freeing it would take something @p s or a role plays.
 */
static bool file_kept(uint16_t s, uint16_t f)
{
    bool kept = false;

    for ( uint32_t r = 0U; (!kept) && ((uint32_t)KUI_PACK_ROLE_COUNT > r); ++r )
    {
        kept = (s_a.pack.role[r] == f);
    }
    if ( (!kept) && (s < s_a.pack.n_scenes) )
    {
        const kui_pack_scene_t *const sc = &s_a.pack.scenes[s];

        kept = (sc->wake == f) || (sc->loop == f);
        for ( uint32_t t = 0U; (!kept) && ((uint32_t)KUI_PACK_TRIGGER_COUNT > t); ++t )
        {
            for ( uint16_t k = 0U; (!kept) && (k < sc->on_count[t]); ++k )
            {
                kept = (s_a.pack.react[sc->on_first[t] + k] == f);
            }
        }
    }
    return kept;
}

void kgui_assets_scene_keep(uint16_t s)
{
    /* A try ends when the face is given a scene again. */
    khal_free(s_a.try_blk);
    s_a.try_blk = NULL;
    if ( NULL == s_a.rt )
    {
        return;
    }
    for ( uint16_t f = 0U; f < s_a.pack.n_files; ++f )
    {
        if ( (FILE_READY == s_a.rt[f].state) && (!file_kept(s, f)) )
        {
            khal_free(s_a.rt[f].blk);
            s_a.rt[f].blk   = NULL;
            s_a.rt[f].state = FILE_UNLOADED;
        }
    }
}

const kui_kody_anim_t *kgui_assets_role(kui_pack_role_t r)
{
    const kui_kody_anim_t *a = NULL;

    if ( ((uint32_t)r < (uint32_t)KUI_PACK_ROLE_COUNT) && (NULL != s_a.rt) )
    {
        uint16_t const f = s_a.pack.role[r];

        if ( (KUI_PACK_NONE != f) && (FILE_READY == s_a.rt[f].state) )
        {
            a = &s_a.rt[f].anim;
        }
    }
    return a;
}

kode_status_t kgui_assets_begin(uint32_t bytes)
{
    kode_status_t st = KODE_DENIED;

    if ( JOB_IDLE == s_a.job )
    {
        s_a.begin_bytes = bytes;
        job_start(JOB_BEGIN);
        job_run();
        st = KODE_OK;
    }
    return st;
}

kode_status_t kgui_assets_commit(void)
{
    kode_status_t st = KODE_DENIED;

    if ( JOB_IDLE == s_a.job )
    {
        s_a.recovering = false;
        job_start(JOB_COMMIT);
        job_run();
        st = KODE_OK;
    }
    return st;
}

kode_status_t kgui_assets_update(const char *url, bool online)
{
    kode_status_t st = KODE_DENIED;
    const char *u = CONFIG_KGUI_ASSETS_URL;

    if ( (NULL != url) && ('\0' != url[0]) )
    {
        u = url;
    }
    if ( JOB_IDLE != s_a.job )
    {
        st = KODE_DENIED;
    }
    else if ( !online )
    {
        job_refused("update", "offline");
        install_end("offline");
        kgui_assets_ask_end(s_a.ans_rc);
        st = KODE_OK;
    }
    else if ( strlen(u) >= sizeof(s_a.url) )
    {
        job_refused("update", "url");
        install_end("url");
        kgui_assets_ask_end(s_a.ans_rc);
        st = KODE_OK;
    }
    else
    {
        size_t cut = 0U;

        (void)snprintf(s_a.url, sizeof(s_a.url), "%s", u);
        (void)snprintf(s_a.base, sizeof(s_a.base), "%s", u);
        /* The folder: everything up to the last '/'. */
        for ( size_t k = 0U; '\0' != s_a.base[k]; ++k )
        {
            if ( '/' == s_a.base[k] )
            {
                cut = k + 1U;
            }
        }
        s_a.base[cut] = '\0';
        job_start(JOB_UPDATE);
        job_run();
        st = KODE_OK;
    }
    return st;
}

bool kgui_assets_installing(void)
{
    return s_a.installing;
}

uint16_t kgui_assets_install_done(void)
{
    return s_a.inst_done;
}

uint16_t kgui_assets_install_of(void)
{
    return s_a.inst_of;
}

const char *kgui_assets_install_why(void)
{
    return s_a.why;
}

void kgui_assets_info(void)
{
    static const char *const ROLE_WORDS[KUI_PACK_ROLE_COUNT] = { "empty", "writing",
                                                                  "goodbye", "loading" };

    say_source(false);
    if ( KUI_PACK_SRC_BUILTIN == s_a.src )
    {
        job_say(false, "scene=\"reloj\" anchor=%u look=1 dead=no",
                (unsigned)kody_reloj.anchor);
    }
    for ( uint16_t i = 0U; i < s_a.pack.n_scenes; ++i )
    {
        const kui_pack_scene_t *const sc = &s_a.pack.scenes[i];
        const char *dead = "no";

        if ( kgui_assets_scene_dead(i) )
        {
            dead = "yes";
        }
        job_say(false, "scene=\"%s\" anchor=%u look=%u dead=%s", sc->name,
                (unsigned)sc->anchor, (unsigned)sc->on_count[KUI_PACK_TRIGGER_LOOK], dead);
    }
    for ( uint32_t r = 0U; (uint32_t)KUI_PACK_ROLE_COUNT > r; ++r )
    {
        uint16_t const f = s_a.pack.role[r];

        if ( (KUI_PACK_SRC_BUILTIN == s_a.src) || (KUI_PACK_NONE == f) )
        {
            job_say(false, "role=%s file=-", ROLE_WORDS[r]);
        }
        else
        {
            job_say(false, "role=%s file=\"%s\"", ROLE_WORDS[r], s_a.pack.files[f].name);
        }
    }
}

kode_status_t kgui_assets_check(kui_pack_src_t src)
{
    kode_status_t st = KODE_DENIED;

    if ( (JOB_IDLE == s_a.job) && ((KUI_PACK_SRC_SD == src) || (KUI_PACK_SRC_FS == src)) )
    {
        s_a.chk_src = src;
        job_start(JOB_CHECK);
        job_run();
        st = KODE_OK;
    }
    return st;
}

kode_status_t kgui_assets_reload(void)
{
    kode_status_t st = KODE_DENIED;

    if ( JOB_IDLE == s_a.job )
    {
        job_start(JOB_RESOLVE);
        job_run();
        st = KODE_OK;
    }
    return st;
}

kode_status_t kgui_assets_try(const char *file)
{
    kode_status_t st = KODE_DENIED;

    if ( (JOB_IDLE == s_a.job) && (KUI_PACK_SRC_BUILTIN != s_a.src)
        && kui_pack_file_name_ok(file) )
    {
        (void)snprintf(s_a.try_name, sizeof(s_a.try_name), "%s", file);
        job_start(JOB_TRY);
        job_run();
        st = KODE_OK;
    }
    return st;
}

void kgui_assets_try_refuse(const char *why)
{
    (void)snprintf(s_a.try_why, sizeof(s_a.try_why), "%s", why);
}

const kui_kody_anim_t *kgui_assets_try_anim(void)
{
    const kui_kody_anim_t *a = NULL;

    if ( NULL != s_a.try_blk )
    {
        a = &s_a.try_anim;
    }
    return a;
}
