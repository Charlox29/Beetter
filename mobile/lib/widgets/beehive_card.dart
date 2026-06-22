import 'package:flutter/material.dart';
import '../models/beehive.dart';
import '../theme/app_theme.dart';
import 'status_dot.dart';

class BeehiveCard extends StatelessWidget {
  final Beehive hive;
  final VoidCallback onTap;

  const BeehiveCard({super.key, required this.hive, required this.onTap});

  @override
  Widget build(BuildContext context) {
    final l = hive.latest;
    final tInt = l?.temperatureInt?.value;
    final status = statusFromTemperature(tInt);

    return Card(
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(12),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  const Icon(Icons.hexagon_outlined, color: kAmberDark, size: 18),
                  const SizedBox(width: 6),
                  Text(
                    hive.id,
                    style: const TextStyle(fontWeight: FontWeight.w700, fontSize: 15),
                  ),
                  const Spacer(),
                  StatusDot(status: status, size: 11),
                ],
              ),
              const SizedBox(height: 12),
              _MetricGrid(latest: l),
              const SizedBox(height: 10),
              Row(
                mainAxisAlignment: MainAxisAlignment.end,
                children: [
                  Text(
                    'View charts →',
                    style: TextStyle(
                      fontSize: 12,
                      color: kAmberDark,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _MetricGrid extends StatelessWidget {
  final BeehiveLatest? latest;
  const _MetricGrid({this.latest});

  @override
  Widget build(BuildContext context) {
    final l = latest;
    return GridView.count(
      crossAxisCount: 3,
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      mainAxisSpacing: 6,
      crossAxisSpacing: 6,
      childAspectRatio: 2.1,
      children: [
        _MetricCell(
          icon: Icons.thermostat,
          color: Colors.red.shade400,
          label: 'Int temp',
          value: _fmtTemp(l?.temperatureInt?.value),
        ),
        _MetricCell(
          icon: Icons.water_drop,
          color: Colors.blue.shade400,
          label: 'Int hum',
          value: _fmtPct(l?.humidityInt?.value),
        ),
        _MetricCell(
          icon: Icons.wb_sunny,
          color: Colors.orange.shade400,
          label: 'Light',
          value: _fmtLight(l?.lightExt?.value),
        ),
        _MetricCell(
          icon: Icons.thermostat_outlined,
          color: kMuted,
          label: 'Ext temp',
          value: _fmtTemp(l?.temperatureExt?.value),
        ),
        _MetricCell(
          icon: Icons.water_outlined,
          color: kMuted,
          label: 'Ext hum',
          value: _fmtPct(l?.humidityExt?.value),
        ),
        _MetricCell(
          icon: Icons.graphic_eq,
          color: Colors.purple.shade400,
          label: 'Int freq',
          value: _fmtFreq(l?.soundFreqInt?.value),
        ),
      ],
    );
  }

  String _fmtTemp(double? v) => v != null ? '${v.toStringAsFixed(1)}°C' : '—';
  String _fmtPct(double? v) => v != null ? '${v.toStringAsFixed(1)}%' : '—';
  String _fmtLight(double? v) => v != null ? '${v.toStringAsFixed(1)}/10' : '—';
  String _fmtFreq(double? v) => v != null ? '${v.toStringAsFixed(0)}Hz' : '—';
}

class _MetricCell extends StatelessWidget {
  final IconData icon;
  final Color color;
  final String label;
  final String value;

  const _MetricCell({
    required this.icon,
    required this.color,
    required this.label,
    required this.value,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: const Color(0xFFF8F9FA),
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color: const Color(0xFFE9ECEF)),
      ),
      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Text(
            value,
            style: TextStyle(
              fontWeight: FontWeight.w700,
              fontSize: 13,
              color: color,
            ),
          ),
          Text(
            label,
            style: const TextStyle(fontSize: 9, color: kMuted),
            overflow: TextOverflow.ellipsis,
          ),
        ],
      ),
    );
  }
}
