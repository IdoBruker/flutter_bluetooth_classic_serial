import Flutter
import UIKit
import CoreBluetooth

// MARK: - Plugin
public class SwiftFlutterBluetoothClassicPlugin: NSObject, FlutterPlugin {
  private let methodChannel: FlutterMethodChannel
  private let stateChannel: FlutterEventChannel
  private let connectionChannel: FlutterEventChannel
  private let dataChannel: FlutterEventChannel
  
  private let stateStreamHandler = BluetoothStateStreamHandler()
  private let connectionStreamHandler = BluetoothConnectionStreamHandler()
  private let dataStreamHandler = BluetoothDataStreamHandler()
  
  private var bluetoothManager: BluetoothManager?
  
  public static func register(with registrar: FlutterPluginRegistrar) {
    let instance = SwiftFlutterBluetoothClassicPlugin(registrar: registrar)
    registrar.addMethodCallDelegate(instance, channel: instance.methodChannel)
  }
    init(registrar: FlutterPluginRegistrar) {
    methodChannel = FlutterMethodChannel(name: "com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic", binaryMessenger: registrar.messenger())
    stateChannel = FlutterEventChannel(name: "com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic_state", binaryMessenger: registrar.messenger())
    connectionChannel = FlutterEventChannel(name: "com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic_connection", binaryMessenger: registrar.messenger())
    dataChannel = FlutterEventChannel(name: "com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic_data", binaryMessenger: registrar.messenger())
    
    super.init()
    
    stateChannel.setStreamHandler(stateStreamHandler)
    connectionChannel.setStreamHandler(connectionStreamHandler)
    dataChannel.setStreamHandler(dataStreamHandler)
    
    bluetoothManager = BluetoothManager(
      stateHandler: stateStreamHandler,
      connectionHandler: connectionStreamHandler,
      dataHandler: dataStreamHandler
    )
  }
  
  public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    switch call.method {
    case "isBluetoothSupported":
      result(true) // iOS always supports Bluetooth
      
    case "isBluetoothEnabled":
      bluetoothManager?.isBluetoothEnabled(completion: result)
      
    case "enableBluetooth":
      // iOS doesn't allow programmatic enabling of Bluetooth
      result(FlutterError(code: "UNSUPPORTED",
                         message: "Cannot enable Bluetooth programmatically on iOS",
                         details: nil))
      
    case "getPairedDevices":
      bluetoothManager?.getPairedDevices(completion: result)
      
    case "startDiscovery":
      bluetoothManager?.startDiscovery(completion: result)
      
    case "stopDiscovery":
      bluetoothManager?.stopDiscovery(completion: result)
      
    case "connect":
      guard let args = call.arguments as? [String: Any],
            let address = args["address"] as? String else {
        result(FlutterError(code: "INVALID_ARGUMENT",
                           message: "Device address is required",
                           details: nil))
        return
      }
      bluetoothManager?.connect(address: address, completion: result)
      
    case "disconnect":
      bluetoothManager?.disconnect(completion: result)
      
    case "sendData":
      guard let args = call.arguments as? [String: Any],
            let typedData = args["data"] as? FlutterStandardTypedData else {
        result(FlutterError(code: "INVALID_ARGUMENT",
                           message: "Data is required",
                           details: nil))
        return
      }
      // FlutterStandardTypedData.data is already a Data object
      bluetoothManager?.sendData(typedData.data, completion: result)
      
    default:
      result(FlutterMethodNotImplemented)
    }
  }
}

// MARK: - Stream Handlers
class BluetoothStateStreamHandler: NSObject, FlutterStreamHandler {
  private var eventSink: FlutterEventSink?
  
  func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
    eventSink = events
    return nil
  }
  
  func onCancel(withArguments arguments: Any?) -> FlutterError? {
    eventSink = nil
    return nil
  }
  
  func send(_ data: Any) {
    DispatchQueue.main.async { [weak self] in
      self?.eventSink?(data)
    }
  }
}

class BluetoothConnectionStreamHandler: NSObject, FlutterStreamHandler {
  private var eventSink: FlutterEventSink?
  
  func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
    eventSink = events
    return nil
  }
  
  func onCancel(withArguments arguments: Any?) -> FlutterError? {
    eventSink = nil
    return nil
  }
  
  func send(_ data: Any) {
    DispatchQueue.main.async { [weak self] in
      self?.eventSink?(data)
    }
  }
}

class BluetoothDataStreamHandler: NSObject, FlutterStreamHandler {
  private var eventSink: FlutterEventSink?
  
  func onListen(withArguments arguments: Any?, eventSink events: @escaping FlutterEventSink) -> FlutterError? {
    eventSink = events
    return nil
  }
  
  func onCancel(withArguments arguments: Any?) -> FlutterError? {
    eventSink = nil
    return nil
  }
  
  func send(_ data: Any) {
    DispatchQueue.main.async { [weak self] in
      self?.eventSink?(data)
    }
  }
}

// MARK: - Bluetooth Manager
class BluetoothManager: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
  private var centralManager: CBCentralManager!
  private var connectedPeripheral: CBPeripheral?
  private var characteristics: [CBCharacteristic] = []
  
  private let stateHandler: BluetoothStateStreamHandler
  private let connectionHandler: BluetoothConnectionStreamHandler
  private let dataHandler: BluetoothDataStreamHandler
  
  // Background queue for Bluetooth operations to prevent UI blocking
  private let bluetoothQueue = DispatchQueue(label: "com.flutter_bluetooth_classic.bluetooth", qos: .userInitiated)
  
  // Store pending connection completion handler
  private var pendingConnectionCompletion: FlutterResult?
  private var pendingConnectionAddress: String?
  private var connectionTimeoutTimer: Timer?
  
  init(stateHandler: BluetoothStateStreamHandler,
       connectionHandler: BluetoothConnectionStreamHandler,
       dataHandler: BluetoothDataStreamHandler) {
    self.stateHandler = stateHandler
    self.connectionHandler = connectionHandler
    self.dataHandler = dataHandler
    super.init()
    // Use background queue to prevent blocking UI thread
    centralManager = CBCentralManager(delegate: self, queue: bluetoothQueue)
  }
  
  func isBluetoothEnabled(completion: @escaping FlutterResult) {
    let isEnabled = centralManager.state == .poweredOn
    DispatchQueue.main.async {
      completion(isEnabled)
    }
  }
  
  func getPairedDevices(completion: @escaping FlutterResult) {
    // iOS doesn't maintain a list of paired devices
    DispatchQueue.main.async {
      completion([])
    }
  }
  
  func startDiscovery(completion: @escaping FlutterResult) {
    guard centralManager.state == .poweredOn else {
      DispatchQueue.main.async {
        completion(FlutterError(code: "BLUETOOTH_OFF",
                              message: "Bluetooth is not enabled",
                              details: nil))
      }
      return
    }
    
    centralManager.scanForPeripherals(withServices: nil)
    DispatchQueue.main.async {
      completion(true)
    }
  }
  
  func stopDiscovery(completion: @escaping FlutterResult) {
    centralManager.stopScan()
    DispatchQueue.main.async {
      completion(true)
    }
  }
  
  func connect(address: String, completion: @escaping FlutterResult) {
    guard let uuid = UUID(uuidString: address),
          let peripheral = centralManager.retrievePeripherals(withIdentifiers: [uuid]).first else {
      DispatchQueue.main.async {
        completion(FlutterError(code: "DEVICE_NOT_FOUND",
                              message: "Device not found",
                              details: nil))
      }
      return
    }
    
    // Cancel any existing connection timeout
    connectionTimeoutTimer?.invalidate()
    
    // Store completion handler and address for later
    pendingConnectionCompletion = completion
    pendingConnectionAddress = address
    
    // Set up connection timeout (10 seconds)
    connectionTimeoutTimer = Timer.scheduledTimer(withTimeInterval: 10.0, repeats: false) { [weak self] _ in
      guard let self = self else { return }
      
      // Connection timed out
      if let pendingCompletion = self.pendingConnectionCompletion {
        self.centralManager.cancelPeripheralConnection(peripheral)
        
        DispatchQueue.main.async {
          pendingCompletion(FlutterError(code: "CONNECTION_TIMEOUT",
                                        message: "Connection timed out after 10 seconds",
                                        details: nil))
        }
        
        // Send connection error event
        self.connectionHandler.send([
          "isConnected": false,
          "deviceAddress": address,
          "status": "TIMEOUT"
        ])
        
        self.pendingConnectionCompletion = nil
        self.pendingConnectionAddress = nil
      }
    }
    
    connectedPeripheral = peripheral
    peripheral.delegate = self
    centralManager.connect(peripheral, options: nil)
  }
  
  func disconnect(completion: @escaping FlutterResult) {
    if let peripheral = connectedPeripheral {
      centralManager.cancelPeripheralConnection(peripheral)
    }
    DispatchQueue.main.async {
      completion(true)
    }
  }
  
  func sendData(_ data: Data, completion: @escaping FlutterResult) {
    guard let characteristic = characteristics.first else {
      DispatchQueue.main.async {
        completion(FlutterError(code: "NOT_CONNECTED",
                              message: "No characteristic available",
                              details: nil))
      }
      return
    }
    
    connectedPeripheral?.writeValue(data, for: characteristic, type: .withResponse)
    DispatchQueue.main.async {
      completion(true)
    }
  }
  
  // MARK: - CBCentralManagerDelegate
  func centralManagerDidUpdateState(_ central: CBCentralManager) {
    let isEnabled = central.state == .poweredOn
    let status: String
    
    switch central.state {
    case .poweredOn:
      status = "ON"
    case .poweredOff:
      status = "OFF"
    case .resetting:
      status = "RESETTING"
    case .unauthorized:
      status = "UNAUTHORIZED"
    case .unsupported:
      status = "UNSUPPORTED"
    case .unknown:
      status = "UNKNOWN"
    @unknown default:
      status = "UNKNOWN"
    }
    
    stateHandler.send([
      "isEnabled": isEnabled,
      "status": status
    ])
  }
  
  func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                     advertisementData: [String : Any], rssi RSSI: NSNumber) {
    let deviceMap: [String: Any] = [
      "name": peripheral.name ?? "Unknown",
      "address": peripheral.identifier.uuidString,
      "paired": false
    ]
    
    stateHandler.send([
      "event": "deviceFound",
      "device": deviceMap
    ])
  }
  
  func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
    // Cancel the connection timeout timer
    connectionTimeoutTimer?.invalidate()
    connectionTimeoutTimer = nil
    
    // Call the pending completion handler
    if let pendingCompletion = pendingConnectionCompletion {
      DispatchQueue.main.async {
        pendingCompletion(true)
      }
      pendingConnectionCompletion = nil
      pendingConnectionAddress = nil
    }
    
    peripheral.discoverServices(nil)
    
    connectionHandler.send([
      "isConnected": true,
      "deviceAddress": peripheral.identifier.uuidString,
      "status": "CONNECTED"
    ])
  }
  
  func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
    // Cancel the connection timeout timer
    connectionTimeoutTimer?.invalidate()
    connectionTimeoutTimer = nil
    
    let errorMessage = error?.localizedDescription ?? "Unknown error"
    
    // Call the pending completion handler with error
    if let pendingCompletion = pendingConnectionCompletion {
      DispatchQueue.main.async {
        pendingCompletion(FlutterError(code: "CONNECTION_FAILED",
                                      message: "Failed to connect: \(errorMessage)",
                                      details: nil))
      }
      pendingConnectionCompletion = nil
      pendingConnectionAddress = nil
    }
    
    // Send connection error event
    connectionHandler.send([
      "isConnected": false,
      "deviceAddress": peripheral.identifier.uuidString,
      "status": "ERROR: \(errorMessage)"
    ])
  }
  
  func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
    connectionHandler.send([
      "isConnected": false,
      "deviceAddress": peripheral.identifier.uuidString,
      "status": "DISCONNECTED"
    ])
  }
  
  // MARK: - CBPeripheralDelegate
  func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
    peripheral.services?.forEach { service in
      peripheral.discoverCharacteristics(nil, for: service)
    }
  }
  
  func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
    if let characteristics = service.characteristics {
      self.characteristics.append(contentsOf: characteristics)
      characteristics.forEach { characteristic in
        peripheral.setNotifyValue(true, for: characteristic)
      }
    }
  }
  
  func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
    if let data = characteristic.value {
      let bytes = [UInt8](data)
      dataHandler.send([
        "deviceAddress": peripheral.identifier.uuidString,
        "data": bytes.map { Int($0) }
      ])
    }
  }
}