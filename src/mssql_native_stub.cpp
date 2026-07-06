#include "mssql_native.h"
#include <string>
#include <cstring>
#include <cstdlib>

// Stub implementation for when FreeTDS is not available
// This allows the package to build without FreeTDS
// Users should build FreeTDS for production use

static char* alloc_string(const char* str) {
    if (!str) return nullptr;
    const size_t len = strlen(str) + 1;
    char* result = (char*)malloc(len);
    if (result) memcpy(result, str, len);
    return result;
}

MSSQL_EXPORT int64_t mssql_connect(
    const char* host, int32_t port, const char* database,
    const char* username, const char* password,
    int32_t trust_server_certificate, int32_t enable_tls, int32_t timeout
) {
    // Stub: return error
    return -1000; // Error code indicating stub implementation
}

MSSQL_EXPORT int32_t mssql_disconnect(int64_t connection_handle) {
    return -1000;
}

MSSQL_EXPORT const char* mssql_execute_query(int64_t connection_handle, const char* query) {
    return alloc_string("{\"columns\":[],\"rows\":[],\"affected\":0}");
}

MSSQL_EXPORT const char* mssql_execute_query_with_params(
    int64_t connection_handle, const char* query, const char* params_json
) {
    return alloc_string("{\"columns\":[],\"rows\":[],\"affected\":0}");
}

MSSQL_EXPORT int32_t mssql_execute_write(int64_t connection_handle, const char* query) {
    return -1000;
}

MSSQL_EXPORT int32_t mssql_execute_write_with_params(
    int64_t connection_handle, const char* query, const char* params_json
) {
    return -1000;
}

MSSQL_EXPORT int32_t mssql_begin_transaction(int64_t connection_handle) {
    return -1000;
}

MSSQL_EXPORT int32_t mssql_commit_transaction(int64_t connection_handle) {
    return -1000;
}

MSSQL_EXPORT int32_t mssql_rollback_transaction(int64_t connection_handle) {
    return -1000;
}

MSSQL_EXPORT int32_t mssql_bulk_insert(
    int64_t connection_handle, const char* table_name,
    const char* data_json, int32_t batch_size
) {
    return -1000;
}

MSSQL_EXPORT const char* mssql_get_last_error(int64_t connection_handle) {
    return alloc_string("FreeTDS not available. Build with FreeTDS for production use. See README for setup instructions.");
}

MSSQL_EXPORT void mssql_free_string(const char* str) {
    if (str) free((void*)str);
}

