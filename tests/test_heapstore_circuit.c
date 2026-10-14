// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file test_heapstore_circuit.c
 * @brief AgentRT 数据分区熔断器单元测试
 *
 * 白盒驱动熔断状态机（CLOSED → OPEN → HALF_OPEN → CLOSED）：
 * 失败注入经内部符号 heapstore_core_circuit_record_failure()，
 * 可观察断言走公共 API heapstore_get_circuit_state() /
 * heapstore_reset_circuit() / heapstore_log_write_fast()。
 * 填补已清退 e2e 死件 scenario_7（circuit breaker）的断言价值。
 */
// @owner: team-B

#include "heapstore.h"
#include "heapstore_core_internal.h"
#include "heapstore_log.h"
#include "airy_memory.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define circ_sleep_ms(ms) Sleep(ms)
#else
#include <unistd.h>
#define circ_sleep_ms(ms) usleep((ms) *1000)
#endif

static void circ_reinit(uint32_t threshold, uint32_t timeout_sec)
{
    heapstore_config_t cfg = heapstore_def_config("test_hs_circuit_store");
    cfg.circuit_breaker_threshold = threshold;
    cfg.circuit_breaker_timeout_sec = timeout_sec;

    assert(heapstore_reload_config(&cfg) == heapstore_SUCCESS);
    assert(heapstore_reset_circuit() == heapstore_SUCCESS);
}

static void circ_defaults(void)
{
    printf("Test: circuit_defaults...");

    heapstore_circuit_info_t info;
    assert(heapstore_get_circuit_state(&info) == heapstore_SUCCESS);
    assert(info.state == heapstore_CIRCUIT_CLOSED);
    assert(info.failure_count == 0);
    assert(info.threshold == 5);
    assert(info.timeout_sec == 30);

    assert(heapstore_get_circuit_state(NULL) == heapstore_ERR_INVALID_PARAM);

    printf("PASS\n");
}

static void circ_cfg_applied(void)
{
    printf("Test: circuit_config_applied...");

    circ_reinit(2, 1);

    heapstore_circuit_info_t info;
    assert(heapstore_get_circuit_state(&info) == heapstore_SUCCESS);
    assert(info.state == heapstore_CIRCUIT_CLOSED);
    assert(info.threshold == 2);
    assert(info.timeout_sec == 1);

    printf("PASS\n");
}

static void circ_opens(void)
{
    printf("Test: circuit_opens_at_threshold...");

    circ_reinit(2, 1);

    heapstore_metrics_t metrics;
    assert(heapstore_get_metrics(&metrics) == heapstore_SUCCESS);
    const uint64_t trips_before = metrics.circuit_breaker_trips;

    heapstore_core_circuit_record_failure();
    heapstore_core_circuit_record_failure();

    heapstore_circuit_info_t info;
    assert(heapstore_get_circuit_state(&info) == heapstore_SUCCESS);
    assert(info.state == heapstore_CIRCUIT_OPEN);
    assert(info.failure_count == 2);
    assert(info.last_failure_time > 0);

    assert(heapstore_circuit_open() == true);
    assert(heapstore_log_write_fast("circuit_test", HEAPSTORE_LOG_INFO, "blocked") ==
           heapstore_ERR_CIRCUIT_OPEN);

    assert(heapstore_get_metrics(&metrics) == heapstore_SUCCESS);
    assert(metrics.circuit_breaker_trips >= trips_before + 1);

    printf("PASS\n");
}

static void circ_half_open(void)
{
    printf("Test: circuit_half_open_after_timeout...");

    circ_reinit(2, 1);

    heapstore_core_circuit_record_failure();
    heapstore_core_circuit_record_failure();
    assert(heapstore_circuit_open() == true);

    circ_sleep_ms(1100);

    /* 超时后 circuit_open() 惰性转换 OPEN → HALF_OPEN 并放行探测 */
    assert(heapstore_circuit_open() == false);

    heapstore_circuit_info_t info;
    assert(heapstore_get_circuit_state(&info) == heapstore_SUCCESS);
    assert(info.state == heapstore_CIRCUIT_HALF_OPEN);

    /* 探测写成功 → 计数清零回 CLOSED */
    assert(heapstore_log_write_fast("circuit_test", HEAPSTORE_LOG_INFO, "probe") ==
           heapstore_SUCCESS);
    assert(heapstore_get_circuit_state(&info) == heapstore_SUCCESS);
    assert(info.state == heapstore_CIRCUIT_CLOSED);
    assert(info.failure_count == 0);

    printf("PASS\n");
}

static void circ_succ_reset(void)
{
    printf("Test: circuit_success_resets_count...");

    circ_reinit(3, 30);

    heapstore_core_circuit_record_failure();
    heapstore_core_circuit_record_failure();

    heapstore_circuit_info_t info;
    assert(heapstore_get_circuit_state(&info) == heapstore_SUCCESS);
    assert(info.failure_count == 2);
    assert(info.state == heapstore_CIRCUIT_CLOSED);

    heapstore_core_circuit_record_success();
    assert(heapstore_get_circuit_state(&info) == heapstore_SUCCESS);
    assert(info.failure_count == 0);

    printf("PASS\n");
}

static void circ_man_reset(void)
{
    printf("Test: circuit_manual_reset...");

    circ_reinit(2, 30);

    heapstore_core_circuit_record_failure();
    heapstore_core_circuit_record_failure();
    assert(heapstore_circuit_open() == true);

    assert(heapstore_reset_circuit() == heapstore_SUCCESS);

    /* 手动复位后无需等超时，写入立即恢复 */
    assert(heapstore_log_write_fast("circuit_test", HEAPSTORE_LOG_INFO, "recovered") ==
           heapstore_SUCCESS);

    heapstore_circuit_info_t info;
    assert(heapstore_get_circuit_state(&info) == heapstore_SUCCESS);
    assert(info.state == heapstore_CIRCUIT_CLOSED);
    assert(info.failure_count == 0);
    assert(info.last_failure_time == 0);

    printf("PASS\n");
}

static void circ_write_ok(void)
{
    printf("Test: circuit_write_success...");

    circ_reinit(5, 30);

    assert(heapstore_log_write_fast("circuit_test", HEAPSTORE_LOG_INFO, "healthy") ==
           heapstore_SUCCESS);

    heapstore_circuit_info_t info;
    assert(heapstore_get_circuit_state(&info) == heapstore_SUCCESS);
    assert(info.state == heapstore_CIRCUIT_CLOSED);
    assert(info.failure_count == 0);

    printf("PASS\n");
}

int main(void)
{
    printf("=== AgentRT heapstore Circuit Breaker Unit Tests ===\n\n");

    heapstore_config_t cfg = heapstore_def_config("test_hs_circuit_store");
    heapstore_error_t err = heapstore_init(&cfg);
    if (err != heapstore_SUCCESS && err != heapstore_ERR_ALREADY_INITIALIZED) {
        printf("SKIP: heapstore_init failed (%s)\n", heapstore_strerror(err));
        return 0;
    }

    circ_defaults();
    circ_cfg_applied();
    circ_opens();
    circ_half_open();
    circ_succ_reset();
    circ_man_reset();
    circ_write_ok();

    heapstore_shutdown();

    printf("\n=== All Circuit Breaker Tests Passed ===\n");
    return 0;
}
