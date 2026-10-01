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

    /* 白盒：无 trace 入口不得写入任何 trace_id，且载荷字段必须落位。 */
    const heapstore_batch_item_t *first = ctx->head;
    TEST_ASSERT(first != NULL, "head should be linked after add_log");
    if (first) {
        TEST_ASSERT_EQ(HEAPSTORE_BATCH_ITEM_LOG, (int)first->type, "node type should be LOG");
        TEST_ASSERT(strcmp(first->data.log.service, "test_service") == 0,
                    "service should be copied");
        TEST_ASSERT(strcmp(first->data.log.message, "Test message") == 0,
                    "message should be copied");
        TEST_ASSERT_EQ(HEAPSTORE_LOG_INFO, first->data.log.level, "level should be copied");
        TEST_ASSERT(first->data.log.trace_id[0] == '\0', "trace_id should stay empty");
    }

    err = heapstore_batch_add_log_with_trace(ctx, "test_service2", HEAPSTORE_LOG_ERROR,
                                             "trace_001", "Error message");
    TEST_ASSERT_EQ(heapstore_SUCCESS, err, "add_log_with_trace should succeed");
    TEST_ASSERT_EQ(2, (int)heapstore_batch_get_count(ctx), "count should be 2 after second add");

    const heapstore_batch_item_t *second = first ? first->next : NULL;
    TEST_ASSERT(second != NULL, "second node should be linked");
    if (second) {
        TEST_ASSERT(strcmp(second->data.log.trace_id, "trace_001") == 0,
                    "trace variant should carry trace_id");
    }

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

/**
 * @brief 填充 7 类记录用于入队/校验测试。
 */
static void fill_records(heapstore_session_record_t *session, heapstore_agent_record_t *agent,
                         heapstore_skill_record_t *skill, heapstore_memory_pool_t *pool,
                         heapstore_memory_allocation_t *alloc, heapstore_ipc_channel_t *channel,
                         heapstore_ipc_buffer_t *buffer)
{
    memset(session, 0, sizeof(*session));
    memset(agent, 0, sizeof(*agent));
    memset(skill, 0, sizeof(*skill));
    memset(pool, 0, sizeof(*pool));
    memset(alloc, 0, sizeof(*alloc));
    memset(channel, 0, sizeof(*channel));
    memset(buffer, 0, sizeof(*buffer));

    snprintf(session->id, sizeof(session->id), "sess_001");
    snprintf(agent->id, sizeof(agent->id), "agent_001");
    snprintf(skill->id, sizeof(skill->id), "skill_001");
    snprintf(pool->pool_id, sizeof(pool->pool_id), "pool_001");
    snprintf(alloc->allocation_id, sizeof(alloc->allocation_id), "alloc_001");
    snprintf(channel->channel_id, sizeof(channel->channel_id), "ch_001");
    snprintf(buffer->buffer_id, sizeof(buffer->buffer_id), "buf_001");
}

/**
 * @brief 覆盖 7 类记录入队：类型标签序列与载荷落位
 */
static void test_batch_add_records(void)
{
    printf("\n=== Test: Batch Add Records ===\n");

    heapstore_batch_context_t *ctx = heapstore_batch_begin(16);
    if (!ctx) {
        TEST_ASSERT(false, "Failed to init batch context");
        return;
    }

    heapstore_session_record_t session;
    heapstore_agent_record_t agent;
    heapstore_skill_record_t skill;
    heapstore_memory_pool_t pool;
    heapstore_memory_allocation_t alloc;
    heapstore_ipc_channel_t channel;
    heapstore_ipc_buffer_t buffer;
    fill_records(&session, &agent, &skill, &pool, &alloc, &channel, &buffer);

    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_batch_add_session(ctx, &session),
                   "add_session should succeed");
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_batch_add_agent(ctx, &agent),
                   "add_agent should succeed");
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_batch_add_skill(ctx, &skill),
                   "add_skill should succeed");
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_batch_add_memory_pool(ctx, &pool),
                   "add_memory_pool should succeed");
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_batch_add_allocation(ctx, &alloc),
                   "add_allocation should succeed");
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_batch_add_ipc_channel(ctx, &channel),
                   "add_ipc_channel should succeed");
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_batch_add_ipc_buffer(ctx, &buffer),
                   "add_ipc_buffer should succeed");
    TEST_ASSERT_EQ(7, (int)heapstore_batch_get_count(ctx), "count should be 7 after adding");

    /* 白盒校验：节点类型标签与载荷落位必须与入队顺序一一对应 */
    const heapstore_batch_item_type_t expect_types[7] = {
        HEAPSTORE_BATCH_ITEM_SESSION,       HEAPSTORE_BATCH_ITEM_AGENT,
        HEAPSTORE_BATCH_ITEM_SKILL,         HEAPSTORE_BATCH_ITEM_MEMORY_POOL,
        HEAPSTORE_BATCH_ITEM_MEMORY_ALLOC,  HEAPSTORE_BATCH_ITEM_IPC_CHANNEL,
        HEAPSTORE_BATCH_ITEM_IPC_BUFFER,
    };
    const heapstore_batch_item_t *node = ctx->head;
    int idx = 0;
    while (node && idx < 7) {
        TEST_ASSERT_EQ(expect_types[idx], node->type, "batch node type should match order");
        node = node->next;
        idx++;
    }
    TEST_ASSERT_EQ(7, idx, "linked list should contain 7 nodes");
    TEST_ASSERT(node == NULL, "node count should stop at 7");

    TEST_ASSERT(strcmp(ctx->head->data.session.id, "sess_001") == 0,
                "session payload should land in session slot");
    TEST_ASSERT(strcmp(ctx->head->next->data.agent.id, "agent_001") == 0,
                "agent payload should land in agent slot");
    TEST_ASSERT(strcmp(ctx->head->next->next->data.skill.id, "skill_001") == 0,
                "skill payload should land in skill slot");
    TEST_ASSERT(strcmp(ctx->head->next->next->next->data.memory_pool.pool_id, "pool_001") == 0,
                "memory_pool payload should land in memory_pool slot");
    TEST_ASSERT(strcmp(ctx->head->next->next->next->next->data.memory_alloc.allocation_id,
                       "alloc_001") == 0,
                "allocation payload should land in memory_alloc slot");
    TEST_ASSERT(strcmp(ctx->head->next->next->next->next->next->data.ipc_channel.channel_id,
                       "ch_001") == 0,
                "ipc_channel payload should land in ipc_channel slot");
    TEST_ASSERT(strcmp(ctx->head->next->next->next->next->next->next->data.ipc_buffer.buffer_id,
                       "buf_001") == 0,
                "ipc_buffer payload should land in ipc_buffer slot");

    heapstore_batch_rollback(ctx);
    TEST_ASSERT_EQ(0, (int)heapstore_batch_get_count(ctx), "count should be 0 after rollback");
    heapstore_batch_context_destroy(ctx);
}

/**
 * @brief 覆盖 7 类记录的参数校验与容量上限
 */
static void test_batch_record_validation(void)
{
    printf("\n=== Test: Record Validation ===\n");

    heapstore_session_record_t session;
    heapstore_agent_record_t agent;
    heapstore_skill_record_t skill;
    heapstore_memory_pool_t pool;
    heapstore_memory_allocation_t alloc;
    heapstore_ipc_channel_t channel;
    heapstore_ipc_buffer_t buffer;
    fill_records(&session, &agent, &skill, &pool, &alloc, &channel, &buffer);

    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_session(NULL, &session),
                   "add_session with NULL ctx should fail");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_session(NULL, NULL),
                   "add_session with NULL both should fail");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_agent(NULL, &agent),
                   "add_agent with NULL ctx should fail");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_skill(NULL, &skill),
                   "add_skill with NULL ctx should fail");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_memory_pool(NULL, &pool),
                   "add_memory_pool with NULL ctx should fail");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_allocation(NULL, &alloc),
                   "add_allocation with NULL ctx should fail");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_ipc_channel(NULL, &channel),
                   "add_ipc_channel with NULL ctx should fail");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_ipc_buffer(NULL, &buffer),
                   "add_ipc_buffer with NULL ctx should fail");

    heapstore_batch_context_t *ctx = heapstore_batch_begin(4);
    if (!ctx) {
        TEST_ASSERT(false, "Failed to init batch context");
        return;
    }

    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_session(ctx, NULL),
                   "add_session with NULL record should fail");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_agent(ctx, NULL),
                   "add_agent with NULL record should fail");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_skill(ctx, NULL),
                   "add_skill with NULL record should fail");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_memory_pool(ctx, NULL),
                   "add_memory_pool with NULL record should fail");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_allocation(ctx, NULL),
                   "add_allocation with NULL record should fail");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_ipc_channel(ctx, NULL),
                   "add_ipc_channel with NULL record should fail");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_batch_add_ipc_buffer(ctx, NULL),
                   "add_ipc_buffer with NULL record should fail");
    TEST_ASSERT_EQ(0, (int)heapstore_batch_get_count(ctx), "rejected adds must not change count");

    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_batch_add_session(ctx, &session), "fill 1/4");
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_batch_add_agent(ctx, &agent), "fill 2/4");
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_batch_add_skill(ctx, &skill), "fill 3/4");
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_batch_add_ipc_buffer(ctx, &buffer), "fill 4/4");
    TEST_ASSERT_EQ(heapstore_ERR_OUT_OF_MEMORY, heapstore_batch_add_ipc_channel(ctx, &channel),
                   "add beyond capacity should return OUT_OF_MEMORY");
    TEST_ASSERT_EQ(4, (int)heapstore_batch_get_count(ctx), "count should stay at capacity");

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
    test_batch_add_log();
    test_batch_parameter_validation();
    test_batch_commit_rollback();
    test_batch_capacity_limit();
    test_batch_destroy_safety();
    test_batch_add_records();
    test_batch_record_validation();

    printf("\n========================================\n");
    printf("Test Results: %d passed, %d failed\n", test_passes, test_failures);
    printf("Total tests: %d\n", test_passes + test_failures);
    printf("========================================\n");

    return (test_failures > 0) ? 1 : 0;
}
