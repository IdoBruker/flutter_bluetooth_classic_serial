# Project Overview

This is a Flutter plugin for Bluetooth Classic communication, supporting Android, iOS, and Windows. The plugin provides functionalities for device discovery, connection management, and data transmission.

The project is structured as a standard Flutter plugin, with a `lib` directory containing the Dart code and platform-specific implementations in the `android`, `ios`, and `windows` directories.

## Building and Running

### Dependencies

To use this plugin, add it to your `pubspec.yaml`:

```yaml
dependencies:
  flutter_bluetooth_classic_serial: ^1.0.3
```

Then run `flutter pub get`.

### Permissions

**Android:**

Add the following permissions to your `android/app/src/main/AndroidManifest.xml`:

```xml
<uses-permission android:name="android.permission.BLUETOOTH" />
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
<uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-permission android:name="android.permission.BLUETOOTH_ADVERTISE" />
```

**iOS:**

Add the following keys to your `ios/Runner/Info.plist`:

```xml
<key>NSBluetoothAlwaysUsageDescription</key>
<string>This app needs Bluetooth access to communicate with devices</string>
<key>NSBluetoothPeripheralUsageDescription</key>
<string>This app needs Bluetooth access to communicate with devices</string>
```

### Running the example

The `example` directory contains a sample application demonstrating the plugin's usage. To run it, navigate to the `example` directory and run:

```bash
flutter run
```

## Development Conventions

### Code Style

The project follows the standard Dart and Flutter linting rules.

### Platform Implementations

- **Android:** The Android implementation uses the native Android Bluetooth APIs. The main logic is in `lib/android-implementation.kt`.

- **iOS:** The iOS implementation uses the `ExternalAccessory` framework, which is designed for MFi (Made for iPhone/iPod/iPad) accessories. The main logic is in `ios/Classes/SwiftFlutterBluetoothClassicPlugin.swift`.

- **Windows:** The Windows implementation uses the Windows Runtime (WinRT) Bluetooth APIs. The main logic is in `windows/bluetooth_manager.cpp` and `windows/bluetooth_manager.h`.

### Communication

The plugin uses method channels to communicate between the Dart code and the platform-specific implementations. The channel names are defined in `lib/flutter_bluetooth_classic_platform_interface.dart`.
