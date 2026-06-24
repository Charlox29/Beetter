import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';
import 'package:flutter_local_notifications/flutter_local_notifications.dart';
import 'package:workmanager/workmanager.dart';

import 'theme/app_theme.dart';
import 'providers/auth_provider.dart';
import 'screens/login_screen.dart';
import 'screens/dashboard_screen.dart';
import 'screens/hive_detail_screen.dart';
import 'screens/settings_screen.dart';
import 'services/storage_service.dart';
import 'services/api_service.dart';
import 'screens/alerts_screen.dart';

// ÔöÇÔöÇ Background alert task ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

const _alertTaskName = 'beetter_alert_check';
const _notifChannelId = 'beetter_alerts';
const _notifChannelName = 'Beetter Alerts';

final _notificationsPlugin = FlutterLocalNotificationsPlugin();

@pragma('vm:entry-point')
void callbackDispatcher() {
  Workmanager().executeTask((task, inputData) async {
    if (task != _alertTaskName) return Future.value(true);

    final storage = StorageService();
    final settings = await storage.getNotificationSettings();
    if (!(settings['enabled'] as bool)) return Future.value(true);

    final api = ApiService();
    try {
      final hives = await api.getBeehives();
      final tempMax = settings['temp_max'] as double;
      final tempMin = settings['temp_min'] as double;
      final humMax = settings['hum_max'] as double;
      final humMin = settings['hum_min'] as double;

      int notifId = 0;
      for (final hive in hives) {
        final l = hive.latest;
        final tInt = l?.temperatureInt?.value;
        final hInt = l?.humidityInt?.value;

        if (tInt != null) {
          String? msg;
          if (tInt > tempMax) msg = 'High interior temperature: ${tInt.toStringAsFixed(1)}┬░C';
          if (tInt < tempMin) msg = 'Low interior temperature: ${tInt.toStringAsFixed(1)}┬░C';
          if (msg != null) {
            await _notificationsPlugin.show(
              notifId++,
              'Beehive ${hive.id} alert',
              msg,
              _notifDetails(),
            );
          }
        }

        if (hInt != null) {
          String? msg;
          if (hInt > humMax) msg = 'High interior humidity: ${hInt.toStringAsFixed(1)}%';
          if (hInt < humMin) msg = 'Low interior humidity: ${hInt.toStringAsFixed(1)}%';
          if (msg != null) {
            await _notificationsPlugin.show(
              notifId++,
              'Beehive ${hive.id} alert',
              msg,
              _notifDetails(),
            );
          }
        }
      }
    } catch (_) {
      // Silently fail ÔÇö no connectivity or session expired.
    }
    return Future.value(true);
  });
}

NotificationDetails _notifDetails() => const NotificationDetails(
      android: AndroidNotificationDetails(
        _notifChannelId,
        _notifChannelName,
        importance: Importance.high,
        priority: Priority.high,
        icon: '@mipmap/ic_launcher',
      ),
      iOS: DarwinNotificationDetails(),
    );

// ÔöÇÔöÇ App entry point ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // Local notifications
  await _notificationsPlugin.initialize(
    const InitializationSettings(
      android: AndroidInitializationSettings('@mipmap/ic_launcher'),
      iOS: DarwinInitializationSettings(),
    ),
  );
  await _notificationsPlugin
      .resolvePlatformSpecificImplementation<
          AndroidFlutterLocalNotificationsPlugin>()
      ?.createNotificationChannel(const AndroidNotificationChannel(
        _notifChannelId,
        _notifChannelName,
        importance: Importance.high,
      ));

  // Background worker
  await Workmanager().initialize(callbackDispatcher, isInDebugMode: false);
  await Workmanager().registerPeriodicTask(
    _alertTaskName,
    _alertTaskName,
    frequency: const Duration(minutes: 15),
    constraints: Constraints(networkType: NetworkType.connected),
    existingWorkPolicy: ExistingPeriodicWorkPolicy.keep,
  );

  runApp(const ProviderScope(child: BeetterApp()));
}

// ÔöÇÔöÇ Router ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

GoRouter _buildRouter(AuthState authState) => GoRouter(
      initialLocation: '/',
      redirect: (context, state) {
        if (authState.status == AuthStatus.unknown) return null;
        final loggedIn = authState.status == AuthStatus.authenticated;
        final onLogin = state.matchedLocation == '/login';
        if (!loggedIn && !onLogin) return '/login';
        if (loggedIn && onLogin) return '/';
        return null;
      },
      routes: [
        GoRoute(path: '/', builder: (_, __) => const DashboardScreen()),
        GoRoute(path: '/login', builder: (_, __) => const LoginScreen()),
        GoRoute(
          path: '/hive/:id',
          builder: (_, state) =>
              HiveDetailScreen(hiveId: state.pathParameters['id']!),
        ),
        GoRoute(path: '/alerts', builder: (_, __) => const AlertsScreen()),
        GoRoute(path: '/settings', builder: (_, __) => const SettingsScreen()),
      ],
    );

// ÔöÇÔöÇ Root widget ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

class BeetterApp extends ConsumerWidget {
  const BeetterApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final authState = ref.watch(authProvider);
    final router = _buildRouter(authState);

    return MaterialApp.router(
      title: 'Beetter',
      theme: buildTheme(),
      routerConfig: router,
      debugShowCheckedModeBanner: false,
    );
  }
}
