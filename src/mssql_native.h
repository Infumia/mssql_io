#ifndef MSSQL_NATIVE_H
#define MSSQL_NATIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Platform-specific export macros
#if defined(_WIN32) || defined(_WIN64)
    #define MSSQL_EXPORT __declspec(dllexport)
#else
    #define MSSQL_EXPORT __attribute__((visibility("default")))
#endif

/**
 * Connect to SQL Server
 * 
 * Supports two authentication modes:
 * - SQL Server Authentication: Provide username and password.
 * - Windows Integrated Authentication: Pass empty username and password.
 *   The current Windows user credentials will be used.
 * 
 * @param host Server hostname or IP address
 * @param port Server port (usually 1433)
 * @param database Database name
 * @param username SQL authentication username (empty string for Windows auth)
 * @param password SQL authentication password (empty string for Windows auth)
 * @param trust_server_certificate If non-zero, trust the server certificate without validation
 * @param timeout Connection timeout in seconds
 * @return Connection handle (>0 on success, <=0 on error)
 */
MSSQL_EXPORT int64_t mssql_connect(
    const char* host,
    int32_t port,
    const char* database,
    const char* username,
    const char* password,
    int32_t trust_server_certificate,
    int32_t timeout
);

/**
 * Disconnect from SQL Server
 * 
 * @param connection_handle Handle returned by mssql_connect
 * @return 0 on success, negative on error
 */
MSSQL_EXPORT int32_t mssql_disconnect(int64_t connection_handle);

/**
 * Execute a SELECT query
 * 
 * @param connection_handle Connection handle
 * @param query SQL query string
 * @return JSON string containing results (must be freed with mssql_free_string)
 *         Format: { "columns": ["col1", "col2"], "rows": [{...}], "affected": 0 }
 */
MSSQL_EXPORT const char* mssql_execute_query(
    int64_t connection_handle,
    const char* query
);

/**
 * Execute a parameterized SELECT query
 * 
 * @param connection_handle Connection handle
 * @param query SQL query with parameter placeholders (@param1, @param2, etc.)
 * @param params_json JSON array of parameters:
 *        [{"name": "@param1", "value": "value1", "type": "NVARCHAR"}, ...]
 * @return JSON string containing results (must be freed with mssql_free_string)
 */
MSSQL_EXPORT const char* mssql_execute_query_with_params(
    int64_t connection_handle,
    const char* query,
    const char* params_json
);

/**
 * Execute a write operation (INSERT/UPDATE/DELETE)
 * 
 * @param connection_handle Connection handle
 * @param query SQL query string
 * @return Number of affected rows (negative on error)
 */
MSSQL_EXPORT int32_t mssql_execute_write(
    int64_t connection_handle,
    const char* query
);

/**
 * Execute a parameterized write operation
 * 
 * @param connection_handle Connection handle
 * @param query SQL query with parameter placeholders
 * @param params_json JSON array of parameters
 * @return Number of affected rows (negative on error)
 */
MSSQL_EXPORT int32_t mssql_execute_write_with_params(
    int64_t connection_handle,
    const char* query,
    const char* params_json
);

/**
 * Begin a transaction
 * 
 * @param connection_handle Connection handle
 * @return 0 on success, negative on error
 */
MSSQL_EXPORT int32_t mssql_begin_transaction(int64_t connection_handle);

/**
 * Commit a transaction
 * 
 * @param connection_handle Connection handle
 * @return 0 on success, negative on error
 */
MSSQL_EXPORT int32_t mssql_commit_transaction(int64_t connection_handle);

/**
 * Rollback a transaction
 * 
 * @param connection_handle Connection handle
 * @return 0 on success, negative on error
 */
MSSQL_EXPORT int32_t mssql_rollback_transaction(int64_t connection_handle);

/**
 * Bulk insert data into a table using BCP
 * 
 * @param connection_handle Connection handle
 * @param table_name Target table name
 * @param data_json JSON array of rows to insert: [{"col1": val1, "col2": val2}, ...]
 * @param batch_size Number of rows to insert per batch
 * @return Number of rows inserted (negative on error)
 */
MSSQL_EXPORT int32_t mssql_bulk_insert(
    int64_t connection_handle,
    const char* table_name,
    const char* data_json,
    int32_t batch_size
);

/**
 * Get last error message for a connection
 * 
 * @param connection_handle Connection handle
 * @return Error message string (must be freed with mssql_free_string)
 */
MSSQL_EXPORT const char* mssql_get_last_error(int64_t connection_handle);

/**
 * Free a string allocated by native functions
 * 
 * @param str String pointer returned by native functions
 */
MSSQL_EXPORT void mssql_free_string(const char* str);

#ifdef __cplusplus
}
#endif

#endif // MSSQL_NATIVE_H

