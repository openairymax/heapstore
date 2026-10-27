// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_journal_mem.c
 * @brief Journal in-memory backend: fixed-capacity ring per domain, used when
 *        heapstore is built without SQLite3.
 */

// @owner: team-B
#include "heapstore_journal.h"
#include "heapstore_registry_internal.h"

#ifndef heapstore_SQLITE_IMPLEMENTATION

/* 每域环形容量：有界即体积可控，满则覆盖最旧记录。 */
#define JMEM_CAP 64

typedef struct {
    unsigned char *slots;
    size_t cap;
    size_t head;
    size_t len;
    size_t rec_size;
} jring_t;

static jring_t s_rings[heapstore_JOUR_MAX];

static const size_t s_rec_size[heapstore_JOUR_MAX] = {
    sizeof(heapstore_turn_rec_t),      sizeof(heapstore_ckpt_rec_t),
    sizeof(heapstore_llm_call_rec_t),  sizeof(heapstore_tool_call_rec_t),
    sizeof(heapstore_event_rec_t),     sizeof(heapstore_cost_rec_t),
};

void hs_jmem_init(void)
{
    for (unsigned int i = 0; i < (unsigned int)heapstore_JOUR_MAX; i++) {
        s_rings[i].rec_size = s_rec_size[i];
        s_rings[i].cap = JMEM_CAP;
        s_rings[i].slots = AIRY_CALLOC(JMEM_CAP, s_rec_size[i]);
        if (!s_rings[i].slots) {
            s_rings[i].cap = 0;
        }
    }
}

void hs_jmem_drop(void)
{
    for (unsigned int i = 0; i < (unsigned int)heapstore_JOUR_MAX; i++) {
        AIRY_FREE(s_rings[i].slots);
        s_rings[i].slots = NULL;
        s_rings[i].cap = 0;
        s_rings[i].head = 0;
        s_rings[i].len = 0;
    }
}

static jring_t *jring_of(heapstore_jour_type_t type)
{
    if ((unsigned int)type >= (unsigned int)heapstore_JOUR_MAX) {
        return NULL;
    }
    return &s_rings[type];
}

static heapstore_error_t jring_put(jring_t *ring, const void *rec)
{
    if (!ring->slots || ring->cap == 0) {
        return heapstore_ERR_OUT_OF_MEMORY;
    }

    unsigned char *slot = ring->slots + (ring->head % ring->cap) * ring->rec_size;
    __builtin_memcpy(slot, rec, ring->rec_size);
    ring->head = (ring->head + 1) % ring->cap;
    if (ring->len < ring->cap) {
        ring->len++;
    }
    return heapstore_SUCCESS;
}

heapstore_error_t hs_jour_add(heapstore_jour_type_t type, const void *rec)
{
    jring_t *ring = jring_of(type);
    if (!ring || !rec) {
        return heapstore_ERR_INVALID_PARAM;
    }
    return jring_put(ring, rec);
}

heapstore_error_t hs_jour_add_n(heapstore_jour_type_t type, const void *recs, size_t n)
{
    jring_t *ring = jring_of(type);
    if (!ring || !recs || n == 0) {
        return heapstore_ERR_INVALID_PARAM;
    }

    const char *base = (const char *)recs;
    for (size_t i = 0; i < n; i++) {
        heapstore_error_t err = jring_put(ring, base + i * ring->rec_size);
        if (err != heapstore_SUCCESS) {
            return err;
        }
    }
    return heapstore_SUCCESS;
}

heapstore_error_t hs_jour_count(heapstore_jour_type_t type, uint64_t *out)
{
    jring_t *ring = jring_of(type);
    if (!ring || !out) {
        return heapstore_ERR_INVALID_PARAM;
    }
    *out = (uint64_t)ring->len;
    return heapstore_SUCCESS;
}

heapstore_error_t hs_jour_trim(heapstore_jour_type_t type, size_t keep)
{
    jring_t *ring = jring_of(type);
    if (!ring) {
        return heapstore_ERR_INVALID_PARAM;
    }
    if (keep < ring->len) {
        ring->len = keep;
    }
    return heapstore_SUCCESS;
}

#endif /* !heapstore_SQLITE_IMPLEMENTATION */
