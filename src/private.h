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

#include <stdio.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define heapstore_MAX_PATH_LEN 512
#define heapstore_MAX_NAME_LEN 128

/**
  * @brief Replace @p dst with @p src in one crash-safe step.
  *
  * POSIX rename(2) atomically replaces an existing destination. Windows
  * rename() fails when the destination already exists, so MoveFileExA() with
  * MOVEFILE_REPLACE_EXISTING is used there to keep the same guarantee: a
  * reader observes either the previous file or the fully written new one,
  * never a partial one.
  *
  * @return 0 on success, -1 on failure.
 */
static inline int heapstore_atomic_replace(const char *src, const char *dst)
{
#ifdef _WIN32
    return MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
#else
    return rename(src, dst);
#endif
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

heapstore_error_t heapstore_ipc_init(void);
void heapstore_ipc_shutdown(void);

heapstore_error_t heapstore_memory_init(void);
void heapstore_memory_shutdown(void);

heapstore_error_t heapstore_log_init(void);
void heapstore_log_shutdown(void);

bool heapstore_registry_is_healthy(void);
bool heapstore_trace_is_healthy(void);
bool heapstore_log_is_healthy(void);
bool heapstore_ipc_is_healthy(void);
bool heapstore_memory_is_healthy(void);

#endif /* AIRY_heapstore_PRIVATE_H */
