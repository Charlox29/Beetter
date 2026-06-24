class AlertsUnavailableException implements Exception {
  const AlertsUnavailableException();
}

class Alert {
  final String id;
  final String beehiveId;
  final String beehiveName;
  final String oldStatus;
  final String newStatus;
  final String source;
  final String? note;
  final String createdAt;
  final bool resolved;

  Alert({
    required this.id,
    required this.beehiveId,
    required this.beehiveName,
    required this.oldStatus,
    required this.newStatus,
    required this.source,
    required this.note,
    required this.createdAt,
    required this.resolved,
  });

  factory Alert.fromJson(Map<String, dynamic> json) => Alert(
        id: json['id'].toString(),
        beehiveId: json['beehive_id'].toString(),
        beehiveName:
            json['beehive_name'] as String? ?? json['beehive_id'].toString(),
        oldStatus: json['old_status'] as String? ?? '',
        newStatus: json['new_status'] as String? ?? 'no_data',
        source: json['source'] as String? ?? '',
        note: json['note'] as String?,
        createdAt: json['created_at'] as String? ?? '',
        resolved: json['resolved'] as bool? ?? true,
      );
}
