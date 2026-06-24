import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../models/alert.dart';
import '../providers/beehives_provider.dart';
import '../theme/app_theme.dart';

// ── Helpers ───────────────────────────────────────────────────────────────────

Color _statusColor(String status) => switch (status.toLowerCase()) {
      'critical' => kStatusCrit,
      'warning' || 'agitated' => kStatusWarn,
      'calm' || 'ok' => kStatusOk,
      _ => kStatusNone,
    };

String _statusLabel(String status) => switch (status.toLowerCase()) {
      'critical' => 'Critical',
      'warning' => 'Warning',
      'agitated' => 'Agitated',
      'calm' || 'ok' => 'Calm',
      'no_data' => 'No data',
      _ => status,
    };

String _fmtTime(String iso) {
  final dt = DateTime.tryParse(iso)?.toLocal();
  if (dt == null) return '';
  return '${dt.hour.toString().padLeft(2, '0')}:${dt.minute.toString().padLeft(2, '0')}';
}

// ── Screen ────────────────────────────────────────────────────────────────────

class AlertsScreen extends ConsumerStatefulWidget {
  const AlertsScreen({super.key});

  @override
  ConsumerState<AlertsScreen> createState() => _AlertsScreenState();
}

class _AlertsScreenState extends ConsumerState<AlertsScreen> {
  int _tab = 0;

  Future<void> _refresh() async => ref.invalidate(alertsProvider);

  @override
  Widget build(BuildContext context) {
    final alertsAsync = ref.watch(alertsProvider);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Alerts'),
        actions: [
          IconButton(
            icon: const Icon(Icons.refresh),
            tooltip: 'Refresh',
            onPressed: _refresh,
          ),
        ],
      ),
      body: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // ── Tab chips ──────────────────────────────────────────────────────
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 12, 16, 8),
            child: Row(
              children: [
                _TabChip(
                  label: 'Active',
                  selected: _tab == 0,
                  onTap: () => setState(() => _tab = 0),
                ),
                const SizedBox(width: 8),
                _TabChip(
                  label: 'History',
                  selected: _tab == 1,
                  onTap: () => setState(() => _tab = 1),
                ),
              ],
            ),
          ),
          // ── Content ────────────────────────────────────────────────────────
          Expanded(
            child: alertsAsync.when(
              loading: () => const Center(
                child: CircularProgressIndicator(color: kAmberDark),
              ),
              error: (err, _) {
                if (err is AlertsUnavailableException) {
                  return RefreshIndicator(
                    color: kAmberDark,
                    onRefresh: _refresh,
                    child: CustomScrollView(
                      physics: const AlwaysScrollableScrollPhysics(),
                      slivers: const [
                        SliverFillRemaining(
                          hasScrollBody: false,
                          child: _UnavailableState(),
                        ),
                      ],
                    ),
                  );
                }
                return Center(
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
                          onPressed: _refresh,
                        ),
                      ],
                    ),
                  ),
                );
              },
              data: (alerts) => _tab == 0
                  ? _ActiveTab(alerts: alerts, onRefresh: _refresh)
                  : _HistoryTab(alerts: alerts, onRefresh: _refresh),
            ),
          ),
        ],
      ),
    );
  }
}

// ── Tab chip button ───────────────────────────────────────────────────────────

class _TabChip extends StatelessWidget {
  final String label;
  final bool selected;
  final VoidCallback onTap;

  const _TabChip({
    required this.label,
    required this.selected,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 8),
        decoration: BoxDecoration(
          color: selected ? kAmberDark : Colors.transparent,
          borderRadius: BorderRadius.circular(6),
          border: selected
              ? null
              : Border.all(color: const Color(0xFFD1D5DB)),
        ),
        child: Text(
          label,
          style: TextStyle(
            fontWeight: FontWeight.w600,
            fontSize: 14,
            color: selected ? Colors.black : kMuted,
          ),
        ),
      ),
    );
  }
}

// ── Active tab ────────────────────────────────────────────────────────────────

class _ActiveTab extends StatelessWidget {
  final List<Alert> alerts;
  final Future<void> Function() onRefresh;

  const _ActiveTab({required this.alerts, required this.onRefresh});

  @override
  Widget build(BuildContext context) {
    // Sort newest first, then keep one per beehive
    final unresolvedSorted = alerts.where((a) => !a.resolved).toList()
      ..sort((a, b) => b.createdAt.compareTo(a.createdAt));
    final Map<String, Alert> latestPerHive = {};
    for (final a in unresolvedSorted) {
      if (!latestPerHive.containsKey(a.beehiveId)) latestPerHive[a.beehiveId] = a;
    }
    final unresolved = latestPerHive.values.toList();

    final recent = [...alerts]..sort((a, b) => b.createdAt.compareTo(a.createdAt));

    if (alerts.isEmpty) {
      return RefreshIndicator(
        color: kAmberDark,
        onRefresh: onRefresh,
        child: const CustomScrollView(
          physics: AlwaysScrollableScrollPhysics(),
          slivers: [
            SliverFillRemaining(
              hasScrollBody: false,
              child: _EmptyAlerts(),
            ),
          ],
        ),
      );
    }

    return RefreshIndicator(
      color: kAmberDark,
      onRefresh: onRefresh,
      child: ListView(
        padding: const EdgeInsets.fromLTRB(16, 4, 16, 24),
        children: [
          _SectionHeader('ACTIVE — ${unresolved.length} UNRESOLVED'),
          const SizedBox(height: 8),
          if (unresolved.isEmpty)
            const _NoAlertsRow()
          else
            ...unresolved.map((a) => Padding(
                  padding: const EdgeInsets.only(bottom: 8),
                  child: _AlertCard(alert: a),
                )),
          const SizedBox(height: 20),
          _SectionHeader('RECENT — CONDENSED'),
          const SizedBox(height: 8),
          if (recent.isEmpty)
            const _NoAlertsRow()
          else
            _CondensedList(alerts: recent),
        ],
      ),
    );
  }
}

// ── History tab ───────────────────────────────────────────────────────────────

class _HistoryTab extends StatelessWidget {
  final List<Alert> alerts;
  final Future<void> Function() onRefresh;

  const _HistoryTab({required this.alerts, required this.onRefresh});

  @override
  Widget build(BuildContext context) {
    if (alerts.isEmpty) {
      return RefreshIndicator(
        color: kAmberDark,
        onRefresh: onRefresh,
        child: const CustomScrollView(
          physics: AlwaysScrollableScrollPhysics(),
          slivers: [
            SliverFillRemaining(
              hasScrollBody: false,
              child: _EmptyAlerts(),
            ),
          ],
        ),
      );
    }

    final sorted = [...alerts]..sort((a, b) => b.createdAt.compareTo(a.createdAt));

    return RefreshIndicator(
      color: kAmberDark,
      onRefresh: onRefresh,
      child: ListView(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 24),
        children: [_CondensedList(alerts: sorted)],
      ),
    );
  }
}

// ── Shared widgets ────────────────────────────────────────────────────────────

class _SectionHeader extends StatelessWidget {
  final String text;
  const _SectionHeader(this.text);

  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      style: const TextStyle(
        fontSize: 11,
        fontWeight: FontWeight.w700,
        color: kMuted,
        letterSpacing: 0.5,
      ),
    );
  }
}

class _AlertCard extends StatelessWidget {
  final Alert alert;
  const _AlertCard({required this.alert});

  @override
  Widget build(BuildContext context) {
    final color = _statusColor(alert.newStatus);
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(8),
        boxShadow: const [
          BoxShadow(color: Colors.black12, blurRadius: 3, offset: Offset(0, 1)),
        ],
      ),
      child: Row(
        children: [
          Container(
            width: 10,
            height: 10,
            decoration: BoxDecoration(shape: BoxShape.circle, color: color),
          ),
          const SizedBox(width: 12),
          Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                alert.beehiveName,
                style: const TextStyle(
                  fontWeight: FontWeight.w700,
                  fontSize: 15,
                  color: kText,
                ),
              ),
              Text(
                _statusLabel(alert.newStatus),
                style: TextStyle(
                  color: color,
                  fontSize: 13,
                  fontWeight: FontWeight.w500,
                ),
              ),
            ],
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Text(
              '"${alert.note ?? ''}"',
              style: const TextStyle(
                fontSize: 13,
                color: kMuted,
                fontStyle: FontStyle.italic,
              ),
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
            ),
          ),
          const SizedBox(width: 12),
          Text(
            _fmtTime(alert.createdAt),
            style: const TextStyle(fontSize: 13, color: kMuted),
          ),
        ],
      ),
    );
  }
}

class _CondensedList extends StatelessWidget {
  final List<Alert> alerts;
  const _CondensedList({required this.alerts});

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(8),
        boxShadow: const [
          BoxShadow(color: Colors.black12, blurRadius: 3, offset: Offset(0, 1)),
        ],
      ),
      child: Column(
        children: [
          for (int i = 0; i < alerts.length; i++) ...[
            if (i > 0)
              const Divider(height: 1, indent: 28, endIndent: 0),
            _AlertCondensedRow(alert: alerts[i]),
          ],
        ],
      ),
    );
  }
}

class _AlertCondensedRow extends StatelessWidget {
  final Alert alert;
  const _AlertCondensedRow({required this.alert});

  @override
  Widget build(BuildContext context) {
    final color = _statusColor(alert.newStatus);
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
      child: Row(
        children: [
          Container(
            width: 8,
            height: 8,
            decoration: BoxDecoration(shape: BoxShape.circle, color: color),
          ),
          const SizedBox(width: 10),
          SizedBox(
            width: 52,
            child: Text(
              alert.beehiveName,
              style: const TextStyle(
                fontWeight: FontWeight.w600,
                fontSize: 13,
                color: kText,
              ),
              overflow: TextOverflow.ellipsis,
            ),
          ),
          const SizedBox(width: 6),
          Expanded(
            child: Text(
              '"${alert.note ?? ''}"',
              style: const TextStyle(
                fontSize: 12,
                color: kMuted,
                fontStyle: FontStyle.italic,
              ),
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
            ),
          ),
          const SizedBox(width: 8),
          SizedBox(
            width: 58,
            child: Text(
              _statusLabel(alert.newStatus),
              style: TextStyle(
                fontSize: 12,
                color: color,
                fontWeight: FontWeight.w500,
              ),
            ),
          ),
          Text(
            _fmtTime(alert.createdAt),
            style: const TextStyle(fontSize: 12, color: kMuted),
          ),
        ],
      ),
    );
  }
}

class _NoAlertsRow extends StatelessWidget {
  const _NoAlertsRow();

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(vertical: 14),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(8),
      ),
      child: const Center(
        child: Text('No alerts', style: TextStyle(fontSize: 13, color: kMuted)),
      ),
    );
  }
}

class _EmptyAlerts extends StatelessWidget {
  const _EmptyAlerts();

  @override
  Widget build(BuildContext context) {
    return const Center(
      child: Padding(
        padding: EdgeInsets.all(40),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.notifications_none, size: 48, color: kMuted),
            SizedBox(height: 12),
            Text(
              'No alerts',
              style: TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.w600,
                color: kText,
              ),
            ),
            SizedBox(height: 6),
            Text(
              'All hives are running normally.',
              style: TextStyle(color: kMuted, fontSize: 13),
            ),
          ],
        ),
      ),
    );
  }
}

class _UnavailableState extends StatelessWidget {
  const _UnavailableState();

  @override
  Widget build(BuildContext context) {
    return const Center(
      child: Padding(
        padding: EdgeInsets.all(40),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.construction_outlined, size: 48, color: kMuted),
            SizedBox(height: 12),
            Text(
              'No alerts — endpoint not yet available',
              textAlign: TextAlign.center,
              style: TextStyle(
                fontSize: 15,
                fontWeight: FontWeight.w600,
                color: kText,
              ),
            ),
            SizedBox(height: 6),
            Text(
              'Pull down to retry when the server is updated.',
              textAlign: TextAlign.center,
              style: TextStyle(color: kMuted, fontSize: 13),
            ),
          ],
        ),
      ),
    );
  }
}
