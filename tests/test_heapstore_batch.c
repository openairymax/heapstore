// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file test_heapstore_batch.c
 * @brief heapstore 批量写入模块单元测试
 *
 * @note 测试覆盖目标: 90%+
 */

// @owner: team-B
#include "heapstore.h"
#include "heapstore_batch.h"
#include "heapstore_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(condition, msg)                        \
    do {                                                   \
        if (!(condition)) {                                \
            printf("FAIL: %s (line %d)\n", msg, __LINE__); \
            test_failures++;                               \
        } else {                                           \
            printf("PASS: %s\n", msg);                     \
            test_passes++;                                 \
        }                                                  \
    } while (0)

#define TEST_ASSERT_EQ(expected, actual, msg)                                            \
    do {                                                                                 \
        if ((expected) != (actual)) {                                                    \
            printf("FAIL: %s (expected=%d, actual=%d, line %d)\n", msg, (int)(expected), \
                   (int)(actual), __LINE__);                                             \
            test_failures++;                                                             \
        } else {                                                                         \
            printf("PASS: %s\n", msg);                                                   \
            test_passes++;                                                               \
        }                                                                                \
    } while (0)

static int test_passes = 0;
static int test_failures = 0;

/**
 * @brief Test batch context init and destroy
 */
static void test_batch_init_destroy(void)
{
    printf("\n=== Test: Batch Init/Destroy ===\n");

    heapstore_batch_context_t *ctx = heapstore_batch_begin(100);
    TEST_ASSERT(ctx != NULL, "batch_begin should succeed");
    TEST_ASSERT_EQ(0, (int)heapstore_batch_get_count(ctx), "initial count should be 0");
    TEST_ASSERT_EQ(100, (int)heapstore_batch_get_capacity(ctx), "capacity should match");

    heapstore_batch_context_destroy(ctx);
}

/**
 * @brief Test batch log addition
 */
static void test_batch_add_log(void)
{
    printf("\n=== Test: Batch Add Log ===\n");

    heapstore_batch_context_t *ctx = heapstore_batch_begin(100);
    if (!ctx) {
        TEST_ASSERT(false, "Failed to init batch context");
        return;
    }

    heapstore_error_t err =
        heapstore_batch_add_log(ctx, "test_service", HEAPSTORE_LOG_INFO, "Test message");
    TEST_ASSERT_EQ(heapstore_SUCCESS, err, "add_log should succeed");
    TEST_ASSERT_EQ(1, (int)heapstore_batch_get_count(ctx), "count should be 1 after add");

    err = heapstore_batch_add_log_with_trace(ctx, "test_service2", HEAPSTORE_LOG_ERROR,
                                             "trace_001", "Error message");
    TEST_ASSERT_EQ(heapstore_SUCCESS, err, "add_log_with_trace should succeed");
    TEST_ASSERT_EQ(2, (int)heapstore_batch_get_count(ctx), "count should be 2 after second add");

    heapstore_batch_context_destroy(ctx);
}

/**
 * @brief Test parameter validation (boundary conditions)
 */
static void test_batch_parameter_validation(void)
{
    printf("\n=== Test: Parameter Validation ===\n");

    /* batch_size=0 语义为默认容量（HEAPSTORE_BATCH_MAX_ITEMS） */
    heapstore_batch_context_t *ctx = heapstore_batch_begin(0);
    TEST_ASSERT(ctx != NULL, "batch_begin with 0 should use default capacity");
    TEST_ASSERT_EQ(HEAPSTORE_BATCH_MAX_ITEMS, (int)heapstore_batch_get_capacity(ctx),
                   "default capacity should be HEAPSTORE_BATCH_MAX_ITEMS");
    heapstore_batch_context_destroy(ctx);

    heapstore_error_t err;
    err = heapstore_batch_add_log(NULL, "svc", HEAPSTORE_LOG_INFO, "msg");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, err, "add_log with NULL ctx should fail");

    ctx = heapstore_batch_begin(10);
    if (!ctx) {
        TEST_ASSERT(false, "Failed to init batch context");
        return;
    }

    err = heapstore_batch_add_log(ctx, NULL, HEAPSTORE_LOG_INFO, "msg");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, err, "add_log with NULL service should fail");

    err = heapstore_batch_add_log(ctx, "svc", HEAPSTORE_LOG_INFO, NULL);
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, err, "add_log with NULL message should fail");

    heapstore_batch_context_destroy(ctx);
}

/**
 * @brief Test batch commit and rollback
 */
static void test_batch_commit_rollback(void)
{
    printf("\n=== Test: Batch Commit/Rollback ===\n");

    heapstore_batch_context_t *ctx = heapstore_batch_begin(100);
    if (!ctx) {
        TEST_ASSERT(false, "Failed to init batch context");
        return;
    }

    for (int i = 0; i < 5; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Log message %d", i);
        heapstore_error_t err =
            heapstore_batch_add_log(ctx, "test_svc", HEAPSTORE_LOG_INFO, msg);
        if (err != heapstore_SUCCESS) {
            break;
        }
    }

    TEST_ASSERT_EQ(5, (int)heapstore_batch_get_count(ctx), "should have 5 items before commit");

    heapstore_error_t err = heapstore_batch_commit(ctx);
    TEST_ASSERT_EQ(heapstore_SUCCESS, err, "commit of log entries should succeed");
    TEST_ASSERT_EQ(0, (int)heapstore_batch_get_count(ctx), "count should be 0 after commit");

    heapstore_batch_context_destroy(ctx);

    ctx = heapstore_batch_begin(50);
    if (!ctx) {
        return;
    }

    err = heapstore_batch_add_log(ctx, "svc", HEAPSTORE_LOG_INFO, "rollback test");
    TEST_ASSERT_EQ(heapstore_SUCCESS, err, "add before rollback should succeed");

    heapstore_batch_rollback(ctx);
    TEST_ASSERT_EQ(0, (int)heapstore_batch_get_count(ctx), "count should be 0 after rollback");

    heapstore_batch_context_destroy(ctx);
}

/**
 * @brief Test capacity limits
 */
static void test_batch_capacity_limit(void)
{
    printf("\n=== Test: Capacity Limit ===\n");

    const size_t small_capacity = 3;
    heapstore_batch_context_t *ctx = heapstore_batch_begin(small_capacity);
    if (!ctx) {
        TEST_ASSERT(false, "Failed to init batch context");
        return;
    }

    TEST_ASSERT_EQ((int)small_capacity, (int)heapstore_batch_get_capacity(ctx),
                   "capacity should match");

    for (size_t i = 0; i < small_capacity; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "Item %zu", i);
        heapstore_error_t err = heapstore_batch_add_log(ctx, "svc", HEAPSTORE_LOG_INFO, msg);
        TEST_ASSERT_EQ(heapstore_SUCCESS, err, "add within capacity should succeed");
    }

    TEST_ASSERT_EQ((int)small_capacity, (int)heapstore_batch_get_count(ctx),
                   "should reach capacity limit");

    heapstore_error_t err = heapstore_batch_add_log(ctx, "svc", HEAPSTORE_LOG_INFO, "overflow");
    TEST_ASSERT_EQ(heapstore_ERR_OUT_OF_MEMORY, err, "overflow should return error");

    heapstore_batch_context_destroy(ctx);
}

/**
 * @brief Test destroy safety (NULL and repeated destroy)
 */
static void test_batch_destroy_safety(void)
{
    printf("\n=== Test: Destroy Safety ===\n");

    heapstore_batch_context_t *ctx = heapstore_batch_begin(10);
    if (!ctx) {
        return;
    }

    heapstore_batch_context_destroy(ctx);
    TEST_ASSERT(true, "destroy must not crash");

    /* Destroying the same pointer twice would be use-after-free; the
     * contract only guarantees NULL-safety. */
    heapstore_batch_context_destroy(NULL);
    TEST_ASSERT(true, "destroy with NULL must not crash");

    TEST_ASSERT_EQ(0, (int)heapstore_batch_get_count(NULL), "get_count with NULL returns 0");
    TEST_ASSERT_EQ(0, (int)heapstore_batch_get_capacity(NULL),
                   "get_capacity with NULL returns 0");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("========================================\n");
    printf("HeapStore Batch Module Unit Tests\n");
    printf("========================================\n");

    test_passes = 0;
    test_failures = 0;

    test_batch_init_destroy();
    test_batch_add_log();
    test_batch_parameter_validation();
    test_batch_commit_rollback();
    test_batch_capacity_limit();
    test_batch_destroy_safety();

    printf("\n========================================\n");
    printf("Test Results: %d passed, %d failed\n", test_passes, test_failures);
    printf("Total tests: %d\n", test_passes + test_failures);
    printf("========================================\n");

    return (test_failures > 0) ? 1 : 0;
}
