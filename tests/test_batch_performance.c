// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file test_batch_performance.c
  * @brief heapstore 批量插入性能测试
 *
 */

// @owner: team-B
#include "../include/heapstore.h"
#include "../include/heapstore_registry.h"
#include "platform.h"
#include "airy_memory.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_RECORDS 1000

/* 性能基线要求每次运行都从空库开始计满插耗时；
 * 上一轮运行（或中断）遗留的 registry.db 会令主键冲突，
 * 故先递归清除两个固定基准目录（airy_io_remove_dir_recursive 幂等）。 */
static void clean_baseline_dirs(void)
{
    airy_io_remove_dir_recursive(AIRY_TMP_DIR "/heapstore_perf_test");
    airy_io_remove_dir_recursive(AIRY_TMP_DIR "/heapstore_perf_test_batch");
}

static void init_agent_record(heapstore_agent_record_t *record, int index)
{
    uint64_t now_s = airy_time_wall_ms() / 1000ULL;

    AIRY_MEMSET(record, 0, sizeof(*record));
    snprintf(record->id, sizeof(record->id), "agent_%06d", index);
    snprintf(record->name, sizeof(record->name), "Test Agent %d", index);
    snprintf(record->type, sizeof(record->type), "test_type");
    snprintf(record->version, sizeof(record->version), "1.0.0");
    snprintf(record->status, sizeof(record->status), "active");
    snprintf(record->config_path, sizeof(record->config_path), "/path/to/config_%d.json", index);
    record->created_at = now_s;
    record->updated_at = now_s;
}

static int test_single_insert_performance(void)
{
    printf("\n=== 测试：单条插入性能（基准） ===\n\n");

    heapstore_config_t config = {0};
    config.root_path = AIRY_TMP_DIR "/heapstore_perf_test";
    config.enable_auto_cleanup = false;

    heapstore_error_t err = heapstore_init(&config);
    if (err != heapstore_SUCCESS) {
        printf("❌ heapstore_init failed: %s\n", heapstore_strerror(err));
        return -1;
    }

    double start_time = (double)airy_time_ms();

    for (int i = 0; i < NUM_RECORDS; i++) {
        heapstore_agent_record_t record;
        init_agent_record(&record, i);

        err = heapstore_registry_add_agent(&record);
        if (err != heapstore_SUCCESS) {
            printf("❌ heapstore_registry_add_agent failed at %d: %s\n", i,
                   heapstore_strerror(err));
            heapstore_shutdown();
            return -1;
        }
    }

    double end_time = (double)airy_time_ms();
    double total_time = end_time - start_time;
    double avg_per_record = total_time / NUM_RECORDS;
    double records_per_sec = NUM_RECORDS / (total_time / 1000.0);

    printf("✅ 单条插入 %d 条记录:\n", NUM_RECORDS);
    printf("   总耗时：%.2f ms\n", total_time);
    printf("   平均每条：%.3f ms\n", avg_per_record);
    printf("   吞吐量：%.2f records/sec\n", records_per_sec);

    heapstore_shutdown();
    return 0;
}

static int test_batch_insert_performance(void)
{
    printf("\n=== 测试：批量插入性能（事务优化） ===\n\n");

    heapstore_config_t config = {0};
    config.root_path = AIRY_TMP_DIR "/heapstore_perf_test_batch";
    config.enable_auto_cleanup = false;

    heapstore_error_t err = heapstore_init(&config);
    if (err != heapstore_SUCCESS) {
        printf("❌ heapstore_init failed: %s\n", heapstore_strerror(err));
        return -1;
    }

    heapstore_agent_record_t *records = malloc(NUM_RECORDS * sizeof(heapstore_agent_record_t));
    if (!records) {
        printf("❌ malloc failed\n");
        heapstore_shutdown();
        return -1;
    }

    for (int i = 0; i < NUM_RECORDS; i++) {
        init_agent_record(&records[i], i);
    }

    double start_time = (double)airy_time_ms();

    err = heapstore_registry_batch_insert_agents(records, NUM_RECORDS);
    if (err != heapstore_SUCCESS) {
        printf("❌ heapstore_registry_batch_insert_agents failed: %s\n", heapstore_strerror(err));
        free(records);
        heapstore_shutdown();
        return -1;
    }

    double end_time = (double)airy_time_ms();
    double total_time = end_time - start_time;
    double avg_per_record = total_time / NUM_RECORDS;
    double records_per_sec = NUM_RECORDS / (total_time / 1000.0);

    printf("✅ 批量插入 %d 条记录:\n", NUM_RECORDS);
    printf("   总耗时：%.2f ms\n", total_time);
    printf("   平均每条：%.3f ms\n", avg_per_record);
    printf("   吞吐量：%.2f records/sec\n", records_per_sec);

    free(records);
    heapstore_shutdown();
    return 0;
}

static int test_comparison(void)
{
    printf("\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║           heapstore 批量插入性能对比测试                 ║\n");
    printf("╠════════════════════════════════════════════════════════╣\n");
    printf("║ 测试记录数：%6d                                   ║\n", NUM_RECORDS);
    printf("╚════════════════════════════════════════════════════════╝\n");
    printf("\n");

    int single_result = test_single_insert_performance();
    int batch_result = test_batch_insert_performance();

    if (single_result == 0 && batch_result == 0) {
        printf("\n");
        printf("╔════════════════════════════════════════════════════════╗\n");
        printf("║                    测试完成！                           ║\n");
        printf("╠════════════════════════════════════════════════════════╣\n");
        printf("║ 预期性能提升：5-10 倍                                   ║\n");
        printf("║ 批量事务显著减少 SQLite 的磁盘 I/O 和锁竞争                ║\n");
        printf("╚════════════════════════════════════════════════════════╝\n");
        printf("\n");
    }

    return (single_result == 0 && batch_result == 0) ? 0 : 1;
}

int main(void)
{
    printf("\n");
    printf("========================================\n");
    printf(" heapstore Batch Insert Performance Test\n");
    printf("========================================\n\n");

    clean_baseline_dirs();

    return test_comparison();
}
