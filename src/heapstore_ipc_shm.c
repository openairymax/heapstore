// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_ipc_shm.c
 * @brief AgentRT data partition IPC POSIX 共享内存区域管理。
 *
 * 本文件拆分自 heapstore_ipc.c，负责 POSIX 命名共享内存区域的创建、
 * 查询与释放（仅在非 Windows 平台编译）。
 */

// @owner: team-B
#ifndef _WIN32

#include "heapstore_ipc_internal.h"

#include "airy_mman.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

ipc_shm_region_t s_shm_regions[IPC_SHM_MAX_REGIONS];
size_t s_shm_region_count = 0;

ipc_shm_region_t *find_or_create_shm(const char *name, size_t size)
{
    for (size_t i = 0; i < s_shm_region_count; i++) {
        if (strcmp(s_shm_regions[i].shm_name + IPC_SHM_PREFIX_LEN, name) == 0) {
            return &s_shm_regions[i];
        }
    }
    if (s_shm_region_count >= IPC_SHM_MAX_REGIONS)
        return NULL;

    ipc_shm_region_t *r = &s_shm_regions[s_shm_region_count];
    snprintf(r->shm_name, sizeof(r->shm_name), IPC_SHM_PREFIX "%s", name);

    r->shm_fd = shm_open(r->shm_name, O_CREAT | O_RDWR, 0666);
    if (r->shm_fd < 0)
        return NULL;

    if (ftruncate(r->shm_fd, (off_t)size) != 0) {
        close(r->shm_fd);
        shm_unlink(r->shm_name);
        return NULL;
    }

    r->mapped = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, r->shm_fd, 0);
    if (r->mapped == MAP_FAILED) {
        close(r->shm_fd);
        shm_unlink(r->shm_name);
        return NULL;
    }

    r->mapped_size = size;
    s_shm_region_count++;
    return r;
}

void ipc_shm_release(const char *name)
{
    for (size_t j = 0; j < s_shm_region_count; j++) {
        if (strcmp(s_shm_regions[j].shm_name + IPC_SHM_PREFIX_LEN, name) == 0) {
            if (s_shm_regions[j].mapped && s_shm_regions[j].mapped != MAP_FAILED)
                munmap(s_shm_regions[j].mapped, s_shm_regions[j].mapped_size);
            if (s_shm_regions[j].shm_fd >= 0)
                close(s_shm_regions[j].shm_fd);
            if (s_shm_regions[j].shm_name[0])
                shm_unlink(s_shm_regions[j].shm_name);
            __builtin_memmove(&s_shm_regions[j], &s_shm_regions[s_shm_region_count - 1],
                              sizeof(ipc_shm_region_t));
            s_shm_region_count--;
            break;
        }
    }
}

void ipc_shm_cleanup_all(void)
{
    for (size_t i = 0; i < s_shm_region_count; i++) {
        if (s_shm_regions[i].mapped && s_shm_regions[i].mapped != MAP_FAILED) {
            munmap(s_shm_regions[i].mapped, s_shm_regions[i].mapped_size);
        }
        if (s_shm_regions[i].shm_fd >= 0) {
            close(s_shm_regions[i].shm_fd);
        }
        if (s_shm_regions[i].shm_name[0]) {
            shm_unlink(s_shm_regions[i].shm_name);
        }
    }
    s_shm_region_count = 0;
}

#endif /* !_WIN32 */
