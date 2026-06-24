import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../models/beehive.dart';
import '../models/alert.dart';
import '../services/api_service.dart';

final _api = ApiService();

// Beehive list
final beehivesProvider = FutureProvider<List<Beehive>>((ref) async {
  return _api.getBeehives();
});

// Alerts
final alertsProvider = FutureProvider<List<Alert>>((ref) async {
  return _api.getAlerts();
});

// Chart data per hive+range
final hiveChartProvider =
    FutureProvider.family<HiveChartData, ({String id, String range})>(
  (ref, args) => _api.getBeehiveData(args.id, args.range),
);
