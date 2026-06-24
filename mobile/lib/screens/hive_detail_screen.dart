import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/beehives_provider.dart';
import '../theme/app_theme.dart';
import '../widgets/line_chart_card.dart';


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
    final hives = ref.watch(beehivesProvider).valueOrNull;
    final beehiveName = hives
        ?.where((h) => h.id == widget.hiveId)
        .firstOrNull
        ?.name;

    return Scaffold(
      appBar: AppBar(
        title: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              beehiveName ?? widget.hiveId,
              style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w700),
            ),
            if (beehiveName != null)
              Text(widget.hiveId,
                  style: const TextStyle(fontSize: 12, color: Colors.white70)),
          ],
        ),
        leading: const BackButton(),
      ),
      body: Column(
        children: [
          // Range selector
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
            child: Row(
              children: ['1H', '6H', '24H', '7D', '30D'].map((label) {
                final value = label.toLowerCase();
                final isSelected = _range == value;
                return Expanded(
                  child: GestureDetector(
                    onTap: () => setState(() => _range = value),
                    child: Container(
                      margin: const EdgeInsets.symmetric(horizontal: 3),
                      padding: const EdgeInsets.symmetric(vertical: 8),
                      decoration: BoxDecoration(
                        color: isSelected ? kAmberDark : Colors.white,
                        borderRadius: BorderRadius.circular(8),
                        border: Border.all(
                          color: isSelected
                              ? kAmberDark
                              : const Color(0xFFD1D5DB),
                        ),
                      ),
                      child: Text(
                        label,
                        textAlign: TextAlign.center,
                        style: TextStyle(
                          fontSize: 13,
                          fontWeight: FontWeight.w700,
                          color: isSelected ? Colors.black : kMuted,
                        ),
                      ),
                    ),
                  ),
                );
              }).toList(),
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
