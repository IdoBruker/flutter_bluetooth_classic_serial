import 'dart:async';
import 'dart:js_interop';
import 'dart:typed_data';

import 'package:flutter_bluetooth_classic_serial/flutter_bluetooth_classic_platform_interface.dart';
import 'package:flutter_web_plugins/flutter_web_plugins.dart';

// --- Web Serial API Definitions ---
// For Bluetooth Classic devices using Serial Port Profile (SPP)

@JS('navigator.serial')
external Serial get serial;

@JS()
extension type Serial._(JSObject _) implements JSObject {
  external JSPromise<SerialPort> requestPort([JSSerialOptions? options]);
  external JSPromise<JSArray<SerialPort>> getPorts();
}

@JS()
extension type SerialPort._(JSObject _) implements JSObject {
  external JSPromise<JSAny?> open(JSSerialOptions options);
  external JSPromise<JSAny?> close();
  external SerialPortReadable? get readable;
  external SerialPortWritable? get writable;
  external bool get connected;
  external JSFunction? get ondisconnect;
  external set ondisconnect(JSFunction? value);
}

@JS()
@anonymous
extension type JSSerialOptions._(JSObject _) implements JSObject {
  external factory JSSerialOptions({
    JSArray<JSObject>? filters,
    int? baudRate,
  });
}

@JS()
@anonymous
extension type JSSerialPortFilter._(JSObject _) implements JSObject {
  external factory JSSerialPortFilter({
    String? bluetoothServiceClassId,
    int? usbVendorId,
    int? usbProductId,
  });
}

@JS()
extension type SerialPortReadable._(JSObject _) implements JSObject {
  external SerialReader getReader();
}

@JS()
extension type SerialReader._(JSObject _) implements JSObject {
  external JSPromise<JSSerialReadResult> read();
  external void releaseLock();
}

@JS()
extension type JSSerialReadResult._(JSObject _) implements JSObject {
  external JSUint8Array? get value;
  external bool get done;
}

@JS()
extension type SerialPortWritable._(JSObject _) implements JSObject {
  external SerialWriter getWriter();
}

@JS()
extension type SerialWriter._(JSObject _) implements JSObject {
  external JSPromise<JSAny?> write(JSUint8Array data);
  external void releaseLock();
}

// --- End Web Serial API Definitions ---

class FlutterBluetoothClassicWeb extends FlutterBluetoothClassicPlatform {
  static void registerWith(Registrar registrar) {
    FlutterBluetoothClassicPlatform.instance = FlutterBluetoothClassicWeb();
  }

  // State controllers
  final _stateController = StreamController<Map<String, dynamic>>.broadcast();
  final _connectionController =
      StreamController<Map<String, dynamic>>.broadcast();
  final _dataController = StreamController<Map<String, dynamic>>.broadcast();

  // Serial port references
  SerialPort? _port;
  SerialReader? _reader;
  SerialWriter? _writer;
  bool _isReading = false;
  bool _isConnecting = false;

  // Storage for discovered ports (Web Serial port objects)
  final Map<String, SerialPort> _discoveredPorts = {};

  void _cleanup() {
    _isReading = false;
    _isConnecting = false;

    if (_reader != null) {
      _reader!.releaseLock();
      _reader = null;
    }

    if (_writer != null) {
      _writer!.releaseLock();
      _writer = null;
    }

    _port = null;
  }

  void _startReading(String deviceAddress) async {
    if (_port == null) return;

    _isReading = true;

    try {
      while (_isReading) {
        final readable = _port!.readable;
        if (readable == null) {
          break;
        }

        _reader = readable.getReader();

        while (true) {
          final result = await _reader!.read().toDart;

          if (result.done) {
            // Reader has been canceled
            _reader!.releaseLock();
            break;
          }

          if (result.value != null) {
            // Convert JS Uint8Array to Dart Uint8List
            final data = result.value!.toDart;

            _dataController.add({
              "deviceAddress": deviceAddress,
              "data": data.toList() // Convert to List<int>
            });
          }
        }
      }
    } catch (e) {
      print("Serial read error: $e");

      // Send connection error event if reading fails
      _connectionController.add({
        "isConnected": false,
        "deviceAddress": deviceAddress,
        "status": "ERROR: Read failed - $e"
      });

      // Cleanup on read error
      _cleanup();
    }
  }

  @override
  Stream<Map<String, dynamic>> get stateStream => _stateController.stream;

  @override
  Stream<Map<String, dynamic>> get connectionStream =>
      _connectionController.stream;

  @override
  Stream<Map<String, dynamic>> get dataStream => _dataController.stream;

  @override
  Future<bool> isBluetoothSupported() async {
    // Check if Web Serial API is available
    try {
      // This will throw if serial is not available
      final _ = serial;

      // Send initial state event (Web Serial is "available" if supported)
      _stateController.add({"isEnabled": true, "status": "AVAILABLE"});

      return true;
    } catch (e) {
      // Send state event indicating Web Serial is not available
      _stateController.add({"isEnabled": false, "status": "NOT_SUPPORTED"});

      return false;
    }
  }

  @override
  Future<bool> isBluetoothEnabled() async {
    // Web Serial API doesn't have an "enabled" check like Bluetooth
    // For web, "enabled" means "supported and available"
    final isSupported = await isBluetoothSupported();

    // Send state event
    _stateController.add({
      "isEnabled": isSupported,
      "status": isSupported ? "AVAILABLE" : "NOT_SUPPORTED"
    });

    return isSupported;
  }

  @override
  Future<bool> enableBluetooth() async {
    // Web cannot programmatically enable Serial access
    return await isBluetoothEnabled();
  }

  @override
  Future<List<Map<String, dynamic>>> getPairedDevices() async {
    try {
      // Get already granted serial ports (previously paired Bluetooth Classic devices)
      final ports = await serial.getPorts().toDart;

      final devices = <Map<String, dynamic>>[];
      for (int i = 0; i < ports.length; i++) {
        final port = ports[i];
        // For Web Serial, we don't have direct access to device names
        // The ports are already granted access, so they're "paired"
        devices.add({
          "name": "Bluetooth Serial Device ${i + 1}",
          "address":
              "serial_port_$i", // Use index as identifier since we don't have hardware address
          "paired": true
        });

        // Store the port for later connection
        _discoveredPorts["serial_port_$i"] = port;
      }

      return devices;
    } catch (e) {
      print("Error getting paired serial ports: $e");
      return [];
    }
  }

  @override
  Future<bool> startDiscovery() async {
    try {
      // Request a serial port filtered by SPP UUID
      // This will show a picker for Bluetooth devices with Serial Port Profile (SPP)
      // SPP UUID: 00001101-0000-1000-8000-00805F9B34FB
      const sppUuid = "00001101-0000-1000-8000-00805f9b34fb";
      final filter = JSSerialPortFilter(bluetoothServiceClassId: sppUuid);
      final filters = [filter].toJS;
      final options = JSSerialOptions(filters: filters);
      final port = await serial.requestPort(options).toDart;

      // Generate a unique identifier for this port
      final portId = "serial_port_${DateTime.now().millisecondsSinceEpoch}";

      _discoveredPorts[portId] = port;

      // Send device found event first
      _stateController.add({
        "event": "deviceFound",
        "device": {
          "name":
              "Bluetooth Classic Device", // Web Serial doesn't provide device names
          "address": portId,
          "paired":
              true // Ports requested via requestPort() are implicitly granted access
        }
      });

      // On web, immediately attempt to connect to the selected device
      return true;
    } catch (e) {
      print("Web Serial discovery error: $e");
      return false;
    }
  }

  @override
  Future<bool> stopDiscovery() async {
    // Web Serial discovery stops as soon as the picker is closed/port selected
    return true;
  }

  @override
  Future<bool> connect(String address) async {
    final port = _discoveredPorts[address];
    if (port == null) {
      print(
          "Serial port not found in cache. Must call startDiscovery or getPairedDevices first on Web.");
      _connectionController.add({
        "isConnected": false,
        "deviceAddress": address,
        "status": "ERROR: Port not found"
      });
      return false;
    }

    try {
      if (_port == port && _port!.connected) {
        _connectionController.add({
          "isConnected": true,
          "deviceAddress": address,
          "status": "CONNECTED"
        });
        return true;
      }

      if (_isConnecting) {
        _connectionController.add({
          "isConnected": false,
          "deviceAddress": address,
          "status": "CONNECTING"
        });
        return false;
      }

      _isConnecting = true;

      // Open the serial port with appropriate options for Bluetooth Classic
      // Baud rate is required by the API but Bluetooth Classic ignores it
      await port.open(JSSerialOptions(baudRate: 9600)).toDart;

      _port = port;
      _isConnecting = false;

      // Notify connection success
      _connectionController.add({
        "isConnected": true,
        "deviceAddress": address,
        "status": "CONNECTED"
      });

      // Set up disconnect handler
      port.ondisconnect = ((JSObject e) {
        _connectionController.add({
          "isConnected": false,
          "deviceAddress": address,
          "status": "DISCONNECTED"
        });

        // Cleanup
        _cleanup();
      }).toJS;

      // Start reading data
      _startReading(address);

      return true;
    } catch (e) {
      print("Web Serial connection error: $e");
      _isConnecting = false;
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
    // Web Serial API cannot act as a Bluetooth server/listener
    // This is a limitation of the Web Serial API
    return false;
  }

  @override
  Future<bool> stopListen() async {
    // No listening was started, so nothing to stop
    return true;
  }

  @override
  Future<bool> disconnect() async {
    if (_port != null) {
      try {
        await _port!.close().toDart;
        _cleanup();
        return true;
      } catch (e) {
        print("Error closing serial port: $e");
        _cleanup();
        return false;
      }
    }
    return false;
  }

  @override
  Future<bool> sendData(Uint8List data) async {
    if (_port == null) return false;
    final writable = _port!.writable;
    if (writable == null) {
      print("Serial port is not writable.");
      return false;
    }

    try {
      // Get writer and write data
      _writer = writable.getWriter();
      await _writer!.write(data.toJS).toDart;
      _writer!.releaseLock();
      _writer = null;

      return true;
    } catch (e) {
      print("Error sending serial data: $e");
      if (_writer != null) {
        _writer!.releaseLock();
        _writer = null;
      }
      return false;
    }
  }
}
