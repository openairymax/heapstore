// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_registry.c
 * @brief AgentRT data partition registry implementation.
 */

// @owner: team-B
#include "heapstore_registry.h"

#include "../include/utils.h"
#include "platform.h"
#include "private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "airy_memory.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef AIRY_HAS_SQLITE3
#define heapstore_SQLITE_IMPLEMENTATION
#endif

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
    if (!record || record->id[0] == '\0')
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    if (find_node_by_id(s_registry.agents, record->id, offsetof(heapstore_agent_record_t, id))) {
        return heapstore_ERR_ALREADY_INITIALIZED;
    }
    registry_node_t *node = AIRY_CALLOC(1, sizeof(registry_node_t));
    if (!node)
        return heapstore_ERR_OUT_OF_MEMORY;
    node->data = AIRY_MALLOC(sizeof(heapstore_agent_record_t));
    if (!node->data) {
        AIRY_FREE(node);
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    __builtin_memcpy(node->data, record, sizeof(heapstore_agent_record_t));
    node->data_size = sizeof(heapstore_agent_record_t);
    node->next = s_registry.agents;
    s_registry.agents = node;
    s_registry.agent_count++;
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_get_agent(const char *id, heapstore_agent_record_t *record)
{
    if (!id || !record)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    registry_node_t *node =
        find_node_by_id(s_registry.agents, id, offsetof(heapstore_agent_record_t, id));
    if (!node)
        return heapstore_ERR_NOT_FOUND;
    __builtin_memcpy(record, node->data, sizeof(heapstore_agent_record_t));
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_update_agent(const heapstore_agent_record_t *record)
{
    if (!record || record->id[0] == '\0')
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    registry_node_t *node =
        find_node_by_id(s_registry.agents, record->id, offsetof(heapstore_agent_record_t, id));
    if (!node)
        return heapstore_ERR_NOT_FOUND;
    __builtin_memcpy(node->data, record, sizeof(heapstore_agent_record_t));
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_delete_agent(const char *id)
{
    if (!id)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    registry_node_t **pp = &s_registry.agents;
    while (*pp) {
        const char *node_id =
            (const char *)((char *)(*pp)->data + offsetof(heapstore_agent_record_t, id));
        if (strcmp(node_id, id) == 0) {
            registry_node_t *victim = *pp;
            *pp = victim->next;
            AIRY_FREE(victim->data);
            AIRY_FREE(victim);
            s_registry.agent_count--;
            return heapstore_SUCCESS;
        }
        pp = &(*pp)->next;
    }
    return heapstore_ERR_NOT_FOUND;
}

heapstore_error_t heapstore_registry_query_agents(const char *filter_type,
                                                  const char *filter_status,
                                                  heapstore_registry_iter_t **iter)
{
    if (!iter)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    heapstore_registry_iter_t *it = AIRY_CALLOC(1, sizeof(heapstore_registry_iter_t));
    if (!it)
        return heapstore_ERR_OUT_OF_MEMORY;
    it->current = s_registry.agents;
    it->type = 0;
    *iter = it;
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_add_skill(const heapstore_skill_record_t *record)
{
    if (!record || record->id[0] == '\0')
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    if (find_node_by_id(s_registry.skills, record->id, offsetof(heapstore_skill_record_t, id))) {
        return heapstore_ERR_ALREADY_INITIALIZED;
    }
    registry_node_t *node = AIRY_CALLOC(1, sizeof(registry_node_t));
    if (!node)
        return heapstore_ERR_OUT_OF_MEMORY;
    node->data = AIRY_MALLOC(sizeof(heapstore_skill_record_t));
    if (!node->data) {
        AIRY_FREE(node);
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    __builtin_memcpy(node->data, record, sizeof(heapstore_skill_record_t));
    node->data_size = sizeof(heapstore_skill_record_t);
    node->next = s_registry.skills;
    s_registry.skills = node;
    s_registry.skill_count++;
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_get_skill(const char *id, heapstore_skill_record_t *record)
{
    if (!id || !record)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    registry_node_t *node =
        find_node_by_id(s_registry.skills, id, offsetof(heapstore_skill_record_t, id));
    if (!node)
        return heapstore_ERR_NOT_FOUND;
    __builtin_memcpy(record, node->data, sizeof(heapstore_skill_record_t));
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_delete_skill(const char *id)
{
    if (!id)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    registry_node_t **pp = &s_registry.skills;
    while (*pp) {
        const char *node_id =
            (const char *)((char *)(*pp)->data + offsetof(heapstore_skill_record_t, id));
        if (strcmp(node_id, id) == 0) {
            registry_node_t *victim = *pp;
            *pp = victim->next;
            AIRY_FREE(victim->data);
            AIRY_FREE(victim);
            s_registry.skill_count--;
            return heapstore_SUCCESS;
        }
        pp = &(*pp)->next;
    }
    return heapstore_ERR_NOT_FOUND;
}

heapstore_error_t heapstore_registry_query_skills(heapstore_registry_iter_t **iter)
{
    if (!iter)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    heapstore_registry_iter_t *it = AIRY_CALLOC(1, sizeof(heapstore_registry_iter_t));
    if (!it)
        return heapstore_ERR_OUT_OF_MEMORY;
    it->current = s_registry.skills;
    it->type = 1;
    *iter = it;
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_add_session(const heapstore_session_record_t *record)
{
    if (!record || record->id[0] == '\0')
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    if (find_node_by_id(s_registry.sessions, record->id,
                        offsetof(heapstore_session_record_t, id))) {
        return heapstore_ERR_ALREADY_INITIALIZED;
    }
    registry_node_t *node = AIRY_CALLOC(1, sizeof(registry_node_t));
    if (!node)
        return heapstore_ERR_OUT_OF_MEMORY;
    node->data = AIRY_MALLOC(sizeof(heapstore_session_record_t));
    if (!node->data) {
        AIRY_FREE(node);
        return heapstore_ERR_OUT_OF_MEMORY;
    }
    __builtin_memcpy(node->data, record, sizeof(heapstore_session_record_t));
    node->data_size = sizeof(heapstore_session_record_t);
    node->next = s_registry.sessions;
    s_registry.sessions = node;
    s_registry.session_count++;
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_get_session(const char *id, heapstore_session_record_t *record)
{
    if (!id || !record)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    registry_node_t *node =
        find_node_by_id(s_registry.sessions, id, offsetof(heapstore_session_record_t, id));
    if (!node)
        return heapstore_ERR_NOT_FOUND;
    __builtin_memcpy(record, node->data, sizeof(heapstore_session_record_t));
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_update_session(const heapstore_session_record_t *record)
{
    if (!record || record->id[0] == '\0')
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    registry_node_t *node =
        find_node_by_id(s_registry.sessions, record->id, offsetof(heapstore_session_record_t, id));
    if (!node)
        return heapstore_ERR_NOT_FOUND;
    __builtin_memcpy(node->data, record, sizeof(heapstore_session_record_t));
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_delete_session(const char *id)
{
    if (!id)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    registry_node_t **pp = &s_registry.sessions;
    while (*pp) {
        const char *node_id =
            (const char *)((char *)(*pp)->data + offsetof(heapstore_session_record_t, id));
        if (strcmp(node_id, id) == 0) {
            registry_node_t *victim = *pp;
            *pp = victim->next;
            AIRY_FREE(victim->data);
            AIRY_FREE(victim);
            s_registry.session_count--;
            return heapstore_SUCCESS;
        }
        pp = &(*pp)->next;
    }
    return heapstore_ERR_NOT_FOUND;
}

heapstore_error_t heapstore_registry_query_sessions(const char *filter_status,
                                                    heapstore_registry_iter_t **iter)
{
    if (!iter)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    heapstore_registry_iter_t *it = AIRY_CALLOC(1, sizeof(heapstore_registry_iter_t));
    if (!it)
        return heapstore_ERR_OUT_OF_MEMORY;
    it->current = s_registry.sessions;
    it->type = 2;
    *iter = it;
    return heapstore_SUCCESS;
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
    if (!records || count == 0)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    for (size_t i = 0; i < count; i++) {
        heapstore_error_t err = heapstore_registry_add_agent(&records[i]);
        if (err != heapstore_SUCCESS)
            return err;
    }
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_batch_insert_sessions(
    const heapstore_session_record_t *records, size_t count)
{
    if (!records || count == 0)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    for (size_t i = 0; i < count; i++) {
        heapstore_error_t err = heapstore_registry_add_session(&records[i]);
        if (err != heapstore_SUCCESS)
            return err;
    }
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_registry_batch_insert_skills(const heapstore_skill_record_t *records,
                                                         size_t count)
{
    if (!records || count == 0)
        return heapstore_ERR_INVALID_PARAM;
    if (!s_registry.initialized)
        return heapstore_ERR_NOT_INITIALIZED;
    for (size_t i = 0; i < count; i++) {
        heapstore_error_t err = heapstore_registry_add_skill(&records[i]);
        if (err != heapstore_SUCCESS)
            return err;
    }
    return heapstore_SUCCESS;
}

bool heapstore_registry_is_healthy(void)
{
    return s_registry.initialized != 0;
}

#endif
