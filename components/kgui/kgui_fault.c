/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_fault.c
 * @brief The fault notice (kgui_fault.h).
 */
#include "kgui_fault.h"

#include <stddef.h>

#include "kgui_syscard.h"

/**
 * @brief Whether a class is one a record holds.
 * @param cls A kgui_fault_class_t.
 * @return true for CRASHED and HUNG.
 */
static bool fault_class_ok(uint8_t cls)
{
    return ((uint8_t)KGUI_FAULT_CRASHED == cls) || ((uint8_t)KGUI_FAULT_HUNG == cls);
}

uint8_t kgui_fault_pack(kgui_fault_rec_t r)
{
    uint8_t byte = 0U;

    if ( fault_class_ok(r.cls) )
    {
        byte = r.cls;
        if ( r.dump )
        {
            byte = (uint8_t)(byte | (uint8_t)KGUI_FAULT_DUMP_BIT);
        }
    }
    return byte;
}

bool kgui_fault_unpack(uint8_t byte, kgui_fault_rec_t *out)
{
    uint8_t const known = (uint8_t)(KGUI_FAULT_CLASS_MASK | KGUI_FAULT_DUMP_BIT);
    uint8_t const cls   = (uint8_t)(byte & (uint8_t)KGUI_FAULT_CLASS_MASK);
    bool const    ok    = (NULL != out) && (0U == (byte & (uint8_t)(~known)))
                          && fault_class_ok(cls);

    if ( ok )
    {
        out->cls  = cls;
        out->dump = (0U != (byte & (uint8_t)KGUI_FAULT_DUMP_BIT));
    }
    return ok;
}

const char *kgui_fault_words(uint8_t cls)
{
    const char *words = "";

    if ( (uint8_t)KGUI_FAULT_CRASHED == cls )
    {
        words = KGUI_STOPPED_CRASHED;
    }
    else if ( (uint8_t)KGUI_FAULT_HUNG == cls )
    {
        words = KGUI_STOPPED_HUNG;
    }
    else
    {
        /* No fault: no words. */
    }
    return words;
}

kgui_fault_verdict_t kgui_fault_verdict(const kgui_fault_net_t *n)
{
    kgui_fault_verdict_t verdict = KGUI_FAULT_RISE_OK;

    if ( (NULL == n) || (!n->dump) || (!n->built) || (!n->wifi_on) )
    {
        /* Nothing to send, or no way to send it: OK alone, at once. */
    }
    else if ( (uint8_t)KGUI_FAULT_CHAN_UP == n->chan )
    {
        if ( n->link )
        {
            verdict = KGUI_FAULT_RISE_SEND;
        }
    }
    else if ( ((uint8_t)KGUI_FAULT_CHAN_COMING == n->chan)
              && ((uint32_t)KGUI_FAULT_NET_WAIT_MS > n->waited_ms) )
    {
        verdict = KGUI_FAULT_WAIT;
    }
    else
    {
        /* A channel that will not come, or the wait spent: OK alone. */
    }
    return verdict;
}

void kgui_fault_begin(kgui_fault_t *f, bool kept, uint8_t kept_byte,
                      kgui_fault_rec_t boot, uint32_t now_ms)
{
    if ( NULL != f )
    {
        kgui_fault_rec_t const none = { (uint8_t)KGUI_FAULT_NONE, false };
        kgui_fault_rec_t       held = none;

        f->rec    = none;
        f->owed   = false;
        f->store  = false;
        f->due    = false;
        f->due_ms = 0U;
        if ( fault_class_ok(boot.cls) )
        {
            f->rec   = boot;
            f->owed  = true;
            f->store = true;
        }
        else if ( kept && kgui_fault_unpack(kept_byte, &held) )
        {
            f->rec  = held;
            f->owed = true;
        }
        else
        {
            /* Nothing owed. */
        }
        if ( f->owed )
        {
            f->due    = true;
            f->due_ms = now_ms;
        }
    }
}

void kgui_fault_taken_down(kgui_fault_t *f)
{
    if ( NULL != f )
    {
        f->due = false;
    }
}

void kgui_fault_input(kgui_fault_t *f, uint32_t now_ms)
{
    if ( (NULL != f) && f->owed && (!f->due) )
    {
        f->due    = true;
        f->due_ms = now_ms;
    }
}

void kgui_fault_answered(kgui_fault_t *f)
{
    if ( NULL != f )
    {
        f->owed  = false;
        f->due   = false;
        f->store = false;
    }
}

bool kgui_fault_yields(uint8_t face)
{
    return ((uint8_t)KGUI_FAULT_FACE_NOTICE == face) || ((uint8_t)KGUI_FAULT_FACE_SENT == face)
           || ((uint8_t)KGUI_FAULT_FACE_FAILED == face);
}
