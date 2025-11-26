import Flutter
import UIKit
import ExternalAccessory

// MARK: - Plugin
public class SwiftFlutterBluetoothClassicPlugin: NSObject, FlutterPlugin {
  private let methodChannel: FlutterMethodChannel
  private let stateChannel: FlutterEventChannel
  private let connectionChannel: FlutterEventChannel
  private let dataChannel: FlutterEventChannel
  
  private let stateStreamHandler = BluetoothStateStreamHandler()
  private let connectionStreamHandler = BluetoothConnectionStreamHandler()
  private let dataStreamHandler = BluetoothDataStreamHandler()
  
  private var accessoryManager: ExternalAccessoryManager?
  
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
    
    accessoryManager = ExternalAccessoryManager(
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
      // ExternalAccessory doesn't provide Bluetooth on/off status
      // We assume it's enabled if accessories are available
      result(true)
      
    case "enableBluetooth":
      // iOS doesn't allow programmatic enabling of Bluetooth
      result(FlutterError(code: "UNSUPPORTED",
                         message: "Cannot enable Bluetooth programmatically on iOS",
                         details: nil))
      
    case "getPairedDevices":
      accessoryManager?.getPairedDevices(completion: result)
      
    case "startDiscovery":
      accessoryManager?.startDiscovery(completion: result)
      
    case "stopDiscovery":
      accessoryManager?.stopDiscovery(completion: result)
      
    case "connect":
      guard let args = call.arguments as? [String: Any],
            let address = args["address"] as? String else {
        result(FlutterError(code: "INVALID_ARGUMENT",
                           message: "Device address is required",
                           details: nil))
        return
      }
      accessoryManager?.connect(address: address, completion: result)
      
    case "disconnect":
      accessoryManager?.disconnect(completion: result)
      
    case "sendData":
      guard let args = call.arguments as? [String: Any],
            let typedData = args["data"] as? FlutterStandardTypedData else {
        result(FlutterError(code: "INVALID_ARGUMENT",
                           message: "Data is required",
                           details: nil))
        return
      }
      accessoryManager?.sendData(typedData.data, completion: result)
      
    case "listen":
      // iOS ExternalAccessory doesn't support acting as a Bluetooth server
      // You can only connect to existing MFi accessories, not advertise and accept connections
      result(FlutterError(code: "UNSUPPORTED",
                         message: "iOS does not support acting as a Bluetooth server for ExternalAccessory. Use connect() to connect to existing accessories.",
                         details: nil))
      
    case "stopListen":
      // iOS ExternalAccessory doesn't support acting as a Bluetooth server
      result(FlutterError(code: "UNSUPPORTED",
                         message: "iOS does not support acting as a Bluetooth server for ExternalAccessory.",
                         details: nil))
      
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

// MARK: - External Accessory Manager
class ExternalAccessoryManager: NSObject {
  // Protocol substring for MFi accessories (legacy code checks for "igaro" in protocol string)
  // See EAConnectionController.m line 117 in legacy implementation
  private let PROTOCOL_SUBSTRING = "igaro"
  
  // Supported device name prefixes (case-insensitive)
  private let SUPPORTED_DEVICES = ["labdisc", "minidisc", "datahub", "forceacc"]
  
  private var currentAccessory: EAAccessory?
  private var currentSession: EASession?
  private var inputStream: InputStream?
  private var outputStream: OutputStream?
  
  private let stateHandler: BluetoothStateStreamHandler
  private let connectionHandler: BluetoothConnectionStreamHandler
  private let dataHandler: BluetoothDataStreamHandler
  
  // Dedicated thread for stream operations with RunLoop
  private var streamThread: Thread?
  private var streamRunLoop: RunLoop?
  private var isStreamThreadRunning = false
  
  // Track connection state to prevent race conditions
  private var isConnecting = false
  private let connectionLock = NSLock()
  
  // Buffer for reading data
  private let readBufferSize = 1024
  private var readBuffer: UnsafeMutablePointer<UInt8>
  
  init(stateHandler: BluetoothStateStreamHandler,
       connectionHandler: BluetoothConnectionStreamHandler,
       dataHandler: BluetoothDataStreamHandler) {
    self.stateHandler = stateHandler
    self.connectionHandler = connectionHandler
    self.dataHandler = dataHandler
    self.readBuffer = UnsafeMutablePointer<UInt8>.allocate(capacity: readBufferSize)
    
    super.init()
    
    // Register for accessory notifications
    NotificationCenter.default.addObserver(
      self,
      selector: #selector(accessoryDidConnect(_:)),
      name: .EAAccessoryDidConnect,
      object: nil
    )
    
    NotificationCenter.default.addObserver(
      self,
      selector: #selector(accessoryDidDisconnect(_:)),
      name: .EAAccessoryDidDisconnect,
      object: nil
    )
    
    // Register for notifications
    EAAccessoryManager.shared().registerForLocalNotifications()
    
    // Send initial state
    sendBluetoothState(isEnabled: true)
  }
  
  deinit {
    readBuffer.deallocate()
    NotificationCenter.default.removeObserver(self)
    stopStreamThread()
  }
  
  // MARK: - Notification Handlers
  
  @objc private func accessoryDidConnect(_ notification: Notification) {
    guard let accessory = notification.userInfo?[EAAccessoryKey] as? EAAccessory else {
      return
    }
    
    // Check if this is a supported device
    let deviceName = accessory.name.lowercased()
    let isSupported = SUPPORTED_DEVICES.contains { deviceName.contains($0) }
    
    if isSupported {
      // Send device found event
      let deviceMap: [String: Any] = [
        "name": accessory.name,
        "address": String(accessory.connectionID),
        "paired": true
      ]
      
      stateHandler.send([
        "event": "deviceFound",
        "device": deviceMap
      ])
    }
  }
  
  @objc private func accessoryDidDisconnect(_ notification: Notification) {
    guard let accessory = notification.userInfo?[EAAccessoryKey] as? EAAccessory else {
      return
    }
    
    // If the disconnected accessory is our current connection
    if accessory.connectionID == currentAccessory?.connectionID {
      closeSession()
      
      connectionHandler.send([
        "isConnected": false,
        "deviceAddress": String(accessory.connectionID),
        "status": "DISCONNECTED"
      ])
    }
  }
  
  // MARK: - Helper Methods
  
  /// Finds a supported protocol string from an accessory's protocol list
  /// Matches legacy implementation that searches for "igaro" substring (EAConnectionController.m:117)
  private func findSupportedProtocol(for accessory: EAAccessory) -> String? {
    for protocolString in accessory.protocolStrings {
      // Use case-insensitive search for protocol substring
      if protocolString.lowercased().contains(PROTOCOL_SUBSTRING.lowercased()) {
        print("[ExternalAccessory] Found supported protocol: \(protocolString) for accessory: \(accessory.name)")
        return protocolString
      }
    }
    print("[ExternalAccessory] No supported protocol found for accessory: \(accessory.name), available protocols: \(accessory.protocolStrings)")
    return nil
  }
  
  // MARK: - Public Methods
  
  func getPairedDevices(completion: @escaping FlutterResult) {
    let accessories = EAAccessoryManager.shared().connectedAccessories
    
    let devices = accessories.compactMap { accessory -> [String: Any]? in
      // Filter to only supported devices
      let deviceName = accessory.name.lowercased()
      let isSupported = SUPPORTED_DEVICES.contains { deviceName.contains($0) }
      
      guard isSupported else { return nil }
      
      return [
        "name": accessory.name,
        "address": String(accessory.connectionID),
        "paired": true
      ]
    }
    
    DispatchQueue.main.async {
      completion(devices)
    }
  }
  
  func startDiscovery(completion: @escaping FlutterResult) {
    // ExternalAccessory doesn't have active discovery like Bluetooth Classic on other platforms
    // Instead, we return the list of connected accessories and show the picker
    // The OS handles pairing through Settings
    
    // First, send events for all currently connected supported devices
    let accessories = EAAccessoryManager.shared().connectedAccessories
    print("[ExternalAccessory] Starting discovery, found \(accessories.count) connected accessories")
    
    for accessory in accessories {
      print("[ExternalAccessory] Accessory: \(accessory.name), serial: \(accessory.serialNumber)")
      print("[ExternalAccessory] Protocols: \(accessory.protocolStrings)")
      
      let deviceName = accessory.name.lowercased()
      let isSupported = SUPPORTED_DEVICES.contains { deviceName.contains($0) }
      
      if isSupported {
        print("[ExternalAccessory] Device \(accessory.name) is supported")
        let deviceMap: [String: Any] = [
          "name": accessory.name,
          "address": String(accessory.connectionID),
          "paired": true
        ]
        
        stateHandler.send([
          "event": "deviceFound",
          "device": deviceMap
        ])
      }
    }
    
    // Show the accessory picker dialog to allow user to connect new accessories
    EAAccessoryManager.shared().showBluetoothAccessoryPicker(withNameFilter: nil) { error in
      if let error = error {
        DispatchQueue.main.async {
          completion(FlutterError(code: "DISCOVERY_ERROR",
                                message: "Failed to show accessory picker: \(error.localizedDescription)",
                                details: nil))
        }
      } else {
        DispatchQueue.main.async {
          completion(true)
        }
      }
    }
  }
  
  func stopDiscovery(completion: @escaping FlutterResult) {
    // Discovery is managed by the OS, nothing to stop
    DispatchQueue.main.async {
      completion(true)
    }
  }
  
  func connect(address: String, completion: @escaping FlutterResult) {
    // Find accessory by connection ID
    guard let connectionID = UInt(address) else {
      DispatchQueue.main.async {
        completion(FlutterError(code: "INVALID_ADDRESS",
                              message: "Invalid device address",
                              details: nil))
      }
      return
    }
    
    let accessories = EAAccessoryManager.shared().connectedAccessories
    print("[ExternalAccessory] Attempting to connect to device with ID: \(connectionID)")
    print("[ExternalAccessory] Available accessories: \(accessories.count)")
    
    guard let accessory = accessories.first(where: { $0.connectionID == connectionID }) else {
      DispatchQueue.main.async {
        completion(FlutterError(code: "DEVICE_NOT_FOUND",
                              message: "Device not found",
                              details: nil))
      }
      return
    }
    
    print("[ExternalAccessory] Found accessory: \(accessory.name), protocols: \(accessory.protocolStrings)")
    
    // Find a supported protocol (flexible matching like legacy code)
    guard let protocolString = findSupportedProtocol(for: accessory) else {
      DispatchQueue.main.async {
        completion(FlutterError(code: "PROTOCOL_NOT_SUPPORTED",
                              message: "Device does not support any protocol containing '\(self.PROTOCOL_SUBSTRING)'. Available protocols: \(accessory.protocolStrings)",
                              details: nil))
      }
      return
    }
    
    print("[ExternalAccessory] Using protocol: \(protocolString)")
    
    // Close any existing session
    closeSession()
    
    // Create new session with the detected protocol
    guard let session = EASession(accessory: accessory, forProtocol: protocolString) else {
      DispatchQueue.main.async {
        completion(FlutterError(code: "SESSION_FAILED",
                              message: "Failed to create session with accessory using protocol: \(protocolString)",
                              details: nil))
      }
      return
    }
    
    // Mark as connecting
    connectionLock.lock()
    isConnecting = true
    connectionLock.unlock()
    
    currentAccessory = accessory
    currentSession = session
    inputStream = session.inputStream
    outputStream = session.outputStream
    
    // Start stream thread and open streams
    startStreamThread { [weak self] success in
      guard let self = self else { return }
      
      // Check if we're still trying to connect (not cancelled by disconnect)
      self.connectionLock.lock()
      let stillConnecting = self.isConnecting
      self.isConnecting = false
      self.connectionLock.unlock()
      
      guard stillConnecting else {
        // Connection was cancelled, don't send success
        return
      }
      
      if success {
        DispatchQueue.main.async {
          completion(true)
          
          self.connectionHandler.send([
            "isConnected": true,
            "deviceAddress": String(accessory.connectionID),
            "status": "CONNECTED"
          ])
        }
      } else {
        self.closeSession()
        DispatchQueue.main.async {
          completion(FlutterError(code: "CONNECTION_FAILED",
                                message: "Failed to open streams",
                                details: nil))
        }
      }
    }
  }
  
  func disconnect(completion: @escaping FlutterResult) {
    // Cancel any pending connection
    connectionLock.lock()
    isConnecting = false
    connectionLock.unlock()
    
    closeSession()
    
    DispatchQueue.main.async {
      completion(true)
    }
  }
  
  func sendData(_ data: Data, completion: @escaping FlutterResult) {
    guard let outputStream = outputStream else {
      DispatchQueue.main.async {
        completion(FlutterError(code: "NOT_CONNECTED",
                              message: "Not connected",
                              details: nil))
      }
      return
    }
    
    guard let runLoop = streamRunLoop else {
      DispatchQueue.main.async {
        completion(FlutterError(code: "NOT_CONNECTED",
                              message: "Stream thread not running",
                              details: nil))
      }
      return
    }
    
    // Perform write on stream thread's RunLoop
    runLoop.perform { [weak self] in
      guard let self = self else { return }
      
      let bytes = [UInt8](data)
      let bytesWritten = outputStream.write(bytes, maxLength: bytes.count)
      
      if bytesWritten < 0 {
        DispatchQueue.main.async {
          completion(FlutterError(code: "WRITE_ERROR",
                                message: "Failed to write data: \(outputStream.streamError?.localizedDescription ?? "Unknown error")",
                                details: nil))
        }
      } else if bytesWritten < bytes.count {
        DispatchQueue.main.async {
          completion(FlutterError(code: "WRITE_ERROR",
                                message: "Only wrote \(bytesWritten) of \(bytes.count) bytes",
                                details: nil))
        }
      } else {
        DispatchQueue.main.async {
          completion(true)
        }
      }
    }
  }
  
  // MARK: - Private Methods - Stream Thread Management
  
  private func startStreamThread(completion: @escaping (Bool) -> Void) {
    stopStreamThread()
    
    isStreamThreadRunning = true
    
    streamThread = Thread { [weak self] in
      guard let self = self else {
        completion(false)
        return
      }
      
      // Get current RunLoop
      self.streamRunLoop = RunLoop.current
      
      // Configure and open streams on this thread
      self.inputStream?.delegate = self
      self.outputStream?.delegate = self
      
      self.inputStream?.schedule(in: .current, forMode: .default)
      self.outputStream?.schedule(in: .current, forMode: .default)
      
      self.inputStream?.open()
      self.outputStream?.open()
      
      // Notify success
      completion(true)
      
      // Run the RunLoop
      while self.isStreamThreadRunning && !Thread.current.isCancelled {
        RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.1))
      }
      
      // Clean up when exiting
      self.inputStream?.close()
      self.outputStream?.close()
      self.inputStream?.remove(from: .current, forMode: .default)
      self.outputStream?.remove(from: .current, forMode: .default)
      self.inputStream?.delegate = nil
      self.outputStream?.delegate = nil
      
      self.streamRunLoop = nil
    }
    
    streamThread?.start()
  }
  
  private func stopStreamThread() {
    isStreamThreadRunning = false
    streamThread?.cancel()
    streamThread = nil
    streamRunLoop = nil
  }
  
  private func closeSession() {
    stopStreamThread()
    
    currentSession = nil
    currentAccessory = nil
    inputStream = nil
    outputStream = nil
  }
  
  private func sendBluetoothState(isEnabled: Bool) {
    let status = isEnabled ? "ON" : "OFF"
    stateHandler.send([
      "isEnabled": isEnabled,
      "status": status
    ])
  }
}

// MARK: - Stream Delegate
extension ExternalAccessoryManager: StreamDelegate {
  func stream(_ aStream: Stream, handle eventCode: Stream.Event) {
    switch eventCode {
    case .openCompleted:
      break
      
    case .hasBytesAvailable:
      guard let inputStream = inputStream, aStream == inputStream else { return }
      
      // Read available data
      let bytesRead = inputStream.read(readBuffer, maxLength: readBufferSize)
      
      if bytesRead > 0 {
        let data = Data(bytes: readBuffer, count: bytesRead)
        let bytes = [UInt8](data)
        
        if let deviceAddress = currentAccessory?.connectionID {
          dataHandler.send([
            "deviceAddress": String(deviceAddress),
            "data": bytes.map { Int($0) }
          ])
        }
      }
      
    case .hasSpaceAvailable:
      break
      
    case .errorOccurred:
      if let accessory = currentAccessory {
        connectionHandler.send([
          "isConnected": false,
          "deviceAddress": String(accessory.connectionID),
          "status": "ERROR: \(aStream.streamError?.localizedDescription ?? "Unknown error")"
        ])
      }
      closeSession()
      
    case .endEncountered:
      if let accessory = currentAccessory {
        connectionHandler.send([
          "isConnected": false,
          "deviceAddress": String(accessory.connectionID),
          "status": "DISCONNECTED"
        ])
      }
      closeSession()
      
    default:
      break
    }
  }
}
