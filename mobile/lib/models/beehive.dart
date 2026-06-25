class SensorValue {
  final double? value;
  final String? time;
  SensorValue({this.value, this.time});

  factory SensorValue.fromJson(Map<String, dynamic> json) => SensorValue(
        value: (json['value'] as num?)?.toDouble(),
        time: json['time'] as String?,
      );
}

class BeehiveLatest {
  final String? status;
  final SensorValue? temperatureInt;
  final SensorValue? humidityInt;
  final SensorValue? temperatureExt;
  final SensorValue? humidityExt;
  final SensorValue? soundFreqInt;
  final SensorValue? soundAmpInt;
  final SensorValue? soundFreqExt;
  final SensorValue? soundAmpExt;
  final SensorValue? lightExt;

  BeehiveLatest({
    this.status,
    this.temperatureInt,
    this.humidityInt,
    this.temperatureExt,
    this.humidityExt,
    this.soundFreqInt,
    this.soundAmpInt,
    this.soundFreqExt,
    this.soundAmpExt,
    this.lightExt,
  });

  factory BeehiveLatest.fromJson(Map<String, dynamic> json) => BeehiveLatest(
        status: json['status'] as String?,
        temperatureInt: json['temperature_int'] != null
            ? SensorValue.fromJson(json['temperature_int'])
            : null,
        humidityInt: json['humidity_int'] != null
            ? SensorValue.fromJson(json['humidity_int'])
            : null,
        temperatureExt: json['temperature_ext'] != null
            ? SensorValue.fromJson(json['temperature_ext'])
            : null,
        humidityExt: json['humidity_ext'] != null
            ? SensorValue.fromJson(json['humidity_ext'])
            : null,
        soundFreqInt: json['sound_freq_int'] != null
            ? SensorValue.fromJson(json['sound_freq_int'])
            : null,
        soundAmpInt: json['sound_amp_int'] != null
            ? SensorValue.fromJson(json['sound_amp_int'])
            : null,
        soundFreqExt: json['sound_freq_ext'] != null
            ? SensorValue.fromJson(json['sound_freq_ext'])
            : null,
        soundAmpExt: json['sound_amp_ext'] != null
            ? SensorValue.fromJson(json['sound_amp_ext'])
            : null,
        lightExt: json['light_ext'] != null
            ? SensorValue.fromJson(json['light_ext'])
            : null,
      );
}

class Beehive {
  final String id;
  final String? name;
  final BeehiveLatest? latest;

  Beehive({required this.id, this.name, this.latest});

  String get displayName => name ?? 'Ruche $id';

  factory Beehive.fromJson(Map<String, dynamic> json) => Beehive(
        id: json['id'].toString(),
        name: json['name'] as String?,
        latest: json['latest'] != null
            ? BeehiveLatest.fromJson(json['latest'])
            : null,
      );
}

class ChartSeries {
  final List<String> labels;
  final List<double?> data;

  ChartSeries({required this.labels, required this.data});

  factory ChartSeries.fromJson(Map<String, dynamic> json) => ChartSeries(
        labels: List<String>.from(json['labels'] ?? []),
        data: (json['data'] as List?)
                ?.map((v) => (v as num?)?.toDouble())
                .toList() ??
            [],
      );

  bool get isEmpty => labels.isEmpty;
}

class HiveChartData {
  final ChartSeries? temperatureInt;
  final ChartSeries? temperatureExt;
  final ChartSeries? humidityInt;
  final ChartSeries? humidityExt;
  final ChartSeries? soundFreqInt;
  final ChartSeries? soundAmpInt;
  final ChartSeries? soundFreqExt;
  final ChartSeries? soundAmpExt;
  final ChartSeries? lightExt;

  HiveChartData({
    this.temperatureInt,
    this.temperatureExt,
    this.humidityInt,
    this.humidityExt,
    this.soundFreqInt,
    this.soundAmpInt,
    this.soundFreqExt,
    this.soundAmpExt,
    this.lightExt,
  });

  factory HiveChartData.fromJson(Map<String, dynamic> json) => HiveChartData(
        temperatureInt: json['temperature_int'] != null
            ? ChartSeries.fromJson(json['temperature_int'])
            : null,
        temperatureExt: json['temperature_ext'] != null
            ? ChartSeries.fromJson(json['temperature_ext'])
            : null,
        humidityInt: json['humidity_int'] != null
            ? ChartSeries.fromJson(json['humidity_int'])
            : null,
        humidityExt: json['humidity_ext'] != null
            ? ChartSeries.fromJson(json['humidity_ext'])
            : null,
        soundFreqInt: json['sound_freq_int'] != null
            ? ChartSeries.fromJson(json['sound_freq_int'])
            : null,
        soundAmpInt: json['sound_amp_int'] != null
            ? ChartSeries.fromJson(json['sound_amp_int'])
            : null,
        soundFreqExt: json['sound_freq_ext'] != null
            ? ChartSeries.fromJson(json['sound_freq_ext'])
            : null,
        soundAmpExt: json['sound_amp_ext'] != null
            ? ChartSeries.fromJson(json['sound_amp_ext'])
            : null,
        lightExt: json['light_ext'] != null
            ? ChartSeries.fromJson(json['light_ext'])
            : null,
      );
}
