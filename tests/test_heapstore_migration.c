// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file test_heapstore_migration.c
 * @brief Schema version file robustness tests: fail-closed on corrupt
 *        .schema_version files and atomic version writes.
 *
 * DoD (0.1.15 WS-5): an interrupted upgrade (truncated or emptied version
 * file) must NOT let old-schema data be treated as current-schema data.
 */

// @owner: team-C
#include "heapstore.h"
#include "heapstore_migration.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mirrors internal HEAPSTORE_MIGRATION_VERSION_FILE (heapstore_migration_internal.h). */
#define TEST_VERSION_FILE ".schema_version"
#define TEST_VERSION_TMP TEST_VERSION_FILE ".tmp"

static int g_checks = 0;

#define REQUIRE(cond)                                                          \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            return 1;                                                          \
        }                                                                      \
        g_checks++;                                                            \
    } while (0)

static void version_path(char *buf, size_t cap, const char *root)
{
    snprintf(buf, cap, "%s/%s", root, TEST_VERSION_FILE);
}

static void write_version_file(const char *root, const char *content)
{
    char path[512];
    version_path(path, sizeof(path), root);
    FILE *f = fopen(path, "w");
    if (!f) {
        printf("FAIL: cannot write %s\n", path);
        exit(1);
    }
    fputs(content, f);
    fclose(f);
}

static int file_exists(const char *root, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

/* 测试隔离：migtest_* 是 cwd 相对目录，上一次失败运行残留的损坏
 * .schema_version 会让 fail-closed 的 heapstore_init 在下一次运行
 * 开局即拒绝启动，产生与产品无关的假失败。每个用例开始前清场。 */
#ifdef _WIN32
#include <windows.h>
static void remove_tree(const char *path)
{
    char pattern[512];
    WIN32_FIND_DATAA fd;
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
                continue;
            }
            char child[512];
            snprintf(child, sizeof(child), "%s\\%s", path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                remove_tree(child);
            } else {
                DeleteFileA(child);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(path);
}
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
static void remove_tree(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char child[512];
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        struct stat st;
        if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            remove_tree(child);
        } else {
            unlink(child);
        }
    }
    closedir(dir);
    rmdir(path);
}
#endif

/* Fresh install: no version file -> version 0 -> check stamps CURRENT. */
static int test_fresh_install(const char *root)
{
    remove_tree(root);
    heapstore_config_t cfg = {0};
    cfg.root_path = root;
    REQUIRE(heapstore_init(&cfg) == heapstore_SUCCESS);

    uint32_t ver = 12345;
    REQUIRE(heapstore_migration_get_version(&ver) == heapstore_SUCCESS);
    REQUIRE(ver == HEAPSTORE_SCHEMA_VERSION_CURRENT);

    bool needs_migration = true;
    uint32_t current = 0;
    REQUIRE(heapstore_migration_check(&needs_migration, &current) == heapstore_SUCCESS);
    REQUIRE(!needs_migration);
    REQUIRE(current == HEAPSTORE_SCHEMA_VERSION_CURRENT);

    heapstore_shutdown();
    printf("PASS test_fresh_install\n");
    return 0;
}

static int test_set_get_roundtrip(const char *root)
{
    remove_tree(root);
    heapstore_config_t cfg = {0};
    cfg.root_path = root;
    REQUIRE(heapstore_init(&cfg) == heapstore_SUCCESS);

    REQUIRE(heapstore_migration_set_version(9999) == heapstore_SUCCESS);
    uint32_t ver = 0;
    REQUIRE(heapstore_migration_get_version(&ver) == heapstore_SUCCESS);
    REQUIRE(ver == 9999);

    heapstore_shutdown();
    printf("PASS test_set_get_roundtrip\n");
    return 0;
}

/* A completed set_version must leave no temp file behind (atomic rename). */
static int test_atomic_write_no_tmp_leftover(const char *root)
{
    remove_tree(root);
    heapstore_config_t cfg = {0};
    cfg.root_path = root;
    REQUIRE(heapstore_init(&cfg) == heapstore_SUCCESS);

    REQUIRE(heapstore_migration_set_version(7777) == heapstore_SUCCESS);
    REQUIRE(!file_exists(root, TEST_VERSION_TMP));

    uint32_t ver = 0;
    REQUIRE(heapstore_migration_get_version(&ver) == heapstore_SUCCESS);
    REQUIRE(ver == 7777);

    heapstore_shutdown();
    printf("PASS test_atomic_write_no_tmp_leftover\n");
    return 0;
}

/* Corrupt variants: every one of them must be reported as corrupt, never
 * silently mapped to "version 0" or an arbitrary stale version. */
static int test_corrupt_variants(void)
{
    static const char *variants[] = {
        "",                              /* empty / truncated file        */
        "\n",                            /* newline only                  */
        "   \t \n",                      /* whitespace only               */
        "abc\n",                         /* no digits                     */
        "-5\n",                          /* sign prefix (strtoul negate!) */
        "+5\n",                          /* explicit plus sign            */
        "99999999999999999999999999\n",  /* range overflow (ERANGE)       */
        "10000abc\n",                    /* trailing garbage              */
        "0x10\n",                        /* hex-looking junk in base 10   */
        "1 2\n",                         /* second number on same line    */
    };
    const size_t variant_count = sizeof(variants) / sizeof(variants[0]);

    for (size_t i = 0; i < variant_count; i++) {
        char root[128];
        snprintf(root, sizeof(root), "migtest_corrupt_%zu", i);
        remove_tree(root);

        heapstore_config_t cfg = {0};
        cfg.root_path = root;
        REQUIRE(heapstore_init(&cfg) == heapstore_SUCCESS);

        write_version_file(root, variants[i]);

        uint32_t ver = 4242;
        heapstore_error_t err = heapstore_migration_get_version(&ver);
        if (err != heapstore_ERR_FILE_CORRUPT) {
            printf("FAIL variant[%zu] \"%s\": expected ERR_FILE_CORRUPT, got %d\n", i,
                   variants[i], err);
            return 1;
        }
        REQUIRE(ver == 4242); /* output untouched on failure */

        heapstore_shutdown();
    }

    printf("PASS test_corrupt_variants (%zu cases)\n", variant_count);
    return 0;
}

/* Valid content variants that must keep parsing. */
static int test_valid_variants(void)
{
    static const struct
    {
        const char *content;
        uint32_t expected;
    } variants[] = {
        {"10000\n", 10000},
        {"0\n", 0},
        {"9999 \t\r\n", 9999}, /* trailing whitespace allowed */
    };
    const size_t variant_count = sizeof(variants) / sizeof(variants[0]);

    for (size_t i = 0; i < variant_count; i++) {
        char root[128];
        snprintf(root, sizeof(root), "migtest_valid_%zu", i);
        remove_tree(root);

        heapstore_config_t cfg = {0};
        cfg.root_path = root;
        REQUIRE(heapstore_init(&cfg) == heapstore_SUCCESS);

        write_version_file(root, variants[i].content);

        uint32_t ver = 4242;
        REQUIRE(heapstore_migration_get_version(&ver) == heapstore_SUCCESS);
        if (ver != variants[i].expected) {
            printf("FAIL variant[%zu] \"%s\": expected %u, got %u\n", i, variants[i].content,
                   variants[i].expected, ver);
            return 1;
        }

        heapstore_shutdown();
    }

    printf("PASS test_valid_variants (%zu cases)\n", variant_count);
    return 0;
}

/* DoD: interrupted upgrade simulation. A truncated/emptied version file must
 * abort init (fail-closed) instead of running old-schema data as current. */
static int test_init_fail_closed_on_corrupt(const char *root)
{
    remove_tree(root);
    heapstore_config_t cfg = {0};
    cfg.root_path = root;
    REQUIRE(heapstore_init(&cfg) == heapstore_SUCCESS);
    heapstore_shutdown();

    /* Simulate crash mid-upgrade: version file left empty/truncated. */
    write_version_file(root, "");

    REQUIRE(heapstore_init(&cfg) != heapstore_SUCCESS);

    /* Store refuses to operate: APIs report not-initialized. */
    uint32_t ver = 4242;
    REQUIRE(heapstore_migration_get_version(&ver) == heapstore_ERR_NOT_INITIALIZED);

    /* Recovery: restoring a valid version file re-enables init. */
    write_version_file(root, "10000\n");
    REQUIRE(heapstore_init(&cfg) == heapstore_SUCCESS);
    REQUIRE(heapstore_migration_get_version(&ver) == heapstore_SUCCESS);
    REQUIRE(ver == HEAPSTORE_SCHEMA_VERSION_CURRENT);

    heapstore_shutdown();
    printf("PASS test_init_fail_closed_on_corrupt\n");
    return 0;
}

int main(void)
{
    int rc = 0;
    rc |= test_fresh_install("migtest_fresh");
    rc |= test_set_get_roundtrip("migtest_roundtrip");
    rc |= test_atomic_write_no_tmp_leftover("migtest_atomic");
    rc |= test_corrupt_variants();
    rc |= test_valid_variants();
    rc |= test_init_fail_closed_on_corrupt("migtest_failclosed");

    if (rc != 0) {
        printf("test_heapstore_migration: FAILED\n");
        return 1;
    }
    printf("test_heapstore_migration: ALL PASSED (%d checks)\n", g_checks);
    return 0;
}
