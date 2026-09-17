// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file test_heapstore_trace.c
 * @brief AgentRT 数据分区追踪存储单元测试
 *
 */
// @owner: team-B

#include "heapstore.h"
#include "heapstore_trace.h"
#include "airy_memory.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void test_trace_init_shutdown(void)
{
    printf("Test: trace_init_shutdown...");

    heapstore_error_t err __attribute__((unused)) = heapstore_trace_init();
    assert(err == heapstore_SUCCESS);

    heapstore_trace_shutdown();

    printf("PASS\n");
}

static void test_trace_write_span(void)
{
    printf("Test: trace_write_span...");

    heapstore_error_t err __attribute__((unused)) = heapstore_trace_init();
    assert(err == heapstore_SUCCESS);

    heapstore_span_t span;
    AIRY_MEMSET(&span, 0, sizeof(span));

    snprintf(span.trace_id, sizeof(span.trace_id), "trace_%ld", (long)time(NULL));
    snprintf(span.span_id, sizeof(span.span_id), "span_001");
    span.parent_span_id[0] = '\0';
    snprintf(span.name, sizeof(span.name), "test_operation");
    span.start_time_ns = (uint64_t)time(NULL) * 1000000000;
    span.end_time_ns = span.start_time_ns + 100000000;
    snprintf(span.service_name, sizeof(span.service_name), "test_service");
    snprintf(span.status, sizeof(span.status), "OK");

    err = heapstore_trace_write_span(&span);
    assert(err == heapstore_SUCCESS);

    heapstore_trace_shutdown();

    printf("PASS\n");
}

static void test_trace_write_batch(void)
{
    printf("Test: trace_write_batch...");

    heapstore_error_t err __attribute__((unused)) = heapstore_trace_init();
    assert(err == heapstore_SUCCESS);

    heapstore_span_t spans[5];
    AIRY_MEMSET(spans, 0, sizeof(spans));

    for (int i = 0; i < 5; i++) {
        snprintf(spans[i].trace_id, sizeof(spans[i].trace_id), "trace_batch_%ld", (long)time(NULL));
        snprintf(spans[i].span_id, sizeof(spans[i].span_id), "span_%d", i);
        snprintf(spans[i].name, sizeof(spans[i].name), "batch_operation_%d", i);
        spans[i].start_time_ns = (uint64_t)time(NULL) * 1000000000;
        spans[i].end_time_ns = spans[i].start_time_ns + 50000000;
        snprintf(spans[i].service_name, sizeof(spans[i].service_name), "batch_service");
        snprintf(spans[i].status, sizeof(spans[i].status), "OK");
    }

    err = heapstore_trace_write_spans_batch(spans, 5);
    assert(err == heapstore_SUCCESS);

    heapstore_trace_shutdown();

    printf("PASS\n");
}

static void test_trace_flush(void)
{
    printf("Test: trace_flush...");

    heapstore_error_t err __attribute__((unused)) = heapstore_trace_init();
    assert(err == heapstore_SUCCESS);

    heapstore_span_t span;
    AIRY_MEMSET(&span, 0, sizeof(span));

    snprintf(span.trace_id, sizeof(span.trace_id), "trace_flush_%ld", (long)time(NULL));
    snprintf(span.span_id, sizeof(span.span_id), "span_flush");
    snprintf(span.name, sizeof(span.name), "flush_test");
    span.start_time_ns = (uint64_t)time(NULL) * 1000000000;
    span.end_time_ns = span.start_time_ns + 10000000;
    snprintf(span.service_name, sizeof(span.service_name), "flush_service");
    snprintf(span.status, sizeof(span.status), "OK");

    err = heapstore_trace_write_span(&span);
    assert(err == heapstore_SUCCESS);

    err = heapstore_trace_flush();
    assert(err == heapstore_SUCCESS);

    heapstore_trace_shutdown();

    printf("PASS\n");
}

static void test_trace_invalid_params(void)
{
    printf("Test: trace_invalid_params...");

    heapstore_error_t err __attribute__((unused)) = heapstore_trace_init();
    assert(err == heapstore_SUCCESS);

    err = heapstore_trace_write_span(NULL);
    assert(err == heapstore_ERR_INVALID_PARAM);

    heapstore_span_t invalid_span;
    AIRY_MEMSET(&invalid_span, 0, sizeof(invalid_span));
    err = heapstore_trace_write_span(&invalid_span);
    assert(err == heapstore_ERR_INVALID_PARAM);

    err = heapstore_trace_write_spans_batch(NULL, 0);
    assert(err == heapstore_ERR_INVALID_PARAM);

    err = heapstore_trace_write_spans_batch(NULL, 10);
    assert(err == heapstore_ERR_INVALID_PARAM);

    heapstore_trace_shutdown();

    printf("PASS\n");
}

static void test_trace_stats(void)
{
    printf("Test: trace_stats...");

    heapstore_error_t err __attribute__((unused)) = heapstore_trace_init();
    assert(err == heapstore_SUCCESS);

    uint64_t total = 0, pending = 0, size = 0;

    err = heapstore_trace_get_stats(&total, &pending, &size);
    assert(err == heapstore_SUCCESS);

    heapstore_span_t span;
    AIRY_MEMSET(&span, 0, sizeof(span));

    snprintf(span.trace_id, sizeof(span.trace_id), "trace_stats_%ld", (long)time(NULL));
    snprintf(span.span_id, sizeof(span.span_id), "span_stats");
    snprintf(span.name, sizeof(span.name), "stats_test");
    span.start_time_ns = (uint64_t)time(NULL) * 1000000000;
    span.end_time_ns = span.start_time_ns + 10000000;
    snprintf(span.service_name, sizeof(span.service_name), "stats_service");
    snprintf(span.status, sizeof(span.status), "OK");

    err = heapstore_trace_write_span(&span);
    assert(err == heapstore_SUCCESS);

    err = heapstore_trace_get_stats(&total, &pending, &size);
    assert(err == heapstore_SUCCESS);

    heapstore_trace_shutdown();

    printf("PASS\n");
}

static void fill_span(heapstore_span_t *span, const char *trace_id, const char *span_id, uint64_t t0,
                      uint64_t t1)
{
    AIRY_MEMSET(span, 0, sizeof(*span));
    snprintf(span->trace_id, sizeof(span->trace_id), "%s", trace_id);
    snprintf(span->span_id, sizeof(span->span_id), "%s", span_id);
    snprintf(span->name, sizeof(span->name), "op_%s", span_id);
    span->start_time_ns = t0;
    span->end_time_ns = t1;
    snprintf(span->service_name, sizeof(span->service_name), "query_service");
    snprintf(span->status, sizeof(span->status), "OK");
}

static void test_trace_query_roundtrip(void)
{
    printf("Test: trace_query_roundtrip...");

    heapstore_error_t err __attribute__((unused)) = heapstore_trace_init();
    assert(err == heapstore_SUCCESS);

    for (int i = 0; i < 3; i++) {
        heapstore_span_t span;
        char sid[32];
        snprintf(sid, sizeof(sid), "q_%d", i);
        fill_span(&span, "query_trace", sid, 1000000, 2000000);
        err = heapstore_trace_write_span(&span);
        assert(err == heapstore_SUCCESS);
    }

    heapstore_span_t other;
    fill_span(&other, "other_trace", "other_0", 3000000, 4000000);
    err = heapstore_trace_write_span(&other);
    assert(err == heapstore_SUCCESS);

    heapstore_span_t *out = NULL;
    size_t count = 0;
    err = heapstore_trace_query_by_trace("query_trace", &out, &count);
    assert(err == heapstore_SUCCESS);
    assert(count == 3);
    heapstore_trace_free_spans(out, count);
    out = NULL;

    count = 0;
    err = heapstore_trace_query_by_trace("absent_trace", &out, &count);
    assert(err == heapstore_ERR_NOT_FOUND);
    assert(out == NULL && count == 0);

    out = NULL;
    count = 0;
    err = heapstore_trace_query_by_time_range(0, 2500000, &out, &count);
    assert(err == heapstore_SUCCESS);
    assert(count == 3);
    heapstore_trace_free_spans(out, count);
    out = NULL;

    count = 0;
    err = heapstore_trace_query_by_time_range(0, 1, &out, &count);
    assert(err == heapstore_ERR_NOT_FOUND);
    assert(out == NULL && count == 0);

    heapstore_trace_shutdown();

    printf("PASS\n");
}

static void test_trace_batch_deep_copy(void)
{
    printf("Test: trace_batch_deep_copy...");

    heapstore_error_t err __attribute__((unused)) = heapstore_trace_init();
    assert(err == heapstore_SUCCESS);

    heapstore_span_t spans[4];
    AIRY_MEMSET(spans, 0, sizeof(spans));

    char *owned[4];
    for (int i = 0; i < 4; i++) {
        char sid[32];
        snprintf(sid, sizeof(sid), "deep_%d", i);
        fill_span(&spans[i], "deep_trace", sid, 100000, 200000);
        owned[i] = (char *)malloc(32);
        assert(owned[i] != NULL);
        snprintf(owned[i], 32, "attr-%d", i);
        spans[i].attributes = owned[i];
    }

    err = heapstore_trace_write_spans_batch(spans, 4);
    assert(err == heapstore_SUCCESS);

    /* 提交后释放调用方自有副本：库内若未深拷贝，此处立即形成悬垂指针，
     * 后续查询与 free_spans 会在 ASan/valgrind 下报错。 */
    for (int i = 0; i < 4; i++) {
        free(owned[i]);
        spans[i].attributes = NULL;
    }

    heapstore_span_t *out = NULL;
    size_t count = 0;
    err = heapstore_trace_query_by_trace("deep_trace", &out, &count);
    assert(err == heapstore_SUCCESS);
    assert(count == 4);
    for (size_t i = 0; i < count; i++) {
        char expect[32];
        snprintf(expect, sizeof(expect), "attr-%zu", i);
        assert(out[i].attributes != NULL);
        assert(strcmp((const char *)out[i].attributes, expect) == 0);
    }
    heapstore_trace_free_spans(out, count);

    heapstore_trace_shutdown();

    printf("PASS\n");
}

#define CONC_WRITERS 4
#define CONC_QUERIERS 2
#define CONC_ITERS 100

typedef struct {
    int id;
    int errors;
} conc_arg_t;

static void *conc_writer(void *arg)
{
    conc_arg_t *a = (conc_arg_t *)arg;

    for (int i = 0; i < CONC_ITERS; i++) {
        heapstore_span_t span;
        char sid[32];
        snprintf(sid, sizeof(sid), "w%d_%d", a->id, i);
        fill_span(&span, "conc_trace", sid, 1000, 2000);
        if (heapstore_trace_write_span(&span) != heapstore_SUCCESS) {
            a->errors++;
        }
    }

    return NULL;
}

static void *conc_querier(void *arg)
{
    conc_arg_t *a = (conc_arg_t *)arg;

    for (int i = 0; i < CONC_ITERS; i++) {
        heapstore_span_t *out = NULL;
        size_t count = 0;
        heapstore_error_t qerr = heapstore_trace_query_by_trace("conc_trace", &out, &count);
        if (qerr == heapstore_SUCCESS) {
            if (!out || count == 0) {
                a->errors++;
            }
            heapstore_trace_free_spans(out, count);
        } else if (qerr != heapstore_ERR_NOT_FOUND) {
            a->errors++;
        }

        out = NULL;
        count = 0;
        qerr = heapstore_trace_query_by_time_range(0, 5000, &out, &count);
        if (qerr == heapstore_SUCCESS) {
            if (!out || count == 0) {
                a->errors++;
            }
            heapstore_trace_free_spans(out, count);
        } else if (qerr != heapstore_ERR_NOT_FOUND) {
            a->errors++;
        }
    }

    return NULL;
}

static void test_trace_concurrent_query_write(void)
{
    printf("Test: trace_concurrent_query_write...");

    heapstore_error_t err __attribute__((unused)) = heapstore_trace_init();
    assert(err == heapstore_SUCCESS);

    pthread_t writers[CONC_WRITERS];
    pthread_t queriers[CONC_QUERIERS];
    conc_arg_t wargs[CONC_WRITERS];
    conc_arg_t qargs[CONC_QUERIERS];

    for (int i = 0; i < CONC_QUERIERS; i++) {
        qargs[i].id = i;
        qargs[i].errors = 0;
        assert(pthread_create(&queriers[i], NULL, conc_querier, &qargs[i]) == 0);
    }
    for (int i = 0; i < CONC_WRITERS; i++) {
        wargs[i].id = i;
        wargs[i].errors = 0;
        assert(pthread_create(&writers[i], NULL, conc_writer, &wargs[i]) == 0);
    }

    for (int i = 0; i < CONC_WRITERS; i++) {
        assert(pthread_join(writers[i], NULL) == 0);
    }
    for (int i = 0; i < CONC_QUERIERS; i++) {
        assert(pthread_join(queriers[i], NULL) == 0);
    }

    for (int i = 0; i < CONC_WRITERS; i++) {
        assert(wargs[i].errors == 0);
    }
    for (int i = 0; i < CONC_QUERIERS; i++) {
        assert(qargs[i].errors == 0);
    }

    heapstore_span_t *out = NULL;
    size_t count = 0;
    err = heapstore_trace_query_by_trace("conc_trace", &out, &count);
    assert(err == heapstore_SUCCESS);
    assert(count == (size_t)(CONC_WRITERS * CONC_ITERS));
    heapstore_trace_free_spans(out, count);

    heapstore_trace_shutdown();

    printf("PASS\n");
}

int main(void)
{
    printf("=== AgentRT heapstore Trace Unit Tests ===\n\n");

    /* 隔离测试数据：root 解析依赖 AIRY_HOME 体系，独立 home 避免
     * 触碰真实生产数据分区（~/.airymaxrt/data/agentrt/heapstore） */
    setenv("AIRY_HOME", "/tmp/agentrt_hs_test_home", 1);
    setenv("AIRY_RUNTIME_DIR", "/tmp/agentrt_hs_test_run", 1);

    test_trace_init_shutdown();
    test_trace_write_span();
    test_trace_write_batch();
    test_trace_flush();
    test_trace_invalid_params();
    test_trace_stats();
    test_trace_query_roundtrip();
    test_trace_batch_deep_copy();
    test_trace_concurrent_query_write();

    printf("\n=== All Trace Tests Passed ===\n");
    return 0;
}
