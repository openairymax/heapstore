// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_registry.c
 * @brief AgentRT data partition registry implementation.
 */

// @owner: team-B
#include "heapstore_registry.h"
#include "heapstore_registry_internal.h"

#ifndef heapstore_SQLITE_IMPLEMENTATION

typedef struct registry_node {
    void *data;
    size_t data_size;
    struct registry_node *next;
} registry_node_t;

typedef struct {
    registry_node_t *agents;
    registry_node_t *skills;
    registry_node_t *sessions;
    size_t agent_count;
    size_t skill_count;
    size_t session_count;
    int initialized;
} registry_db_t;

struct heapstore_registry_iter {
    registry_node_t *current;
    int type;
};

static registry_db_t s_registry = {0};

static registry_node_t *find_node_by_id(registry_node_t *head, const char *id, size_t id_offset)
{
    registry_node_t *node = head;
    while (node) {
        if (node->data) {
            const char *node_id = (const char *)((char *)node->data + id_offset);
            if (node_id && strcmp(node_id, id) == 0) {
                return node;
            }
        }
        node = node->next;
    }
    return NULL;
}

static void free_node_list(registry_node_t **head)
{
    registry_node_t *node = *head;
    while (node) {
        registry_node_t *next = node->next;
        AIRY_FREE(node->data);
        AIRY_FREE(node);
        node = next;
    }
    *head = NULL;
}

/* 三域（agent/skill/session）共用的机制视图：各域一条按 id 索引的链表 + 计数。
 * 使域内增删改查批量逻辑单点化，公开 API 退化为薄包装。 */
typedef struct {
    registry_node_t **head;
    size_t *count;
    size_t id_off;
    size_t rec_size;
    int type;
} reg_domain_t;

static const reg_domain_t s_domains[] = {
    {&s_registry.agents, &s_registry.agent_count, offsetof(heapstore_agent_record_t, id),
     sizeof(heapstore_agent_record_t), 0},
    {&s_registry.skills, &s_registry.skill_count, offsetof(heapstore_skill_record_t, id),
     sizeof(heapstore_skill_record_t), 1},
    {&s_registry.sessions, &s_registry.session_count, offsetof(heapstore_session_record_t, id),
     sizeof(heapstore_session_record_t), 2},
};

static heapstore_error_t reg_add(const reg_domain_t *d, const void *record)
{
    if (!record || ((const char *)record)[d->id_off] == '\0')
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    if (find_node_by_id(*d->head, (const char *)record + d->id_off, d->id_off))
        return heapstore_ERR_ALREADY_INITIALIZED;
    registry_node_t *node = AIRY_CALLOC(1, sizeof(registry_node_t));
    if (!node)
        return heapstore_ERR_OUT_OF_MEMORY;
    node->data = AIRY_MALLOC(d->rec_size);
    if (!node->data) {
        AIRY_FREE(node);
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    __builtin_memcpy(node->data, record, d->rec_size);
    node->data_size = d->rec_size;
    node->next = *d->head;
    *d->head = node;
    (*d->count)++;
    return heapstore_SUCCESS;
}

static heapstore_error_t reg_get(const reg_domain_t *d, const char *id, void *out)
{
    if (!id || !out)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    registry_node_t *node = find_node_by_id(*d->head, id, d->id_off);
    if (!node)
        return heapstore_ERR_NOT_FOUND;
    __builtin_memcpy(out, node->data, d->rec_size);
    return heapstore_SUCCESS;
}

static heapstore_error_t reg_update(const reg_domain_t *d, const void *record)
{
    if (!record || ((const char *)record)[d->id_off] == '\0')
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    registry_node_t *node = find_node_by_id(*d->head, (const char *)record + d->id_off, d->id_off);
    if (!node)
        return heapstore_ERR_NOT_FOUND;
    __builtin_memcpy(node->data, record, d->rec_size);
    return heapstore_SUCCESS;
}

static heapstore_error_t reg_delete(const reg_domain_t *d, const char *id)
{
    if (!id)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    registry_node_t **pp = d->head;
    while (*pp) {
        const char *node_id = (const char *)((char *)(*pp)->data + d->id_off);
        if (strcmp(node_id, id) == 0) {
            registry_node_t *victim = *pp;
            *pp = victim->next;
            AIRY_FREE(victim->data);
            AIRY_FREE(victim);
            (*d->count)--;
            return heapstore_SUCCESS;
        }
        pp = &(*pp)->next;
    }
    return heapstore_ERR_NOT_FOUND;
}

static heapstore_error_t reg_query(const reg_domain_t *d, heapstore_registry_iter_t **iter)
{
    if (!iter)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    heapstore_registry_iter_t *it = AIRY_CALLOC(1, sizeof(heapstore_registry_iter_t));
    if (!it)
        return heapstore_ERR_OUT_OF_MEMORY;
    it->current = *d->head;
    it->type = d->type;
    *iter = it;
    return heapstore_SUCCESS;
}

static heapstore_error_t reg_batch(const reg_domain_t *d, const void *records, size_t count)
{
    if (!records || count == 0)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    for (size_t i = 0; i < count; i++) {
        heapstore_error_t err = reg_add(d, (const char *)records + i * d->rec_size);
        if (err != heapstore_SUCCESS)
            return err;
    }
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_init(void)
{
    __builtin_memset(&s_registry, 0, sizeof(s_registry));
    s_registry.initialized = 1;
    return heapstore_SUCCESS;
}

void heapstore_registry_shutdown(void)
{
    free_node_list(&s_registry.agents);
    free_node_list(&s_registry.skills);
    free_node_list(&s_registry.sessions);
    __builtin_memset(&s_registry, 0, sizeof(s_registry));
}

heapstore_error_t heapstore_registry_add_agent(const heapstore_agent_record_t *record)
{
    return reg_add(&s_domains[0], record);
}

heapstore_error_t heapstore_registry_get_agent(const char *id, heapstore_agent_record_t *record)
{
    return reg_get(&s_domains[0], id, record);
}

heapstore_error_t heapstore_registry_update_agent(const heapstore_agent_record_t *record)
{
    return reg_update(&s_domains[0], record);
}

heapstore_error_t heapstore_registry_delete_agent(const char *id)
{
    return reg_delete(&s_domains[0], id);
}

heapstore_error_t heapstore_registry_query_agents(const char *filter_type,
                                                  const char *filter_status,
                                                  heapstore_registry_iter_t **iter)
{
    (void)filter_type;
    (void)filter_status;
    return reg_query(&s_domains[0], iter);
}

heapstore_error_t heapstore_registry_add_skill(const heapstore_skill_record_t *record)
{
    return reg_add(&s_domains[1], record);
}

heapstore_error_t heapstore_registry_get_skill(const char *id, heapstore_skill_record_t *record)
{
    return reg_get(&s_domains[1], id, record);
}

heapstore_error_t heapstore_registry_delete_skill(const char *id)
{
    return reg_delete(&s_domains[1], id);
}

heapstore_error_t heapstore_registry_query_skills(heapstore_registry_iter_t **iter)
{
    return reg_query(&s_domains[1], iter);
}

heapstore_error_t heapstore_registry_add_session(const heapstore_session_record_t *record)
{
    return reg_add(&s_domains[2], record);
}

heapstore_error_t heapstore_registry_get_session(const char *id, heapstore_session_record_t *record)
{
    return reg_get(&s_domains[2], id, record);
}

heapstore_error_t heapstore_registry_update_session(const heapstore_session_record_t *record)
{
    return reg_update(&s_domains[2], record);
}

heapstore_error_t heapstore_registry_delete_session(const char *id)
{
    return reg_delete(&s_domains[2], id);
}

heapstore_error_t heapstore_registry_query_sessions(const char *filter_status,
                                                    heapstore_registry_iter_t **iter)
{
    (void)filter_status;
    return reg_query(&s_domains[2], iter);
}

heapstore_error_t heapstore_registry_iter_next(heapstore_registry_iter_t *iter, void *record)
{
    if (!iter || !record)
        return heapstore_ERR_INVALID_PARAM;
    if (!iter->current)
        return heapstore_ERR_NOT_FOUND;
    __builtin_memcpy(record, iter->current->data, iter->current->data_size);
    iter->current = iter->current->next;
    return heapstore_SUCCESS;
}

void heapstore_registry_iter_destroy(heapstore_registry_iter_t *iter)
{
    AIRY_FREE(iter);
}

heapstore_error_t heapstore_registry_vacuum(void)
{
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_batch_insert_agents(const heapstore_agent_record_t *records,
                                                         size_t count)
{
    return reg_batch(&s_domains[0], records, count);
}

heapstore_error_t hs_batch_insert_sessions(
    const heapstore_session_record_t *records, size_t count)
{
    return reg_batch(&s_domains[2], records, count);
}

heapstore_error_t heapstore_registry_batch_insert_skills(const heapstore_skill_record_t *records,
                                                         size_t count)
{
    return reg_batch(&s_domains[1], records, count);
}

bool heapstore_registry_is_healthy(void)
{
    return s_registry.initialized != 0;
}

#endif
