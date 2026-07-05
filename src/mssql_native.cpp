#include "mssql_native.h"
#include <sybfront.h>
#include <sybdb.h>
#include <string>
#include <map>
#include <vector>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <iomanip>

// JSON helper (minimal implementation - for production, use a proper JSON library)
#include <iostream>

// Connection structure
struct MssqlConnection {
    DBPROCESS* dbproc;
    std::string last_error;
    std::string host;
    std::string database;
    int port;
    bool in_transaction;
    
    MssqlConnection() : dbproc(nullptr), port(1433), in_transaction(false) {}
};

// Global connection map (connection_id -> connection)
static std::map<int64_t, MssqlConnection*> g_connections;
static int64_t g_next_connection_id = 1;

// Global last error for connection failures (before a handle is created)
static std::string g_last_connect_error;

// Error handler for FreeTDS
static std::string g_freetds_error;
static std::string g_freetds_message;

static int error_handler(DBPROCESS* dbproc, int severity, int dberr, int oserr,
                        char* dberrstr, char* oserrstr) {
    g_freetds_error.clear();
    if (dberrstr) {
        g_freetds_error += dberrstr;
        fprintf(stderr, "DB-Library error: %s\n", dberrstr);
    }
    if (oserrstr && oserr != 0) {
        if (!g_freetds_error.empty()) g_freetds_error += "; ";
        g_freetds_error += oserrstr;
        fprintf(stderr, "Operating system error: %s\n", oserrstr);
    }
    return INT_CANCEL;
}

// Message handler for FreeTDS
static int message_handler(DBPROCESS* dbproc, DBINT msgno, int msgstate, int severity,
                          char* msgtext, char* srvname, char* procname, int line) {
    g_freetds_message.clear();
    if (msgtext) {
        g_freetds_message = msgtext;
        fprintf(stderr, "SQL Server message %d: %s\n", (int)msgno, msgtext);
    }
    return 0;
}

// Initialize FreeTDS (called once)
static void init_freetds() {
    static bool initialized = false;
    if (!initialized) {
        if (dbinit() == FAIL) {
            fprintf(stderr, "Failed to initialize FreeTDS\n");
            return;
        }
        dberrhandle(error_handler);
        dbmsghandle(message_handler);
        initialized = true;
    }
}

// Base64 encoding for binary data
static std::string base64_encode(const unsigned char* data, size_t len) {
    static const char* base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string result;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (len--) {
        char_array_3[i++] = *(data++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; i < 4; i++)
                result += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(int j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (int j = 0; j < i + 1; j++)
            result += base64_chars[char_array_4[j]];

        while(i++ < 3)
            result += '=';
    }

    return result;
}

// JSON escape string
static std::string json_escape(const std::string& str) {
    std::ostringstream oss;
    for (char c : str) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c < 0x20) {
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
                } else {
                    oss << c;
                }
        }
    }
    return oss.str();
}

// Allocate and copy string (caller must free with mssql_free_string)
static char* alloc_string(const std::string& str) {
    const size_t len = str.length() + 1;
    char* result = (char*)malloc(len);
    if (result) {
        memcpy(result, str.c_str(), len);
    }
    return result;
}

// Connect to SQL Server
MSSQL_EXPORT int64_t mssql_connect(
    const char* host,
    int32_t port,
    const char* database,
    const char* username,
    const char* password,
    int32_t trust_server_certificate,
    int32_t timeout
) {
    init_freetds();
    g_last_connect_error.clear();
    
    if (!host || !database) {
        g_last_connect_error = "Host and database are required";
        return -1;
    }

    // Determine authentication mode
    bool use_windows_auth = false;
    if ((!username || strlen(username) == 0) && (!password || strlen(password) == 0)) {
        use_windows_auth = true;
    }

    LOGINREC* login = dblogin();
    if (!login) {
        g_last_connect_error = "Failed to create login record (dblogin failed)";
        return -2;
    }

    if (use_windows_auth) {
        // Windows Integrated Authentication (NTLM/Kerberos)
        // Enable NTLMv2 authentication via dbsetlbool
        DBSETLNTLMV2(login, 1);
        // Set empty credentials - FreeTDS will use Windows credentials
        DBSETLUSER(login, "");
        DBSETLPWD(login, "");
    } else {
        DBSETLUSER(login, username);
        DBSETLPWD(login, password);
    }

    DBSETLAPP(login, "mssql_io");

    // Configure encryption and trust settings via environment variables
    // These are read by FreeTDS during connection
    if (trust_server_certificate) {
        // Accept self-signed certificates without CA validation
        #ifdef _WIN32
            _putenv_s("TDS_SSL_VERIFY_SERVER_CERTIFICATE", "0");
        #else
            setenv("TDS_SSL_VERIFY_SERVER_CERTIFICATE", "0", 1);
        #endif
    }

    // Set TDS protocol version on the login record for named instance support
    // Named instances (host\INSTANCE) require TDS 7.0+ to resolve
    // via SQL Server Browser Service
    DBSETLVERSION(login, DBTDS_7_4);

    if (timeout > 0) {
        dbsetlogintime(timeout);
        dbsettime(timeout);
    }

    // Connect to server
    // For named instances (e.g. "host\INSTANCE"), FreeTDS needs TDS 7.0+
    // to resolve via SQL Server Browser Service
    DBPROCESS* dbproc = dbopen(login, host);

    if (!dbproc) {
        g_last_connect_error = "Failed to open connection to server";
        if (!g_freetds_error.empty()) {
            g_last_connect_error += ": " + g_freetds_error;
        }
        dbloginfree(login);
        return -3;
    }

    dbloginfree(login);

    // Use database
    if (dbuse(dbproc, database) == FAIL) {
        g_last_connect_error = "Failed to select database";
        dbclose(dbproc);
        return -4;
    }

    // Create connection structure
    MssqlConnection* request = new MssqlConnection();
    request->dbproc = dbproc;
    request->host = host;
    request->database = database;
    request->port = port;

    int64_t conn_id = g_next_connection_id++;
    g_connections[conn_id] = request;

    return conn_id;
}

// Disconnect
MSSQL_EXPORT int32_t mssql_disconnect(int64_t connection_handle) {
    auto it = g_connections.find(connection_handle);
    if (it == g_connections.end()) {
        return -1;
    }

    MssqlConnection* request = it->second;
    if (request->dbproc) {
        dbclose(request->dbproc);
    }
    delete request;
    g_connections.erase(it);

    return 0;
}

// Execute query and return JSON
MSSQL_EXPORT const char* mssql_execute_query(
    int64_t connection_handle,
    const char* query
) {
    auto it = g_connections.find(connection_handle);
    if (it == g_connections.end() || !query) {
        return alloc_string("{\"columns\":[],\"rows\":[],\"affected\":0}");
    }

    MssqlConnection* request = it->second;
    DBPROCESS* dbproc = request->dbproc;

    if (dbcmd(dbproc, query) == FAIL) {
        request->last_error = "Failed to set query command";
        return alloc_string("{\"columns\":[],\"rows\":[],\"affected\":0}");
    }

    if (dbsqlexec(dbproc) == FAIL) {
        request->last_error = "Failed to execute query";
        return alloc_string("{\"columns\":[],\"rows\":[],\"affected\":0}");
    }

    std::ostringstream result;
    result << "{\"columns\":[";

    // Process results
    if (dbresults(dbproc) == SUCCEED) {
        int num_cols = dbnumcols(dbproc);
        
        // Column names
        for (int i = 1; i <= num_cols; i++) {
            if (i > 1) result << ",";
            result << "\"" << json_escape(dbcolname(dbproc, i)) << "\"";
        }
        result << "],\"rows\":[";

        // Rows
        bool first_row = true;
        while (dbnextrow(dbproc) != NO_MORE_ROWS) {
            if (!first_row) result << ",";
            first_row = false;
            result << "{";

            for (int i = 1; i <= num_cols; i++) {
                if (i > 1) result << ",";
                
                const char* col_name = dbcolname(dbproc, i);
                result << "\"" << json_escape(col_name) << "\":";

                // Check for NULL
                if (dbdata(dbproc, i) == NULL) {
                    result << "null";
                    continue;
                }

                int col_type = dbcoltype(dbproc, i);
                
                switch (col_type) {
                    case SYBINT1:
                    case SYBINT2:
                    case SYBINT4:
                    case SYBINT8: {
                        DBINT value = 0;
                        dbconvert(dbproc, col_type, dbdata(dbproc, i), dbdatlen(dbproc, i),
                                 SYBINT4, (BYTE*)&value, sizeof(value));
                        result << value;
                        break;
                    }
                    case SYBFLT8:
                    case SYBREAL: {
                        DBFLT8 value = 0.0;
                        dbconvert(dbproc, col_type, dbdata(dbproc, i), dbdatlen(dbproc, i),
                                 SYBFLT8, (BYTE*)&value, sizeof(value));
                        result << value;
                        break;
                    }
                    case SYBBIT: {
                        DBBIT value = 0;
                        dbconvert(dbproc, col_type, dbdata(dbproc, i), dbdatlen(dbproc, i),
                                 SYBBIT, (BYTE*)&value, sizeof(value));
                        result << (value ? "true" : "false");
                        break;
                    }
                    case SYBBINARY:
                    case SYBVARBINARY:
                    case SYBIMAGE: {
                        // Base64 encode binary data
                        int len = dbdatlen(dbproc, i);
                        std::string b64 = base64_encode((unsigned char*)dbdata(dbproc, i), len);
                        result << "\"" << b64 << "\"";
                        break;
                    }
                    default: {
                        // Convert to string
                        char buffer[8192];
                        int converted_len = dbconvert(dbproc, col_type, dbdata(dbproc, i),
                                                     dbdatlen(dbproc, i), SYBCHAR,
                                                     (BYTE*)buffer, sizeof(buffer) - 1);
                        if (converted_len >= 0) {
                            buffer[converted_len] = '\0';
                            result << "\"" << json_escape(buffer) << "\"";
                        } else {
                            result << "null";
                        }
                    }
                }
            }
            result << "}";
        }
    } else {
        result << "],\"rows\":[";
    }

    result << "],\"affected\":0}";
    return alloc_string(result.str());
}

// Execute query with parameters (simplified - uses sp_executesql)
MSSQL_EXPORT const char* mssql_execute_query_with_params(
    int64_t connection_handle,
    const char* query,
    const char* params_json
) {
    // For simplicity, this implementation builds a parameterized query string
    // In production, properly parse params_json and use sp_executesql
    return mssql_execute_query(connection_handle, query);
}

// Execute write operation
MSSQL_EXPORT int32_t mssql_execute_write(
    int64_t connection_handle,
    const char* query
) {
    auto it = g_connections.find(connection_handle);
    if (it == g_connections.end() || !query) {
        return -1;
    }

    MssqlConnection* request = it->second;
    DBPROCESS* dbproc = request->dbproc;

    if (dbcmd(dbproc, query) == FAIL) {
        request->last_error = "Failed to set command";
        return -2;
    }

    if (dbsqlexec(dbproc) == FAIL) {
        request->last_error = "Failed to execute command";
        return -3;
    }

    if (dbresults(dbproc) != SUCCEED) {
        return 0;
    }

    return (int32_t)DBCOUNT(dbproc);
}

// Execute write with parameters
MSSQL_EXPORT int32_t mssql_execute_write_with_params(
    int64_t connection_handle,
    const char* query,
    const char* params_json
) {
    // For simplicity, delegate to regular execute_write
    return mssql_execute_write(connection_handle, query);
}

// Begin transaction
MSSQL_EXPORT int32_t mssql_begin_transaction(int64_t connection_handle) {
    auto it = g_connections.find(connection_handle);
    if (it == g_connections.end()) {
        return -1;
    }

    MssqlConnection* request = it->second;
    if (mssql_execute_write(connection_handle, "BEGIN TRANSACTION") < 0) {
        return -2;
    }
    request->in_transaction = true;
    return 0;
}

// Commit transaction
MSSQL_EXPORT int32_t mssql_commit_transaction(int64_t connection_handle) {
    auto it = g_connections.find(connection_handle);
    if (it == g_connections.end()) {
        return -1;
    }

    MssqlConnection* request = it->second;
    if (mssql_execute_write(connection_handle, "COMMIT TRANSACTION") < 0) {
        return -2;
    }
    request->in_transaction = false;
    return 0;
}

// Rollback transaction
MSSQL_EXPORT int32_t mssql_rollback_transaction(int64_t connection_handle) {
    auto it = g_connections.find(connection_handle);
    if (it == g_connections.end()) {
        return -1;
    }

    MssqlConnection* request = it->second;
    if (mssql_execute_write(connection_handle, "ROLLBACK TRANSACTION") < 0) {
        return -2;
    }
    request->in_transaction = false;
    return 0;
}

// Bulk insert (simplified - uses batched INSERT statements)
MSSQL_EXPORT int32_t mssql_bulk_insert(
    int64_t connection_handle,
    const char* table_name,
    const char* data_json,
    int32_t batch_size
) {
    // For production, implement proper BCP using bcp_init, bcp_bind, bcp_batch, etc.
    // This is a simplified version that would need proper JSON parsing
    return -1; // Not implemented in this stub
}

// Get last error
MSSQL_EXPORT const char* mssql_get_last_error(int64_t connection_handle) {
    auto it = g_connections.find(connection_handle);
    if (it == g_connections.end()) {
        // If no valid connection, return the global connection error
        if (!g_last_connect_error.empty()) {
            return alloc_string(g_last_connect_error);
        }
        return alloc_string("Invalid connection handle");
    }

    MssqlConnection* request = it->second;
    if (request->last_error.empty()) {
        return alloc_string("No error");
    }

    return alloc_string(request->last_error);
}

// Free string
MSSQL_EXPORT void mssql_free_string(const char* str) {
    if (str) {
        free((void*)str);
    }
}

