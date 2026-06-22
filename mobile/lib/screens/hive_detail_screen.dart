import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/beehives_provider.dart';
import '../theme/app_theme.dart';
import '../widgets/line_chart_card.dart';

const _ranges = ['1h', '6h', '24h', '7d', '30d'];

class HiveDetailScreen extends ConsumerStatefulWidget {
  final String hiveId;
  const HiveDetailScreen({super.key, required this.hiveId});

  @override
  ConsumerState<HiveDetailScreen> createState() => _HiveDetailScreenState();
}

class _HiveDetailScreenState extends ConsumerState<HiveDetailScreen> {
  String _range = '24h';

  @override
  Widget build(BuildContext context) {
    final args = (id: widget.hiveId, range: _range);
    final chartAsync = ref.watch(hiveChartProvider(args));

    return Scaffold(
      appBar: AppBar(
        title: Text('Beehive ${widget.hiveId}'),
        leading: const BackButton(),
      ),
      body: Column(
        children: [
          // Range selector
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
            child: SingleChildScrollView(
              scrollDirection: Axis.horizontal,
              child: Row(
                children: _ranges.map((r) {
                  final active = r == _range;
                  return Padding(
                    padding: const EdgeInsets.only(right: 6),
                    child: FilterChip(
                      label: Text(r.toUpperCase()),
                      selected: active,
                      onSelected: (_) => setState(() {
                        _range = r;
                        ref.invalidate(hiveChartProvider(args));
                      }),
                      selectedColor: kAmber,
                      checkmarkColor: Colors.black,
                    ),
                  );
                }).toList(),
              ),
            ),
          ),
          // Charts
          Expanded(
            child: chartAsync.when(
              loading: () => const Center(
                  child: CircularProgressIndicator(color: kAmberDark)),
              error: (err, _) => Center(
                child: Padding(
                  padding: const EdgeInsets.all(24),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      const Icon(Icons.error_outline, size: 40, color: kMuted),
                      const SizedBox(height: 10),
                      Text(err.toString(),
                          textAlign: TextAlign.center,
                          style: const TextStyle(color: kMuted)),
                      const SizedBox(height: 14),
                      ElevatedButton.icon(
                        icon: const Icon(Icons.refresh),
                        label: const Text('Retry'),
                        onPressed: () => ref.invalidate(hiveChartProvider(args)),
                      ),
                    ],
                  ),
                ),
              ),
              data: (d) => ListView(
                padding: const EdgeInsets.fromLTRB(16, 8, 16, 32),
                children: [
                  LineChartCard(
                    title: 'Temperature (°C)',
                    series: [d.temperatureInt, d.temperatureExt],
                    seriesLabels: const ['Interior', 'Exterior'],
                    seriesColors: [Colors.red.shade400, Colors.orange.shade400],
                    unit: '°',
                  ),
                  const SizedBox(height: 12),
                  LineChartCard(
                    title: 'Humidity (%)',
                    series: [d.humidityInt, d.humidityExt],
                    seriesLabels: const ['Interior', 'Exterior'],
                    seriesColors: [Colors.blue.shade400, Colors.cyan.shade400],
                    unit: '%',
                  ),
                  const SizedBox(height: 12),
                  LineChartCard(
                    title: 'Sound — Peak frequency (Hz)',
                    series: [d.soundFreqInt, d.soundFreqExt],
                    seriesLabels: const ['Interior', 'Exterior'],
                    seriesColors: [Colors.purple.shade400, Colors.purple.shade200],
                  ),
                  const SizedBox(height: 12),
                  LineChartCard(
                    title: 'Sound — Amplitude (RMS)',
                    series: [d.soundAmpInt, d.soundAmpExt],
                    seriesLabels: const ['Interior', 'Exterior'],
                    seriesColors: [Colors.deepPurple.shade400, Colors.deepPurple.shade200],
                  ),
                  const SizedBox(height: 12),
                  LineChartCard(
                    title: 'Light (0–10)',
                    series: [d.lightExt],
                    seriesLabels: const ['Light'],
                    seriesColors: [Colors.amber.shade600],
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
