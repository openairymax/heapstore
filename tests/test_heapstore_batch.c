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
#include "platform.h"
#include "io.h"

#include <stdio.h>
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

/** commit 落库走 SQLite registry：固定根目录先递归清除保证幂等。 */
static void clean_batch_dir(void)
{
    airy_io_remove_dir_recursive(AIRY_TMP_DIR "/heapstore_batch_test");
}

static void fill_session(heapstore_session_record_t *session, const char *id)
{
    memset(session, 0, sizeof(*session));
    snprintf(session->id, sizeof(session->id), "%s", id);
}

/**
 * @brief Test batch context init and destroy
 */
static void test_batch_init_destroy(void)
{
    printf("\n=== Test: Batch Init/Destroy ===\n");

    heapstore_batch_context_t *ctx = heapstore_batch_begin(100);
    TEST_ASSERT(ctx != NULL, "batch_begin should succeed");
    TEST_ASSERT_EQ(0, (int)heapstore_batch_get_count(ctx), "initial count should be 0");
    TEST_ASSERT(ctx != NULL && ctx->capacity == 100, "capacity should match");

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
    TEST_ASSERT(ctx != NULL && ctx->capacity == HEAPSTORE_BATCH_MAX_ITEMS,
                "default capacity should be HEAPSTORE_BATCH_MAX_ITEMS");
    heapstore_batch_context_destroy(ctx);

    heapstore_session_record_t session;
    fill_session(&session, "sess_param");

    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_session(NULL, &session),
                   "add_session with NULL ctx should fail");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_session(NULL, NULL),
                   "add_session with NULL both should fail");

    ctx = heapstore_batch_begin(10);
    if (!ctx) {
        TEST_ASSERT(false, "Failed to init batch context");
        return;
    }

    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_session(ctx, NULL),
                   "add_session with NULL record should fail");
    TEST_ASSERT_EQ(0, (int)heapstore_batch_get_count(ctx), "rejected adds must not change count");

    heapstore_batch_context_destroy(ctx);
}

/**
 * @brief Test batch commit（commit 走 registry 落库，须先初始化 heapstore）
 */
static void test_batch_commit(void)
{
    printf("\n=== Test: Batch Commit ===\n");

    clean_batch_dir();

    heapstore_config_t config = {0};
    config.root_path = AIRY_TMP_DIR "/heapstore_batch_test";
    config.enable_auto_cleanup = false;
    heapstore_error_t err = heapstore_init(&config);
    TEST_ASSERT_EQ(heapstore_SUCCESS, err, "heapstore_init should succeed");

    heapstore_batch_context_t *ctx = heapstore_batch_begin(100);
    if (!ctx) {
        TEST_ASSERT(false, "Failed to init batch context");
        heapstore_shutdown();
        return;
    }

    for (int i = 0; i < 5; i++) {
        char id[64];
        snprintf(id, sizeof(id), "batch_s%d", i);
        heapstore_session_record_t session;
        fill_session(&session, id);
        err = heapstore_batch_add_session(ctx, &session);
        if (err != heapstore_SUCCESS) {
            break;
        }
    }

    TEST_ASSERT_EQ(5, (int)heapstore_batch_get_count(ctx), "should have 5 items before commit");

    err = heapstore_batch_commit(ctx);
    TEST_ASSERT_EQ(heapstore_SUCCESS, err, "commit of session entries should succeed");
    TEST_ASSERT_EQ(0, (int)heapstore_batch_get_count(ctx), "count should be 0 after commit");

    heapstore_batch_context_destroy(ctx);
    heapstore_shutdown();
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

    TEST_ASSERT(ctx->capacity == small_capacity, "capacity should match");

    for (size_t i = 0; i < small_capacity; i++) {
        char id[32];
        snprintf(id, sizeof(id), "sess_cap%zu", i);
        heapstore_session_record_t session;
        fill_session(&session, id);
        heapstore_error_t err = heapstore_batch_add_session(ctx, &session);
        TEST_ASSERT_EQ(heapstore_SUCCESS, err, "add within capacity should succeed");
    }

    TEST_ASSERT_EQ((int)small_capacity, (int)heapstore_batch_get_count(ctx),
                   "should reach capacity limit");

    heapstore_session_record_t overflow;
    fill_session(&overflow, "sess_overflow");
    heapstore_error_t err = heapstore_batch_add_session(ctx, &overflow);
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
}

/**
 * @brief session-only 入队白盒：链表顺序与载荷落位
 */
static void test_batch_add_records(void)
{
    printf("\n=== Test: Batch Add Session Records ===\n");

    heapstore_batch_context_t *ctx = heapstore_batch_begin(16);
    if (!ctx) {
        TEST_ASSERT(false, "Failed to init batch context");
        return;
    }

    static const char *const ids[3] = { "sess_a", "sess_b", "sess_c" };
    for (int i = 0; i < 3; i++) {
        heapstore_session_record_t session;
        fill_session(&session, ids[i]);
        TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_batch_add_session(ctx, &session),
                       "add_session should succeed");
    }
    TEST_ASSERT_EQ(3, (int)heapstore_batch_get_count(ctx), "count should be 3 after adding");

    /* 白盒校验：链表顺序与载荷落位必须与入队顺序一一对应 */
    const heapstore_batch_item_t *node = ctx->head;
    int idx = 0;
    while (node && idx < 3) {
        TEST_ASSERT(strcmp(node->session.id, ids[idx]) == 0,
                    "session payload should land in order");
        node = node->next;
        idx++;
    }
    TEST_ASSERT_EQ(3, idx, "linked list should contain 3 nodes");
    TEST_ASSERT(node == NULL, "node count should stop at 3");

    heapstore_batch_context_destroy(ctx);
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
    test_batch_parameter_validation();
    test_batch_commit();
    test_batch_capacity_limit();
    test_batch_destroy_safety();
    test_batch_add_records();

    printf("\n========================================\n");
    printf("Test Results: %d passed, %d failed\n", test_passes, test_failures);
    printf("Total tests: %d\n", test_passes + test_failures);
    printf("========================================\n");

    return (test_failures > 0) ? 1 : 0;
}
