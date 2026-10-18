// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_core_circuit.c
 * @brief heapstore circuit breaker adapter.
 *
 * The CLOSED/OPEN/HALF_OPEN state machine is owned by the commons
 * circuit_breaker SSoT. This file keeps only the heapstore-specific
 * configuration mirror (failure threshold, timeout) and the mapping onto
 * the public heapstore_circuit_info_t reporting surface.
 */

// @owner: team-B
#include "heapstore_core_internal.h"

#include "circuit_breaker.h"

#define HEAPSTORE_CB_NAME "heapstore"

static cb_manager_t s_cb_mgr;
static circuit_breaker_t s_cb;
static airy_mtx_t s_cb_lock;
static bool s_cb_lock_ready;
static uint32_t s_cb_threshold = heapstore_DEFAULT_CIRCUIT_THRESHOLD;
static uint32_t s_cb_timeout_sec = heapstore_DEFAULT_CIRCUIT_TIMEOUT_SEC;

static cb_config_t cb_config_build(void)
{
    cb_config_t cfg = cb_create_default_config();

    cfg.failure_threshold = s_cb_threshold;
    cfg.success_threshold = 1;
    cfg.timeout_ms = s_cb_timeout_sec * 1000u;
    cfg.half_open_max_calls = 1;
    cfg.failure_rate_threshold = 0;
    cfg.enable_slow_call_detection = false;

    return cfg;
}

/* Caller must hold s_cb_lock. */
static bool cb_arm(void)
{
    if (!s_cb_mgr) {
        s_cb_mgr = cb_manager_create();
        if (!s_cb_mgr) {
            return false;
        }
    }

    if (s_cb) {
        cb_destroy(s_cb);
        s_cb = NULL;
    }

    cb_config_t cfg = cb_config_build();
    s_cb = cb_create(s_cb_mgr, HEAPSTORE_CB_NAME, &cfg);

    return s_cb != NULL;
}

/* Acquire the adapter lock only when a live breaker is present. */
static bool cb_enter(void)
{
    if (!s_cb_lock_ready) {
        return false;
    }

    airy_mtx_lock(&s_cb_lock);
    if (!s_cb) {
        airy_mtx_unlock(&s_cb_lock);
        return false;
    }

    return true;
}

void heapstore_core_circuit_init(void)
{
    if (!s_cb_lock_ready) {
        if (airy_mtx_init(&s_cb_lock) != 0) {
            return;
        }
        s_cb_lock_ready = true;
    }

    airy_mtx_lock(&s_cb_lock);
    cb_arm();
    airy_mtx_unlock(&s_cb_lock);
}

void heapstore_circ_stop(void)
{
    if (!s_cb_lock_ready) {
        return;
    }

    airy_mtx_lock(&s_cb_lock);
    if (s_cb) {
        cb_destroy(s_cb);
        s_cb = NULL;
    }
    if (s_cb_mgr) {
        cb_manager_destroy(s_cb_mgr);
        s_cb_mgr = NULL;
    }
    airy_mtx_unlock(&s_cb_lock);

    airy_mtx_destroy(&s_cb_lock);
    s_cb_lock_ready = false;
}

void heapstore_core_circuit_record_success(void)
{
    if (!cb_enter()) {
        return;
    }

    cb_state_t before = cb_get_state(s_cb);
    cb_record_success(s_cb, 0);
    if (before == CB_STATE_HALF_OPEN && cb_get_state(s_cb) == CB_STATE_CLOSED) {
        AIRY_LOG_INFO("heapstore: circuit breaker CLOSED (probe succeeded)");
    }

    airy_mtx_unlock(&s_cb_lock);
}

void heapstore_core_circuit_record_failure(void)
{
    if (!cb_enter()) {
        return;
    }

    cb_state_t before = cb_get_state(s_cb);
    cb_record_failure(s_cb, 0);
    if (before != CB_STATE_OPEN && cb_get_state(s_cb) == CB_STATE_OPEN) {
        AIRY_LOG_ERROR("heapstore: circuit breaker OPEN (threshold=%u failures, timeout=%us)",
                       s_cb_threshold, s_cb_timeout_sec);
        heapstore_core_metrics_note_circuit_trip();
    }

    airy_mtx_unlock(&s_cb_lock);
}

bool heapstore_circuit_open(void)
{
    bool blocked = false;

    if (!cb_enter()) {
        return false;
    }

    if (cb_get_state(s_cb) == CB_STATE_OPEN) {
        blocked = !cb_allow_request(s_cb);
        if (!blocked) {
            AIRY_LOG_INFO("heapstore: circuit breaker HALF-OPEN (timeout elapsed)");
        }
    }

    airy_mtx_unlock(&s_cb_lock);
    return blocked;
}

void heapstore_core_circuit_apply_config(uint32_t threshold, uint32_t timeout_sec)
{
    uint32_t th = threshold > 0 ? threshold : s_cb_threshold;
    uint32_t to = timeout_sec > 0 ? timeout_sec : s_cb_timeout_sec;

    if (!s_cb_lock_ready) {
        s_cb_threshold = th;
        s_cb_timeout_sec = to;
        return;
    }

    airy_mtx_lock(&s_cb_lock);
    if (th != s_cb_threshold || to != s_cb_timeout_sec) {
        s_cb_threshold = th;
        s_cb_timeout_sec = to;
        cb_arm();
    }
    airy_mtx_unlock(&s_cb_lock);
}

heapstore_error_t heapstore_get_circuit_state(heapstore_circuit_info_t *info)
{
    if (!heapstore_ready()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    if (!info) {
        return heapstore_ERR_INVALID_PARAM;
    }

    info->state = heapstore_CIRCUIT_CLOSED;
    info->failure_count = 0;
    info->last_failure_time = 0;
    info->threshold = s_cb_threshold;
    info->timeout_sec = s_cb_timeout_sec;

    if (!cb_enter()) {
        return heapstore_SUCCESS;
    }

    cb_stats_t stats;
    info->state = (heapstore_circuit_state_t)cb_get_state(s_cb);
    if (cb_get_stats(s_cb, &stats) == AIRY_SUCCESS && stats.consecutive_failures > 0) {
        info->failure_count = stats.consecutive_failures;
        info->last_failure_time = stats.last_failure_time;
    }

    airy_mtx_unlock(&s_cb_lock);
    return heapstore_SUCCESS;
}

heapstore_error_t heapstore_reset_circuit(void)
{
    if (!heapstore_ready()) {
        return heapstore_ERR_NOT_INITIALIZED;
    }

    if (cb_enter()) {
        cb_reset(s_cb);
        airy_mtx_unlock(&s_cb_lock);
    }

    return heapstore_SUCCESS;
}
