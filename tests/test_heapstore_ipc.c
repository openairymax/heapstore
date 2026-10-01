// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file test_heapstore_ipc.c
 * @brief AgentRT 数据分区 IPC 数据存储单元测试
 *
 */
// @owner: team-B

#include "heapstore.h"
#include "heapstore_ipc.h"
#include "heapstore_ipc_internal.h"
#include "airy_memory.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void test_ipc_init_shutdown(void)
{
    printf("Test: ipc_init_shutdown...");

    heapstore_error_t err = heapstore_ipc_init();
    assert(err == heapstore_SUCCESS || err == heapstore_ERR_ALREADY_INITIALIZED);

    heapstore_ipc_shutdown();
    printf("PASS\n");
}

static void test_ipc_channel_crud(void)
{
    printf("Test: ipc_channel_crud...");

    heapstore_error_t err = heapstore_ipc_init();
    assert(err == heapstore_SUCCESS);

    heapstore_ipc_channel_t channel;
    AIRY_MEMSET(&channel, 0, sizeof(channel));

    snprintf(channel.channel_id, sizeof(channel.channel_id), "ch_%ld", (long)time(NULL));
    snprintf(channel.name, sizeof(channel.name), "Test Channel");
    snprintf(channel.type, sizeof(channel.type), "binder");
    channel.created_at = (uint64_t)time(NULL);
    channel.last_activity_at = channel.created_at;
    channel.buffer_size = 4096;
    channel.current_usage = 1024;
    snprintf(channel.status, sizeof(channel.status), "active");

    err = heapstore_ipc_record_channel(&channel);
    assert(err == heapstore_SUCCESS);

    heapstore_ipc_channel_t get_ch;
    AIRY_MEMSET(&get_ch, 0, sizeof(get_ch));

    err = heapstore_ipc_get_channel(channel.channel_id, &get_ch);
    assert(err == heapstore_SUCCESS);
    assert(strcmp(get_ch.name, channel.name) == 0);
    assert(strcmp(get_ch.type, channel.type) == 0);
    assert(get_ch.buffer_size == channel.buffer_size);
    assert(get_ch.current_usage == channel.current_usage);

    err = heapstore_ipc_update_channel_activity(channel.channel_id);
    assert(err == heapstore_SUCCESS);

    char path[heapstore_IPC_MAX_PATH + 256];
    snprintf(path, sizeof(path), "%s/channels/%s.json", s_ipc_path, channel.channel_id);
    remove(path);

    printf("PASS\n");
}

static void test_ipc_buffer_crud(void)
{
    printf("Test: ipc_buffer_crud...");

    heapstore_error_t err = heapstore_ipc_init();
    assert(err == heapstore_SUCCESS);

    heapstore_ipc_channel_t channel;
    AIRY_MEMSET(&channel, 0, sizeof(channel));

    snprintf(channel.channel_id, sizeof(channel.channel_id), "ch_buf_%ld", (long)time(NULL));
    snprintf(channel.name, sizeof(channel.name), "Buffer Test Channel");
    snprintf(channel.type, sizeof(channel.type), "shared_memory");
    channel.created_at = (uint64_t)time(NULL);
    snprintf(channel.status, sizeof(channel.status), "active");

    err = heapstore_ipc_record_channel(&channel);
    assert(err == heapstore_SUCCESS);

    heapstore_ipc_buffer_t buffer;
    AIRY_MEMSET(&buffer, 0, sizeof(buffer));

    snprintf(buffer.buffer_id, sizeof(buffer.buffer_id), "buf_%ld", (long)time(NULL));
    snprintf(buffer.channel_id, sizeof(buffer.channel_id), "%s", channel.channel_id);
    buffer.created_at = (uint64_t)time(NULL);
    buffer.size = 8192;
    buffer.used = 4096;
    snprintf(buffer.status, sizeof(buffer.status), "active");

    err = heapstore_ipc_record_buffer(&buffer);
    assert(err == heapstore_SUCCESS);

    heapstore_ipc_buffer_t get_buf;
    AIRY_MEMSET(&get_buf, 0, sizeof(get_buf));

    err = heapstore_ipc_get_buffer(buffer.buffer_id, &get_buf);
    assert(err == heapstore_SUCCESS);
    assert(strcmp(get_buf.channel_id, buffer.channel_id) == 0);
    assert(get_buf.size == buffer.size);
    assert(get_buf.used == buffer.used);

    char path[heapstore_IPC_MAX_PATH + 256];
    snprintf(path, sizeof(path), "%s/buffers/%s.json", s_ipc_path, buffer.buffer_id);
    remove(path);
    snprintf(path, sizeof(path), "%s/channels/%s.json", s_ipc_path, channel.channel_id);
    remove(path);

    printf("PASS\n");
}

static void test_ipc_stats(void)
{
    printf("Test: ipc_stats...");

    heapstore_error_t init_err = heapstore_ipc_init();
    assert(init_err == heapstore_SUCCESS || init_err == heapstore_ERR_ALREADY_INITIALIZED);

    uint32_t channel_count = 0;
    uint32_t buffer_count = 0;
    uint64_t total_size = 0;

    heapstore_error_t err =
        heapstore_ipc_get_stats(&channel_count, &buffer_count, &total_size);
    assert(err == heapstore_SUCCESS);

    printf("  Channels: %u, Buffers: %u, Total Size: %lu\n", channel_count, buffer_count,
           (unsigned long)total_size);

    printf("PASS\n");
}

static void test_ipc_invalid_params(void)
{
    printf("Test: ipc_invalid_params...");

    heapstore_error_t err = heapstore_ipc_init();
    assert(err == heapstore_SUCCESS);

    err = heapstore_ipc_record_channel(NULL);
    assert(err == heapstore_ERR_INVALID_PARAM);

    heapstore_ipc_channel_t invalid_ch;
    AIRY_MEMSET(&invalid_ch, 0, sizeof(invalid_ch));
    err = heapstore_ipc_record_channel(&invalid_ch);
    assert(err == heapstore_ERR_INVALID_PARAM);

    err = heapstore_ipc_record_buffer(NULL);
    assert(err == heapstore_ERR_INVALID_PARAM);

    heapstore_ipc_buffer_t invalid_buf;
    AIRY_MEMSET(&invalid_buf, 0, sizeof(invalid_buf));
    err = heapstore_ipc_record_buffer(&invalid_buf);
    assert(err == heapstore_ERR_INVALID_PARAM);

    err = heapstore_ipc_get_channel(NULL, NULL);
    assert(err == heapstore_ERR_INVALID_PARAM);

    err = heapstore_ipc_get_buffer(NULL, NULL);
    assert(err == heapstore_ERR_INVALID_PARAM);

    err = heapstore_ipc_update_channel_activity(NULL);
    assert(err == heapstore_ERR_INVALID_PARAM);

    printf("PASS\n");
}

static void test_ipc_not_found(void)
{
    printf("Test: ipc_not_found...");

    heapstore_error_t err = heapstore_ipc_init();
    assert(err == heapstore_SUCCESS);

    heapstore_ipc_channel_t channel;
    AIRY_MEMSET(&channel, 0, sizeof(channel));

    err = heapstore_ipc_get_channel("nonexistent_id", &channel);
    assert(err == heapstore_ERR_NOT_FOUND);

    heapstore_ipc_buffer_t buffer;
    AIRY_MEMSET(&buffer, 0, sizeof(buffer));

    err = heapstore_ipc_get_buffer("nonexistent_id", &buffer);
    assert(err == heapstore_ERR_NOT_FOUND);

    err = heapstore_ipc_update_channel_activity("nonexistent_id");
    assert(err == heapstore_ERR_NOT_FOUND);

    printf("PASS\n");
}

static void test_ipc_multiple_channels(void)
{
    printf("Test: ipc_multiple_channels...");

    heapstore_error_t init_err = heapstore_ipc_init();
    assert(init_err == heapstore_SUCCESS);

    for (int i = 0; i < 5; i++) {
        heapstore_ipc_channel_t channel;
        AIRY_MEMSET(&channel, 0, sizeof(channel));

        snprintf(channel.channel_id, sizeof(channel.channel_id), "ch_multi_%d_%ld", i,
                 (long)time(NULL));
        snprintf(channel.name, sizeof(channel.name), "Channel %d", i);
        snprintf(channel.type, sizeof(channel.type), "type_%d", i);
        channel.created_at = (uint64_t)time(NULL);
        snprintf(channel.status, sizeof(channel.status), "active");

        heapstore_error_t err = heapstore_ipc_record_channel(&channel);
        assert(err == heapstore_SUCCESS || err == heapstore_ERR_OUT_OF_MEMORY);
    }

    uint32_t count = 0;
    heapstore_ipc_get_stats(&count, NULL, NULL);
    printf("  Total channels recorded: %u\n", count);

    printf("PASS\n");
}

static void test_ipc_persist_roundtrip(void)
{
    printf("Test: ipc_persist_roundtrip...");

    heapstore_error_t err = heapstore_ipc_init();
    assert(err == heapstore_SUCCESS);

    heapstore_ipc_channel_t channel;
    AIRY_MEMSET(&channel, 0, sizeof(channel));
    snprintf(channel.channel_id, sizeof(channel.channel_id), "ch_rt_%ld", (long)time(NULL));
    snprintf(channel.name, sizeof(channel.name), "Round Trip Channel");
    snprintf(channel.type, sizeof(channel.type), "binder");
    snprintf(channel.status, sizeof(channel.status), "active");
    channel.created_at = 1700000001ULL;
    channel.last_activity_at = 1700000002ULL;
    channel.buffer_size = 65536U;
    channel.current_usage = 4096U;

    assert(persist_channel_to_file(&channel) == heapstore_SUCCESS);

    heapstore_ipc_channel_t back;
    AIRY_MEMSET(&back, 0xAA, sizeof(back));
    assert(load_channel_from_file(channel.channel_id, &back) == heapstore_SUCCESS);
    assert(strcmp(back.channel_id, channel.channel_id) == 0);
    assert(strcmp(back.name, channel.name) == 0);
    assert(strcmp(back.type, channel.type) == 0);
    assert(strcmp(back.status, channel.status) == 0);
    assert(back.created_at == channel.created_at);
    assert(back.last_activity_at == channel.last_activity_at);
    assert(back.buffer_size == channel.buffer_size);
    assert(back.current_usage == channel.current_usage);

    heapstore_ipc_buffer_t buffer;
    AIRY_MEMSET(&buffer, 0, sizeof(buffer));
    snprintf(buffer.buffer_id, sizeof(buffer.buffer_id), "buf_rt_%ld", (long)time(NULL));
    snprintf(buffer.channel_id, sizeof(buffer.channel_id), "%s", channel.channel_id);
    buffer.size = 131072U;
    buffer.used = 2048U;
    buffer.created_at = 1700000003ULL;
    snprintf(buffer.status, sizeof(buffer.status), "active");

    assert(persist_buffer_to_file(&buffer) == heapstore_SUCCESS);

    heapstore_ipc_buffer_t back_buf;
    AIRY_MEMSET(&back_buf, 0xAA, sizeof(back_buf));
    assert(load_buffer_from_file(buffer.buffer_id, &back_buf) == heapstore_SUCCESS);
    assert(strcmp(back_buf.buffer_id, buffer.buffer_id) == 0);
    assert(strcmp(back_buf.channel_id, buffer.channel_id) == 0);
    assert(back_buf.size == buffer.size);
    assert(back_buf.used == buffer.used);
    assert(back_buf.created_at == buffer.created_at);
    assert(strcmp(back_buf.status, buffer.status) == 0);

    heapstore_ipc_channel_t missing;
    AIRY_MEMSET(&missing, 0, sizeof(missing));
    assert(load_channel_from_file("no_such_channel", &missing) == heapstore_ERR_NOT_FOUND);
    assert(load_channel_from_file(NULL, &missing) == heapstore_ERR_INVALID_PARAM);
    assert(load_channel_from_file(channel.channel_id, NULL) == heapstore_ERR_INVALID_PARAM);

    char path[heapstore_IPC_MAX_PATH + 256];
    snprintf(path, sizeof(path), "%s/channels/%s.json", s_ipc_path, channel.channel_id);
    remove(path);
    snprintf(path, sizeof(path), "%s/buffers/%s.json", s_ipc_path, buffer.buffer_id);
    remove(path);

    printf("PASS\n");
}

int main(void)
{
    printf("=== AgentRT heapstore IPC Unit Tests ===\n\n");

    /* 隔离测试数据：root 解析依赖 AIRY_HOME 体系，独立 home 避免
     * 触碰真实生产数据分区（~/.airymaxrt/data/agentrt/heapstore） */
    setenv("AIRY_HOME", "/tmp/agentrt_hs_test_home", 1);
    setenv("AIRY_RUNTIME_DIR", "/tmp/agentrt_hs_test_run", 1);

    test_ipc_init_shutdown();
    test_ipc_channel_crud();
    test_ipc_buffer_crud();
    test_ipc_stats();
    test_ipc_invalid_params();
    test_ipc_not_found();
    test_ipc_multiple_channels();
    test_ipc_persist_roundtrip();

    printf("\n=== All IPC Tests Passed ===\n");
    return 0;
}
