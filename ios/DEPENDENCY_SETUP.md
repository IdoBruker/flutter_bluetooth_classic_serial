# Setup Instructions - Using as a Dependency

## Important! Read This First

**This document is for developers using flutter_bluetooth_classic_serial as a dependency in their own Flutter project.**

If you're developing the plugin itself, see `ENTITLEMENTS_SETUP.md`.

---

## Overview

When using this plugin as a dependency, you need to add entitlements to **YOUR app**, not to the plugin itself.

### What You Need to Do:

1. ✅ Add entitlements file to your iOS project
2. ✅ Update your app's Info.plist
3. ✅ Link the entitlements in Xcode

---

## Step 1: Add the Plugin to pubspec.yaml

In your Flutter project:

```yaml
dependencies:
  flutter_bluetooth_classic_serial:
    git: https://github.com/IdoBruker/flutter_bluetooth_classic_serial
```

Run:

```bash
flutter pub get
```

---

## Step 2: Create Entitlements File in Your App

### 2.1 Create the File

Navigate to your project directory and create:

```bash
YOUR_APP/ios/Runner/Runner.entitlements
```

### 2.2 File Contents

Copy the following content:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>com.apple.external-accessory.wireless-configuration</key>
	<true/>
</dict>
</plist>
```

---

## Step 3: Update Info.plist in Your App

### 3.1 Open the File

```bash
YOUR_APP/ios/Runner/Info.plist
```

### 3.2 Add These Entries

If they don't exist, add before the final `</dict>`:

```xml
<key>UISupportedExternalAccessoryProtocols</key>
<array>
    <string>Tigaro.com</string>
</array>

<key>NSBluetoothAlwaysUsageDescription</key>
<string>This app needs Bluetooth to communicate with external devices (Labdisc, MiniDisc, DataHub, Forceacc)</string>

<key>NSBluetoothPeripheralUsageDescription</key>
<string>This app needs Bluetooth to communicate with external devices (Labdisc, MiniDisc, DataHub, Forceacc)</string>
```

**Note:** Customize the description text for your app's specific use case.

---

## Step 4: Link Entitlements in Xcode

### 4.1 Open Your Project in Xcode

```bash
cd YOUR_APP
open ios/Runner.xcworkspace
```

**Important:** Open `.xcworkspace` not `.xcodeproj`

### 4.2 Select Your Target

1. Click the blue project in the sidebar (Project Navigator)
2. Under **TARGETS**, select your app (usually named Runner)

### 4.3 Add the Capability

**Option A (Recommended) - Via Capabilities:**

1. Click the **Signing & Capabilities** tab
2. Click **+ Capability** in the top-left corner
3. Search for "**External Accessory Communication**"
4. Click it to add
5. Xcode will automatically link the entitlements file

**Option B - Manual Linking:**

1. Go to **Build Settings**
2. Search for "**Code Signing Entitlements**"
3. Add the value: `Runner/Runner.entitlements`
4. Do this for Debug, Release, and Profile

### 4.4 Verify

1. Return to **Signing & Capabilities**
2. Verify you see: **Code Signing Entitlements: Runner/Runner.entitlements**
3. Verify `Runner.entitlements` appears in Project Navigator

---

## Step 5: Clean and Build

### 5.1 Clean the Project

In Xcode:

- **Product** → **Clean Build Folder** (or **Cmd+Shift+K**)

### 5.2 Build and Run

```bash
flutter clean
flutter pub get
flutter run
```

Or in Xcode:

- Click the Run button (or **Cmd+R**)

---

## Step 6: Testing

### 6.1 Pair a Device

1. iOS Settings → Bluetooth
2. Pair your Labdisc device
3. Verify it shows as "Connected"

### 6.2 Run Your App and Check Logs

If everything works, you should see in the console:

```
[ExternalAccessory] Starting discovery, found 1 connected accessories
[ExternalAccessory] Accessory: Labdisc-12345, serial: XXXXX
[ExternalAccessory] Protocols: ["Tigaro.com"]
[ExternalAccessory] Device Labdisc-12345 is supported
```

Instead of:

```
[#ExternalAccessory] Returning connectedAccessories count 0
```

---

## Your Folder Structure

After setup, it should look like this:

```
YOUR_APP/
├── ios/
│   ├── Runner/
│   │   ├── Info.plist                 ✅ Updated
│   │   ├── Runner.entitlements        ✅ Added
│   │   └── ...
│   └── Runner.xcworkspace/
├── lib/
├── pubspec.yaml                        ✅ Contains the plugin
└── ...
```

---

## Troubleshooting

### Still Getting count 0?

#### 1. ✅ Verify Device is Paired

iOS Settings → Bluetooth → Ensure Labdisc is Connected (not just Paired)

#### 2. ✅ Check Entitlements

In Xcode → Signing & Capabilities:

- Verify "External Accessory Communication" appears, or
- Verify "Code Signing Entitlements: Runner/Runner.entitlements" is shown

#### 3. ✅ Check Info.plist

Verify `UISupportedExternalAccessoryProtocols` and `NSBluetoothAlwaysUsageDescription` exist in **YOUR app** (not in the plugin).

#### 4. ✅ Clean Everything

```bash
# Clean Flutter
flutter clean

# Delete build artifacts
rm -rf ios/Pods
rm -rf ios/.symlinks
rm ios/Podfile.lock

# Reinstall
flutter pub get
cd ios && pod install && cd ..

# Rebuild
flutter run
```

#### 5. ✅ Delete App from Device

Sometimes permissions don't update. Delete the app from your device and reinstall.

#### 6. ✅ Use a Physical Device

External Accessory **does NOT work in the simulator**. You must use a physical iOS device.

---

## Common Errors

### "Protocol not supported"

The device doesn't support the expected protocol. Check logs:

```
[ExternalAccessory] No supported protocol found for accessory: Labdisc-XXX,
available protocols: ["SomeOtherProtocol.com"]
```

**Solution:**

1. Add the actual protocol to your `Info.plist`:

```xml
<key>UISupportedExternalAccessoryProtocols</key>
<array>
    <string>Tigaro.com</string>
    <string>SomeOtherProtocol.com</string>  <!-- Add the protocol from logs -->
</array>
```

### "Device not found"

The device is not paired or not connected.

**Solution:**

- Settings → Bluetooth → Ensure device is connected
- Turn Bluetooth off and on
- Unpair and re-pair the device

### App Crashes on Launch

Check that you have the permissions in `Info.plist`:

- `NSBluetoothAlwaysUsageDescription`
- `NSBluetoothPeripheralUsageDescription`

iOS requires these descriptions, or the app will crash.

---

## Usage Example

```dart
import 'package:flutter_bluetooth_classic_serial/flutter_bluetooth_classic.dart';

// Get paired devices
List<BluetoothDevice> devices = await FlutterBluetoothClassic.getPairedDevices();

print('Found ${devices.length} devices');

// Connect to device
if (devices.isNotEmpty) {
  bool connected = await FlutterBluetoothClassic.connect(devices[0].address);
  print('Connected: $connected');
}
```

---

## Additional Links

- 📄 Plugin Documentation: `README.md`
- 📄 Technical Documentation: `ios/ENTITLEMENTS_SETUP.md`
- 🔧 Implementation Changes: `ios/README_CHANGES.md`
- 🇮🇱 Hebrew Instructions: `ios/DEPENDENCY_SETUP_HE.md`

---

## Support

If you still have issues:

1. Run the app in Debug mode
2. Include all console logs
3. Screenshot of Signing & Capabilities in Xcode
4. Verify your device is MFi-certified

---

**Version:** 1.0.3  
**Last Updated:** November 2025
