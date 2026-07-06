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
#include <ctime>
#include <cerrno>

// JSON helper (minimal implementation - for production, use a proper JSON library)
#include <iostream>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#endif

static std::string get_debug_log_path() {
#ifdef _WIN32
    char temp_buf[MAX_PATH] = {0};
    DWORD len = GetTempPathA(MAX_PATH, temp_buf);
    if (len > 0 && len < MAX_PATH) {
        std::string path(temp_buf);
        if (path.back() != '\\' && path.back() != '/') {
            path += "\\";
        }
        path += "mssql_io.log";
        return path;
    }
    return "mssql_io.log";
#else
    return "/tmp/mssql_io.log";
#endif
}

static std::string get_temp_file_path(const std::string& file_name) {
#ifdef _WIN32
    char temp_buf[MAX_PATH] = {0};
    DWORD len = GetTempPathA(MAX_PATH, temp_buf);
    if (len > 0 && len < MAX_PATH) {
        std::string path(temp_buf);
        if (path.back() != '\\' && path.back() != '/') {
            path += "\\";
        }
        path += file_name;
        return path;
    }
    return file_name;
#else
    std::string path("/tmp/");
    path += file_name;
    return path;
#endif
}

static void set_env_var(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

static void debug_log(const std::string& message) {
    const std::string path = get_debug_log_path();
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path.c_str(), "a");
#else
    f = fopen(path.c_str(), "a");
#endif
    if (!f) {
        return;
    }

    std::time_t now = std::time(nullptr);
    char time_buffer[32] = {0};
#ifdef _WIN32
    tm local_time = {};
    localtime_s(&local_time, &now);
    std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &local_time);
#else
    tm local_time = {};
    localtime_r(&now, &local_time);
    std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &local_time);
#endif
    fprintf(f, "[%s] [native] %s\n", time_buffer, message.c_str());
    fclose(f);
}

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
        std::ostringstream oss;
        oss << "DB-Library error"
            << " severity=" << severity
            << " dberr=" << dberr
            << " oserr=" << oserr
            << " message=" << dberrstr;
        debug_log(oss.str());
    }
    if (oserrstr && oserr != 0) {
        if (!g_freetds_error.empty()) g_freetds_error += "; ";
        g_freetds_error += oserrstr;
        fprintf(stderr, "Operating system error: %s\n", oserrstr);
        std::ostringstream oss;
        oss << "Operating system error"
            << " oserr=" << oserr
            << " message=" << oserrstr;
        debug_log(oss.str());
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
        std::ostringstream oss;
        oss << "SQL Server message"
            << " msgno=" << (int)msgno
            << " state=" << msgstate
            << " severity=" << severity
            << " server=" << (srvname ? srvname : "")
            << " proc=" << (procname ? procname : "")
            << " line=" << line
            << " text=" << msgtext;
        debug_log(oss.str());
    }
    return 0;
}

// Initialize FreeTDS (called once)
static void init_freetds() {
    static bool initialized = false;
    if (initialized) return;
    if (dbinit() == FAIL) {
        fprintf(stderr, "Failed to initialize FreeTDS\n");
        debug_log("Failed to initialize FreeTDS");
        return;
    }
    dberrhandle(error_handler);
    dbmsghandle(message_handler);
    initialized = true;
    debug_log("FreeTDS initialized and handlers registered");
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

#ifdef _WIN32
// Query SQL Server Browser Service (UDP 1434) to resolve dynamic port for a named instance.
// Returns the TCP port number, or -1 on failure.
static int resolve_browser_port(const char* hostname, const char* instance_name) {
    if (!hostname || !instance_name || strlen(instance_name) == 0) {
        return -1;
    }

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        debug_log("SQL Browser lookup failed: WSAStartup failed");
        return -1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Failed to create UDP socket\n");
        debug_log("SQL Browser lookup failed: failed to create UDP socket");
        WSACleanup();
        return -1;
    }

    // Set receive timeout (2 seconds)
    int timeout_ms = 2000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));

    // Set up server address (browser service runs on UDP 1434)
    struct addrinfo hints = {}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "1434");

    if (getaddrinfo(hostname, port_str, &hints, &result) != 0 || !result) {
        fprintf(stderr, "Failed to resolve browser service host: %s\n", hostname);
        debug_log(std::string("SQL Browser lookup failed: could not resolve host ") + hostname);
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    // Build browser query packet: 0x04 + instance name (ASCII)
    size_t instance_len = strlen(instance_name);
    std::vector<unsigned char> packet(1 + instance_len);
    packet[0] = 0x04;
    memcpy(packet.data() + 1, instance_name, instance_len);

    // Send query
    int sent = sendto(sock, (const char*)packet.data(), (int)packet.size(), 0,
                      result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);

    if (sent == SOCKET_ERROR) {
        fprintf(stderr, "Failed to send browser query\n");
        debug_log(std::string("SQL Browser lookup failed: sendto failed for instance ") + instance_name);
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    // Receive response
    char response[4096];
    struct sockaddr_in from_addr = {};
    int from_len = sizeof(from_addr);

    int received = recvfrom(sock, response, sizeof(response) - 1, 0,
                            (struct sockaddr*)&from_addr, &from_len);
    closesocket(sock);
    WSACleanup();

    if (received <= 0) {
        fprintf(stderr, "No response from SQL Server Browser Service\n");
        debug_log(std::string("SQL Browser lookup failed: no UDP 1434 response from ") + hostname);
        return -1;
    }
    response[received] = '\0';

    // Parse response: semicolon-delimited key-value pairs
    // Look for "tcp" key and extract the port value
    // Format: ServerName;...;tcp;56787;...
    std::string resp(response, received);
    std::istringstream ss(resp);
    std::string token;
    bool found_tcp = false;

    while (std::getline(ss, token, ';')) {
        if (found_tcp) {
            // This token should be the port number
            int port = 0;
            for (char c : token) {
                if (c >= '0' && c <= '9') {
                    port = port * 10 + (c - '0');
                } else {
                    // Non-digit in what should be a port number
                    port = 0;
                    break;
                }
            }
            if (port > 0 && port <= 65535) {
                fprintf(stderr, "Browser service resolved port: %d\n", port);
                std::ostringstream oss;
                oss << "SQL Browser resolved instance " << instance_name
                    << " on host " << hostname
                    << " to tcp port " << port;
                debug_log(oss.str());
                return port;
            }
            break;
        }
        if (token == "tcp") {
            found_tcp = true;
        }
    }

    fprintf(stderr, "TCP port not found in browser service response\n");
    debug_log(std::string("SQL Browser lookup failed: tcp port not found in response: ") + resp);
    return -1;
}

// Split "host\\INSTANCE" into hostname and instance name.
// Returns true if the input contains a backslash (named instance).
static bool split_host_instance(const std::string& host, std::string& hostname, std::string& instance) {
    size_t pos = host.find('\\');
    if (pos == std::string::npos) {
        return false;
    }
    hostname = host.substr(0, pos);
    instance = host.substr(pos + 1);
    return !hostname.empty() && !instance.empty();
}

// Write a freetds.conf with the resolved port for diagnostics/fallback.
// The primary connection path uses "host:port" directly because that avoids
// depending on FreeTDS loading a temporary config alias.
// Returns true on success.
static bool write_tds_conf(const std::string& hostname, int port, bool enable_tls) {
    const std::string conf_path = get_temp_file_path("mssql_io_freetds.conf");

    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, conf_path.c_str(), "w");
#else
    f = fopen(conf_path.c_str(), "w");
#endif
    if (!f) {
        fprintf(stderr, "Failed to write freetds.conf to %s\n", conf_path.c_str());
        char err_buf[256] = {0};
#ifdef _WIN32
        strerror_s(err_buf, sizeof(err_buf), errno);
#else
        strerror_r(errno, err_buf, sizeof(err_buf));
#endif
        std::ostringstream oss;
        oss << "Failed to write FreeTDS config to " << conf_path
            << " errno=" << errno
            << " error=" << err_buf;
        debug_log(oss.str());
        return false;
    }

    fprintf(f, "[global]\n");
    fprintf(f, "    tds version = 7.4\n");
    fprintf(f, "    client charset = UTF-8\n\n");
    fprintf(f, "[%s]\n", hostname.c_str());
    fprintf(f, "    host = %s\n", hostname.c_str());
    fprintf(f, "    port = %d\n", port);
    fprintf(f, "    tds version = 7.4\n");
    fprintf(f, "    client charset = UTF-8\n");
    fprintf(f, "    encryption = %s\n\n", enable_tls ? "request" : "off");
    fprintf(f, "[mssql_io_resolved]\n");
    fprintf(f, "    host = %s\n", hostname.c_str());
    fprintf(f, "    port = %d\n", port);
    fprintf(f, "    tds version = 7.4\n");
    fprintf(f, "    client charset = UTF-8\n");
    fprintf(f, "    encryption = %s\n", enable_tls ? "request" : "off");
    fclose(f);

    // Also set FREETDSCONF env var as fallback (note: NO underscore)
    set_env_var("FREETDSCONF", conf_path);

    fprintf(stderr, "Wrote freetds.conf: %s (host=%s, port=%d)\n",
            conf_path.c_str(), hostname.c_str(), port);
    std::ostringstream oss;
    oss << "Wrote FreeTDS config " << conf_path
        << " for host=" << hostname
        << " port=" << port
        << " encryption=" << (enable_tls ? "request" : "off");
    debug_log(oss.str());
    return true;
}
#endif

// Connect to SQL Server
MSSQL_EXPORT int64_t mssql_connect(
    const char* host,
    int32_t port,
    const char* database,
    const char* username,
    const char* password,
    int32_t trust_server_certificate,
    int32_t enable_tls,
    int32_t timeout
) {
    g_last_connect_error.clear();
    
    if (!host || !database) {
        g_last_connect_error = "Host and database are required";
        debug_log("Connection failed before start: host or database was null");
        return -1;
    }

    {
        const bool use_windows_auth =
            (!username || strlen(username) == 0) && (!password || strlen(password) == 0);
        std::ostringstream oss;
        oss << "Connection attempt started"
            << " host=" << host
            << " port=" << port
            << " database=" << database
            << " auth=" << (use_windows_auth ? "windows" : "sql")
            << " trust_server_certificate=" << trust_server_certificate
            << " enable_tls=" << enable_tls
            << " timeout=" << timeout
            << " log_file=" << get_debug_log_path();
        debug_log(oss.str());
    }

    const std::string tds_dump_path = get_temp_file_path("mssql_io_tdsdump.log");
    const std::string tds_config_dump_path = get_temp_file_path("mssql_io_tdsconfig.log");
    set_env_var("TDSDUMP", tds_dump_path);
    set_env_var("TDSDUMPCONFIG", tds_config_dump_path);
    debug_log(std::string("Enabled FreeTDS dumps TDSDUMP=") + tds_dump_path +
              " TDSDUMPCONFIG=" + tds_config_dump_path);

    if (!enable_tls) {
        set_env_var("TDSENCRYPTION", "off");
        debug_log("Configured FreeTDS encryption off through TDSENCRYPTION=off");
    } else {
        set_env_var("TDSENCRYPTION", "request");
        debug_log("Configured FreeTDS encryption request through TDSENCRYPTION=request");
    }

    // Resolve named instance (host\INSTANCE) to dynamic port via SQL Server Browser Service.
    std::string actual_host = host;
    #ifdef _WIN32
    {
        std::string hostname, instance_name;
        if (split_host_instance(host, hostname, instance_name)) {
            debug_log(std::string("Named instance detected: host=") + hostname + " instance=" + instance_name);
            int resolved_port = resolve_browser_port(hostname.c_str(), instance_name.c_str());
            if (resolved_port > 0) {
                port = resolved_port;
                std::ostringstream endpoint;
                endpoint << hostname << ":" << resolved_port;
                actual_host = endpoint.str();
                write_tds_conf(hostname, resolved_port, enable_tls != 0);
                {
                    std::ostringstream port_value;
                    port_value << resolved_port;
                    set_env_var("TDSPORT", port_value.str());
                }
                fprintf(stderr, "Named instance '%s' resolved to port %d\n", instance_name.c_str(), port);
                debug_log(std::string("Named instance will connect directly through resolved endpoint ") + actual_host);
            } else {
                fprintf(stderr, "Warning: Could not resolve named instance '%s' via Browser Service, "
                        "FreeTDS will attempt its own resolution\n", instance_name.c_str());
                debug_log(std::string("Named instance resolution failed; passing original host to FreeTDS: ") + actual_host);
            }
        }
    }
    #endif

    // Initialize FreeTDS
    init_freetds();
    bool use_windows_auth = false;
    if ((!username || strlen(username) == 0) && (!password || strlen(password) == 0)) {
        use_windows_auth = true;
    }

    LOGINREC* login = dblogin();
    if (!login) {
        g_last_connect_error = "Failed to create login record (dblogin failed)";
        debug_log(g_last_connect_error);
        return -2;
    }

    if (use_windows_auth) {
        debug_log("Configuring FreeTDS login for Windows Integrated Authentication");
        // Windows Integrated Authentication (NTLM/Kerberos)
        // Enable NTLMv2 authentication via dbsetlbool
        DBSETLNTLMV2(login, 1);
        // Set empty credentials - FreeTDS will use Windows credentials
        DBSETLUSER(login, "");
        DBSETLPWD(login, "");
    } else {
        debug_log(std::string("Configuring FreeTDS login for SQL Authentication user=") + (username ? username : ""));
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
        debug_log("Configured FreeTDS to skip server certificate verification");
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
    // For resolved named instances, actual_host is "hostname:port".
    // For direct connections, actual_host is the original host string.
    debug_log(std::string("Calling dbopen with server name ") + actual_host);
    DBPROCESS* dbproc = dbopen(login, actual_host.c_str());

    if (!dbproc) {
        g_last_connect_error = "Failed to open connection to server";
        if (!g_freetds_error.empty()) {
            g_last_connect_error += ": " + g_freetds_error;
        }
        if (!g_freetds_message.empty()) {
            g_last_connect_error += "; server message: " + g_freetds_message;
        }
        debug_log(std::string("Connection failed at dbopen: ") + g_last_connect_error);
        dbloginfree(login);
        return -3;
    }

    dbloginfree(login);
    debug_log("dbopen succeeded");

    // Use database
    if (dbuse(dbproc, database) == FAIL) {
        g_last_connect_error = "Failed to select database";
        if (!g_freetds_error.empty()) {
            g_last_connect_error += ": " + g_freetds_error;
        }
        if (!g_freetds_message.empty()) {
            g_last_connect_error += "; server message: " + g_freetds_message;
        }
        debug_log(g_last_connect_error);
        dbclose(dbproc);
        return -4;
    }
    debug_log(std::string("Database selected: ") + database);

    // Create connection structure
    MssqlConnection* request = new MssqlConnection();
    request->dbproc = dbproc;
    request->host = host;
    request->database = database;
    request->port = port;

    int64_t conn_id = g_next_connection_id++;
    g_connections[conn_id] = request;

    {
        std::ostringstream oss;
        oss << "Connection succeeded handle=" << conn_id
            << " original_host=" << host
            << " actual_host=" << actual_host
            << " port=" << port
            << " database=" << database;
        debug_log(oss.str());
    }
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

    DBINT count = DBCOUNT(dbproc);
    return count > 0 ? (int32_t)count : 0;
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

