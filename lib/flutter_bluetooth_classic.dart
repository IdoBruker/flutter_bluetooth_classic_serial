import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'flutter_bluetooth_classic_platform_interface.dart';

class FlutterBluetoothClassic {
  // Singleton instance
  static FlutterBluetoothClassic? _instance;

  // Stream controllers
  final _stateStreamController = StreamController<BluetoothState>.broadcast();
  final _connectionStreamController =
      StreamController<BluetoothConnectionState>.broadcast();
  final _dataStreamController = StreamController<BluetoothData>.broadcast();
  final _discoveredDevicesController =
      StreamController<BluetoothDevice>.broadcast();

  // Public streams that can be subscribed to
  Stream<BluetoothState> get onStateChanged => _stateStreamController.stream;
  Stream<BluetoothConnectionState> get onConnectionChanged =>
      _connectionStreamController.stream;
  Stream<BluetoothData> get onDataReceived => _dataStreamController.stream;
  Stream<BluetoothDevice> get onDeviceDiscovered =>
      _discoveredDevicesController.stream;

  String _appName = "";

  /// Factory constructor to maintain a single instance of the class
  factory FlutterBluetoothClassic(String appName) {
    _instance ??= FlutterBluetoothClassic._(appName);
    return _instance!;
  }

  /// Private constructor that sets up the event listeners
  FlutterBluetoothClassic._(String appName) {
    _appName = appName;
    
    // Listen for state changes
    FlutterBluetoothClassicPlatform.instance.stateStream.listen((event) {
      // Check if this is a special event (deviceFound, permissionResult, etc.)
      if (event.containsKey('event')) {
        final String eventType = event['event'];

        if (eventType == 'deviceFound') {
          // Handle device discovery
          final deviceMap = event['device'];
          if (deviceMap != null) {
            final device = BluetoothDevice.fromMap(deviceMap);
            _discoveredDevicesController.add(device);
          }
        }
        // Other event types can be handled here if needed
      } else {
        // Regular state change event
        _stateStreamController.add(BluetoothState.fromMap(event));
      }
    });

    // Listen for connection changes
    FlutterBluetoothClassicPlatform.instance.connectionStream.listen((event) {
      _connectionStreamController.add(BluetoothConnectionState.fromMap(event));
    });

    // Listen for data received
    FlutterBluetoothClassicPlatform.instance.dataStream.listen((event) {
      _dataStreamController.add(BluetoothData.fromMap(event));
    });
  }

  /// Check if Bluetooth is supported on the device
  Future<bool> isBluetoothSupported() async {
    try {
      return await FlutterBluetoothClassicPlatform.instance.isBluetoothSupported();
    } catch (e) {
      throw BluetoothException('Failed to check Bluetooth support: $e');
    }
  }

  /// Check if Bluetooth is enabled
  Future<bool> isBluetoothEnabled() async {
    try {
      return await FlutterBluetoothClassicPlatform.instance.isBluetoothEnabled();
    } catch (e) {
      throw BluetoothException('Failed to check Bluetooth status: $e');
    }
  }

  /// Request to enable Bluetooth
  Future<bool> enableBluetooth() async {
    try {
      return await FlutterBluetoothClassicPlatform.instance.enableBluetooth();
    } catch (e) {
      throw BluetoothException('Failed to enable Bluetooth: $e');
    }
  }

  /// Get paired devices
  Future<List<BluetoothDevice>> getPairedDevices() async {
    try {
      final List<Map<String, dynamic>> devices =
          await FlutterBluetoothClassicPlatform.instance.getPairedDevices();
      return devices.map((device) => BluetoothDevice.fromMap(device)).toList();
    } catch (e) {
      throw BluetoothException('Failed to get paired devices: $e');
    }
  }

  /// Start discovery for nearby Bluetooth devices
  Future<bool> startDiscovery() async {
    try {
      return await FlutterBluetoothClassicPlatform.instance.startDiscovery();
    } catch (e) {
      throw BluetoothException('Failed to start discovery: $e');
    }
  }

  /// Stop discovery
  Future<bool> stopDiscovery() async {
    try {
      return await FlutterBluetoothClassicPlatform.instance.stopDiscovery();
    } catch (e) {
      throw BluetoothException('Failed to stop discovery: $e');
    }
  }

  /// Connect to a device
  Future<bool> connect(String address) async {
    try {
      return await FlutterBluetoothClassicPlatform.instance.connect(address);
    } catch (e) {
      throw BluetoothException('Failed to connect to device: $e');
    }
  }

  Future<bool> listen() async {
    try {
      return await FlutterBluetoothClassicPlatform.instance.listen();
    } catch (e) {
      throw BluetoothException('Failed to listen for incoming connections: $e');
    }
  }

  Future<bool> stopListen() async {
    try {
      return await FlutterBluetoothClassicPlatform.instance.stopListen();
    } catch (e) {
      throw BluetoothException('Failed to stop bluetooth server: $e');
    }
  }

  /// Disconnect from a device
  Future<bool> disconnect() async {
    try {
      return await FlutterBluetoothClassicPlatform.instance.disconnect();
    } catch (e) {
      throw BluetoothException('Failed to disconnect: $e');
    }
  }

  /// Send data to the connected device
  Future<bool> sendData(Uint8List data) async {
    try {
      return await FlutterBluetoothClassicPlatform.instance.sendData(data);
    } catch (e) {
      throw BluetoothException('Failed to send data: $e');
    }
  }

  /// Send string data to the connected device
  Future<bool> sendString(String message) async {
    try {
      final Uint8List data = Uint8List.fromList(utf8.encode(message));
      return await sendData(data);
    } catch (e) {
      throw BluetoothException('Failed to send string: $e');
    }
  }

  /// Dispose of resources
  void dispose() {
    _stateStreamController.close();
    _connectionStreamController.close();
    _dataStreamController.close();
    _discoveredDevicesController.close();
  }
}

// Data Models

class BluetoothException implements Exception {
  final String message;

  BluetoothException(this.message);

  @override
  String toString() => 'BluetoothException: $message';
}

class BluetoothState {
  final bool isEnabled;
  final String status;

  BluetoothState({required this.isEnabled, required this.status});

  factory BluetoothState.fromMap(dynamic map) {
    return BluetoothState(
      isEnabled: map['isEnabled'],
      status: map['status'],
    );
  }
}

class BluetoothDevice {
  final String name;
  final String address;
  final bool paired;

  BluetoothDevice({
    required this.name,
    required this.address,
    required this.paired,
  });

  factory BluetoothDevice.fromMap(dynamic map) {
    return BluetoothDevice(
      name: map['name'] ?? 'Unknown',
      address: map['address'],
      paired: map['paired'] ?? false,
    );
  }

  Map<String, dynamic> toMap() {
    return {
      'name': name,
      'address': address,
      'paired': paired,
    };
  }
}

class BluetoothConnectionState {
  final bool isConnected;
  final String deviceAddress;
  final String status;

  BluetoothConnectionState({
    required this.isConnected,
    required this.deviceAddress,
    required this.status,
  });

  factory BluetoothConnectionState.fromMap(dynamic map) {
    print(map);
    return BluetoothConnectionState(
      isConnected: map['isConnected'],
      deviceAddress: map['deviceAddress'],
      status: map['status'],
    );
  }
}

class BluetoothData {
  final String deviceAddress;
  final List<int> data;

  BluetoothData({
    required this.deviceAddress,
    required this.data,
  });

  String asString() {
    return utf8.decode(data, allowMalformed: true);
  }

  factory BluetoothData.fromMap(dynamic map) {
    print(map);
    return BluetoothData(
      deviceAddress: map['deviceAddress'],
      data: List<int>.from(map['data']),
    );
  }
}
