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

void kgui_fault_net_unheard(kgui_fault_net_t *n, bool wifi_heard, bool chan_heard)
{
    if ( NULL != n )
    {
        if ( !wifi_heard )
        {
            n->wifi_on = true;
        }
        if ( !chan_heard )
        {
            n->chan = (uint8_t)KGUI_FAULT_CHAN_COMING;
        }
    }
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

const char *kgui_fault_send_words(uint8_t result)
{
    const char *words = KGUI_FAULT_WHY_NO_ANSWER;

    switch ( result )
    {
        case (uint8_t)KGUI_FAULT_SEND_OK: {
            words = KGUI_FAULT_SENT;
            break;
        }
        case (uint8_t)KGUI_FAULT_SEND_REFUSED: {
            words = KGUI_FAULT_WHY_REFUSED;
            break;
        }
        case (uint8_t)KGUI_FAULT_SEND_BUSY: {
            words = KGUI_CARD_BUSY;
            break;
        }
        case (uint8_t)KGUI_FAULT_SEND_CHANNEL: {
            words = KGUI_FAULT_WHY_CHANNEL;
            break;
        }
        case (uint8_t)KGUI_FAULT_SEND_NONE: {
            words = KGUI_FAULT_WHY_NONE;
            break;
        }
        default: {
            /* No answer, or an end this build does not know. */
            break;
        }
    }
    return words;
}

bool kgui_fault_on_answer(uint8_t *face, bool card_up, uint8_t result)
{
    bool const taken = (NULL != face) && card_up && ((uint8_t)KGUI_FAULT_FACE_SENDING == *face);

    if ( taken )
    {
        if ( (uint8_t)KGUI_FAULT_SEND_OK == result )
        {
            *face = (uint8_t)KGUI_FAULT_FACE_SENT;
        }
        else
        {
            *face = (uint8_t)KGUI_FAULT_FACE_FAILED;
        }
    }
    return taken;
}

kgui_fault_step_t kgui_fault_face_step(uint8_t face, uint32_t since_ms)
{
    kgui_fault_step_t step = KGUI_FAULT_STEP_STAY;

    if ( ((uint8_t)KGUI_FAULT_FACE_SENDING == face) && ((uint32_t)KGUI_FAULT_SEND_GUARD_MS <= since_ms) )
    {
        step = KGUI_FAULT_STEP_FAIL;
    }
    else if ( ((uint8_t)KGUI_FAULT_FACE_SENT == face) && ((uint32_t)KGUI_FAULT_SENT_MS <= since_ms) )
    {
        step = KGUI_FAULT_STEP_CLOSE;
    }
    else
    {
        /* The notice and the failure wait for a person; the rest wait on. */
    }
    return step;
}
