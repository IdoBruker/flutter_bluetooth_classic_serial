# iOS Entitlements Configuration for External Accessory

## Overview

This project requires iOS entitlements to access External Accessory devices (Labdisc, MiniDisc, DataHub, Forceacc) via Bluetooth. The entitlements file has been created, but it must be linked in Xcode.

## Why Entitlements are Required

iOS restricts access to External Accessory devices for security and privacy. The `com.apple.external-accessory.wireless-configuration` entitlement grants your app permission to:

- Discover connected MFi (Made for iPhone/iPad) Bluetooth accessories
- Communicate with these accessories using the External Accessory framework
- Access the `EAAccessoryManager.shared().connectedAccessories` list

Without this entitlement, `connectedAccessories` will always return an empty array, even if devices are paired.

## Setup Steps

### For the Plugin (ios/Runner)

1. Open `ios/Runner.xcworkspace` in Xcode (NOT the .xcodeproj file)
2. Select the **Runner** target in the project navigator
3. Go to the **Signing & Capabilities** tab
4. Verify that `Runner.entitlements` is listed under the entitlements file
5. If not listed:
   - Click the **+ Capability** button
   - Search for "External Accessory Communication"
   - Add it to your target
   - Or manually link the entitlements file:
     - Go to **Build Settings** → Search for "Code Signing Entitlements"
     - Set the value to: `Runner/Runner.entitlements`

### For the Example App (example/ios/Runner)

1. Open `example/ios/Runner.xcworkspace` in Xcode
2. Select the **Runner** target
3. Go to the **Signing & Capabilities** tab
4. Follow the same steps as above to link `Runner/Runner.entitlements`

## Verification

After linking the entitlements:

1. Clean the build folder: **Product → Clean Build Folder** (Cmd+Shift+K)
2. Build and run the app
3. Check the console logs for:
   ```
   [ExternalAccessory] Returning connectedAccessories count: X
   [ExternalAccessory] Accessory name: ...
   ```
4. The count should now be > 0 if you have a paired Labdisc device

## Required Info.plist Configuration

The following keys must be present in your `Info.plist` (already configured):

```xml
<key>UISupportedExternalAccessoryProtocols</key>
<array>
    <string>Tigaro.com</string>
</array>

<key>NSBluetoothAlwaysUsageDescription</key>
<string>This app needs Bluetooth to communicate with external devices</string>

<key>NSBluetoothPeripheralUsageDescription</key>
<string>This app needs Bluetooth to communicate with external devices</string>
```

## Protocol Detection

The implementation uses flexible protocol matching (like the legacy GlobiLab code):

- Searches for any protocol string containing "igaro" (case-insensitive)
- Legacy reference: `EAConnectionController.m` line 117
- This allows the app to work with various protocol string formats (e.g., "Tigaro.com", "tigaro.com", etc.)

## Troubleshooting

### Still getting count 0?

1. **Check device pairing**: Go to iOS Settings → Bluetooth, verify the device is paired
2. **Check entitlements**: In Xcode, verify the entitlements file is properly linked
3. **Check protocol strings**: Run the app and check logs for the device's advertised protocols
4. **Update Info.plist**: Add all protocol strings to `UISupportedExternalAccessoryProtocols` array
5. **Check device**: Ensure the device is MFi-certified and advertising External Accessory protocols

### Protocol not supported error?

The device's protocol string must contain "igaro" (case-insensitive). Check the device logs to see what protocols it's advertising, then update:

- `PROTOCOL_SUBSTRING` in `SwiftFlutterBluetoothClassicPlugin.swift`
- Add the protocol string to `Info.plist` → `UISupportedExternalAccessoryProtocols`

## Additional Resources

- [Apple: External Accessory Framework](https://developer.apple.com/documentation/externalaccessory)
- [Apple: Communicating with External Accessories](https://developer.apple.com/documentation/externalaccessory/communicating_with_external_accessories)
- [MFi Program](https://mfi.apple.com/)
