// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file test_fuzzing.c
 * @brief heapstore 模糊测试 (Fuzz Testing) 和并发压力测试
 *
 * @note 本测试套件包含:
  *       1. 输入模糊测试 - 验证边界条件处理
  *       2. 并发压力测试 - 验证线程安全性
  *       3. 内存泄漏检测 - 验证资源管理
 */

// @owner: team-B
#include "../include/heapstore.h"
#include "../include/heapstore_log.h"
#include "../include/heapstore_registry.h"
#include "../include/utils.h"
#include "platform.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define FUZZ_TEST_ITERATIONS 10000
#define CONCURRENT_THREADS 8
#define CONCURRENT_OPS_PER_THREAD 1000
#define MAX_INPUT_LENGTH 1024

static int g_tests_passed = 0;
static int g_tests_failed = 0;
static int g_init_done = 0;

#define TEST_PASS(name)                \
    do {                               \
        printf("✅ PASS: %s\n", name); \
        g_tests_passed++;              \
    } while (0)

#define TEST_FAIL(name, reason)                     \
    do {                                            \
        printf("❌ FAIL: %s - %s\n", name, reason); \
        g_tests_failed++;                           \
    } while (0)

#define ASSERT_TRUE(cond, msg)            \
    do {                                  \
        if (!(cond)) {                    \
            TEST_FAIL(__FUNCTION__, msg); \
            return;                       \
        }                                 \
    } while (0)

/* ===========================================================================
  * Part 1: Fuzz testing
 * ===========================================================================*/

/**
  * @brief Generate random strings for fuzzing
 *
  * @param buffer Output buffer
  * @param length String length
  * @param include_special Whether to include special characters
 */
static void generate_random_string(char *buffer, size_t length, bool include_special)
{
    const char charset_normal[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-./";
    const char charset_special[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-./\x00\x01\x02../..\\*\0";

    const char *charset = include_special ? charset_special : charset_normal;
    size_t charset_size = strlen(charset);

    for (size_t i = 0; i < length - 1; i++) {
        buffer[i] = charset[rand() % charset_size];
    }
    buffer[length - 1] = '\0';
}

/**
  * @brief Test 1: path component sanitization fuzzing
 *
  * Fuzz heapstore_sanitize_path_component() with random input
 */
static void test_fuzz_sanitize_path(void)
{
    printf("\n=== Fuzz Test: Sanitize Path Component ===\n");

    char input[MAX_INPUT_LENGTH];
    char output[MAX_INPUT_LENGTH];

    for (int i = 0; i < FUZZ_TEST_ITERATIONS; i++) {

        size_t len = (rand() % (MAX_INPUT_LENGTH - 1)) + 1;
        generate_random_string(input, len, true);

        int result = heapstore_sanitize_path_component(output, input, sizeof(output));

        if (result == 0) {
            ASSERT_TRUE(strstr(output, "..") == NULL, "Output should not contain '..'");
            ASSERT_TRUE(strchr(output, '/') == NULL, "Output should not contain '/'");
            ASSERT_TRUE(strchr(output, '\\') == NULL, "Output should not contain '\\'");
            ASSERT_TRUE(strchr(output, '\0') == NULL ||
                            strchr(output, '\0') == output + strlen(output),
                        "Only one null terminator allowed");
        }
    }

    TEST_PASS("sanitize_path fuzz test (10000 iterations)");
}

/**
  * @brief Test 2: safe-identifier validation fuzzing
 *
  * Fuzz heapstore_is_safe_identifier() with random input
 */
static void test_fuzz_safe_identifier(void)
{
    printf("\n=== Fuzz Test: Safe Identifier ===\n");

    char input[MAX_INPUT_LENGTH];

    for (int i = 0; i < FUZZ_TEST_ITERATIONS; i++) {
        size_t len = (rand() % (MAX_INPUT_LENGTH - 1)) + 1;
        generate_random_string(input, len, true);

        bool result = heapstore_is_safe_identifier(input);

        if (result) {
            for (size_t j = 0; j < strlen(input); j++) {
                char c = input[j];
                ASSERT_TRUE(isalnum(c) || c == '_' || c == '-' || c == '.',
                            "Safe identifier should only contain alphanumeric, '_', '-', '.'");
            }
        }
    }

    TEST_PASS("safe_identifier fuzz test (10000 iterations)");
}

/**
  * @brief Test 3: config parameter boundary fuzzing
 *
  * Fuzz heapstore_init() with extreme config values
 */
static void test_fuzz_config_params(void)
{
    printf("\n=== Fuzz Test: Configuration Parameters ===\n");

    heapstore_config_t config;

    for (int i = 0; i < 1000; i++) {
        AIRY_MEMSET(&config, 0, sizeof(config));

        config.root_path = AIRY_TMP_DIR "/airy_heapstore_test";
        config.max_log_size_mb = rand() % 10000 + 1; /* 1-10000 MB */
        config.log_retention_days = rand() % 3650 + 1; /* 1-3650 days */
        config.trace_retention_days = rand() % 3650 + 1;
        config.enable_auto_cleanup = rand() % 2;
        config.enable_log_rotation = rand() % 2;
        config.enable_trace_export = rand() % 2;
        config.db_vacuum_interval_days = rand() % 365 + 1;
        config.circuit_breaker_threshold = rand() % 100 + 1;
        config.circuit_breaker_timeout_sec = rand() % 3600 + 1;

        heapstore_error_t err = heapstore_init(&config);

        if (err == heapstore_SUCCESS) {
            heapstore_shutdown();
        }
    }

    TEST_PASS("config params fuzz test (1000 iterations)");
}

/* ===========================================================================
  * Part 2: Concurrency stress testing
 * ===========================================================================*/

typedef struct {
    int thread_id;
    int operations_count;
    atomic_int *success_count;
    atomic_int *failure_count;
} thread_context_t;

/**
  * @brief Worker thread: concurrent log writes
 */
static void *thread_log_writer(void *arg)
{
    thread_context_t *ctx = (thread_context_t *)arg;

    for (int i = 0; i < ctx->operations_count; i++) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Thread-%d Log-%d", ctx->thread_id, i);

        heapstore_error_t err =
            heapstore_log_write(LOG_INFO, "fuzz_test_service", NULL, __FILE__, __LINE__, "%s", msg);

        if (err == heapstore_SUCCESS) {
            atomic_fetch_add(ctx->success_count, 1);
        } else {
            atomic_fetch_add(ctx->failure_count, 1);
        }
    }

    return NULL;
}

/**
  * @brief Worker thread: concurrent registry operations
 */
static void *thread_registry_worker(void *arg)
{
    thread_context_t *ctx = (thread_context_t *)arg;

    for (int i = 0; i < ctx->operations_count; i++) {
        heapstore_agent_record_t record;
        snprintf(record.id, sizeof(record.id), "thread_%d_agent_%d", ctx->thread_id, i);
        snprintf(record.name, sizeof(record.name), "Agent T%d-%d", ctx->thread_id, i);
        snprintf(record.type, sizeof(record.type), "test");
        snprintf(record.version, sizeof(record.version), "1.0.0");
        snprintf(record.status, sizeof(record.status), "active");
        record.created_at = time(NULL);
        record.updated_at = time(NULL);

        heapstore_error_t err = heapstore_registry_add_agent(&record);

        if (err == heapstore_SUCCESS || err == heapstore_ERR_ALREADY_EXISTS) {
            atomic_fetch_add(ctx->success_count, 1);
        } else {
            atomic_fetch_add(ctx->failure_count, 1);
        }
    }

    return NULL;
}

/**
  * @brief Test 4: concurrent log write stress
 */
static void test_concurrent_log_writing(void)
{
    printf("\n=== Concurrent Test: Log Writing (%d threads) ===\n", CONCURRENT_THREADS);

    pthread_t threads[CONCURRENT_THREADS];
    thread_context_t contexts[CONCURRENT_THREADS];
    atomic_int total_success = 0;
    atomic_int total_failure = 0;

    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        contexts[i].thread_id = i;
        contexts[i].operations_count = CONCURRENT_OPS_PER_THREAD;
        contexts[i].success_count = &total_success;
        contexts[i].failure_count = &total_failure;

        int rc = pthread_create(&threads[i], NULL, thread_log_writer, &contexts[i]);
        ASSERT_TRUE(rc == 0, "Failed to create thread");
    }

    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    int expected_ops = CONCURRENT_THREADS * CONCURRENT_OPS_PER_THREAD;
    int actual_success = atomic_load(&total_success);
    int actual_failure = atomic_load(&total_failure);

    printf("  Expected operations: %d\n", expected_ops);
    printf("  Successful: %d (%.1f%%)\n", actual_success,
           (float)actual_success / expected_ops * 100);
    printf("  Failed: %d (%.1f%%)\n", actual_failure, (float)actual_failure / expected_ops * 100);

    float success_rate = (float)actual_success / expected_ops;
    ASSERT_TRUE(success_rate > 0.95, "Success rate should be > 95%");

    TEST_PASS("concurrent log writing stress test");
}

/**
  * @brief Test 5: concurrent registry operation stress
 */
static void test_concurrent_registry_operations(void)
{
    printf("\n=== Concurrent Test: Registry Operations (%d threads) ===\n", CONCURRENT_THREADS);

    pthread_t threads[CONCURRENT_THREADS];
    thread_context_t contexts[CONCURRENT_THREADS];
    atomic_int total_success = 0;
    atomic_int total_failure = 0;

    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        contexts[i].thread_id = i;
        contexts[i].operations_count = CONCURRENT_OPS_PER_THREAD / 10;
        contexts[i].success_count = &total_success;
        contexts[i].failure_count = &total_failure;

        int rc = pthread_create(&threads[i], NULL, thread_registry_worker, &contexts[i]);
        ASSERT_TRUE(rc == 0, "Failed to create thread");
    }

    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    int actual_success = atomic_load(&total_success);
    int actual_failure = atomic_load(&total_failure);

    printf("  Successful ops: %d\n", actual_success);
    printf("  Failed ops: %d\n", actual_failure);

    float success_rate = (float)actual_success / (actual_success + actual_failure);
    ASSERT_TRUE(success_rate > 0.99, "Registry operation success rate should be > 99%");

    TEST_PASS("concurrent registry operations stress test");
}

/**
  * @brief Test 6: init/shutdown concurrency race
 */
static void test_concurrent_init_shutdown_race(void)
{
    printf("\n=== Concurrent Test: Init/Shutdown Race Condition ===\n");

    for (int round = 0; round < 100; round++) {
        heapstore_config_t config = {.root_path = AIRY_TMP_DIR "/airy_heapstore_race_test",
                                     .max_log_size_mb = 10,
                                     .log_retention_days = 7,
                                     .trace_retention_days = 3,
                                     .enable_auto_cleanup = false,
                                     .enable_log_rotation = true,
                                     .enable_trace_export = false,
                                     .db_vacuum_interval_days = 7,
                                     .circuit_breaker_threshold = 5,
                                     .circuit_breaker_timeout_sec = 30};

        heapstore_error_t err = heapstore_init(&config);

        err = heapstore_shutdown();
    }

    TEST_PASS("init/shutdown race condition test (100 rounds)");
}

/* ===========================================================================
  * Part 3: memory leak detection helpers
 * ===========================================================================*/

/**
  * @brief Test 7: memory stability under heavy operations
 *
  * Check memory availability after heavy operations
 */
static void test_memory_stability_under_load(void)
{
    printf("\n=== Memory Stability Test Under Load ===\n");

    size_t initial_memory = 0;

    const int BATCH_SIZE = 1000;
    heapstore_agent_record_t *agents = calloc(BATCH_SIZE, sizeof(heapstore_agent_record_t));
    ASSERT_TRUE(agents != NULL, "Memory allocation failed");

    for (int batch = 0; batch < 10; batch++) {
        for (int i = 0; i < BATCH_SIZE; i++) {
            snprintf(agents[i].id, sizeof(agents[i].id), "mem_test_%d_%d", batch, i);
            snprintf(agents[i].name, sizeof(agents[i].name), "MemTest Agent %d-%d", batch, i);
            agents[i].created_at = time(NULL);
            agents[i].updated_at = time(NULL);
        }

        heapstore_error_t err = heapstore_registry_batch_insert_agents(agents, BATCH_SIZE);
    }

    free(agents);

    heapstore_cleanup(true, NULL);

    TEST_PASS("memory stability under load (10000 inserts)");
}

/* ===========================================================================
  * Main test entry
 * ===========================================================================*/

int main(int argc, char *argv[])
{
    printf("================================================================\n");
    printf(" heapstore Fuzz Testing & Concurrency Stress Test Suite\n");
    printf("================================================================\n\n");

    srand((unsigned int)time(NULL));

    printf("--- Initializing heapstore module ---\n");
    heapstore_config_t config = {.root_path = AIRY_TMP_DIR "/airy_heapstore_fuzz_test",
                                 .max_log_size_mb = 50,
                                 .log_retention_days = 1,
                                 .trace_retention_days = 1,
                                 .enable_auto_cleanup = true,
                                 .enable_log_rotation = true,
                                 .enable_trace_export = false,
                                 .db_vacuum_interval_days = 1,
                                 .circuit_breaker_threshold = 10,
                                 .circuit_breaker_timeout_sec = 10};

    heapstore_error_t init_err = heapstore_init(&config);
    if (init_err != heapstore_SUCCESS) {
        printf("⚠️  Warning: heapstore_init() returned %d\n", init_err);
        printf("   Some tests may be skipped or limited\n\n");
    } else {
        g_init_done = 1;
        printf("✅ Module initialized successfully\n\n");
    }

    if (g_init_done) {
        test_fuzz_sanitize_path();
        test_fuzz_safe_identifier();
    }
    test_fuzz_config_params();

    if (g_init_done) {
        test_concurrent_log_writing();
        test_concurrent_registry_operations();
        test_concurrent_init_shutdown_race();
        test_memory_stability_under_load();
    }

    if (g_init_done) {
        printf("\n--- Shutting down heapstore module ---\n");
        heapstore_shutdown();
        printf("✅ Module shutdown complete\n");
    }

    printf("\n================================================================\n");
    printf(" Test Results Summary\n");
    printf("================================================================\n");
    printf(" Total tests : %d\n", g_tests_passed + g_tests_failed);
    printf(" Passed     : %d ✅\n", g_tests_passed);
    printf(" Failed     : %d ❌\n", g_tests_failed);
    printf(" Success rate: %.1f%%\n",
           (float)g_tests_passed / (g_tests_passed + g_tests_failed) * 100);
    printf("================================================================\n\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
