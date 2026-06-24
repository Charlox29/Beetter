import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../services/api_service.dart';
import '../services/storage_service.dart';

enum AuthStatus { unknown, authenticated, unauthenticated }

class AuthState {
  final AuthStatus status;
  final String? username;
  final String? error;

  const AuthState({
    this.status = AuthStatus.unknown,
    this.username,
    this.error,
  });

  AuthState copyWith({AuthStatus? status, String? username, String? error}) =>
      AuthState(
        status: status ?? this.status,
        username: username ?? this.username,
        error: error,
      );
}

class AuthNotifier extends StateNotifier<AuthState> {
  final ApiService _api = ApiService();
  final StorageService _storage = StorageService();

  AuthNotifier() : super(const AuthState()) {
    _checkSession();
  }

  Future<void> _checkSession() async {
    final loggedIn = await _storage.isLoggedIn();
    final username = await _storage.getUsername();
    state = AuthState(
      status: loggedIn ? AuthStatus.authenticated : AuthStatus.unauthenticated,
      username: username,
    );
  }

  Future<void> login(String serverUrl, String username, String password) async {
    state = state.copyWith(status: AuthStatus.unknown, error: null);
    try {
      final data = await _api.login(serverUrl, username, password);
      await _storage.saveSession(
        serverUrl,
        data['token'] as String,
        data['username'] as String,
      );
      state = AuthState(
        status: AuthStatus.authenticated,
        username: data['username'] as String,
      );
    } on ApiException catch (e) {
      state = AuthState(
        status: AuthStatus.unauthenticated,
        error: e.message,
      );
    }
  }

  Future<void> logout() async {
    await _api.logout();
    await _storage.clearSession();
    state = const AuthState(status: AuthStatus.unauthenticated);
  }
}

final authProvider = StateNotifierProvider<AuthNotifier, AuthState>(
  (_) => AuthNotifier(),
);
