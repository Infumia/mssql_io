/// Configuration for establishing a connection to SQL Server
class ConnectionConfig {
  /// Server IP address or hostname
  final String host;

  /// Server port (default is 1433)
  final int port;

  /// Database name to connect to
  final String databaseName;

  /// Username for SQL authentication.
  /// When null, Windows Integrated Authentication is used (requires password to also be null).
  final String? username;

  /// Password for SQL authentication.
  /// When null, Windows Integrated Authentication is used (requires username to also be null).
  final String? password;

  /// Trust the server certificate without validation.
  /// Defaults to true when using Windows Integrated Authentication.
  final bool trustServerCertificate;

  /// Connection timeout in seconds (default is 15)
  final int timeoutInSeconds;

  /// Enable TLS/SSL encryption (depends on FreeTDS configuration)
  final bool enableTls;

  /// Auto-reconnect on connection loss
  final bool autoReconnect;

  /// Maximum reconnection attempts (default is 3)
  final int maxReconnectAttempts;

  /// Reconnection delay in seconds (default is 2)
  final int reconnectDelaySeconds;

  /// Whether this config uses Windows Integrated Authentication.
  /// Returns true when both username and password are null.
  bool get useWindowsAuthentication => username == null && password == null;

  const ConnectionConfig({
    required this.host,
    this.port = 1433,
    required this.databaseName,
    this.username,
    this.password,
    this.trustServerCertificate = false,
    this.timeoutInSeconds = 15,
    this.enableTls = true,
    this.autoReconnect = false,
    this.maxReconnectAttempts = 3,
    this.reconnectDelaySeconds = 2,
  });

  /// Create a copy with modified fields
  ConnectionConfig copyWith({
    String? host,
    int? port,
    String? databaseName,
    String? username,
    String? password,
    bool? trustServerCertificate,
    int? timeoutInSeconds,
    bool? enableTls,
    bool? autoReconnect,
    int? maxReconnectAttempts,
    int? reconnectDelaySeconds,
  }) {
    return ConnectionConfig(
      host: host ?? this.host,
      port: port ?? this.port,
      databaseName: databaseName ?? this.databaseName,
      username: username ?? this.username,
      password: password ?? this.password,
      trustServerCertificate:
          trustServerCertificate ?? this.trustServerCertificate,
      timeoutInSeconds: timeoutInSeconds ?? this.timeoutInSeconds,
      enableTls: enableTls ?? this.enableTls,
      autoReconnect: autoReconnect ?? this.autoReconnect,
      maxReconnectAttempts: maxReconnectAttempts ?? this.maxReconnectAttempts,
      reconnectDelaySeconds:
          reconnectDelaySeconds ?? this.reconnectDelaySeconds,
    );
  }

  /// Create a copy with nullable fields explicitly settable to null
  ConnectionConfig copyWithNullable({
    String? host,
    int? port,
    String? databaseName,
    String? username,
    String? password,
    bool? trustServerCertificate,
    int? timeoutInSeconds,
    bool? enableTls,
    bool? autoReconnect,
    int? maxReconnectAttempts,
    int? reconnectDelaySeconds,
    bool clearUsername = false,
    bool clearPassword = false,
  }) {
    return ConnectionConfig(
      host: host ?? this.host,
      port: port ?? this.port,
      databaseName: databaseName ?? this.databaseName,
      username: clearUsername ? null : (username ?? this.username),
      password: clearPassword ? null : (password ?? this.password),
      trustServerCertificate:
          trustServerCertificate ?? this.trustServerCertificate,
      timeoutInSeconds: timeoutInSeconds ?? this.timeoutInSeconds,
      enableTls: enableTls ?? this.enableTls,
      autoReconnect: autoReconnect ?? this.autoReconnect,
      maxReconnectAttempts: maxReconnectAttempts ?? this.maxReconnectAttempts,
      reconnectDelaySeconds:
          reconnectDelaySeconds ?? this.reconnectDelaySeconds,
    );
  }

  @override
  String toString() {
    // Don't log password for security
    final authMode =
        useWindowsAuthentication ? 'windows' : 'sql (${username ?? ""})';
    return 'ConnectionConfig(host: $host, port: $port, database: $databaseName, '
        'auth: $authMode, trustCert: $trustServerCertificate, '
        'timeout: ${timeoutInSeconds}s, tls: $enableTls)';
  }
}
