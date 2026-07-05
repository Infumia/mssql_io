import 'dart:ffi' as ffi;
import 'package:flutter_test/flutter_test.dart';
import 'package:mssql_io/src/ffi/mssql_ffi_bindings.dart';
import 'package:mssql_io/mssql_io.dart';
import 'package:ffi/ffi.dart';

// Mock FFI bindings for testing
class MockMssqlFfiBindings extends MssqlFfiBindings {
  int nextConnectionHandle = 1;
  Map<int, bool> connections = {};
  Map<int, bool> transactions = {};
  String? lastQuery;
  String? lastParamsJson;

  @override
  int connect({
    required String host,
    required int port,
    required String database,
    required String username,
    required String password,
    required bool trustServerCertificate,
    required int timeout,
  }) {
    final handle = nextConnectionHandle++;
    connections[handle] = true;
    return handle;
  }

  @override
  int disconnect(int connectionHandle) {
    if (!connections.containsKey(connectionHandle)) {
      return -1;
    }
    connections.remove(connectionHandle);
    return 0;
  }

  @override
  ffi.Pointer<Utf8> executeQuery(int connectionHandle, String query) {
    lastQuery = query;
    if (!connections.containsKey(connectionHandle)) {
      return _allocString('{"columns":[],"rows":[],"affected":0}');
    }

    // Mock result
    const mockResult = '''
    {
      "columns": ["id", "name", "age"],
      "rows": [
        {"id": 1, "name": "Alice", "age": 25},
        {"id": 2, "name": "Bob", "age": 30}
      ],
      "affected": 0
    }
    ''';
    return _allocString(mockResult);
  }

  @override
  ffi.Pointer<Utf8> executeQueryWithParams(
    int connectionHandle,
    String query,
    String paramsJson,
  ) {
    lastQuery = query;
    lastParamsJson = paramsJson;

    const mockResult = '''
    {
      "columns": ["id", "name"],
      "rows": [
        {"id": 1, "name": "Test User"}
      ],
      "affected": 0
    }
    ''';
    return _allocString(mockResult);
  }

  @override
  int executeWrite(int connectionHandle, String query) {
    lastQuery = query;
    if (!connections.containsKey(connectionHandle)) {
      return -1;
    }
    return 3; // Mock affected rows
  }

  @override
  int executeWriteWithParams(
    int connectionHandle,
    String query,
    String paramsJson,
  ) {
    lastQuery = query;
    lastParamsJson = paramsJson;
    return 2; // Mock affected rows
  }

  @override
  int beginTransaction(int connectionHandle) {
    if (!connections.containsKey(connectionHandle)) {
      return -1;
    }
    transactions[connectionHandle] = true;
    return 0;
  }

  @override
  int commitTransaction(int connectionHandle) {
    if (!transactions.containsKey(connectionHandle)) {
      return -1;
    }
    transactions.remove(connectionHandle);
    return 0;
  }

  @override
  int rollbackTransaction(int connectionHandle) {
    if (!transactions.containsKey(connectionHandle)) {
      return -1;
    }
    transactions.remove(connectionHandle);
    return 0;
  }

  @override
  int bulkInsert(
    int connectionHandle,
    String tableName,
    String dataJson,
    int batchSize,
  ) {
    if (!connections.containsKey(connectionHandle)) {
      return -1;
    }
    return 100; // Mock inserted rows
  }

  @override
  ffi.Pointer<Utf8> getLastError(int connectionHandle) {
    return _allocString('Mock error message');
  }

  @override
  void freeResultString(ffi.Pointer<Utf8> stringPtr) {
    if (stringPtr.address != 0) {
      malloc.free(stringPtr);
    }
  }

  @override
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

  ffi.Pointer<Utf8> _allocString(String str) {
    return str.toNativeUtf8();
  }
}

void main() {
  group('MssqlConnection', () {
    late MockMssqlFfiBindings mockBindings;
    late MssqlConnection connection;

    setUp(() {
      mockBindings = MockMssqlFfiBindings();
      connection = MssqlConnection.getInstanceWithBindings(mockBindings);
    });

    group('Connection Management', () {
      test('connects successfully with valid credentials', () async {
        final result = await connection.connect(
          host: '192.168.1.100',
          port: 1433,
          databaseName: 'TestDB',
          username: 'sa',
          password: 'Password123',
        );

        expect(result, true);
        expect(connection.isConnected, true);
      });

      test('connects successfully with Windows authentication', () async {
        final result = await connection.connect(
          host: '192.168.1.100',
          port: 1433,
          databaseName: 'TestDB',
          trustServerCertificate: true,
        );

        expect(result, true);
        expect(connection.isConnected, true);
        expect(connection.config?.useWindowsAuthentication, true);
        expect(connection.config?.trustServerCertificate, true);
      });

      test('disconnect closes connection', () async {
        await connection.connect(
          host: 'localhost',
          databaseName: 'TestDB',
          username: 'sa',
          password: 'pass',
        );

        await connection.disconnect();

        expect(connection.isConnected, false);
        expect(mockBindings.connections, isEmpty);
      });

      test('throws ConnectionException when not connected', () async {
        expect(
          () => connection.getData('SELECT * FROM Users'),
          throwsA(isA<ConnectionException>()),
        );
      });
    });

    group('Query Operations', () {
      setUp(() async {
        await connection.connect(
          host: 'localhost',
          databaseName: 'TestDB',
          username: 'sa',
          password: 'pass',
        );
      });

      test('getData executes SELECT query', () async {
        final result = await connection.getData('SELECT * FROM Users');

        expect(result.columns, isNotEmpty);
        expect(result.rows.length, 2);
        expect(result.rows[0]['name'], 'Alice');
        expect(mockBindings.lastQuery, 'SELECT * FROM Users');
      });

      test('getDataWithParams executes parameterized query', () async {
        final result = await connection.getDataWithParams(
          'SELECT * FROM Users WHERE age > @minAge',
          [SqlParameter(name: 'minAge', value: 18)],
        );

        expect(result.rows, isNotEmpty);
        expect(mockBindings.lastQuery, contains('Users'));
        expect(mockBindings.lastParamsJson, contains('minAge'));
      });

      test('writeData executes INSERT/UPDATE/DELETE', () async {
        final affected = await connection.writeData(
          'INSERT INTO Users (name) VALUES ("Charlie")',
        );

        expect(affected, 3);
        expect(mockBindings.lastQuery, contains('INSERT'));
      });

      test('writeDataWithParams executes parameterized write', () async {
        final affected = await connection.writeDataWithParams(
          'UPDATE Users SET age = @age WHERE id = @id',
          [
            SqlParameter(name: 'age', value: 26),
            SqlParameter(name: 'id', value: 1),
          ],
        );

        expect(affected, 2);
        expect(mockBindings.lastParamsJson, contains('age'));
      });
    });

    group('Transaction Operations', () {
      setUp(() async {
        await connection.connect(
          host: 'localhost',
          databaseName: 'TestDB',
          username: 'sa',
          password: 'pass',
        );
      });

      test('beginTransaction starts transaction', () async {
        await connection.beginTransaction();

        expect(connection.isInTransaction, true);
        expect(mockBindings.transactions, isNotEmpty);
      });

      test('commit commits transaction', () async {
        await connection.beginTransaction();
        await connection.commit();

        expect(connection.isInTransaction, false);
      });

      test('rollback rolls back transaction', () async {
        await connection.beginTransaction();
        await connection.rollback();

        expect(connection.isInTransaction, false);
      });

      test('throws when beginning transaction twice', () async {
        await connection.beginTransaction();

        expect(
          () => connection.beginTransaction(),
          throwsA(isA<TransactionException>()),
        );
      });

      test('throws when committing without transaction', () async {
        expect(
          () => connection.commit(),
          throwsA(isA<TransactionException>()),
        );
      });

      test('disconnect rolls back active transaction', () async {
        await connection.beginTransaction();
        await connection.disconnect();

        expect(connection.isInTransaction, false);
      });
    });

    group('Bulk Insert', () {
      setUp(() async {
        await connection.connect(
          host: 'localhost',
          databaseName: 'TestDB',
          username: 'sa',
          password: 'pass',
        );
      });

      test('bulkInsert inserts multiple rows', () async {
        final rows = [
          {'name': 'User1', 'age': 20},
          {'name': 'User2', 'age': 21},
          {'name': 'User3', 'age': 22},
        ];

        final inserted = await connection.bulkInsert(
          'Users',
          rows,
          batchSize: 1000,
        );

        expect(inserted, 100);
      });

      test('bulkInsert returns 0 for empty list', () async {
        final inserted = await connection.bulkInsert('Users', []);
        expect(inserted, 0);
      });
    });

    group('Connection Config', () {
      test('config is accessible after connect', () async {
        await connection.connect(
          host: '192.168.1.100',
          port: 5000,
          databaseName: 'MyDB',
          username: 'user',
          password: 'pass',
        );

        final config = connection.config;
        expect(config, isNotNull);
        expect(config!.host, '192.168.1.100');
        expect(config.port, 5000);
        expect(config.databaseName, 'MyDB');
      });

      test('config is null before connect', () {
        expect(connection.config, isNull);
      });
    });
  });
}
