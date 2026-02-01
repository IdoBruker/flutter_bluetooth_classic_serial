import FlutterMacOS
import Foundation
import IOBluetooth

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
    methodChannel = FlutterMethodChannel(
      name: "com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic",
      binaryMessenger: registrar.messenger
    )
    stateChannel = FlutterEventChannel(
      name: "com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic_state",
      binaryMessenger: registrar.messenger
    )
    connectionChannel = FlutterEventChannel(
      name: "com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic_connection",
      binaryMessenger: registrar.messenger
    )
    dataChannel = FlutterEventChannel(
      name: "com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic_data",
      binaryMessenger: registrar.messenger
    )

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
      result(true)

    case "isBluetoothEnabled":
      bluetoothManager?.isBluetoothEnabled(completion: result)

    case "enableBluetooth":
      result(FlutterError(code: "UNSUPPORTED",
                         message: "Cannot enable Bluetooth programmatically on macOS",
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
      bluetoothManager?.sendData(typedData.data, completion: result)

    case "listen":
      bluetoothManager?.listen(completion: result)

    case "stopListen":
      bluetoothManager?.stopListen(completion: result)

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
class BluetoothManager: NSObject {
  private let stateHandler: BluetoothStateStreamHandler
  private let connectionHandler: BluetoothConnectionStreamHandler
  private let dataHandler: BluetoothDataStreamHandler

  private var inquiry: IOBluetoothDeviceInquiry?
  private var discoveredDevices: [String: IOBluetoothDevice] = [:]

  private var connectedDevice: IOBluetoothDevice?
  private var rfcommChannel: IOBluetoothRFCOMMChannel?

  private var serverNotification: IOBluetoothUserNotification?
  private var listeningChannelID: BluetoothRFCOMMChannelID = 1

  private var ioThread: Thread?
  private var ioRunLoop: RunLoop?
  private var isIOThreadRunning = false

  private let connectionLock = NSLock()
  private var isConnecting = false

  init(stateHandler: BluetoothStateStreamHandler,
       connectionHandler: BluetoothConnectionStreamHandler,
       dataHandler: BluetoothDataStreamHandler) {
    self.stateHandler = stateHandler
    self.connectionHandler = connectionHandler
    self.dataHandler = dataHandler
    super.init()
    startIOThread()
    sendBluetoothState()
  }

  deinit {
    stopIOThread()
  }

  func isBluetoothEnabled(completion: @escaping FlutterResult) {
    let isEnabled = IOBluetoothHostController.default().powerState == kBluetoothHCIPowerStateON
    DispatchQueue.main.async {
      completion(isEnabled)
    }
  }

  func getPairedDevices(completion: @escaping FlutterResult) {
    let devices = (IOBluetoothDevice.pairedDevices() as? [IOBluetoothDevice]) ?? []
    let result = devices.map { device in
      [
        "name": device.name ?? "Unknown",
        "address": device.addressString ?? "",
        "paired": true
      ] as [String: Any]
    }
    DispatchQueue.main.async {
      completion(result)
    }
  }

  func startDiscovery(completion: @escaping FlutterResult) {
    performOnIOThread { [weak self] in
      guard let self = self else { return }
      self.inquiry?.stop()
      self.discoveredDevices.removeAll()

      let inquiry = IOBluetoothDeviceInquiry(delegate: self)
      inquiry?.updateNewDeviceNames = true
      self.inquiry = inquiry

      let status = inquiry?.start() ?? kIOReturnError
      DispatchQueue.main.async {
        if status == kIOReturnSuccess {
          completion(true)
        } else {
          completion(FlutterError(code: "DISCOVERY_ERROR",
                                  message: "Failed to start discovery",
                                  details: nil))
        }
      }
    }
  }

  func stopDiscovery(completion: @escaping FlutterResult) {
    performOnIOThread { [weak self] in
      self?.inquiry?.stop()
      DispatchQueue.main.async {
        completion(true)
      }
    }
  }

  func connect(address: String, completion: @escaping FlutterResult) {
    performOnIOThread { [weak self] in
      guard let self = self else { return }
      guard let device = self.deviceForAddress(address) else {
        DispatchQueue.main.async {
          completion(FlutterError(code: "DEVICE_NOT_FOUND",
                                  message: "Device not found",
                                  details: nil))
        }
        return
      }

      self.connectionLock.lock()
      self.isConnecting = true
      self.connectionLock.unlock()

      let channelID = self.resolveRFCOMMChannelID(for: device) ?? self.listeningChannelID
      var channel: IOBluetoothRFCOMMChannel?
      let status = device.openRFCOMMChannelSync(&channel, withChannelID: channelID, delegate: self)

      self.connectionLock.lock()
      let stillConnecting = self.isConnecting
      self.isConnecting = false
      self.connectionLock.unlock()

      guard stillConnecting else { return }

      if status == kIOReturnSuccess, let channel = channel {
        self.connectedDevice = device
        self.rfcommChannel = channel
        DispatchQueue.main.async {
          completion(true)
          self.connectionHandler.send([
            "isConnected": true,
            "deviceAddress": device.addressString ?? address,
            "status": "CONNECTED"
          ])
        }
      } else {
        DispatchQueue.main.async {
          completion(FlutterError(code: "CONNECTION_FAILED",
                                  message: "Failed to open RFCOMM channel",
                                  details: nil))
        }
      }
    }
  }

  func disconnect(completion: @escaping FlutterResult) {
    performOnIOThread { [weak self] in
      guard let self = self else { return }
      self.connectionLock.lock()
      self.isConnecting = false
      self.connectionLock.unlock()

      let deviceAddress = self.connectedDevice?.addressString ?? "unknown"
      self.rfcommChannel?.closeChannel()
      self.rfcommChannel = nil
      self.connectedDevice = nil

      DispatchQueue.main.async {
        completion(true)
        self.connectionHandler.send([
          "isConnected": false,
          "deviceAddress": deviceAddress,
          "status": "DISCONNECTED"
        ])
      }
    }
  }

  func sendData(_ data: Data, completion: @escaping FlutterResult) {
    performOnIOThread { [weak self] in
      guard let self = self else { return }
      guard let channel = self.rfcommChannel else {
        DispatchQueue.main.async {
          completion(FlutterError(code: "NOT_CONNECTED",
                                  message: "Not connected",
                                  details: nil))
        }
        return
      }

      let bytes = [UInt8](data)
      let status = channel.writeSync(bytes, length: UInt16(bytes.count))
      DispatchQueue.main.async {
        if status == kIOReturnSuccess {
          completion(true)
        } else {
          completion(FlutterError(code: "WRITE_ERROR",
                                  message: "Failed to write data",
                                  details: nil))
        }
      }
    }
  }

  func listen(completion: @escaping FlutterResult) {
    performOnIOThread { [weak self] in
      guard let self = self else { return }
      self.serverNotification?.unregister()
      self.serverNotification = IOBluetoothRFCOMMChannel.register(forChannelID: self.listeningChannelID,
                                                                   delegate: self)
      DispatchQueue.main.async {
        if self.serverNotification != nil {
          completion(true)
        } else {
          completion(FlutterError(code: "LISTEN_FAILED",
                                  message: "Failed to register RFCOMM server channel",
                                  details: nil))
        }
      }
    }
  }

  func stopListen(completion: @escaping FlutterResult) {
    performOnIOThread { [weak self] in
      self?.serverNotification?.unregister()
      self?.serverNotification = nil
      DispatchQueue.main.async {
        completion(true)
      }
    }
  }

  private func sendBluetoothState() {
    let isEnabled = IOBluetoothHostController.default().powerState == kBluetoothHCIPowerStateON
    let status = isEnabled ? "ON" : "OFF"
    stateHandler.send([
      "isEnabled": isEnabled,
      "status": status
    ])
  }

  private func deviceForAddress(_ address: String) -> IOBluetoothDevice? {
    if let existing = discoveredDevices[address] {
      return existing
    }
    if let device = IOBluetoothDevice(addressString: address) {
      return device
    }
    let paired = (IOBluetoothDevice.pairedDevices() as? [IOBluetoothDevice]) ?? []
    return paired.first { $0.addressString == address }
  }

  private func resolveRFCOMMChannelID(for device: IOBluetoothDevice) -> BluetoothRFCOMMChannelID? {
    let sppUUID = IOBluetoothSDPUUID.uuid16(0x1101)
    if let record = device.getServiceRecord(for: sppUUID) {
      var channelID: BluetoothRFCOMMChannelID = 0
      if record.getRFCOMMChannelID(&channelID) == kIOReturnSuccess {
        return channelID
      }
    }

    if let services = device.services as? [IOBluetoothSDPServiceRecord] {
      for record in services {
        var channelID: BluetoothRFCOMMChannelID = 0
        if record.getRFCOMMChannelID(&channelID) == kIOReturnSuccess {
          return channelID
        }
      }
    }
    return nil
  }

  // MARK: - Background Thread / RunLoop
  private func startIOThread() {
    stopIOThread()
    isIOThreadRunning = true
    ioThread = Thread { [weak self] in
      guard let self = self else { return }
      self.ioRunLoop = RunLoop.current
      while self.isIOThreadRunning && !Thread.current.isCancelled {
        RunLoop.current.run(mode: .default, before: Date(timeIntervalSinceNow: 0.1))
      }
      self.ioRunLoop = nil
    }
    ioThread?.name = "FlutterBluetoothClassicIO"
    ioThread?.start()
  }

  private func stopIOThread() {
    isIOThreadRunning = false
    ioThread?.cancel()
    ioThread = nil
    ioRunLoop = nil
  }

  private func performOnIOThread(_ block: @escaping () -> Void) {
    guard let runLoop = ioRunLoop else {
      DispatchQueue.global(qos: .userInitiated).async(execute: block)
      return
    }
    runLoop.perform(block)
  }
}

// MARK: - IOBluetoothDeviceInquiryDelegate
extension BluetoothManager: IOBluetoothDeviceInquiryDelegate {
  func deviceInquiryDeviceFound(_ sender: IOBluetoothDeviceInquiry!, device: IOBluetoothDevice!) {
    guard let device = device else { return }
    let address = device.addressString ?? ""
    discoveredDevices[address] = device

    let deviceMap: [String: Any] = [
      "name": device.name ?? "Unknown",
      "address": address,
      "paired": false
    ]

    stateHandler.send([
      "event": "deviceFound",
      "device": deviceMap
    ])
  }

  func deviceInquiryComplete(_ sender: IOBluetoothDeviceInquiry!, error: IOReturn, aborted: Bool) {
    if error != kIOReturnSuccess && !aborted {
      stateHandler.send([
        "event": "discoveryError",
        "error": "Discovery failed"
      ])
    }
  }
}

// MARK: - IOBluetoothRFCOMMChannelDelegate
extension BluetoothManager: IOBluetoothRFCOMMChannelDelegate {
  func rfcommChannelData(_ rfcommChannel: IOBluetoothRFCOMMChannel!,
                         data dataPointer: UnsafeMutableRawPointer!,
                         length dataLength: Int) {
    guard let dataPointer = dataPointer else { return }
    let data = Data(bytes: dataPointer, count: dataLength)
    let bytes = [UInt8](data)
    let deviceAddress = connectedDevice?.addressString ?? "unknown"

    dataHandler.send([
      "deviceAddress": deviceAddress,
      "data": bytes.map { Int($0) }
    ])
  }

  func rfcommChannelClosed(_ rfcommChannel: IOBluetoothRFCOMMChannel!) {
    let deviceAddress = connectedDevice?.addressString ?? "unknown"
    self.rfcommChannel = nil
    self.connectedDevice = nil

    connectionHandler.send([
      "isConnected": false,
      "deviceAddress": deviceAddress,
      "status": "DISCONNECTED"
    ])
  }

  func rfcommChannelOpenComplete(_ rfcommChannel: IOBluetoothRFCOMMChannel!, status error: IOReturn) {
    if error != kIOReturnSuccess {
      let deviceAddress = connectedDevice?.addressString ?? "unknown"
      connectionHandler.send([
        "isConnected": false,
        "deviceAddress": deviceAddress,
        "status": "ERROR: Failed to open RFCOMM channel"
      ])
    }
  }

  func rfcommChannelOpened(_ rfcommChannel: IOBluetoothRFCOMMChannel!) {
    guard let device = rfcommChannel.device else { return }
    connectedDevice = device
    self.rfcommChannel = rfcommChannel
    connectionHandler.send([
      "isConnected": true,
      "deviceAddress": device.addressString ?? "unknown",
      "status": "CONNECTED"
    ])
  }
}

