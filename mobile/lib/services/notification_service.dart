import 'package:flutter_local_notifications/flutter_local_notifications.dart';
import '../models/beehive.dart';
import 'storage_service.dart';

class NotificationService {
  static final _plugin = FlutterLocalNotificationsPlugin();

  static Future<void> checkAndNotify(List<Beehive> hives) async {
    final storage = StorageService();
    final prefs = await storage.getNotificationPrefs();

    if (!(prefs['enabled'] ?? true)) return;

    for (final hive in hives) {
      final status = hive.latest?.status?.toLowerCase() ?? '';

      final shouldNotify = switch (status) {
        'critical' => prefs['critical'] ?? true,
        'agitated' || 'warning' => prefs['warning'] ?? true,
        'no_data' || 'silent' || '' => prefs['no_data'] ?? true,
        _ => false,
      };

      if (!shouldNotify) continue;

      final lastKnown = await storage.getLastKnownStatus(hive.id);
      if (lastKnown == status) continue;

      await _plugin.show(
        hive.id.hashCode,
        'Ruche ${hive.displayName}',
        _statusMessage(status),
        const NotificationDetails(
          android: AndroidNotificationDetails(
            'beetter_alerts',
            'Beetter Alerts',
            importance: Importance.high,
            priority: Priority.high,
            icon: '@mipmap/ic_launcher',
          ),
          iOS: DarwinNotificationDetails(),
        ),
      );

      await storage.saveLastKnownStatus(hive.id, status);
    }
  }

  static String _statusMessage(String status) => switch (status) {
        'critical' => '🔴 État critique détecté !',
        'agitated' || 'warning' => '🟠 Ruche agitée',
        'no_data' || 'silent' || '' => '⚫ Plus de données reçues',
        _ => 'Changement d\'état détecté',
      };
}
