import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import '../models/beehive.dart';
import '../theme/app_theme.dart';

class LineChartCard extends StatelessWidget {
  final String title;
  final List<ChartSeries?> series;
  final List<String> seriesLabels;
  final List<Color> seriesColors;
  final String unit;

  const LineChartCard({
    super.key,
    required this.title,
    required this.series,
    required this.seriesLabels,
    required this.seriesColors,
    this.unit = '',
  });

  @override
  Widget build(BuildContext context) {
    final hasData = series.any((s) => s != null && !s.isEmpty);

    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 16, 16, 8),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Text(
                  title,
                  style: const TextStyle(fontWeight: FontWeight.w700, fontSize: 13),
                ),
                const Spacer(),
                if (series.length > 1)
                  Wrap(
                    spacing: 8,
                    children: List.generate(
                      series.length,
                      (i) => Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Container(
                            width: 10,
                            height: 3,
                            color: seriesColors[i],
                          ),
                          const SizedBox(width: 3),
                          Text(seriesLabels[i],
                              style: const TextStyle(fontSize: 10, color: kMuted)),
                        ],
                      ),
                    ),
                  ),
              ],
            ),
            const SizedBox(height: 12),
            SizedBox(
              height: 110,
              child: hasData ? _buildChart() : _buildEmpty(),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildEmpty() => const Center(
        child: Text('No data', style: TextStyle(color: kMuted, fontSize: 12)),
      );

  Widget _buildChart() {
    final lineBarsData = <LineChartBarData>[];

    for (int si = 0; si < series.length; si++) {
      final s = series[si];
      if (s == null || s.isEmpty) continue;
      final spots = <FlSpot>[];
      for (int i = 0; i < s.labels.length; i++) {
        final v = s.data[i];
        if (v != null) spots.add(FlSpot(i.toDouble(), v));
      }
      if (spots.isEmpty) continue;
      lineBarsData.add(LineChartBarData(
        spots: spots,
        color: seriesColors[si],
        barWidth: 2,
        dotData: FlDotData(show: spots.length < 50),
        belowBarData: series.length == 1
            ? BarAreaData(
                show: true,
                color: seriesColors[si].withOpacity(0.12),
              )
            : BarAreaData(show: false),
      ));
    }

    // X-axis tick labels: show first, middle, last timestamp
    final firstSeries = series.firstWhere((s) => s != null && !s.isEmpty);
    final labels = firstSeries!.labels;
    final tickIndices = {0, labels.length ~/ 2, labels.length - 1};

    return LineChart(
      LineChartData(
        lineBarsData: lineBarsData,
        gridData: FlGridData(
          show: true,
          drawVerticalLine: false,
          getDrawingHorizontalLine: (v) =>
              FlLine(color: Colors.black.withOpacity(0.06), strokeWidth: 1),
        ),
        borderData: FlBorderData(show: false),
        titlesData: FlTitlesData(
          leftTitles: AxisTitles(
            sideTitles: SideTitles(
              showTitles: true,
              reservedSize: 38,
              getTitlesWidget: (v, _) => Text(
                '$unit${v.toStringAsFixed(0)}',
                style: const TextStyle(fontSize: 9, color: kMuted),
              ),
            ),
          ),
          bottomTitles: AxisTitles(
            sideTitles: SideTitles(
              showTitles: true,
              reservedSize: 18,
              getTitlesWidget: (value, _) {
                final idx = value.toInt();
                if (!tickIndices.contains(idx) || idx >= labels.length) {
                  return const SizedBox.shrink();
                }
                final raw = labels[idx];
                // Format: "2026-01-15T14:30:00Z" → "14:30"
                final t = raw.length >= 16 ? raw.substring(11, 16) : raw;
                return Text(t, style: const TextStyle(fontSize: 9, color: kMuted));
              },
            ),
          ),
          topTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
          rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
        ),
        lineTouchData: LineTouchData(
          touchTooltipData: LineTouchTooltipData(
            getTooltipColor: (_) => kDarkSurface.withOpacity(0.85),
            getTooltipItems: (touchedSpots) => touchedSpots.map((ts) {
              final label = ts.barIndex < seriesLabels.length
                  ? '${seriesLabels[ts.barIndex]}: '
                  : '';
              return LineTooltipItem(
                '$label${ts.y.toStringAsFixed(1)}$unit',
                const TextStyle(color: Colors.white, fontSize: 11),
              );
            }).toList(),
          ),
        ),
      ),
    );
  }
}
