import 'package:flutter/material.dart';
import '../theme/app_theme.dart';

enum HiveStatus { ok, warn, crit, noData }

class StatusDot extends StatelessWidget {
  final HiveStatus status;
  final double size;

  const StatusDot({super.key, required this.status, this.size = 10});

  Color get _color => switch (status) {
        HiveStatus.ok => kStatusOk,
        HiveStatus.warn => kStatusWarn,
        HiveStatus.crit => kStatusCrit,
        HiveStatus.noData => kStatusNone,
      };

  @override
  Widget build(BuildContext context) => Container(
        width: size,
        height: size,
        decoration: BoxDecoration(shape: BoxShape.circle, color: _color),
      );
}

HiveStatus statusFromTemperature(double? t) {
  if (t == null) return HiveStatus.noData;
  if (t >= 42 || t <= 28) return HiveStatus.crit;
  if (t >= 38 || t <= 32) return HiveStatus.warn;
  return HiveStatus.ok;
}
