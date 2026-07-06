import 'dart:convert';
import 'dart:async';
import 'dart:io';
import 'package:flutter/foundation.dart';

import 'models/connection_config.dart';
import 'models/query_result.dart';
import 'models/sql_parameter.dart';
import 'exceptions/mssql_exceptions.dart';
import 'ffi/mssql_ffi_bindings.dart';

/// Main entry point for SQL Server operations
///
/// This is a singleton class - use `MssqlConnection.getInstance()` to access it.
class MssqlConnection {
  static MssqlConnection? _instance;
  final MssqlFfiBindings _bindings;

  int? _connectionHandle;
  ConnectionConfig? _config;
  bool _isConnected = false;
  bool _isInTransaction = false;
  int _reconnectAttempts = 0;

  MssqlConnection._internal(this._bindings);

  /// Get singleton instance
  static MssqlConnection getInstance() {
    _instance ??= MssqlConnection._internal(MssqlFfiBindings());
    return _instance!;
  }

  /// For testing - inject custom bindings
  @visibleForTesting
  static MssqlConnection getInstanceWithBindings(MssqlFfiBindings bindings) {
    _instance = MssqlConnection._internal(bindings);
    return _instance!;
  }

  /// Check if currently connected
  bool get isConnected => _isConnected;

  /// Check if in an active transaction
  bool get isInTransaction => _isInTransaction;

  /// Get current connection configuration (credentials are not exposed)
  ConnectionConfig? get config => _config;

  /// Connect to SQL Server
  ///
  /// Returns `true` on success, throws [ConnectionException] on failure.
  ///
  /// Supports two authentication modes:
  /// - **SQL Server Authentication**: Provide both [username] and [password].
  /// - **Windows Integrated Authentication**: Omit both [username] and [password]
  ///   (or pass null). Uses the current Windows user credentials.
  ///
  /// Example (SQL Authentication):
  /// ```dart
  /// final request = MssqlConnection.getInstance();
  /// final success = await request.connect(
  ///   host: '192.168.1.100',
  ///   port: 1433,
  ///   databaseName: 'MyDatabase',
  ///   username: 'sa',
  ///   password: 'MyPassword123',
  ///   timeoutInSeconds: 15,
  /// );
  /// ```
  ///
  /// Example (Windows Authentication):
  /// ```dart
  /// final request = MssqlConnection.getInstance();
  /// final success = await request.connect(
  ///   host: '192.168.1.100',
  ///   port: 1433,
  ///   databaseName: 'MyDatabase',
  ///   trustServerCertificate: true,
  /// );
  /// ```
  Future<bool> connect({
    required String host,
    int port = 1433,
    required String databaseName,
    String? username,
    String? password,
    bool trustServerCertificate = false,
    int timeoutInSeconds = 15,
    bool enableTls = false,
    bool autoReconnect = false,
    int maxReconnectAttempts = 3,
    int reconnectDelaySeconds = 2,
  }) async {
    await _debugLog(
      'connect() requested host=$host port=$port database=$databaseName '
      'auth=${username == null && password == null ? 'windows' : 'sql'} '
      'username=${username ?? ''} trustServerCertificate=$trustServerCertificate '
      'timeout=${timeoutInSeconds}s enableTls=$enableTls',
    );

    if (_isConnected) {
      await _debugLog('Existing connection detected; disconnecting before reconnect');
      await disconnect();
    }

    _config = ConnectionConfig(
      host: host,
      port: port,
      databaseName: databaseName,
      username: username,
      password: password,
      trustServerCertificate: trustServerCertificate,
      timeoutInSeconds: timeoutInSeconds,
      enableTls: enableTls,
      autoReconnect: autoReconnect,
      maxReconnectAttempts: maxReconnectAttempts,
      reconnectDelaySeconds: reconnectDelaySeconds,
    );

    return _attemptConnection();
  }

  /// Internal connection attempt
  Future<bool> _attemptConnection() async {
    if (_config == null) {
      throw ConnectionException('Connection config is not set');
    }

    try {
      await _debugLog('Native connect attempt starting with ${_config.toString()}');
      final handle = _bindings.connect(
        host: _config!.host,
        port: _config!.port,
        database: _config!.databaseName,
        username: _config!.username ?? '',
        password: _config!.password ?? '',
        trustServerCertificate: _config!.trustServerCertificate,
        enableTls: _config!.enableTls,
        timeout: _config!.timeoutInSeconds,
      );
      await _debugLog('Native connect returned handle=$handle');

      if (handle <= 0) {
        final error = _getLastError(handle);
        await _debugLog('Native connect failed handle=$handle error=$error');

        // Check if this is the stub implementation error
        if (handle == -1000 || error.contains('FreeTDS not available')) {
          throw ConnectionException(
            'FreeTDS library not found. The plugin is using a stub implementation.\n\n'
            'To connect to SQL Server, you need to build FreeTDS:\n'
            '• Android: Run `cd android && ./build_freetds.sh`\n'
            '• iOS: Run `cd ios && ./build_freetds.sh` (macOS only)\n'
            '• macOS: Install via `brew install freetds`\n'
            '• Linux: Install via `sudo apt-get install freetds-dev`\n'
            '• Windows: Install via `vcpkg install freetds:x64-windows`\n\n'
            'See README.md for detailed setup instructions.',
            details: error,
            errorCode: handle,
          );
        }

        // Build detailed error message with connection info
        final authMode = _config!.useWindowsAuthentication
            ? 'Windows Authentication'
            : 'SQL Authentication (user: ${_config!.username})';
        final instanceInfo = _config!.host.contains('\\')
            ? ' (named instance: ${_config!.host})'
            : '';

        throw ConnectionException(
          'Failed to connect to SQL Server$instanceInfo using $authMode',
          details: error,
          errorCode: handle,
        );
      }

      _connectionHandle = handle;
      _isConnected = true;
      _reconnectAttempts = 0;
      await _debugLog('Connection established handle=$handle');
      return true;
    } catch (e) {
      await _debugLog('Connection attempt threw ${e.runtimeType}: $e');
      if (e is ConnectionException) rethrow;
      throw ConnectionException(
        'Connection failed',
        details: e.toString(),
      );
    }
  }

  /// Disconnect from SQL Server
  ///
  /// Closes the connection and frees native resources.
  Future<void> disconnect() async {
    if (_connectionHandle != null) {
      try {
        await _debugLog('Disconnecting handle=$_connectionHandle');
        if (_isInTransaction) {
          await rollback();
        }
        _bindings.disconnect(_connectionHandle!);
      } catch (e) {
        await _debugLog('Disconnect warning: $e');
        debugPrint('Warning: Error during disconnect: $e');
      } finally {
        _connectionHandle = null;
        _isConnected = false;
        _isInTransaction = false;
      }
    }
  }

  /// Execute a SELECT query and return results
  ///
  /// Example:
  /// ```dart
  /// final result = await request.getData('SELECT * FROM Users WHERE Age > 18');
  /// for (final row in result.rows) {
  ///   print('Name: ${row['Name']}, Age: ${row['Age']}');
  /// }
  /// ```
  Future<QueryResult> getData(String query) async {
    _ensureConnected();

    try {
      final resultPtr = _bindings.executeQuery(_connectionHandle!, query);
      final jsonString = _bindings.getStringAndFree(resultPtr);

      if (jsonString.isEmpty) {
        throw QueryException(
          'Empty result from query execution',
          query: query,
        );
      }

      return QueryResult.fromJson(jsonString);
    } catch (e) {
      await _handleConnectionError(e);
      if (e is QueryException) rethrow;
      throw QueryException(
        'Query execution failed',
        query: query,
        details: e.toString(),
      );
    }
  }

  /// Execute a parameterized SELECT query
  ///
  /// Prevents SQL injection by using sp_executesql internally.
  ///
  /// Example:
  /// ```dart
  /// final result = await request.getDataWithParams(
  ///   'SELECT * FROM Users WHERE Age > @minAge AND City = @city',
  ///   [
  ///     SqlParameter(name: 'minAge', value: 18),
  ///     SqlParameter(name: 'city', value: 'New York'),
  ///   ],
  /// );
  /// ```
  Future<QueryResult> getDataWithParams(
    String query,
    List<SqlParameter> params,
  ) async {
    _ensureConnected();

    final paramsJson = _encodeParameters(params);

    try {
      final resultPtr = _bindings.executeQueryWithParams(
        _connectionHandle!,
        query,
        paramsJson,
      );
      final jsonString = _bindings.getStringAndFree(resultPtr);

      if (jsonString.isEmpty) {
        throw QueryException(
          'Empty result from parameterized query',
          query: query,
        );
      }

      return QueryResult.fromJson(jsonString);
    } catch (e) {
      await _handleConnectionError(e);
      if (e is QueryException) rethrow;
      throw QueryException(
        'Parameterized query execution failed',
        query: query,
        details: e.toString(),
      );
    }
  }

  /// Execute a write operation (INSERT/UPDATE/DELETE)
  ///
  /// Returns the number of affected rows.
  ///
  /// Example:
  /// ```dart
  /// final affected = await request.writeData(
  ///   "INSERT INTO Users (Name, Age) VALUES ('John', 25)"
  /// );
  /// print('Inserted $affected rows');
  /// ```
  Future<int> writeData(String query) async {
    _ensureConnected();

    try {
      final affected = _bindings.executeWrite(_connectionHandle!, query);

      if (affected < 0) {
        final error = _getLastError(_connectionHandle!);
        throw QueryException(
          'Write operation failed',
          query: query,
          details: error,
          errorCode: affected,
        );
      }

      return affected;
    } catch (e) {
      await _handleConnectionError(e);
      if (e is QueryException) rethrow;
      throw QueryException(
        'Write operation failed',
        query: query,
        details: e.toString(),
      );
    }
  }

  /// Execute a parameterized write operation
  ///
  /// Prevents SQL injection for INSERT/UPDATE/DELETE operations.
  ///
  /// Example:
  /// ```dart
  /// final affected = await request.writeDataWithParams(
  ///   'UPDATE Users SET Age = @age WHERE Name = @name',
  ///   [
  ///     SqlParameter(name: 'age', value: 26),
  ///     SqlParameter(name: 'name', value: 'John'),
  ///   ],
  /// );
  /// ```
  Future<int> writeDataWithParams(
    String query,
    List<SqlParameter> params,
  ) async {
    _ensureConnected();

    final paramsJson = _encodeParameters(params);

    try {
      final affected = _bindings.executeWriteWithParams(
        _connectionHandle!,
        query,
        paramsJson,
      );

      if (affected < 0) {
        final error = _getLastError(_connectionHandle!);
        throw QueryException(
          'Parameterized write operation failed',
          query: query,
          details: error,
          errorCode: affected,
        );
      }

      return affected;
    } catch (e) {
      await _handleConnectionError(e);
      if (e is QueryException) rethrow;
      throw QueryException(
        'Parameterized write operation failed',
        query: query,
        details: e.toString(),
      );
    }
  }

  /// Begin a transaction
  ///
  /// All subsequent operations will be part of this transaction until
  /// commit() or rollback() is called.
  ///
  /// Example:
  /// ```dart
  /// await request.beginTransaction();
  /// try {
  ///   await request.writeData('INSERT INTO Users (Name) VALUES ("Alice")');
  ///   await request.writeData('INSERT INTO Orders (UserId) VALUES (1)');
  ///   await request.commit();
  /// } catch (e) {
  ///   await request.rollback();
  /// }
  /// ```
  Future<void> beginTransaction() async {
    _ensureConnected();

    if (_isInTransaction) {
      throw TransactionException('Transaction already in progress');
    }

    try {
      final result = _bindings.beginTransaction(_connectionHandle!);

      if (result < 0) {
        final error = _getLastError(_connectionHandle!);
        throw TransactionException(
          'Failed to begin transaction',
          details: error,
          errorCode: result,
        );
      }

      _isInTransaction = true;
    } catch (e) {
      if (e is TransactionException) rethrow;
      throw TransactionException(
        'Failed to begin transaction',
        details: e.toString(),
      );
    }
  }

  /// Commit the current transaction
  ///
  /// Makes all changes permanent since beginTransaction() was called.
  Future<void> commit() async {
    _ensureConnected();

    if (!_isInTransaction) {
      throw TransactionException('No transaction in progress');
    }

    try {
      final result = _bindings.commitTransaction(_connectionHandle!);

      if (result < 0) {
        final error = _getLastError(_connectionHandle!);
        throw TransactionException(
          'Failed to commit transaction',
          details: error,
          errorCode: result,
        );
      }

      _isInTransaction = false;
    } catch (e) {
      if (e is TransactionException) rethrow;
      throw TransactionException(
        'Failed to commit transaction',
        details: e.toString(),
      );
    }
  }

  /// Rollback the current transaction
  ///
  /// Reverts all changes since beginTransaction() was called.
  Future<void> rollback() async {
    _ensureConnected();

    if (!_isInTransaction) {
      throw TransactionException('No transaction in progress');
    }

    try {
      final result = _bindings.rollbackTransaction(_connectionHandle!);

      if (result < 0) {
        final error = _getLastError(_connectionHandle!);
        throw TransactionException(
          'Failed to rollback transaction',
          details: error,
          errorCode: result,
        );
      }

      _isInTransaction = false;
    } catch (e) {
      if (e is TransactionException) rethrow;
      throw TransactionException(
        'Failed to rollback transaction',
        details: e.toString(),
      );
    }
  }

  /// Bulk insert rows into a table
  ///
  /// Uses FreeTDS BCP or batched prepared statements for efficient inserts.
  ///
  /// Example:
  /// ```dart
  /// final rows = [
  ///   {'Name': 'Alice', 'Age': 25},
  ///   {'Name': 'Bob', 'Age': 30},
  ///   {'Name': 'Charlie', 'Age': 35},
  /// ];
  /// final inserted = await request.bulkInsert('Users', rows, batchSize: 1000);
  /// print('Inserted $inserted rows');
  /// ```
  Future<int> bulkInsert(
    String tableName,
    List<Map<String, dynamic>> rows, {
    int batchSize = 1000,
  }) async {
    _ensureConnected();

    if (rows.isEmpty) {
      return 0;
    }

    final dataJson = jsonEncode(rows);

    try {
      final inserted = _bindings.bulkInsert(
        _connectionHandle!,
        tableName,
        dataJson,
        batchSize,
      );

      if (inserted < 0) {
        final error = _getLastError(_connectionHandle!);
        throw QueryException(
          'Bulk insert failed',
          details: error,
          errorCode: inserted,
        );
      }

      return inserted;
    } catch (e) {
      await _handleConnectionError(e);
      if (e is QueryException) rethrow;
      throw QueryException(
        'Bulk insert failed',
        details: e.toString(),
      );
    }
  }

  /// Ensure connection is active
  void _ensureConnected() {
    if (!_isConnected || _connectionHandle == null) {
      throw ConnectionException('Not connected to SQL Server');
    }
  }

  /// Get last error message from native layer
  String _getLastError(int handle) {
    try {
      final errorPtr = _bindings.getLastError(handle);
      return _bindings.getStringAndFree(errorPtr);
    } catch (e) {
      return 'Unknown error';
    }
  }

  static String get debugLogPath {
    final explicitPath = Platform.environment['MSSQL_IO_LOG_FILE'];
    if (explicitPath != null && explicitPath.isNotEmpty) {
      return explicitPath;
    }
    return '${Directory.systemTemp.path}${Platform.pathSeparator}mssql_io.log';
  }

  static Future<void> _debugLog(String message) async {
    try {
      final file = File(debugLogPath);
      await file.writeAsString(
        '[${DateTime.now().toIso8601String()}] [dart] $message\n',
        mode: FileMode.append,
        flush: true,
      );
    } catch (_) {
      // Logging must never change database behavior.
    }
  }

  /// Handle connection errors and attempt reconnection if configured
  Future<void> _handleConnectionError(dynamic error) async {
    if (_config?.autoReconnect != true) {
      return;
    }

    // Check if error indicates connection loss
    final errorStr = error.toString().toLowerCase();
    if (errorStr.contains('connection') ||
        errorStr.contains('disconnect') ||
        errorStr.contains('timeout')) {
      _isConnected = false;

      if (_reconnectAttempts < _config!.maxReconnectAttempts) {
        _reconnectAttempts++;
        debugPrint(
          'Connection lost. Attempting reconnection '
          '($_reconnectAttempts/${_config!.maxReconnectAttempts})...',
        );

        await Future.delayed(
          Duration(seconds: _config!.reconnectDelaySeconds),
        );

        try {
          await _attemptConnection();
          debugPrint('Reconnection successful');
        } catch (e) {
          debugPrint('Reconnection failed: $e');
        }
      }
    }
  }

  /// Encode parameters to JSON format for native layer
  String _encodeParameters(List<SqlParameter> params) {
    final List<Map<String, dynamic>> paramList = params.map((param) {
      return {
        'name': param.name.startsWith('@') ? param.name : '@${param.name}',
        'value': param.value,
        'type': param.type?.sqlTypeName ?? _inferType(param.value),
      };
    }).toList();

    return jsonEncode(paramList);
  }

  /// Infer SQL type from Dart value
  String _inferType(dynamic value) {
    if (value == null) return 'NULL';
    if (value is int) return 'INT';
    if (value is double) return 'FLOAT';
    if (value is bool) return 'BIT';
    if (value is String) return 'NVARCHAR';
    if (value is DateTime) return 'DATETIME2';
    return 'NVARCHAR';
  }
}
