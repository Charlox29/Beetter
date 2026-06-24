# Documentation technique — Application mobile (`mobile/`)

Flutter, Dart ≥ 3.3.0, Flutter ≥ 3.19.0. Cible Android (API 26+) et iOS (12+).

---

## Architecture

```
┌─────────────────────────────────────────────┐
│  UI Layer                                   │
│  screens/          widgets/                 │
│  LoginScreen       BeehiveCard              │
│  DashboardScreen   LineChartCard            │
│  HiveDetailScreen  StatusDot                │
│  SettingsScreen                             │
└──────────────┬──────────────────────────────┘
               │ ref.watch / ref.read
┌──────────────▼──────────────────────────────┐
│  State Layer (Riverpod)                     │
│  authProvider       → AuthNotifier          │
│  beehivesProvider   → FutureProvider        │
│  hiveChartProvider  → FutureProvider.family │
└──────────────┬──────────────────────────────┘
               │ await
┌──────────────▼──────────────────────────────┐
│  Service Layer                              │
│  ApiService         StorageService          │
│  (Dio + JWT)        (FlutterSecureStorage)  │
└──────────────┬──────────────────────────────┘
               │ HTTPS
          server/:5001
```

---

## Dépendances (`mobile/pubspec.yaml`)

| Paquet | Version | Rôle |
|---|---|---|
| `flutter_riverpod` | ^2.5.1 | State management (Notifier pattern) |
| `go_router` | ^14.2.7 | Navigation déclarative |
| `dio` | ^5.4.3 | Client HTTP avec intercepteurs |
| `flutter_secure_storage` | ^9.2.2 | Stockage chiffré (Keystore / Keychain) |
| `fl_chart` | ^0.68.0 | Graphes linéaires |
| `workmanager` | ^0.5.2 | Tâches périodiques en arrière-plan |
| `flutter_local_notifications` | ^17.2.1 | Notifications locales |
| `intl` | ^0.19.0 | Formatage des dates |

---

## Entrée de l'application (`mobile/lib/main.dart`)

### Initialisation

```dart
void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  // Initialise le canal de notification (Android Notification Channel)
  await FlutterLocalNotificationsPlugin().initialize(...);
  // Enregistre la tâche arrière-plan
  Workmanager().initialize(callbackDispatcher);
  Workmanager().registerPeriodicTask(
    'beetter_alert_check',
    'beetter_alert_check',
    frequency: Duration(minutes: 15),
    constraints: Constraints(networkType: NetworkType.connected),
  );
  runApp(ProviderScope(child: BeeterApp()));
}
```

### Routeur (GoRouter)

```dart
/           → DashboardScreen   (redirect → /login si non authentifié)
/login      → LoginScreen
/hive/:id   → HiveDetailScreen
/settings   → SettingsScreen
```

La redirection est gérée par `authProvider` : si `AuthStatus.unauthenticated`, toute tentative d'accès à `/` ou `/hive/:id` redirige vers `/login`.

### Tâche d'alerte arrière-plan (`callbackDispatcher`)

```dart
void callbackDispatcher() {
  Workmanager().executeTask((taskName, inputData) async {
    final api    = ApiService();
    final storage = StorageService();
    final beehives = await api.getBeehives();
    final settings = await storage.getNotificationSettings();
    for (final hive in beehives) {
      final temp = hive.latest?.temperatureInt?.value;
      if (temp != null && (temp > settings['threshold_temp_max']
                        || temp < settings['threshold_temp_min'])) {
        // Affiche une notification locale
        ...
      }
      // Idem pour humidity
    }
    return true;
  });
}
```

---

## Modèles (`mobile/lib/models/beehive.dart`)

```dart
class SensorValue {
  double? value;
  String? time;
  SensorValue.fromJson(Map json)
}

class BeehiveLatest {
  SensorValue? temperatureInt, humidityInt;
  SensorValue? temperatureExt, humidityExt;
  SensorValue? soundFreqInt, soundAmpInt;
  SensorValue? soundFreqExt, soundAmpExt;
  SensorValue? lightExt;
  BeehiveLatest.fromJson(Map json)
}

class Beehive {
  String id;
  String? name;
  String? source;
  BeehiveLatest? latest;
  Beehive.fromJson(Map json)
}

class ChartSeries {
  List<String> labels;
  List<double?> data;
}

class HiveChartData {
  ChartSeries? temperatureInt, temperatureExt;
  ChartSeries? humidityInt, humidityExt;
  ChartSeries? soundFreqInt, soundFreqExt;
  ChartSeries? soundAmpInt, soundAmpExt;
  ChartSeries? lightExt;
  HiveChartData.fromJson(Map json)
}
```

---

## Service API (`mobile/lib/services/api_service.dart`)

```dart
class ApiService {
  Dio _dio;

  // Dio interceptor : ajoute automatiquement "Authorization: Bearer <token>"
  // à toutes les requêtes (sauf login)

  Future<Map> login(String serverUrl, String username, String password)
    // POST {serverUrl}/api/auth/login
    // Body : {username, password}
    // Response : {token, username, role, expires_at}
    // Sauvegarde serverUrl + token + username via StorageService

  Future<void> logout()
    // POST {serverUrl}/api/auth/logout (best-effort)
    // Efface la session StorageService

  Future<List<Beehive>> getBeehives()
    // GET {serverUrl}/api/beehives
    // Header : Authorization: Bearer <JWT>
    // Response : {beehives: [...]}

  Future<HiveChartData> getBeehiveData(String beehiveId, {String range = '24h'})
    // GET {serverUrl}/api/beehives/{id}/data?range={range}
    // Header : Authorization: Bearer <JWT>
}
```

**Timeouts** : connect 10s, receive 15s.

**Erreurs** : `ApiException(message, statusCode)` levée sur toute erreur HTTP.

---

## Stockage sécurisé (`mobile/lib/services/storage_service.dart`)

`FlutterSecureStorage` chiffre les valeurs via **Android Keystore** (Android) et **Keychain** (iOS).

**Clés stockées** :

| Clé | Type | Description |
|---|---|---|
| `server_url` | String | URL base du server/ |
| `auth_token` | String | JWT actif |
| `username` | String | Nom d'utilisateur connecté |
| `notifications_on` | bool | Alertes activées |
| `threshold_temp_max` | double | Seuil max température (défaut 38.0) |
| `threshold_temp_min` | double | Seuil min température (défaut 28.0) |
| `threshold_hum_max` | double | Seuil max humidité (défaut 85.0) |
| `threshold_hum_min` | double | Seuil min humidité (défaut 40.0) |

```dart
await storage.saveSession(serverUrl, token, username)
await storage.clearSession()
bool loggedIn = await storage.isLoggedIn()
await storage.saveNotificationSettings({
  'enabled': true,
  'threshold_temp_max': 38.0,
  'threshold_temp_min': 28.0,
  'threshold_hum_max': 85.0,
  'threshold_hum_min': 40.0,
})
Map settings = await storage.getNotificationSettings()
```

---

## State management — Riverpod (`mobile/lib/providers/`)

### `authProvider` (`auth_provider.dart`)

```dart
enum AuthStatus { unknown, authenticated, unauthenticated }

class AuthState {
  AuthStatus status;
  String?    username;
  String?    error;
}

class AuthNotifier extends StateNotifier<AuthState> {
  AuthNotifier() {
    _checkSession();   // Vérifie le token au démarrage
  }

  Future<void> login(serverUrl, username, password) async {
    // → ApiService.login()
    // → StorageService.saveSession()
    // → state = AuthState(status: authenticated, username: username)
  }

  Future<void> logout() async {
    // → ApiService.logout()
    // → StorageService.clearSession()
    // → state = AuthState(status: unauthenticated)
  }
}

final authProvider = StateNotifierProvider<AuthNotifier, AuthState>
```

### `beehivesProvider` (`beehives_provider.dart`)

```dart
final beehivesProvider = FutureProvider<List<Beehive>>((ref) async {
  return ApiService().getBeehives();
});

final hiveChartProvider = FutureProvider.family<
    HiveChartData, ({String id, String range})>((ref, params) async {
  return ApiService().getBeehiveData(params.id, range: params.range);
});
```

Rafraîchissement : `ref.invalidate(beehivesProvider)` depuis le `RefreshIndicator` du dashboard.

---

## Écrans (`mobile/lib/screens/`)

### `LoginScreen`

Champs : Server URL, Username, Password.  
Validation : l'URL doit commencer par `http://` ou `https://`.  
Submit : `ref.read(authProvider.notifier).login(url, user, pass)`.  
Erreur : affichée si `authState.error != null`.

### `DashboardScreen`

`ListView` de `BeehiveCard` avec `RefreshIndicator`.  
Appelle `ref.watch(beehivesProvider)`.  
Pull-to-refresh : `ref.invalidate(beehivesProvider)`.

### `HiveDetailScreen`

Reçoit `id` en paramètre GoRouter.  
5 `LineChartCard` (température, humidité, fréquence sonore, amplitude, luminosité).  
Sélecteur de plage via `FilterChip` : 1h / 6h / 24h / 7d / 30d.  
Appelle `ref.watch(hiveChartProvider((id: id, range: selectedRange)))`.

### `SettingsScreen`

- Informations du compte (username, server URL)
- Bouton déconnexion avec dialog de confirmation
- Toggle notifications
- Sliders seuils : temp max/min (0–60°C), hum max/min (0–100%)

---

## Widgets (`mobile/lib/widgets/`)

### `BeehiveCard`

Carte par ruche sur le dashboard. Affiche un `GridView.count` 3×3 pour les 9 mesures scalaires, avec `StatusDot` basé sur la température intérieure.

### `LineChartCard`

Wraps `fl_chart` `LineChart`. Supporte les séries multiples (ex. temp int + ext sur le même graphe). Ticks X : premier / milieu / dernier point. Tooltip : valeur + unité.

### `StatusDot`

Indicateur coloré (vert / orange / rouge / gris) calculé à partir de la valeur de température.

---

## Thème (`mobile/lib/theme/app_theme.dart`)

Aligné sur le design system de `app/` et `server/`.

```dart
const kAmber      = Color(0xFFF5CB5C);   // Amber principal
const kAmberDark  = Color(0xFFFFB300);   // Amber foncé (hover)
const kDarkSurface = Color(0xFF333533);  // Sidebar / AppBar
const kPageBg     = Color(0xFFE8EDDF);   // Fond de page (beige clair)
const kStatusOk   = Color(0xFF639922);   // Vert statut OK
const kStatusWarn = Color(0xFFF97316);   // Orange avertissement
const kStatusCrit = Color(0xFFE24B4A);   // Rouge critique
```

`buildTheme()` retourne un `ThemeData` Material 3 avec :
- `colorSchemeSeed` amber, `brightness` light
- `AppBarTheme` fond `kDarkSurface`, icônes blanches
- `CardTheme` avec élévation 2 et radius 12

---

## Configuration Android (`mobile/android/`)

```gradle
// app/build.gradle
applicationId  : "fr.esiee.beetter"
minSdk         : 26   (Android 8.0)
targetSdk      : 34
compileSdk     : 34
```

```xml
<!-- AndroidManifest.xml permissions -->
INTERNET
POST_NOTIFICATIONS
RECEIVE_BOOT_COMPLETED  (WorkManager persist across reboots)
```

---

## Configuration iOS (`mobile/ios/`)

**Bundle ID** : `fr.esiee.beetter`  
**Min iOS** : 12.0

```xml
<!-- Info.plist -->
<key>NSLocalNetworkUsageDescription</key>
<string>Beetter connects to your local Beetter server...</string>
```

---

## Build et déploiement

### Android

```bash
cd mobile
flutter pub get
flutter build apk --release
# APK : build/app/outputs/flutter-apk/app-release.apk
adb install build/app/outputs/flutter-apk/app-release.apk

# Bundle pour le Play Store
flutter build appbundle --release
# AAB : build/app/outputs/bundle/release/app-release.aab
```

### iOS (macOS + Xcode requis)

```bash
cd mobile
flutter pub get
open ios/Runner.xcworkspace   # ou flutter run -d <device_id>
flutter build ios --release   # archive dans Xcode pour TestFlight / App Store
```

### Développement

```bash
flutter run                   # debug sur appareil connecté / émulateur
flutter run --release         # mode release sur appareil connecté
flutter test                  # tests unitaires
```
