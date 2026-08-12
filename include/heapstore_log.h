/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file heapstore_log.h
 * @brief AgentRT data partition logging interface.
 */

/* @owner: team-B */
#ifndef AIRY_RT_HEAPSTORE_LOG_H
#define AIRY_RT_HEAPSTORE_LOG_H

#include "heapstore.h"

#include <stdarg.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief Log level
 */
typedef enum {
    HEAPSTORE_LOG_DEBUG = 0,
    HEAPSTORE_LOG_INFO = 1,
    HEAPSTORE_LOG_WARN = 2,
    HEAPSTORE_LOG_ERROR = 3,
    HEAPSTORE_LOG_FATAL = 4
} heapstore_log_level_t;

/**
  * @brief Log handler type
 */
typedef enum {
    HEAPSTORE_LOG_HANDLER_FILE = 0,
    HEAPSTORE_LOG_HANDLER_STDOUT = 1,
    HEAPSTORE_LOG_HANDLER_STDERR = 2
} heapstore_log_handler_type_t;

/**
  * @brief Log file information
 */
typedef struct {
    char path[512];
    uint64_t size_bytes;
    uint32_t line_count;
    time_t created_at;
    time_t modified_at;
} heapstore_log_file_info_t;


/**
  * @brief Initialize the log system
 *
  * @return heapstore_error_t Error code
 *
  * @ownership Manages all resources internally
 * @threadsafe no (not safe for concurrent calls)
 * @reentrant no
 *
 * @see heapstore_log_shutdown()
 * @since v1.0.0
 */
heapstore_error_t heapstore_log_init(void);

/**
  * @brief Shut down the log system
 *
  * @ownership Releases all resources internally
 * @threadsafe no
 * @reentrant no
 *
 * @see heapstore_log_init()
 * @since v1.0.0
 */
void heapstore_log_shutdown(void);

/**
  * @brief Write a log entry
 *
  * @param level [in] Log level
  * @param service [in] Service name
  * @param trace_id [in] Trace ID (may be NULL)
  * @param file [in] File name
  * @param line [in] Line number
  * @param format [in] Format string
  * @param ... [in] Variadic arguments
 *
  * @ownership Caller owns the lifetime of all parameters
 * @threadsafe yes
 * @reentrant no
 *
  * @note Use the heapstore_LOG_* macros instead of direct calls
 */
void heapstore_log_write(heapstore_log_level_t level, const char *service, const char *trace_id,
                         const char *file, int line, const char *format, ...);

/**
  * @brief Write a log entry (va_list variant)
 *
  * @param level [in] Log level
  * @param service [in] Service name
  * @param trace_id [in] Trace ID (may be NULL)
  * @param file [in] File name
  * @param line [in] Line number
  * @param format [in] Format string
 * @param args [in] va_list
 *
  * @ownership Caller owns the lifetime of all parameters
 * @threadsafe yes
 * @reentrant no
 */
void heapstore_log_writev(heapstore_log_level_t level, const char *service, const char *trace_id,
                          const char *file, int line, const char *format, va_list args);

/**
  * @brief Get the current log level
 *
  * @return heapstore_log_level_t Current log level
 *
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_log_level_t heapstore_log_get_level(void);

/**
  * @brief Set the log level
 *
  * @param level [in] Log level
 *
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
void heapstore_log_set_level(heapstore_log_level_t level);

/**
  * @brief Get the log path of a service
 *
  * @param service [in] Service name
  * @param buffer [out] Output buffer
  * @param buffer_size [in] Buffer size
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns allocation and freeing of buffer
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_error_t heapstore_log_get_service_path(const char *service, char *buffer,
                                                 size_t buffer_size);

/**
  * @brief Perform log rotation
 *
  * @return heapstore_error_t Error code
 *
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_log_rotate(void);

/**
  * @brief Clean up expired log files
 *
  * @param days_to_keep [in] Retention days
  * @param freed_bytes [out] Bytes freed
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns allocation and freeing of freed_bytes
 * @threadsafe yes
 * @reentrant no

 * @since v1.0.0*/
heapstore_error_t heapstore_log_cleanup(int days_to_keep, uint64_t *freed_bytes);

/**
  * @brief Get log file information
 *
  * @param service [in] Service name（NULL for the main log）
  * @param info [out] Output file information
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns allocation and freeing of info
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_error_t heapstore_log_get_file_info(const char *service, heapstore_log_file_info_t *info);

/**
  * @brief Get log statistics
 *
  * @param total_files [out] Total file count
  * @param total_size_bytes [out] Total size
  * @param oldest_timestamp [out] Oldest log timestamp
  * @return heapstore_error_t Error code
 *
  * @ownership Caller owns allocation and freeing of all output params
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
heapstore_error_t heapstore_log_get_stats(uint32_t *total_files, uint64_t *total_size_bytes,
                                          time_t *oldest_timestamp);

/**
  * @brief Check whether the log subsystem is healthy
 *
  * @return bool true if healthy
 *
 * @threadsafe yes
 * @reentrant yes

 * @since v1.0.0*/
bool heapstore_log_is_healthy(void);

#define HEAPSTORE_LOG_ERROR(service, trace_id, fmt, ...)                                 \
    heapstore_log_write(HEAPSTORE_LOG_ERROR, service, trace_id, __FILE__, __LINE__, fmt, \
                        ##__VA_ARGS__)

#define HEAPSTORE_LOG_WARN(service, trace_id, fmt, ...)                                 \
    heapstore_log_write(HEAPSTORE_LOG_WARN, service, trace_id, __FILE__, __LINE__, fmt, \
                        ##__VA_ARGS__)

#define HEAPSTORE_LOG_INFO(service, trace_id, fmt, ...)                                 \
    heapstore_log_write(HEAPSTORE_LOG_INFO, service, trace_id, __FILE__, __LINE__, fmt, \
                        ##__VA_ARGS__)

#define HEAPSTORE_LOG_DEBUG(service, trace_id, fmt, ...)                                 \
    heapstore_log_write(HEAPSTORE_LOG_DEBUG, service, trace_id, __FILE__, __LINE__, fmt, \
                        ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* AIRY_RT_HEAPSTORE_LOG_H */
