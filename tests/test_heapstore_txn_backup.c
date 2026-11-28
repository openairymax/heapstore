// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file test_heapstore_txn_backup.c
 * @brief heapstore 显式事务边界与整库在线备份单元测试（0.1.21 D-21）
 *
 * @note 测试覆盖目标: 事务状态机（begin/commit/rollback/错误码）与
 *       heapstore_backup() 备份后以新根重新初始化并读回数据。
 */

// @owner: team-B
#include "heapstore.h"
#include "heapstore_registry.h"
#include "platform.h"
#include "io.h"
#include "airy_memory.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int test_passes = 0;
static int test_failures = 0;

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
        long test_exp = (long)(expected);                                                \
        long test_act = (long)(actual);                                                  \
        if (test_exp != test_act) {                                                      \
            printf("FAIL: %s (expected=%ld, actual=%ld, line %d)\n", msg, test_exp,      \
                   test_act, __LINE__);                                                  \
            test_failures++;                                                             \
        } else {                                                                         \
            printf("PASS: %s\n", msg);                                                   \
            test_passes++;                                                               \
        }                                                                                \
    } while (0)

#define TEST_TXN_ROOT AIRY_TMP_DIR "/heapstore_d21_txn"
#define TEST_BACKUP_SRC AIRY_TMP_DIR "/heapstore_d21_backup_src"
#define TEST_BACKUP_DST AIRY_TMP_DIR "/heapstore_d21_backup_dst"

static void clean_test_dirs(void)
{
    airy_io_remove_dir_recursive(TEST_TXN_ROOT);
    airy_io_remove_dir_recursive(TEST_BACKUP_SRC);
    airy_io_remove_dir_recursive(TEST_BACKUP_DST);
}

static heapstore_error_t init_at(const char *root)
{
    heapstore_config_t config = {0};
    config.root_path = root;
    config.enable_auto_cleanup = false;
    return heapstore_init(&config);
}

static void fill_session(heapstore_session_record_t *session, const char *id)
{
    AIRY_MEMSET(session, 0, sizeof(*session));
    snprintf(session->id, sizeof(session->id), "%s", id);
    snprintf(session->user_id, sizeof(session->user_id), "user_d21");
    session->created_at = (uint64_t)time(NULL);
    session->last_active_at = session->created_at;
    session->ttl_seconds = 3600;
    snprintf(session->status, sizeof(session->status), "active");
}

static void test_txn_state_machine(void)
{
    printf("\n=== Test: Transaction State Machine ===\n");

    /* 未初始化：三个入口都必须给出明确错误码。 */
    TEST_ASSERT_EQ(heapstore_ERR_NOT_INITIALIZED, heapstore_txn_begin(),
                   "txn_begin before init should fail");
    TEST_ASSERT_EQ(heapstore_ERR_NOT_INITIALIZED, heapstore_txn_commit(),
                   "txn_commit before init should fail");
    TEST_ASSERT_EQ(heapstore_ERR_NOT_INITIALIZED, heapstore_txn_rollback(),
                   "txn_rollback before init should fail");
    TEST_ASSERT_EQ(heapstore_ERR_INVALID_PARAM, heapstore_backup(NULL),
                   "backup with NULL path should fail");
    TEST_ASSERT_EQ(heapstore_ERR_NOT_INITIALIZED, heapstore_backup(TEST_BACKUP_DST),
                   "backup before init should fail");

    TEST_ASSERT_EQ(heapstore_SUCCESS, init_at(TEST_TXN_ROOT), "heapstore_init should succeed");

    /* 未 begin 时 commit/rollback 必须失败。 */
    TEST_ASSERT_EQ(heapstore_ERR_TXN_NOT_ACTIVE, heapstore_txn_commit(),
                   "txn_commit without begin should fail");
    TEST_ASSERT_EQ(heapstore_ERR_TXN_NOT_ACTIVE, heapstore_txn_rollback(),
                   "txn_rollback without begin should fail");

    /* 重复 begin 必须失败。 */
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_txn_begin(), "txn_begin should succeed");
    TEST_ASSERT_EQ(heapstore_ERR_TXN_ACTIVE, heapstore_txn_begin(),
                   "duplicate txn_begin should fail");

    /* rollback：写入的会话必须消失。 */
    heapstore_session_record_t rolled_back;
    fill_session(&rolled_back, "d21_rb_session");
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_registry_add_session(&rolled_back),
                   "add_session inside transaction should succeed");
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_txn_rollback(),
                   "txn_rollback should succeed");

    heapstore_session_record_t fetched;
    AIRY_MEMSET(&fetched, 0, sizeof(fetched));
    TEST_ASSERT_EQ(heapstore_ERR_NOT_FOUND, heapstore_registry_get_session(rolled_back.id, &fetched),
                   "rolled back session must not exist");

    /* commit：写入的会话必须持久化。 */
    heapstore_session_record_t committed;
    fill_session(&committed, "d21_commit_session");
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_txn_begin(), "second txn_begin should succeed");
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_registry_add_session(&committed),
                   "add_session inside transaction should succeed");
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_txn_commit(), "txn_commit should succeed");

    AIRY_MEMSET(&fetched, 0, sizeof(fetched));
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_registry_get_session(committed.id, &fetched),
                   "committed session must exist");
    TEST_ASSERT(strcmp(fetched.user_id, committed.user_id) == 0,
                "committed session content must match");

    heapstore_shutdown();
}

static void test_backup_restore(void)
{
    printf("\n=== Test: Backup And Restore ===\n");

    TEST_ASSERT_EQ(heapstore_SUCCESS, init_at(TEST_BACKUP_SRC), "backup source init should succeed");

    heapstore_session_record_t source;
    fill_session(&source, "d21_backup_session");
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_registry_add_session(&source),
                   "source add_session should succeed");

    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_backup(TEST_BACKUP_DST),
                   "heapstore_backup should succeed");

    heapstore_shutdown();

    /* 备份目录作为新根重新初始化，读回会话以证明数据被完整复制。 */
    TEST_ASSERT_EQ(heapstore_SUCCESS, init_at(TEST_BACKUP_DST), "backup destination init should succeed");

    heapstore_session_record_t restored;
    AIRY_MEMSET(&restored, 0, sizeof(restored));
    TEST_ASSERT_EQ(heapstore_SUCCESS, heapstore_registry_get_session(source.id, &restored),
                   "backed up session must exist in destination root");
    TEST_ASSERT(strcmp(restored.user_id, source.user_id) == 0,
                "backed up session content must match");

    heapstore_shutdown();
}

int main(void)
{
    printf("=== heapstore transaction/backup tests (0.1.21 D-21) ===\n");

    clean_test_dirs();
    test_txn_state_machine();
    test_backup_restore();

    printf("\n%d passed, %d failed\n", test_passes, test_failures);
    return test_failures == 0 ? 0 : 1;
}
