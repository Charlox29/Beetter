import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/auth_provider.dart';
import '../services/storage_service.dart';
import '../theme/app_theme.dart';

class SettingsScreen extends ConsumerStatefulWidget {
  const SettingsScreen({super.key});

  @override
  ConsumerState<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends ConsumerState<SettingsScreen> {
  final _storage = StorageService();
  bool _notificationsOn = false;
  double _tempMax = 38.0;
  double _tempMin = 28.0;
  double _humMax = 85.0;
  double _humMin = 40.0;
  bool _loading = true;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final s = await _storage.getNotificationSettings();
    if (mounted) {
      setState(() {
        _notificationsOn = s['enabled'] as bool;
        _tempMax = s['temp_max'] as double;
        _tempMin = s['temp_min'] as double;
        _humMax = s['hum_max'] as double;
        _humMin = s['hum_min'] as double;
        _loading = false;
      });
    }
  }

  Future<void> _save() async {
    await _storage.saveNotificationSettings(
      enabled: _notificationsOn,
      tempMax: _tempMax,
      tempMin: _tempMin,
      humMax: _humMax,
      humMin: _humMin,
    );
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Settings saved'),
          backgroundColor: kStatusOk,
          duration: Duration(seconds: 2),
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final username = ref.watch(authProvider).username ?? '';

    return Scaffold(
      appBar: AppBar(
        title: const Text('Settings'),
        leading: const BackButton(),
      ),
      body: _loading
          ? const Center(child: CircularProgressIndicator(color: kAmberDark))
          : ListView(
              padding: const EdgeInsets.all(16),
              children: [
                // Account
                _SectionCard(
                  title: 'Account',
                  icon: Icons.person_outline,
                  children: [
                    ListTile(
                      leading: const Icon(Icons.person_circle, color: kAmberDark),
                      title: Text(username,
                          style: const TextStyle(fontWeight: FontWeight.w600)),
                      subtitle: const Text('Logged in'),
                      contentPadding: EdgeInsets.zero,
                    ),
                    const Divider(height: 1),
                    ListTile(
                      leading: const Icon(Icons.logout, color: kStatusCrit),
                      title: const Text('Sign out',
                          style: TextStyle(color: kStatusCrit)),
                      contentPadding: EdgeInsets.zero,
                      onTap: () async {
                        final confirm = await showDialog<bool>(
                          context: context,
                          builder: (_) => AlertDialog(
                            title: const Text('Sign out'),
                            content: const Text(
                                'Are you sure you want to sign out?'),
                            actions: [
                              TextButton(
                                onPressed: () => Navigator.pop(context, false),
                                child: const Text('Cancel'),
                              ),
                              TextButton(
                                onPressed: () => Navigator.pop(context, true),
                                child: const Text('Sign out',
                                    style: TextStyle(color: kStatusCrit)),
                              ),
                            ],
                          ),
                        );
                        if (confirm == true) {
                          await ref.read(authProvider.notifier).logout();
                        }
                      },
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                // Notifications
                _SectionCard(
                  title: 'Notifications',
                  icon: Icons.notifications_outlined,
                  children: [
                    SwitchListTile(
                      title: const Text('Enable alert notifications'),
                      value: _notificationsOn,
                      activeColor: kAmberDark,
                      contentPadding: EdgeInsets.zero,
                      onChanged: (v) => setState(() => _notificationsOn = v),
                    ),
                    if (_notificationsOn) ...[
                      const Divider(height: 1),
                      const SizedBox(height: 8),
                      const Text('Interior temperature (°C)',
                          style: TextStyle(
                              fontSize: 13, fontWeight: FontWeight.w600)),
                      _ThresholdRow(
                        label: 'Max',
                        value: _tempMax,
                        min: 30,
                        max: 50,
                        onChanged: (v) => setState(() => _tempMax = v),
                      ),
                      _ThresholdRow(
                        label: 'Min',
                        value: _tempMin,
                        min: 10,
                        max: 35,
                        onChanged: (v) => setState(() => _tempMin = v),
                      ),
                      const SizedBox(height: 8),
                      const Text('Interior humidity (%)',
                          style: TextStyle(
                              fontSize: 13, fontWeight: FontWeight.w600)),
                      _ThresholdRow(
                        label: 'Max',
                        value: _humMax,
                        min: 50,
                        max: 100,
                        onChanged: (v) => setState(() => _humMax = v),
                      ),
                      _ThresholdRow(
                        label: 'Min',
                        value: _humMin,
                        min: 0,
                        max: 60,
                        onChanged: (v) => setState(() => _humMin = v),
                      ),
                    ],
                  ],
                ),
                const SizedBox(height: 20),
                ElevatedButton.icon(
                  icon: const Icon(Icons.check),
                  label: const Text('Save settings'),
                  onPressed: _save,
                ),
              ],
            ),
    );
  }
}

class _SectionCard extends StatelessWidget {
  final String title;
  final IconData icon;
  final List<Widget> children;

  const _SectionCard(
      {required this.title, required this.icon, required this.children});

  @override
  Widget build(BuildContext context) => Card(
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(icon, size: 16, color: kAmberDark),
                  const SizedBox(width: 6),
                  Text(title,
                      style: const TextStyle(
                          fontWeight: FontWeight.w700, fontSize: 14)),
                ],
              ),
              const SizedBox(height: 12),
              ...children,
            ],
          ),
        ),
      );
}

class _ThresholdRow extends StatelessWidget {
  final String label;
  final double value;
  final double min;
  final double max;
  final ValueChanged<double> onChanged;

  const _ThresholdRow({
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.onChanged,
  });

  @override
  Widget build(BuildContext context) => Row(
        children: [
          SizedBox(
              width: 30,
              child: Text(label,
                  style: const TextStyle(fontSize: 12, color: kMuted))),
          Expanded(
            child: Slider(
              value: value,
              min: min,
              max: max,
              divisions: ((max - min) * 2).toInt(),
              activeColor: kAmberDark,
              label: value.toStringAsFixed(1),
              onChanged: onChanged,
            ),
          ),
          SizedBox(
            width: 42,
            child: Text(value.toStringAsFixed(1),
                style: const TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w600)),
          ),
        ],
      );
}
