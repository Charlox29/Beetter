import 'package:dio/dio.dart';
import 'storage_service.dart';
import '../models/beehive.dart';

class ApiException implements Exception {
  final String message;
  ApiException(this.message);
  @override
  String toString() => message;
}

class ApiService {
  final StorageService _storage = StorageService();
  late final Dio _dio;

  ApiService() {
    _dio = Dio(BaseOptions(
      connectTimeout: const Duration(seconds: 10),
      receiveTimeout: const Duration(seconds: 15),
    ));

    _dio.interceptors.add(InterceptorsWrapper(
      onRequest: (options, handler) async {
        final token = await _storage.getToken();
        if (token != null) {
          options.headers['Authorization'] = 'Bearer $token';
        }
        handler.next(options);
      },
    ));
  }

  Future<String> _baseUrl() async {
    final url = await _storage.getServerUrl();
    if (url == null || url.isEmpty) throw ApiException('Server URL not configured');
    return url.endsWith('/') ? url.substring(0, url.length - 1) : url;
  }

  Future<Map<String, dynamic>> login(
      String serverUrl, String username, String password) async {
    final base = serverUrl.endsWith('/')
        ? serverUrl.substring(0, serverUrl.length - 1)
        : serverUrl;
    try {
      final resp = await _dio.post(
        '$base/api/auth/login',
        data: {'username': username, 'password': password},
        options: Options(headers: {'Authorization': null}),
      );
      return resp.data as Map<String, dynamic>;
    } on DioException catch (e) {
      if (e.response?.statusCode == 401) {
        throw ApiException('Invalid username or password');
      }
      throw ApiException('Connection failed: ${e.message}');
    }
  }

  Future<void> logout() async {
    try {
      final base = await _baseUrl();
      await _dio.post('$base/api/auth/logout');
    } catch (_) {
      // Best-effort — client clears session regardless.
    }
  }

  Future<List<Beehive>> getBeehives() async {
    final base = await _baseUrl();
    try {
      final resp = await _dio.get('$base/api/beehives');
      final data = resp.data as Map<String, dynamic>;
      return (data['beehives'] as List)
          .map((e) => Beehive.fromJson(e as Map<String, dynamic>))
          .toList();
    } on DioException catch (e) {
      if (e.response?.statusCode == 401) {
        throw ApiException('Session expired — please log in again');
      }
      throw ApiException('Failed to load beehives: ${e.message}');
    }
  }

  Future<HiveChartData> getBeehiveData(String beehiveId, String range) async {
    final base = await _baseUrl();
    try {
      final resp = await _dio.get(
        '$base/api/beehives/$beehiveId/data',
        queryParameters: {'range': range},
      );
      return HiveChartData.fromJson(resp.data as Map<String, dynamic>);
    } on DioException catch (e) {
      if (e.response?.statusCode == 401) {
        throw ApiException('Session expired — please log in again');
      }
      throw ApiException('Failed to load chart data: ${e.message}');
    }
  }
}
