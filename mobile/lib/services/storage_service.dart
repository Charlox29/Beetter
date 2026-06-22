import 'package:flutter_secure_storage/flutter_secure_storage.dart';

class StorageService {
  static const _storage = FlutterSecureStorage();

  static const _keyServerUrl = 'server_url';
  static const _keyToken = 'auth_token';
  static const _keyUsername = 'username';
  static const _keyNotificationsOn = 'notifications_on';
  static const _keyTempMax = 'threshold_temp_max';
  static const _keyTempMin = 'threshold_temp_min';
  static const _keyHumMax = 'threshold_hum_max';
  static const _keyHumMin = 'threshold_hum_min';

  Future<void> saveSession(String serverUrl, String token, String username) async {
    await _storage.write(key: _keyServerUrl, value: serverUrl);
    await _storage.write(key: _keyToken, value: token);
    await _storage.write(key: _keyUsername, value: username);
  }

  Future<void> clearSession() async {
    await _storage.delete(key: _keyServerUrl);
    await _storage.delete(key: _keyToken);
    await _storage.delete(key: _keyUsername);
  }

  Future<String?> getServerUrl() => _storage.read(key: _keyServerUrl);
  Future<String?> getToken() => _storage.read(key: _keyToken);
  Future<String?> getUsername() => _storage.read(key: _keyUsername);

  Future<bool> isLoggedIn() async {
    final token = await _storage.read(key: _keyToken);
    return token != null && token.isNotEmpty;
  }

  Future<void> saveNotificationSettings({
    required bool enabled,
    required double tempMax,
    required double tempMin,
    required double humMax,
    required double humMin,
  }) async {
    await _storage.write(key: _keyNotificationsOn, value: enabled.toString());
    await _storage.write(key: _keyTempMax, value: tempMax.toString());
    await _storage.write(key: _keyTempMin, value: tempMin.toString());
    await _storage.write(key: _keyHumMax, value: humMax.toString());
    await _storage.write(key: _keyHumMin, value: humMin.toString());
  }

  Future<Map<String, dynamic>> getNotificationSettings() async {
    return {
      'enabled': (await _storage.read(key: _keyNotificationsOn)) == 'true',
      'temp_max': double.tryParse(await _storage.read(key: _keyTempMax) ?? '') ?? 38.0,
      'temp_min': double.tryParse(await _storage.read(key: _keyTempMin) ?? '') ?? 28.0,
      'hum_max': double.tryParse(await _storage.read(key: _keyHumMax) ?? '') ?? 85.0,
      'hum_min': double.tryParse(await _storage.read(key: _keyHumMin) ?? '') ?? 40.0,
    };
  }
}
