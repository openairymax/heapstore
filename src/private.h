/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file private.h
 * @brief Internal header for the AgentRT data partition.
 */

/* @owner: team-B */
#ifndef AIRY_heapstore_PRIVATE_H
#define AIRY_heapstore_PRIVATE_H

#include "../include/heapstore.h"
#include "atomic_compat.h"
#include "platform.h"

#include <stdio.h>

#define heapstore_MAX_PATH_LEN 512
#define heapstore_MAX_NAME_LEN 128

/* 覆盖式改名：语义与错误码由平台层的唯一原语给出，本模块不再自持一份
 * Win32 实现——两份并行实现意味着两处各自演进，改一处漏一处。 */
static inline int heapstore_atomic_replace(const char *src, const char *dst)
{
    return airy_file_rename(src, dst);
}

typedef struct heapstore_submodule heapstore_submodule_t;

typedef heapstore_error_t (*submodule_init_fn)(void);
typedef void (*submodule_shutdown_fn)(void);

struct heapstore_submodule {
    const char *name;
    submodule_init_fn init;
    submodule_shutdown_fn shutdown;
    atomic_bool initialized;
};

extern heapstore_submodule_t g_heapstore_submodules[];

heapstore_error_t heapstore_registry_init(void);
void heapstore_registry_shutdown(void);

heapstore_error_t heapstore_trace_init(void);
void heapstore_trace_shutdown(void);

heapstore_error_t heapstore_memory_init(void);
void heapstore_memory_shutdown(void);

heapstore_error_t heapstore_log_init(void);
void heapstore_log_shutdown(void);

bool heapstore_registry_is_healthy(void);
bool heapstore_trace_is_healthy(void);
bool heapstore_log_is_healthy(void);
bool heapstore_memory_is_healthy(void);

#endif /* AIRY_heapstore_PRIVATE_H */
