import 'package:flutter_test/flutter_test.dart';
import 'package:mssql_io/mssql_io.dart';

void main() {
  group('ConnectionConfig', () {
    test('creates with required parameters', () {
      final config = ConnectionConfig(
        host: '192.168.1.100',
        databaseName: 'TestDB',
        username: 'sa',
        password: 'Password123',
      );

      expect(config.host, '192.168.1.100');
      expect(config.port, 1433);
      expect(config.databaseName, 'TestDB');
      expect(config.username, 'sa');
      expect(config.password, 'Password123');
      expect(config.trustServerCertificate, false);
      expect(config.timeoutInSeconds, 15);
      expect(config.enableTls, true);
      expect(config.autoReconnect, false);
      expect(config.useWindowsAuthentication, false);
    });

    test('creates with custom parameters', () {
      final config = ConnectionConfig(
        host: 'localhost',
        port: 5000,
        databaseName: 'MyDB',
        username: 'user',
        password: 'pass',
        trustServerCertificate: true,
        timeoutInSeconds: 30,
        enableTls: false,
        autoReconnect: true,
        maxReconnectAttempts: 5,
        reconnectDelaySeconds: 3,
      );

      expect(config.port, 5000);
      expect(config.trustServerCertificate, true);
      expect(config.timeoutInSeconds, 30);
      expect(config.enableTls, false);
      expect(config.autoReconnect, true);
      expect(config.maxReconnectAttempts, 5);
      expect(config.reconnectDelaySeconds, 3);
    });

    test('creates with Windows authentication (no username/password)', () {
      final config = ConnectionConfig(
        host: '192.168.1.100',
        databaseName: 'TestDB',
        trustServerCertificate: true,
      );

      expect(config.username, isNull);
      expect(config.password, isNull);
      expect(config.useWindowsAuthentication, true);
      expect(config.trustServerCertificate, true);
    });

    test('useWindowsAuthentication is false when username is provided', () {
      final config = ConnectionConfig(
        host: 'server',
        databaseName: 'DB',
        username: 'sa',
        password: null,
      );

      expect(config.useWindowsAuthentication, false);
    });

    test('useWindowsAuthentication is false when password is provided', () {
      final config = ConnectionConfig(
        host: 'server',
        databaseName: 'DB',
        username: null,
        password: 'pass',
      );

      expect(config.useWindowsAuthentication, false);
    });

    test('copyWith creates modified copy', () {
      final original = ConnectionConfig(
        host: 'server1',
        databaseName: 'DB1',
        username: 'user1',
        password: 'pass1',
      );

      final modified = original.copyWith(
        host: 'server2',
        port: 5000,
      );

      expect(modified.host, 'server2');
      expect(modified.port, 5000);
      expect(modified.databaseName, 'DB1');
      expect(modified.username, 'user1');
    });

    test('copyWith can switch to Windows auth', () {
      final original = ConnectionConfig(
        host: 'server',
        databaseName: 'DB',
        username: 'sa',
        password: 'pass',
      );

      final modified = original.copyWithNullable(
        clearUsername: true,
        clearPassword: true,
        trustServerCertificate: true,
      );

      expect(modified.useWindowsAuthentication, true);
      expect(modified.trustServerCertificate, true);
    });

    test('toString does not expose password', () {
      final config = ConnectionConfig(
        host: 'server',
        databaseName: 'DB',
        username: 'user',
        password: 'SuperSecret123',
      );

      final stringRep = config.toString();
      expect(stringRep, isNot(contains('SuperSecret123')));
      expect(stringRep, contains('server'));
      expect(stringRep, contains('DB'));
      expect(stringRep, contains('sql (user)'));
    });

    test('toString shows windows auth mode', () {
      final config = ConnectionConfig(
        host: 'server',
        databaseName: 'DB',
        trustServerCertificate: true,
      );

      final stringRep = config.toString();
      expect(stringRep, contains('windows'));
      expect(stringRep, contains('trustCert: true'));
    });
  });
}
