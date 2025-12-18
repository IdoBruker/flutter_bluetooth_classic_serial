import 'dart:async';
import 'dart:js_interop';
import 'dart:typed_data';

import 'package:flutter_bluetooth_classic_serial/flutter_bluetooth_classic_platform_interface.dart';
import 'package:flutter_web_plugins/flutter_web_plugins.dart';
import 'package:web/web.dart' as web;

// --- Web Bluetooth API Definitions ---
// Since package:web doesn't fully support Web Bluetooth yet, we define the interop types here.

extension type BluetoothNavigator(web.Navigator navigator) implements JSObject {
  @JS('bluetooth')
  external Bluetooth? get bluetooth;
}

@JS('Bluetooth')
extension type Bluetooth._(JSObject _) implements JSObject {
  external JSPromise<JSBoolean> getAvailability();
  external JSPromise<BluetoothDevice> requestDevice(RequestDeviceOptions options);
}

@JS()
@anonymous
extension type RequestDeviceOptions._(JSObject _) implements JSObject {
  external factory RequestDeviceOptions({
    JSArray<BluetoothLEScanFilter>? filters,
    JSArray<JSString>? optionalServices,
    JSBoolean? acceptAllDevices,
  });
}

@JS()
@anonymous
extension type BluetoothLEScanFilter._(JSObject _) implements JSObject {
  external factory BluetoothLEScanFilter({
    JSArray<JSString>? services,
    JSString? name,
    JSString? namePrefix,
  });
}

@JS('BluetoothDevice')
extension type BluetoothDevice._(JSObject _) implements JSObject {
  external String get id;
  external String? get name;
  external BluetoothRemoteGATTServer? get gatt;
}

@JS('BluetoothRemoteGATTServer')
extension type BluetoothRemoteGATTServer._(JSObject _) implements JSObject {
  external BluetoothDevice get device;
  external JSBoolean get connected;
  external JSPromise<BluetoothRemoteGATTServer> connect();
  external void disconnect();
  external JSPromise<BluetoothRemoteGATTService> getPrimaryService(JSAny service);
}

@JS('BluetoothRemoteGATTService')
extension type BluetoothRemoteGATTService._(JSObject _) implements JSObject {
  external String get uuid;
  external JSBoolean get isPrimary;
  external JSPromise<BluetoothRemoteGATTCharacteristic> getCharacteristic(JSAny characteristic);
}

@JS('BluetoothRemoteGATTCharacteristic')
extension type BluetoothRemoteGATTCharacteristic._(JSObject _) implements JSObject {
  external String get uuid;
  external BluetoothRemoteGATTService get service;
  external JSDataView? get value; 
  external JSPromise<BluetoothRemoteGATTCharacteristic> startNotifications();
  external JSPromise<JSAny?> readValue();
  external JSPromise<JSAny?> writeValue(JSAny value);
  external void addEventListener(String type, JSFunction listener);
  external void removeEventListener(String type, JSFunction listener);
}

// --- End Web Bluetooth API Definitions ---

class FlutterBluetoothClassicWeb extends FlutterBluetoothClassicPlatform {
  static void registerWith(Registrar registrar) {
    FlutterBluetoothClassicPlatform.instance = FlutterBluetoothClassicWeb();
  }

  // UUIDs for Nordic UART Service (NUS) - Standard for Serial over BLE
  static const String _uartServiceUuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
  static const String _uartRxCharacteristicUuid = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"; // Notify
  static const String _uartTxCharacteristicUuid = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"; // Write

  // State controllers
  final _stateController = StreamController<Map<String, dynamic>>.broadcast();
  final _connectionController = StreamController<Map<String, dynamic>>.broadcast();
  final _dataController = StreamController<Map<String, dynamic>>.broadcast();

  // Connected device reference
  BluetoothDevice? _connectedDevice;
  BluetoothRemoteGATTCharacteristic? _txCharacteristic;
  BluetoothRemoteGATTCharacteristic? _rxCharacteristic;
  JSFunction? _rxListener;
  JSFunction? _disconnectListener;

  // Storage for discovered devices (Web Bluetooth device objects)
  final Map<String, BluetoothDevice> _discoveredDevices = {};

  @override
  Stream<Map<String, dynamic>> get stateStream => _stateController.stream;

  @override
  Stream<Map<String, dynamic>> get connectionStream => _connectionController.stream;

  @override
  Stream<Map<String, dynamic>> get dataStream => _dataController.stream;

  Bluetooth? get _bluetooth => BluetoothNavigator(web.window.navigator).bluetooth;

  @override
  Future<bool> isBluetoothSupported() async {
    return _bluetooth != null;
  }

  @override
  Future<bool> isBluetoothEnabled() async {
    if (_bluetooth == null) return false;
    try {
      final availability = await _bluetooth!.getAvailability().toDart;
      return availability.toDart;
    } catch (e) {
      return false;
    }
  }

  @override
  Future<bool> enableBluetooth() async {
    // Web cannot programmatically enable Bluetooth
    return await isBluetoothEnabled();
  }

  @override
  Future<List<Map<String, dynamic>>> getPairedDevices() async {
    // Web Bluetooth doesn't expose a list of paired devices without user interaction
    // We can only return the currently connected device if any
    if (_connectedDevice != null && _connectedDevice!.gatt?.connected.toDart == true) {
      return [
        {
          "name": _connectedDevice!.name ?? "Unknown",
          "address": _connectedDevice!.id,
          "paired": true
        }
      ];
    }
    return [];
  }

  @override
  Future<bool> startDiscovery() async {
    if (_bluetooth == null) return false;

    try {
      final options = RequestDeviceOptions(
        filters: [
          BluetoothLEScanFilter(services: [_uartServiceUuid.toJS].toJS)
        ].toJS,
        optionalServices: [_uartServiceUuid.toJS].toJS,
      );

      final device = await _bluetooth!.requestDevice(options).toDart;
      
      _discoveredDevices[device.id] = device;

      _stateController.add({
        "event": "deviceFound",
        "device": {
          "name": device.name ?? "Unknown",
          "address": device.id,
          "paired": false
        }
      });

      return true;
    } catch (e) {
      print("Web Bluetooth discovery error: $e");
      return false;
    }
  }

  @override
  Future<bool> stopDiscovery() async {
    // Web Bluetooth discovery stops as soon as the picker is closed/device selected
    return true;
  }

  @override
  Future<bool> connect(String address) async {
    final device = _discoveredDevices[address];
    if (device == null) {
      print("Device not found in cache. Must call startDiscovery first on Web.");
      return false;
    }

    try {
      // Connect to GATT
      if (device.gatt == null) return false;
      
      // Note: connect() is usually on the GATT server
      final server = await device.gatt!.connect().toDart;
      _connectedDevice = device;

      // Get Service
      final service = await server.getPrimaryService(_uartServiceUuid.toJS).toDart;

      // Get Characteristics
      _txCharacteristic = await service.getCharacteristic(_uartTxCharacteristicUuid.toJS).toDart;
      _rxCharacteristic = await service.getCharacteristic(_uartRxCharacteristicUuid.toJS).toDart;

      // Start Notifications for RX
      if (_rxCharacteristic != null) {
        await _rxCharacteristic!.startNotifications().toDart;
        
        _rxListener = ((JSObject e) {
           final target = e.getProperty('target'.toJS) as BluetoothRemoteGATTCharacteristic;
           final value = target.value;
           
           if (value != null) {
             final data = Uint8List(value.byteLength.toDartInt);
             for(int i=0; i<value.byteLength.toDartInt; i++) {
               data[i] = value.getUint8(i.toJS).toDartInt;
             }
             
             _dataController.add({
               "deviceAddress": device.id,
               "data": data.toList() // Convert to List<int>
             });
           }
        }).toJS;

        _rxCharacteristic!.addEventListener('characteristicvaluechanged', _rxListener!);
      }

      // Notify connection success
      _connectionController.add({
        "isConnected": true,
        "deviceAddress": device.id,
        "status": "CONNECTED"
      });
      
      // Handle disconnection
      _disconnectListener = ((JSObject e) {
        _connectionController.add({
          "isConnected": false,
          "deviceAddress": device.id,
          "status": "DISCONNECTED"
        });
        
        // Cleanup listeners
        if (_rxCharacteristic != null && _rxListener != null) {
           _rxCharacteristic!.removeEventListener('characteristicvaluechanged', _rxListener!);
        }
        if (_connectedDevice != null && _disconnectListener != null) {
           // device.removeEventListener('gattserverdisconnected', _disconnectListener!);
           // Note: BluetoothDevice inherits from EventTarget, need to add addEventListener to BluetoothDevice definition if needed
           // For now assuming we bound it to something that works or using the event stream provider if available
           // Since I removed EventStreamProviders usage to rely on direct addEventListener, I need to add it to BluetoothDevice definition too.
        }

        _connectedDevice = null;
        _txCharacteristic = null;
        _rxCharacteristic = null;
        _rxListener = null;
        _disconnectListener = null;
      }).toJS;

      // Add listener to device
      // Casting to JSAny to call addEventListener because I didn't add it to BluetoothDevice interface yet
      (device as JSObject).callMethod('addEventListener'.toJS, 'gattserverdisconnected'.toJS, _disconnectListener!);

      return true;
    } catch (e) {
      print("Web connection error: $e");
      _connectionController.add({
        "isConnected": false,
        "deviceAddress": address,
        "status": "ERROR: $e"
      });
      return false;
    }
  }

  @override
  Future<bool> listen() async {
    // Web cannot act as a server
    return false; 
  }

  @override
  Future<bool> stopListen() async {
    return true;
  }

  @override
  Future<bool> disconnect() async {
    if (_connectedDevice != null && _connectedDevice!.gatt != null) {
      _connectedDevice!.gatt!.disconnect();
      return true;
    }
    return false;
  }

  @override
  Future<bool> sendData(Uint8List data) async {
    if (_txCharacteristic == null) return false;
    try {
      // writeValue expects a BufferSource (ArrayBuffer or ArrayBufferView)
      // Uint8List is a view, so it should work or might need conversion
      await _txCharacteristic!.writeValue(data.toJS).toDart;
      return true;
    } catch (e) {
      print("Error sending data: $e");
      return false;
    }
  }
}

extension on JSObject {
  JSAny? getProperty(JSAny key) => (this as JSAny).getProperty(key);
  JSAny? callMethod(JSAny method, [JSAny? arg1, JSAny? arg2]) => (this as JSAny).callMethod(method, arg1, arg2);
}

extension on JSAny {
  external JSAny? getProperty(JSAny key);
  external JSAny? callMethod(JSAny method, [JSAny? arg1, JSAny? arg2]);
}

extension on JSDataView {
   external JSNumber get byteLength;
   external JSNumber getUint8(JSNumber byteOffset);
}
