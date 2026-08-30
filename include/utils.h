/* SPDX-FileCopyrightText: 2025-2026 SPHARX Ltd. */
/* SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0 */

/**
 * @file utils.h
 * @brief AgentRT heapstore common utility functions interface.
 */

/* @owner: team-B */
#ifndef heapstore_UTILS_H
#define heapstore_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief Ensure the directory exists, creating nested dirs if needed
 *
 * @param path [in] directory path
 * @return bool true on success, false on failure
 *
 * @ownership caller owns the lifetime of path
 * @threadsafe yes
 * @reentrant yes
 *
  * @note Supports creating multi-level nested directories
 */
bool heapstore_dir_ensure(const char *path);

/**
  * @brief Compute total size and file count of a directory
 *
 * @param path [in] directory path
  * @param out_size [out] Output total size (bytes)
 * @param out_count [out] output file count
 * @return bool true on success, false on failure
 *
 * @ownership caller owns the lifetime of path
 * @threadsafe yes
 * @reentrant yes
 *
  * @note Recursively account for subdirectory sizes
 * @since v1.0.0
 */
bool heapstore_dir_size(const char *path, uint64_t *out_size, uint32_t *out_count);

/**
  * @brief Sanitize a path component against traversal and injection attacks
 *
  * Sanitizes user-supplied identifiers such as service names and channel IDs，
  * Prevents path traversal (e.g. "../../../etc/passwd") and injection。
 *
  * @param output [out] Output buffer for the sanitized string
  * @param input [in] Input string to sanitize
  * @param size [in] Output buffer size (bytes)
  * @return int 0 on success, -1 on invalid input
 *
  * @ownership Caller owns the lifetime of output and input
 * @threadsafe yes
 * @reentrant yes
 *
 * @note
  * - Rejects input containing ".." (path traversal)
  * - Rejects input containing "/" or "\\" (path separators)
  * - Rejects input containing NUL bytes (NUL injection)
  * - Only safe chars allowed: alphanumeric, underscore, hyphen, dot
  * - Dangerous characters are replaced with underscore
  * - Input length must not exceed size-1
 *
  * @warning Call before constructing any file path
 *
 * @see heapstore_dir_ensure()
 *
 * @since v0.1.0
 *
 * @example
 * @code
 * char safe_name[256];
 * if (heapstore_path_clean(safe_name, "../../../etc/passwd", sizeof(safe_name)) != 0)
 * {
 *
 *     return ERROR_INVALID_PARAM;
 * }
 *
 * @endcode
 */
int heapstore_path_clean(char *output, const char *input, size_t size);

/**
  * @brief Verify an identifier is safe (no path traversal or dangerous patterns)
 *
  * Lightweight variant of heapstore_path_clean，
  * Only checks safety; does not sanitize。
 *
  * @param input [in] Input string to validate
  * @return bool true if safe, false if dangerous patterns found
 *
 * @ownership caller owns the lifetime of input
 * @threadsafe yes
 * @reentrant yes
 *
 * @note
  * - Checks for ".." path traversal patterns
  * - Checks for "/" and "\\" path separators
  * - Checks for NUL-byte injection
  * - Checks that only safe characters are present
 *
 * @see heapstore_path_clean()
 *
 * @since v0.1.0
 */
bool heapstore_ident_safe(const char *input);

#ifdef __cplusplus
}
#endif

#endif /* heapstore_UTILS_H */
