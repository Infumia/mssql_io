import 'dart:ffi' as ffi;
import 'dart:io' if (dart.library.html) 'dart:html';
import 'package:ffi/ffi.dart';

/// FFI bindings for the native MSSQL library
class MssqlFfiBindings {
  late final ffi.DynamicLibrary _library;
  late final _MssqlNativeFunctions _functions;

  MssqlFfiBindings() {
    _library = _loadLibrary();
    _functions = _MssqlNativeFunctions(_library);
  }

  /// Load the native library based on platform
  ffi.DynamicLibrary _loadLibrary() {
    const String libName = 'mssql_io';

    // Check if we're on web (FFI not supported)
    if (identical(0, 0.0)) {
      // This is a compile-time check for web
      throw UnsupportedError(
        'FFI is not supported on web platform. '
        'Use MssqlIoWeb.configure() instead for web applications.',
      );
    }

    if (Platform.isAndroid) {
      return ffi.DynamicLibrary.open('lib$libName.so');
    } else if (Platform.isIOS || Platform.isMacOS) {
      return ffi.DynamicLibrary.process();
    } else if (Platform.isLinux) {
      return ffi.DynamicLibrary.open('lib$libName.so');
    } else if (Platform.isWindows) {
      return ffi.DynamicLibrary.open('$libName.dll');
    }

    throw UnsupportedError(
        'Platform ${Platform.operatingSystem} is not supported');
  }

  /// Connect to SQL Server
  /// Returns connection handle (>0 on success, <=0 on error)
  int connect({
    required String host,
    required int port,
    required String database,
    required String username,
    required String password,
    required bool trustServerCertificate,
    required int timeout,
  }) {
    final hostPtr = host.toNativeUtf8();
    final databasePtr = database.toNativeUtf8();
    final usernamePtr = username.toNativeUtf8();
    final passwordPtr = password.toNativeUtf8();

    try {
      return _functions.connect(
        hostPtr.cast(),
        port,
        databasePtr.cast(),
        usernamePtr.cast(),
        passwordPtr.cast(),
        trustServerCertificate ? 1 : 0,
        timeout,
      );
    } finally {
      malloc.free(hostPtr);
      malloc.free(databasePtr);
      malloc.free(usernamePtr);
      malloc.free(passwordPtr);
    }
  }

  /// Disconnect from SQL Server
  /// Returns 0 on success, negative on error
  int disconnect(int connectionHandle) {
    return _functions.disconnect(connectionHandle);
  }

  /// Execute a query and get results as JSON string
  /// Returns JSON string pointer (must be freed with freeResultString)
  ffi.Pointer<Utf8> executeQuery(int connectionHandle, String query) {
    final queryPtr = query.toNativeUtf8();
    try {
      final resultPtr = _functions.executeQuery(
        connectionHandle,
        queryPtr.cast(),
      );
      return resultPtr.cast<Utf8>();
    } finally {
      malloc.free(queryPtr);
    }
  }

  /// Execute a parameterized query with JSON-encoded parameters
  /// Returns JSON string pointer (must be freed with freeResultString)
  ffi.Pointer<Utf8> executeQueryWithParams(
    int connectionHandle,
    String query,
    String paramsJson,
  ) {
    final queryPtr = query.toNativeUtf8();
    final paramsPtr = paramsJson.toNativeUtf8();
    try {
      final resultPtr = _functions.executeQueryWithParams(
        connectionHandle,
        queryPtr.cast(),
        paramsPtr.cast(),
      );
      return resultPtr.cast<Utf8>();
    } finally {
      malloc.free(queryPtr);
      malloc.free(paramsPtr);
    }
  }

  /// Execute a write operation (INSERT/UPDATE/DELETE)
  /// Returns affected row count (negative on error)
  int executeWrite(int connectionHandle, String query) {
    final queryPtr = query.toNativeUtf8();
    try {
      return _functions.executeWrite(
        connectionHandle,
        queryPtr.cast(),
      );
    } finally {
      malloc.free(queryPtr);
    }
  }

  /// Execute a parameterized write operation
  /// Returns affected row count (negative on error)
  int executeWriteWithParams(
    int connectionHandle,
    String query,
    String paramsJson,
  ) {
    final queryPtr = query.toNativeUtf8();
    final paramsPtr = paramsJson.toNativeUtf8();
    try {
      return _functions.executeWriteWithParams(
        connectionHandle,
        queryPtr.cast(),
        paramsPtr.cast(),
      );
    } finally {
      malloc.free(queryPtr);
      malloc.free(paramsPtr);
    }
  }

  /// Begin a transaction
  /// Returns 0 on success, negative on error
  int beginTransaction(int connectionHandle) {
    return _functions.beginTransaction(connectionHandle);
  }

  /// Commit a transaction
  /// Returns 0 on success, negative on error
  int commitTransaction(int connectionHandle) {
    return _functions.commitTransaction(connectionHandle);
  }

  /// Rollback a transaction
  /// Returns 0 on success, negative on error
  int rollbackTransaction(int connectionHandle) {
    return _functions.rollbackTransaction(connectionHandle);
  }

  /// Bulk insert data into a table
  /// Returns number of rows inserted (negative on error)
  int bulkInsert(
    int connectionHandle,
    String tableName,
    String dataJson,
    int batchSize,
  ) {
    final tablePtr = tableName.toNativeUtf8();
    final dataPtr = dataJson.toNativeUtf8();
    try {
      return _functions.bulkInsert(
        connectionHandle,
        tablePtr.cast(),
        dataPtr.cast(),
        batchSize,
      );
    } finally {
      malloc.free(tablePtr);
      malloc.free(dataPtr);
    }
  }

  /// Get last error message
  /// Returns error message pointer (must be freed with freeResultString)
  ffi.Pointer<Utf8> getLastError(int connectionHandle) {
    final errorPtr = _functions.getLastError(connectionHandle);
    return errorPtr.cast<Utf8>();
  }

  /// Free a result string returned by native functions
  void freeResultString(ffi.Pointer<Utf8> stringPtr) {
    _functions.freeResultString(stringPtr.cast());
  }

  /// Convert native UTF-8 pointer to Dart string and free the pointer
  String getStringAndFree(ffi.Pointer<Utf8> stringPtr) {
    if (stringPtr.address == 0) {
      return '';
    }
    try {
      return stringPtr.toDartString();
    } finally {
      freeResultString(stringPtr);
    }
  }
}

/// Native function signatures
class _MssqlNativeFunctions {
  final ffi.DynamicLibrary _lib;

  _MssqlNativeFunctions(this._lib);

  late final connect = _lib.lookupFunction<
      ffi.Int64 Function(
        ffi.Pointer<Utf8>,
        ffi.Int32,
        ffi.Pointer<Utf8>,
        ffi.Pointer<Utf8>,
        ffi.Pointer<Utf8>,
        ffi.Int32,
        ffi.Int32,
      ),
      int Function(
        ffi.Pointer<Utf8>,
        int,
        ffi.Pointer<Utf8>,
        ffi.Pointer<Utf8>,
        ffi.Pointer<Utf8>,
        int,
        int,
      )>('mssql_connect');

  late final disconnect =
      _lib.lookupFunction<ffi.Int32 Function(ffi.Int64), int Function(int)>(
          'mssql_disconnect');

  late final executeQuery = _lib.lookupFunction<
      ffi.Pointer<Utf8> Function(ffi.Int64, ffi.Pointer<Utf8>),
      ffi.Pointer<Utf8> Function(
          int, ffi.Pointer<Utf8>)>('mssql_execute_query');

  late final executeQueryWithParams = _lib.lookupFunction<
      ffi.Pointer<Utf8> Function(
          ffi.Int64, ffi.Pointer<Utf8>, ffi.Pointer<Utf8>),
      ffi.Pointer<Utf8> Function(int, ffi.Pointer<Utf8>,
          ffi.Pointer<Utf8>)>('mssql_execute_query_with_params');

  late final executeWrite = _lib.lookupFunction<
      ffi.Int32 Function(ffi.Int64, ffi.Pointer<Utf8>),
      int Function(int, ffi.Pointer<Utf8>)>('mssql_execute_write');

  late final executeWriteWithParams = _lib.lookupFunction<
      ffi.Int32 Function(ffi.Int64, ffi.Pointer<Utf8>, ffi.Pointer<Utf8>),
      int Function(int, ffi.Pointer<Utf8>,
          ffi.Pointer<Utf8>)>('mssql_execute_write_with_params');

  late final beginTransaction =
      _lib.lookupFunction<ffi.Int32 Function(ffi.Int64), int Function(int)>(
          'mssql_begin_transaction');

  late final commitTransaction =
      _lib.lookupFunction<ffi.Int32 Function(ffi.Int64), int Function(int)>(
          'mssql_commit_transaction');

  late final rollbackTransaction =
      _lib.lookupFunction<ffi.Int32 Function(ffi.Int64), int Function(int)>(
          'mssql_rollback_transaction');

  late final bulkInsert = _lib.lookupFunction<
      ffi.Int32 Function(
          ffi.Int64, ffi.Pointer<Utf8>, ffi.Pointer<Utf8>, ffi.Int32),
      int Function(
          int, ffi.Pointer<Utf8>, ffi.Pointer<Utf8>, int)>('mssql_bulk_insert');

  late final getLastError = _lib.lookupFunction<
      ffi.Pointer<Utf8> Function(ffi.Int64),
      ffi.Pointer<Utf8> Function(int)>('mssql_get_last_error');

  late final freeResultString = _lib.lookupFunction<
      ffi.Void Function(ffi.Pointer<Utf8>),
      void Function(ffi.Pointer<Utf8>)>('mssql_free_string');
}
