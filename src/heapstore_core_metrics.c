// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_core_metrics.c
 * @brief heapstore internal operation metrics: counters, concurrency
 *        peak tracking and public metrics query/reset APIs
 *        (functional domain after heapstore_core.c split).
 */

// @owner: team-B
#include "heapstore_core_internal.h"

#include "heapstore.h"

#include "atomic_compat.h"

typedef struct {
    atomic_uint_fast64_t total_operations;
    atomic_uint_fast64_t failed_operations;
    atomic_uint_fast64_t fast_path_operations;
    atomic_uint_fast64_t slow_path_operations;
    atomic_uint_fast64_t circuit_breaker_trips;
    atomic_uint_fast64_t total_operation_time_ns;
    atomic_uint_fast64_t peak_concurrent_ops;
    atomic_uint_fast32_t current_concurrent_ops;
} heapstore_internal_metrics_t;

static heapstore_internal_metrics_t s_metrics = {.total_operations = 0,
                                                 .failed_operations = 0,
                                                 .fast_path_operations = 0,
                                                 .slow_path_operations = 0,
                                                 .circuit_breaker_trips = 0,
                                                 .total_operation_time_ns = 0,
                                                 .peak_concurrent_ops = 0,
                                                 .current_concurrent_ops = 0};

void heapstore_core_metrics_init(void)
{
    atomic_init(&s_metrics.total_operations, 0);
    atomic_init(&s_metrics.failed_operations, 0);
    atomic_init(&s_metrics.fast_path_operations, 0);
    atomic_init(&s_metrics.slow_path_operations, 0);
    atomic_init(&s_metrics.circuit_breaker_trips, 0);
    atomic_init(&s_metrics.total_operation_time_ns, 0);
    atomic_init(&s_metrics.peak_concurrent_ops, 0);
    atomic_init(&s_metrics.current_concurrent_ops, 0);
}

void heapstore_core_metrics_update(uint64_t elapsed_ns, bool is_fast_path, bool is_failed)
{
    atomic_fetch_add(&s_metrics.total_operations, 1);
    atomic_fetch_add(&s_metrics.total_operation_time_ns, elapsed_ns);

    uint32_t current_ops = atomic_fetch_add(&s_metrics.current_concurrent_ops, 1) + 1;
    uint64_t peak = atomic_load(&s_metrics.peak_concurrent_ops);
    while (current_ops > peak) {
        if (atomic_compare_exchange_weak(&s_metrics.peak_concurrent_ops, &peak, current_ops)) {
            break;
        }
    }
    atomic_fetch_sub(&s_metrics.current_concurrent_ops, 1);

    if (is_fast_path) {
        atomic_fetch_add(&s_metrics.fast_path_operations, 1);
    } else {
        atomic_fetch_add(&s_metrics.slow_path_operations, 1);
    }

    if (is_failed) {
        atomic_fetch_add(&s_metrics.failed_operations, 1);
    }
}

void heapstore_core_metrics_note_circuit_trip(void)
{
    atomic_fetch_add(&s_metrics.circuit_breaker_trips, 1);
}

heapstore_error_t heapstore_get_metrics(heapstore_metrics_t *metrics)
{
    if (!heapstore_is_initialized()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    if (!metrics) {
        return heapstore_ERR_INVALID_PARAM;
    }

    metrics->total_operations = atomic_load(&s_metrics.total_operations);
    metrics->failed_operations = atomic_load(&s_metrics.failed_operations);
    metrics->fast_path_operations = atomic_load(&s_metrics.fast_path_operations);
    metrics->slow_path_operations = atomic_load(&s_metrics.slow_path_operations);
    metrics->circuit_breaker_trips = atomic_load(&s_metrics.circuit_breaker_trips);
    metrics->peak_concurrent_ops = atomic_load(&s_metrics.peak_concurrent_ops);

    uint64_t total_ops = atomic_load(&s_metrics.total_operations);
    uint64_t total_time = atomic_load(&s_metrics.total_operation_time_ns);
    metrics->avg_operation_time_ns = (total_ops > 0) ? (double)total_time / total_ops : 0.0;

    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_reset_metrics(void)
{
    if (!heapstore_is_initialized()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    atomic_store(&s_metrics.total_operations, 0);
    atomic_store(&s_metrics.failed_operations, 0);
    atomic_store(&s_metrics.fast_path_operations, 0);
    atomic_store(&s_metrics.slow_path_operations, 0);
    atomic_store(&s_metrics.circuit_breaker_trips, 0);
    atomic_store(&s_metrics.total_operation_time_ns, 0);
    atomic_store(&s_metrics.peak_concurrent_ops, 0);

    return heapstore_SUCCESS;
}
