# Beetter Mobile

Flutter cross-platform app for Android and iOS.

## Setup

```bash
# Install Flutter SDK first: https://docs.flutter.dev/get-started/install
flutter pub get

# Android
flutter run                          # debug on connected device
flutter build apk --release          # release APK

# iOS (macOS + Xcode required)
flutter build ios --release
```

## Architecture

- **State**: Riverpod (`flutter_riverpod`)
- **Navigation**: go_router
- **HTTP**: Dio with automatic Bearer token injection
- **Storage**: flutter_secure_storage (encrypted)
- **Charts**: fl_chart
- **Notifications**: workmanager + flutter_local_notifications (15-min background check)

## Usage

On first launch, enter the server URL (e.g. `https://beetter-server.example.com`), your
username and password. The token is stored securely and used for all API calls.
