/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file heapstore_core_internal.h
 * @brief Internal header shared by the heapstore_core functional domains
 *        (core lifecycle / read-write paths / circuit breaker / metrics /
 *        error strings) after heapstore_core.c was split.
 */

/* @owner: team-B */
#ifndef AIRY_HEAPSTORE_CORE_INTERNAL_H
#define AIRY_HEAPSTORE_CORE_INTERNAL_H

#include "heapstore.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef heapstore_MAX_PATH_LEN
#define heapstore_MAX_PATH_LEN 512
#endif
#define heapstore_MAX_SUBPATHS 32
#define heapstore_CORE_PATH_COUNT 7

#define heapstore_DEFAULT_CIRCUIT_THRESHOLD 5
#define heapstore_DEFAULT_CIRCUIT_TIMEOUT_SEC 30

/* ---- Directory layout tables (owned by heapstore_core.c) ---- */
extern const heapstore_path_type_t heapstore_core_path_order[heapstore_CORE_PATH_COUNT];
extern const char *heapstore_core_path_names[heapstore_CORE_PATH_COUNT];
extern const char *heapstore_core_subpath_map[heapstore_CORE_PATH_COUNT][heapstore_MAX_SUBPATHS];

/* ---- Core state accessors (heapstore_core.c) ---- */

/**
 * @brief Snapshot the retention/cleanup settings under the config lock.
 */
void heapstore_core_get_cleanup_config(bool *enable_auto_cleanup, uint32_t *log_retention_days,
                                       uint32_t *trace_retention_days);

/* ---- Circuit breaker (heapstore_core_circuit.c) ---- */
void heapstore_core_circuit_init(void);
void heapstore_core_circuit_record_success(void);
void heapstore_core_circuit_record_failure(void);
bool heapstore_circuit_open(void);
void heapstore_core_circuit_apply_config(uint32_t threshold, uint32_t timeout_sec);

/* ---- Operation metrics (heapstore_core_metrics.c) ---- */
void heapstore_core_metrics_init(void);
void heapstore_core_metrics_update(uint64_t elapsed_ns, bool is_fast_path, bool is_failed);

/**
 * @brief Count one circuit breaker trip (called from the circuit breaker).
 */
void heapstore_core_metrics_note_circuit_trip(void);

#endif /* AIRY_HEAPSTORE_CORE_INTERNAL_H */
