import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';
import '../providers/auth_provider.dart';
import '../providers/beehives_provider.dart';
import '../theme/app_theme.dart';
import '../widgets/beehive_card.dart';

class DashboardScreen extends ConsumerStatefulWidget {
  const DashboardScreen({super.key});

  @override
  ConsumerState<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends ConsumerState<DashboardScreen> {
  Timer? _timer;
  DateTime _lastUpdated = DateTime.now();

  @override
  void initState() {
    super.initState();
    _timer = Timer.periodic(const Duration(seconds: 30), (_) {
      ref.invalidate(beehivesProvider);
      setState(() => _lastUpdated = DateTime.now());
    });
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  String _formatTime(DateTime t) {
    final h = t.hour.toString().padLeft(2, '0');
    final m = t.minute.toString().padLeft(2, '0');
    final s = t.second.toString().padLeft(2, '0');
    return '$h:$m:$s';
  }

  @override
  Widget build(BuildContext context) {
    final hivesAsync = ref.watch(beehivesProvider);
    final username = ref.watch(authProvider).username ?? '';

    return Scaffold(
      appBar: AppBar(
        title: const Text('Beetter'),
        actions: [
          IconButton(
            icon: const Icon(Icons.refresh),
            tooltip: 'Refresh',
            onPressed: () {
              ref.invalidate(beehivesProvider);
              setState(() => _lastUpdated = DateTime.now());
            },
          ),
          IconButton(
            icon: const Icon(Icons.settings_outlined),
            tooltip: 'Settings',
            onPressed: () => context.push('/settings'),
          ),
        ],
      ),
      body: RefreshIndicator(
        color: kAmberDark,
        onRefresh: () async {
          ref.invalidate(beehivesProvider);
          setState(() => _lastUpdated = DateTime.now());
        },
        child: hivesAsync.when(
          loading: () => const Center(
            child: CircularProgressIndicator(color: kAmberDark),
          ),
          error: (err, _) => Center(
            child: Padding(
              padding: const EdgeInsets.all(24),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const Icon(Icons.cloud_off, size: 48, color: kMuted),
                  const SizedBox(height: 12),
                  Text(
                    err.toString(),
                    textAlign: TextAlign.center,
                    style: const TextStyle(color: kMuted),
                  ),
                  const SizedBox(height: 16),
                  ElevatedButton.icon(
                    icon: const Icon(Icons.refresh),
                    label: const Text('Retry'),
                    onPressed: () {
                      ref.invalidate(beehivesProvider);
                      setState(() => _lastUpdated = DateTime.now());
                    },
                  ),
                ],
              ),
            ),
          ),
          data: (hives) {
            if (hives.isEmpty) {
              return const Center(
                child: Padding(
                  padding: EdgeInsets.all(32),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Icon(Icons.cloud_download_outlined,
                          size: 48, color: kMuted),
                      SizedBox(height: 12),
                      Text(
                        'No beehives found.\nThe Raspberry Pi will push data once configured.',
                        textAlign: TextAlign.center,
                        style: TextStyle(color: kMuted),
                      ),
                    ],
                  ),
                ),
              );
            }

            return ListView(
              padding: const EdgeInsets.fromLTRB(16, 12, 16, 24),
              children: [
                Padding(
                  padding: const EdgeInsets.only(bottom: 12),
                  child: Text(
                    'Hello, $username',
                    style: const TextStyle(
                        fontWeight: FontWeight.w700,
                        fontSize: 17,
                        color: kText),
                  ),
                ),
                ...hives.map(
                  (hive) => Padding(
                    padding: const EdgeInsets.only(bottom: 12),
                    child: BeehiveCard(
                      hive: hive,
                      onTap: () => context.push('/hive/${hive.id}'),
                    ),
                  ),
                ),
                Padding(
                  padding: const EdgeInsets.only(top: 4),
                  child: Text(
                    'Updated at ${_formatTime(_lastUpdated)} · auto-refresh 30 s',
                    textAlign: TextAlign.center,
                    style: const TextStyle(fontSize: 11, color: kMuted),
                  ),
                ),
              ],
            );
          },
        ),
      ),
    );
  }
}
