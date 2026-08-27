/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file heapstore_ipc_internal.h
 * @brief AgentRT data partition IPC 模块内部共享定义（拆分自 heapstore_ipc.c）。
 */

/* @owner: team-B */
#ifndef AIRY_heapstore_IPC_INTERNAL_H
#define AIRY_heapstore_IPC_INTERNAL_H

#include "heapstore_ipc.h"
#include "private.h"
#include "atomic_compat.h"

#include <platform.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define heapstore_IPC_MAX_CHANNELS 256
#define heapstore_IPC_MAX_BUFFERS 1024
#define heapstore_IPC_MAX_PATH 512

/* 模块全局状态（定义于 heapstore_ipc.c，拆分文件共享） */
extern bool s_initialized;
extern airy_mtx_t s_ipc_lock;
extern heapstore_ipc_channel_t s_channels[heapstore_IPC_MAX_CHANNELS];
extern size_t s_channel_count;
extern heapstore_ipc_buffer_t s_buffers[heapstore_IPC_MAX_BUFFERS];
extern size_t s_buffer_count;
extern char s_ipc_path[heapstore_IPC_MAX_PATH];

/* JSON 持久化（heapstore_ipc_persist.c） */
heapstore_error_t persist_channel_to_file(const heapstore_ipc_channel_t *channel);
heapstore_error_t persist_buffer_to_file(const heapstore_ipc_buffer_t *buffer);
heapstore_error_t load_channel_from_file(const char *channel_id, heapstore_ipc_channel_t *channel);
heapstore_error_t load_buffer_from_file(const char *buffer_id, heapstore_ipc_buffer_t *buffer);

#ifndef _WIN32
/* POSIX 共享内存区域管理（heapstore_ipc_shm.c） */
typedef struct {
    char shm_name[256];
    int shm_fd;
    void *mapped;
    size_t mapped_size;
} ipc_shm_region_t;

#define IPC_SHM_MAX_REGIONS 32
#define IPC_SHM_PREFIX "/airy_ipc_"
#define IPC_SHM_PREFIX_LEN ((sizeof(IPC_SHM_PREFIX)) - 1) /* 10 字符，不含 NUL */
extern ipc_shm_region_t s_shm_regions[IPC_SHM_MAX_REGIONS];
extern size_t s_shm_region_count;

ipc_shm_region_t *find_or_create_shm(const char *name, size_t size);
void ipc_shm_release(const char *name);
void ipc_shm_cleanup_all(void);
#endif /* !_WIN32 */

#endif /* AIRY_heapstore_IPC_INTERNAL_H */
