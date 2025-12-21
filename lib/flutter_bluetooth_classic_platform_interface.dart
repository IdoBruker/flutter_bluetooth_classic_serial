import 'dart:async';

import 'package:flutter/services.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';

abstract class FlutterBluetoothClassicPlatform extends PlatformInterface {
  FlutterBluetoothClassicPlatform() : super(token: _token);

  static final Object _token = Object();
  static FlutterBluetoothClassicPlatform _instance = _DefaultPlatform();

  static FlutterBluetoothClassicPlatform get instance => _instance;

  static set instance(FlutterBluetoothClassicPlatform instance) {
    PlatformInterface.verifyToken(instance, _token);
    _instance = instance;
  }

  // State streams
  Stream<Map<String, dynamic>> get stateStream;
  Stream<Map<String, dynamic>> get connectionStream;
  Stream<Map<String, dynamic>> get dataStream;

  // Methods
  Future<bool> isBluetoothSupported();
  Future<bool> isBluetoothEnabled();
  Future<bool> enableBluetooth();
  Future<List<Map<String, dynamic>>> getPairedDevices();
  Future<bool> startDiscovery();
  Future<bool> stopDiscovery();
  Future<bool> connect(String address);
  Future<bool> listen();
  Future<bool> disconnect();
  Future<bool> stopListen();
  Future<bool> sendData(Uint8List data);
}

class _DefaultPlatform extends FlutterBluetoothClassicPlatform {
  static const MethodChannel _channel = MethodChannel(
      'com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic');
  static const EventChannel _stateChannel = EventChannel(
      'com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic_state');
  static const EventChannel _connectionChannel = EventChannel(
      'com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic_connection');
  static const EventChannel _dataChannel = EventChannel(
      'com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic_data');

  // Helper function to convert map keys to String
  Map<String, dynamic> _convertMapKeysToString(
      Map<dynamic, dynamic> originalMap) {
    final Map<String, dynamic> typedMap = {};
    originalMap.forEach((key, value) {
      if (key is String) {
        typedMap[key] = value;
      } else {
        typedMap[key.toString()] = value; // Fallback for non-string keys
      }
    });
    return typedMap;
  }

  @override
  Stream<Map<String, dynamic>> get stateStream => _stateChannel
      .receiveBroadcastStream()
      .map((event) => _convertMapKeysToString(event as Map<dynamic, dynamic>));

  @override
  Stream<Map<String, dynamic>> get connectionStream => _connectionChannel
      .receiveBroadcastStream()
      .map((event) => _convertMapKeysToString(event as Map<dynamic, dynamic>));

  @override
  Stream<Map<String, dynamic>> get dataStream => _dataChannel
      .receiveBroadcastStream()
      .map((event) => _convertMapKeysToString(event as Map<dynamic, dynamic>));

  @override
  Future<bool> isBluetoothSupported() async {
    return await _channel.invokeMethod('isBluetoothSupported') ?? false;
  }

  @override
  Future<bool> isBluetoothEnabled() async {
    return await _channel.invokeMethod('isBluetoothEnabled') ?? false;
  }

  @override
  Future<bool> enableBluetooth() async {
    return await _channel.invokeMethod('enableBluetooth') ?? false;
  }

  @override
  Future<List<Map<String, dynamic>>> getPairedDevices() async {
    final result = await _channel.invokeMethod('getPairedDevices');
    if (result == null) {
      return [];
    }
    final List<dynamic> rawList = result;
    return rawList.map((e) {
      final Map<dynamic, dynamic> rawMap = e;
      final Map<String, dynamic> typedMap = {};
      rawMap.forEach((key, value) {
        if (key is String) {
          typedMap[key] = value;
        } else {
          typedMap[key.toString()] =
              value; // Fallback in case of non-string key
        }
      });
      return typedMap;
    }).toList();
  }

  @override
  Future<bool> startDiscovery() async {
    return await _channel.invokeMethod('startDiscovery') ?? false;
  }

  @override
  Future<bool> stopDiscovery() async {
    return await _channel.invokeMethod('stopDiscovery') ?? false;
  }

  @override
  Future<bool> connect(String address) async {
    return await _channel.invokeMethod('connect', {'address': address}) ??
        false;
  }

  @override
  Future<bool> listen() async {
    return await _channel.invokeMethod('listen') ?? false;
  }

  @override
  Future<bool> disconnect() async {
    return await _channel.invokeMethod('disconnect') ?? false;
  }

  @override
  Future<bool> stopListen() async {
    return await _channel.invokeMethod('stopListen') ?? false;
  }

  @override
  Future<bool> sendData(Uint8List data) async {
    return await _channel.invokeMethod('sendData', {'data': data}) ?? false;
  }
}
