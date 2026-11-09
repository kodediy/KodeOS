/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_group_stubs.c
 * @brief What kapp_cat.c links against for its override table, which the group model never uses.
 */
#include <stdarg.h>
#include <stddef.h>

#include "khal_nvs.h"
#include "khal_sync.h"
#include "khal_system.h"
#include "kode_log.h"

void *khal_alloc_psram(size_t align, size_t size)
{
    (void)align;
    (void)size;
    return NULL;
}

khal_mutex_t khal_mutex_create(void)
{
    return NULL;
}

void khal_mutex_lock(khal_mutex_t m)
{
    (void)m;
}

void khal_mutex_unlock(khal_mutex_t m)
{
    (void)m;
}

kode_status_t khal_nvs_init(void)
{
    return KODE_ERROR;
}

kode_status_t khal_nvs_get_blob(const char *ns, const char *key,
                                void *out, size_t *len)
{
    (void)ns;
    (void)key;
    (void)out;
    (void)len;
    return KODE_ERROR;
}

kode_status_t khal_nvs_set_blob(const char *ns, const char *key,
                                const void *value, size_t len)
{
    (void)ns;
    (void)key;
    (void)value;
    (void)len;
    return KODE_ERROR;
}

void kode_log(kode_log_lvl_t lvl, const char *tag, const char *fmt, ...)
{
    (void)lvl;
    (void)tag;
    (void)fmt;
}
