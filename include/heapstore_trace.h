/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file heapstore_trace.h
 * @brief AgentRT data partition trace storage interface.
 */

/* @owner: team-B */
#ifndef AIRY_heapstore_TRACE_H
#define AIRY_heapstore_TRACE_H

#include "heapstore.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
  * @brief Trace exporter configuration
 */
typedef struct heapstore_trace_exporter_config {
    bool enabled;
    char export_path[256];
    size_t batch_size;
    uint32_t export_interval_sec;
    char export_format[16];
} heapstore_trace_exporter_config_t;

/**
  * @brief Initialize the trace storage subsystem
 *
  * @return heapstore_error_t Error code
 *
  * @ownership Manages all resources internally
 * @threadsafe no (not safe for concurrent calls)
 * @reentrant no
 *
 * @see heapstore_trace_shutdown()
 * @since v1.0.0
 */
heapstore_error_t heapstore_trace_init(void);

/**
  * @brief Shut down the trace storage subsystem
 *
  * @ownership Releases all resources internally
 * @threadsafe no
 * @reentrant no
 *
 * @see heapstore_trace_init()
 * @since v1.0.0
 */
void heapstore_trace_shutdown(void);

/**
  * @brief Write a span record
 *
  * @param span [in] Span record
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of span
 * @threadsafe yes
 * @reentrant no
 */
heapstore_error_t heapstore_trace_write_span(const heapstore_span_t *span);

/**
  * @brief Batch-write span records
 *
  * @param spans [in] Span array
  * @param count [in] Span count
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of spans
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_trace_write_spans_batch(const heapstore_span_t *spans, size_t count);

/**
  * @brief Query all spans by trace_id
 *
  * @param trace_id [in] Trace ID
  * @param spans [out] Output span array (caller frees)
  * @param count [out] Output span count
  * @return heapstore_error_t Error code
 *
  * @ownership Caller must free spans via heapstore_trace_free_spans
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_trace_query_by_trace(const char *trace_id, heapstore_span_t **spans,
                                                 size_t *count);

/**
  * @brief Query spans by time range
 *
  * @param start_time [in] Start time (nanoseconds)
  * @param end_time [in] End time (nanoseconds)
  * @param spans [out] Output span array (caller frees)
  * @param count [out] Output span count
  * @return heapstore_error_t Error code
 *
  * @ownership Caller must free spans via heapstore_trace_free_spans
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_trace_query_by_time_range(uint64_t start_time, uint64_t end_time,
                                                      heapstore_span_t **spans, size_t *count);

/**
  * @brief Free a span array
 *
  * @param spans [in] Span array
 *
  * @ownership Caller must pass a valid spans pointer
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
void heapstore_trace_free_spans(heapstore_span_t *spans);

/**
  * @brief Configure the trace exporter
 *
  * @param manager [in] Exporter configuration
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns the lifetime of manager
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_trace_config_exporter(const heapstore_trace_exporter_config_t *manager);

/**
  * @brief Force-export pending trace data
 *
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_trace_flush(void);

/**
  * @brief Get trace storage statistics
 *
  * @param total_spans [out] Output total span count
  * @param pending_spans [out] Output pending span count
  * @param total_size_bytes [out] Output total storage size
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns allocation and freeing of all output params
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_error_t heapstore_trace_get_stats(uint64_t *total_spans, uint64_t *pending_spans,
                                            uint64_t *total_size_bytes);

/**
  * @brief Clean up expired trace data
 *
  * @param days_to_keep [in] Retention days
  * @param freed_bytes [out] Bytes freed
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns allocation and freeing of freed_bytes
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_trace_cleanup(int days_to_keep, uint64_t *freed_bytes);

/**
  * @brief Check whether the trace subsystem is healthy
 *
  * @return bool true if healthy
 *
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
bool heapstore_trace_is_healthy(void);

/**
  * @brief Export all trace data as a JSON string
 *
  * @param out_json [out] Output JSON string (caller frees)
  * @param include_events [in] Whether to include event info (reserved)
  * @return heapstore_error_t Error code
 *
  * @ownership Caller must free out_json with free()
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_trace_export_to_json(char **out_json, bool include_events);

#ifdef __cplusplus
}
#endif

#endif /* AIRY_heapstore_TRACE_H */
