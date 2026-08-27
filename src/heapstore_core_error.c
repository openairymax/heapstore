// SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd.
// SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0

/**
 * @file heapstore_core_error.c
 * @brief heapstore error string mapping (functional domain after
 *        heapstore_core.c split).
 */

// @owner: team-B
#include "heapstore.h"

const char *heapstore_strerror(heapstore_error_t err)
{
    switch (err) {
    case heapstore_SUCCESS:
        return "[OK] heapstore_SUCCESS: Operation completed successfully";

    case heapstore_ERR_INVALID_PARAM:
        return "[ERROR] heapstore_ERR_INVALID_PARAM: Invalid parameter provided. "
               "(context: param=NULL or out_of_range). "
               "Suggestion: Check function arguments against API documentation.";

    case heapstore_ERR_NOT_INITIALIZED:
        return "[ERROR] heapstore_ERR_NOT_INITIALIZED: heapstore module not initialized. "
               "(context: heapstore_init() not called). "
               "Suggestion: Call heapstore_init() before using any other APIs.";

    case heapstore_ERR_ALREADY_INITIALIZED:
        return "[ERROR] heapstore_ERR_ALREADY_INITIALIZED: heapstore already initialized. "
               "(context: duplicate heapstore_init() call). "
               "Suggestion: Check initialization logic, avoid duplicate calls.";

    case heapstore_ERR_DIR_CREATE_FAILED:
        return "[ERROR] heapstore_ERR_DIR_CREATE_FAILED: Failed to create directory. "
               "(context: path=/xxx/yyy, errno=13). "
               "Suggestion: Check filesystem permissions and disk space.";

    case heapstore_ERR_DIR_NOT_FOUND:
        return "[ERROR] heapstore_ERR_DIR_NOT_FOUND: Directory not found. "
               "(context: path does not exist). "
               "Suggestion: Verify the directory path and ensure it exists.";

    case heapstore_ERR_PERMISSION_DENIED:
        return "[ERROR] heapstore_ERR_PERMISSION_DENIED: Permission denied. "
               "(context: insufficient privileges for operation). "
               "Suggestion: Check file permissions or run with appropriate privileges.";

    case heapstore_ERR_OUT_OF_MEMORY:
        return "[ERROR] heapstore_ERR_OUT_OF_MEMORY: Out of memory. "
               "(context: malloc/realloc failed). "
               "Suggestion: Check system memory availability and reduce workload.";

    case heapstore_ERR_DB_INIT_FAILED:
        return "[ERROR] heapstore_ERR_DB_INIT_FAILED: Database initialization failed. "
               "(context: SQLite init error). "
               "Suggestion: Check database file permissions and disk space.";

    case heapstore_ERR_DB_QUERY_FAILED:
        return "[ERROR] heapstore_ERR_DB_QUERY_FAILED: Database query failed. "
               "(context: SQL execution error). "
               "Suggestion: Check SQL syntax and database integrity.";

    case heapstore_ERR_FILE_OPEN_FAILED:
        return "[ERROR] heapstore_ERR_FILE_OPEN_FAILED: Failed to open file. "
               "(context: fopen() failed). "
               "Suggestion: Check file path, permissions, and disk space.";

    case heapstore_ERR_CONFIG_INVALID:
        return "[ERROR] heapstore_ERR_CONFIG_INVALID: Invalid configuration. "
               "(context: config parameter validation failed). "
               "Suggestion: Review configuration parameters against documentation.";

    case heapstore_ERR_FILE_OPERATION_FAILED:
        return "[ERROR] heapstore_ERR_FILE_OPERATION_FAILED: File operation failed. "
               "(context: fread/fwrite/fseek error). "
               "Suggestion: Check file handle validity and disk space.";

    case heapstore_ERR_FILE_NOT_FOUND:
        return "[ERROR] heapstore_ERR_FILE_NOT_FOUND: File not found. "
               "(context: specified file does not exist). "
               "Suggestion: Verify file path and ensure the file exists.";

    case heapstore_ERR_NOT_FOUND:
        return "[ERROR] heapstore_ERR_NOT_FOUND: Requested resource not found. "
               "(context: record/query result not found). "
               "Suggestion: Check the resource ID or query parameters.";

    case heapstore_ERR_CIRCUIT_OPEN:
        return "[ERROR] heapstore_ERR_CIRCUIT_OPEN: Circuit breaker is open. "
               "(context: too many consecutive failures). "
               "Suggestion: Wait for circuit breaker timeout or check subsystem health.";

    case heapstore_ERR_TIMEOUT:
        return "[ERROR] heapstore_ERR_TIMEOUT: Operation timeout. "
               "(context: operation exceeded timeout_ms). "
               "Suggestion: Increase timeout or check system performance.";

    case heapstore_ERR_INTERNAL:
        return "[ERROR] heapstore_ERR_INTERNAL: Internal error. "
               "(context: unexpected error occurred). "
               "Suggestion: Check logs for details and contact support if issue persists.";

    default:
        return "[ERROR] Unknown error code. "
               "(context: undefined error). "
               "Suggestion: This is likely a bug, please report to developers.";
    }
}
