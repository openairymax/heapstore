// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_core_circuit.c
 * @brief heapstore circuit breaker: failure counting, open/half-open
 *        state machine and public circuit state APIs
 *        (functional domain after heapstore_core.c split).
 */

// @owner: team-B
#include "heapstore_core_internal.h"

#include "heapstore.h"
#include "logging.h"
#include "logging_compat.h"

#include <time.h>

#include "atomic_compat.h"

typedef struct {
    atomic_uint_fast32_t state;
    atomic_uint_fast32_t failure_count;
    atomic_uint_fast64_t last_failure_time;
    uint32_t threshold;
    uint32_t timeout_sec;
} heapstore_circuit_breaker_t;

static heapstore_circuit_breaker_t s_circuit_breaker = {.state = 0,
                                                        .failure_count = 0,
                                                        .last_failure_time = 0,
                                                        .threshold =
                                                            heapstore_DEFAULT_CIRCUIT_THRESHOLD,
                                                        .timeout_sec =
                                                            heapstore_DEFAULT_CIRCUIT_TIMEOUT_SEC};

void heapstore_core_circuit_init(void)
{
    atomic_init(&s_circuit_breaker.state, 0);
    atomic_init(&s_circuit_breaker.failure_count, 0);
    atomic_init(&s_circuit_breaker.last_failure_time, 0);
}

void heapstore_core_circuit_record_success(void)
{
    uint32_t prev = atomic_exchange(&s_circuit_breaker.failure_count, 0);
    if (prev > 0) {
        AIRY_LOG_INFO("heapstore: circuit breaker reset (was %u failures)", prev);
    }
    atomic_store(&s_circuit_breaker.state, 0);
}

void heapstore_core_circuit_record_failure(void)
{
    uint32_t count = atomic_fetch_add(&s_circuit_breaker.failure_count, 1) + 1;
    uint64_t now = (uint64_t)time(NULL);
    atomic_store(&s_circuit_breaker.last_failure_time, now);

    if (count >= s_circuit_breaker.threshold) {
        if (atomic_exchange(&s_circuit_breaker.state, 1) == 0) {
            AIRY_LOG_ERROR("heapstore: circuit breaker OPEN (threshold=%u failures, timeout=%us)",
                           s_circuit_breaker.threshold, s_circuit_breaker.timeout_sec);
        }
        heapstore_core_metrics_note_circuit_trip();
    }
}

bool heapstore_core_circuit_is_open(void)
{
    uint32_t state = atomic_load(&s_circuit_breaker.state);
    if (state == 0) {
        return false;
    }
    if (state == 2) {
        return false;
    }

    uint64_t last_failure = atomic_load(&s_circuit_breaker.last_failure_time);
    uint64_t now = (uint64_t)time(NULL);
    if (now - last_failure >= s_circuit_breaker.timeout_sec) {
        if (atomic_exchange(&s_circuit_breaker.state, 2) == 1) {
            AIRY_LOG_INFO("heapstore: circuit breaker HALF-OPEN (timeout elapsed)");
        }
        return false;
    }
    return true;
}

void heapstore_core_circuit_apply_config(uint32_t threshold, uint32_t timeout_sec)
{
    if (threshold > 0) {
        s_circuit_breaker.threshold = threshold;
    }
    if (timeout_sec > 0) {
        s_circuit_breaker.timeout_sec = timeout_sec;
    }
}

heapstore_error_t heapstore_get_circuit_state(heapstore_circuit_info_t *info)
{
    if (!heapstore_is_initialized()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    if (!info) {
        return heapstore_ERR_INVALID_PARAM;
    }

    uint32_t state = atomic_load(&s_circuit_breaker.state);
    info->state = (heapstore_circuit_state_t)state;
    info->failure_count = atomic_load(&s_circuit_breaker.failure_count);
    info->last_failure_time = atomic_load(&s_circuit_breaker.last_failure_time);
    info->threshold = s_circuit_breaker.threshold;
    info->timeout_sec = s_circuit_breaker.timeout_sec;

    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_reset_circuit(void)
{
    if (!heapstore_is_initialized()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    atomic_store(&s_circuit_breaker.state, 0);
    atomic_store(&s_circuit_breaker.failure_count, 0);
    atomic_store(&s_circuit_breaker.last_failure_time, 0);

    return heapstore_SUCCESS;
}
