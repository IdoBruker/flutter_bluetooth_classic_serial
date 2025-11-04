import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_bluetooth_classic_serial/flutter_bluetooth_classic.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Bluetooth Classic Demo',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.deepPurple),
        useMaterial3: true,
      ),
      home: const BluetoothClassicDemo(),
    );
  }
}

class MyHomePage extends StatefulWidget {
  const MyHomePage({super.key, required this.title});

  // This widget is the home page of your application. It is stateful, meaning
  // that it has a State object (defined below) that contains fields that affect
  // how it looks.

  // This class is the configuration for the state. It holds the values (in this
  // case the title) provided by the parent (in this case the App widget) and
  // used by the build method of the State. Fields in a Widget subclass are
  // always marked "final".

  final String title;

  @override
  State<MyHomePage> createState() => _MyHomePageState();
}

class _MyHomePageState extends State<MyHomePage> {
  int _counter = 0;

  void _incrementCounter() {
    setState(() {
      // This call to setState tells the Flutter framework that something has
      // changed in this State, which causes it to rerun the build method below
      // so that the display can reflect the updated values. If we changed
      // _counter without calling setState(), then the build method would not be
      // called again, and so nothing would appear to happen.
      _counter++;
    });
  }

  @override
  Widget build(BuildContext context) {
    // This method is rerun every time setState is called, for instance as done
    // by the _incrementCounter method above.
    //
    // The Flutter framework has been optimized to make rerunning build methods
    // fast, so that you can just rebuild anything that needs updating rather
    // than having to individually change instances of widgets.
    return Scaffold(
      appBar: AppBar(
        // TRY THIS: Try changing the color here to a specific color (to
        // Colors.amber, perhaps?) and trigger a hot reload to see the AppBar
        // change color while the other colors stay the same.
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        // Here we take the value from the MyHomePage object that was created by
        // the App.build method, and use it to set our appbar title.
        title: Text(widget.title),
      ),
      body: Center(
        // Center is a layout widget. It takes a single child and positions it
        // in the middle of the parent.
        child: Column(
          // Column is also a layout widget. It takes a list of children and
          // arranges them vertically. By default, it sizes itself to fit its
          // children horizontally, and tries to be as tall as its parent.
          //
          // Column has various properties to control how it sizes itself and
          // how it positions its children. Here we use mainAxisAlignment to
          // center the children vertically; the main axis here is the vertical
          // axis because Columns are vertical (the cross axis would be
          // horizontal).
          //
          // TRY THIS: Invoke "debug painting" (choose the "Toggle Debug Paint"
          // action in the IDE, or press "p" in the console), to see the
          // wireframe for each widget.
          mainAxisAlignment: MainAxisAlignment.center,
          children: <Widget>[
            const Text('You have pushed the button this many times:'),
            Text(
              '$_counter',
              style: Theme.of(context).textTheme.headlineMedium,
            ),
          ],
        ),
      ),
      floatingActionButton: FloatingActionButton(
        onPressed: _incrementCounter,
        tooltip: 'Increment',
        child: const Icon(Icons.add),
      ), // This trailing comma makes auto-formatting nicer for build methods.
    );
  }
}

class BluetoothClassicDemo extends StatefulWidget {
  const BluetoothClassicDemo({super.key});

  @override
  State<BluetoothClassicDemo> createState() => _BluetoothClassicDemoState();
}

class _BluetoothClassicDemoState extends State<BluetoothClassicDemo> {
  late FlutterBluetoothClassic _bluetooth;
  bool _isBluetoothAvailable = false;
  BluetoothConnectionState? _connectionState;
  List<BluetoothDevice> _pairedDevices = [];
  BluetoothDevice? _connectedDevice;
  String _receivedData = '';
  final TextEditingController _messageController = TextEditingController();
  bool _isConnecting = false;

  // Discovery-related state
  final Set<String> _discoveredDeviceAddresses = {};
  bool _isScanning = false;
  Timer? _discoveryTimer;

  @override
  void initState() {
    super.initState();
    _bluetooth = FlutterBluetoothClassic("app");
    _initBluetooth();
  }

  Future<void> _initBluetooth() async {
    try {
      bool isSupported = await _bluetooth.isBluetoothSupported();
      bool isEnabled = await _bluetooth.isBluetoothEnabled();
      setState(() {
        _isBluetoothAvailable = isSupported && isEnabled;
      });

      if (isSupported && isEnabled) {
        _loadPairedDevices();
        _listenToConnectionState();
        _listenToIncomingData();
        _listenToDeviceDiscovery();
      }
    } catch (e) {
      debugPrint('Error initializing Bluetooth: $e');
    }
  }

  Future<void> _loadPairedDevices() async {
    try {
      List<BluetoothDevice> devices = await _bluetooth.getPairedDevices();
      setState(() {
        _pairedDevices = devices;
      });

      // Start discovery to find devices in range
      _startDiscoveryWithTimeout();
    } catch (e) {
      debugPrint('Error loading paired devices: $e');
    }
  }

  void _listenToConnectionState() {
    _bluetooth.onConnectionChanged.listen((state) {
      setState(() {
        _connectionState = state;
        if (state.isConnected) {
          // Find the connected device
          _connectedDevice = _pairedDevices.firstWhere(
            (device) => device.address == state.deviceAddress,
            orElse: () => BluetoothDevice(
              name: 'Unknown Device',
              address: state.deviceAddress,
              paired: false,
            ),
          );
        } else {
          _connectedDevice = null;
        }
      });
    });
  }

  void _listenToIncomingData() {
    _bluetooth.onDataReceived.listen((data) {
      setState(() {
        _receivedData += '${data.data}\n';
      });
    });
  }

  void _listenToDeviceDiscovery() {
    _bluetooth.onDeviceDiscovered.listen((device) {
      setState(() {
        _discoveredDeviceAddresses.add(device.address);
      });
      debugPrint('Discovered device: ${device.name} (${device.address})');
    });
  }

  Future<void> _startDiscoveryWithTimeout() async {
    try {
      // Cancel any existing timer
      _discoveryTimer?.cancel();

      // Clear previous discoveries
      setState(() {
        _discoveredDeviceAddresses.clear();
        _isScanning = true;
      });

      // Start discovery
      await _bluetooth.startDiscovery();
      debugPrint('Started device discovery');

      // Set timer to stop discovery after 8 seconds
      _discoveryTimer = Timer(const Duration(seconds: 8), () async {
        try {
          await _bluetooth.stopDiscovery();
          if (mounted) {
            setState(() {
              _isScanning = false;
            });
          }
          debugPrint(
            'Stopped device discovery. Found ${_discoveredDeviceAddresses.length} devices',
          );
        } catch (e) {
          debugPrint('Error stopping discovery: $e');
        }
      });
    } catch (e) {
      debugPrint('Error starting discovery: $e');
      setState(() {
        _isScanning = false;
      });
    }
  }

  Future<void> _connectToDevice(BluetoothDevice device) async {
    setState(() {
      _isConnecting = true;
    });

    try {
      await _bluetooth.connect(device.address);
    } catch (e) {
      debugPrint('Error connecting to device: $e');
      if (mounted) {
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('Failed to connect: $e')));
      }
    } finally {
      if (mounted) {
        setState(() {
          _isConnecting = false;
        });
      }
    }
  }

  Future<void> _disconnect() async {
    try {
      await _bluetooth.disconnect();
    } catch (e) {
      debugPrint('Error disconnecting: $e');
    }
  }

  int _calculateChecksum(Uint8List msg) {
    int cs = 0;
    for (int i = 0; i < msg.length - 1; i++) {
      cs = (cs + msg[i]) & 0xFF;
    }
    return (0x00 - cs) & 0xFF;
  }

  Future<void> _sendMessage() async {
    if (_connectionState?.isConnected == true) {
      try {
        ByteData bufferBuilder = ByteData(4);
        bufferBuilder.setUint8(0, 0x47);
        bufferBuilder.setUint8(1, 0x14);
        bufferBuilder.setUint8(2, 0x22);
        bufferBuilder.setUint8(3, 0);

        bufferBuilder.setUint8(
          3,
          _calculateChecksum(bufferBuilder.buffer.asUint8List()),
        );
        Uint8List msg = bufferBuilder.buffer.asUint8List();

        // Send Uint8List directly to platform channel (zero-copy)
        await _bluetooth.sendData(msg);
        debugPrint(
          'Sent CMD_RUN message: ${msg.map((b) => '0x${b.toRadixString(16).padLeft(2, '0')}').join(', ')}',
        );
        _messageController.clear();
      } catch (e) {
        debugPrint('Error sending message: $e');
        if (mounted) {
          ScaffoldMessenger.of(
            context,
          ).showSnackBar(SnackBar(content: Text('Failed to send message: $e')));
        }
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        title: const Text('Bluetooth Classic Demo'),
      ),
      body: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Bluetooth Status
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16.0),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Bluetooth Status',
                      style: Theme.of(context).textTheme.titleMedium,
                    ),
                    const SizedBox(height: 8),
                    Row(
                      children: [
                        Icon(
                          _isBluetoothAvailable
                              ? Icons.bluetooth
                              : Icons.bluetooth_disabled,
                          color: _isBluetoothAvailable
                              ? Colors.blue
                              : Colors.grey,
                        ),
                        const SizedBox(width: 8),
                        Text(
                          _isBluetoothAvailable ? 'Available' : 'Not Available',
                        ),
                      ],
                    ),
                    const SizedBox(height: 8),
                    Row(
                      children: [
                        Icon(
                          _connectionState?.isConnected == true
                              ? Icons.link
                              : Icons.link_off,
                          color: _connectionState?.isConnected == true
                              ? Colors.green
                              : Colors.red,
                        ),
                        const SizedBox(width: 8),
                        Text(_connectionState?.status ?? 'disconnected'),
                      ],
                    ),
                    if (_connectedDevice != null) ...[
                      const SizedBox(height: 8),
                      Text('Connected to: ${_connectedDevice!.name}'),
                      Text('Address: ${_connectedDevice!.address}'),
                    ],
                  ],
                ),
              ),
            ),

            const SizedBox(height: 16),

            // Nearby Devices (Filtered by Discovery)
            Expanded(
              flex: 2,
              child: Card(
                child: Padding(
                  padding: const EdgeInsets.all(16.0),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          Expanded(
                            child: Row(
                              children: [
                                Text(
                                  'Nearby Devices',
                                  style: Theme.of(
                                    context,
                                  ).textTheme.titleMedium,
                                ),
                                if (_isScanning) ...[
                                  const SizedBox(width: 12),
                                  const SizedBox(
                                    width: 16,
                                    height: 16,
                                    child: CircularProgressIndicator(
                                      strokeWidth: 2,
                                    ),
                                  ),
                                  const SizedBox(width: 8),
                                  const Text(
                                    'Scanning...',
                                    style: TextStyle(fontSize: 12),
                                  ),
                                ],
                              ],
                            ),
                          ),
                          IconButton(
                            onPressed: _isScanning ? null : _loadPairedDevices,
                            icon: const Icon(Icons.refresh),
                          ),
                        ],
                      ),
                      const SizedBox(height: 8),
                      Expanded(
                        child: () {
                          // Filter devices to show only those discovered (in range)
                          final nearbyDevices = _pairedDevices
                              .where(
                                (d) => _discoveredDeviceAddresses.contains(
                                  d.address,
                                ),
                              )
                              .toList();

                          if (_pairedDevices.isEmpty) {
                            return const Center(
                              child: Text('No paired devices found'),
                            );
                          }

                          if (_isScanning && nearbyDevices.isEmpty) {
                            return const Center(
                              child: Text('Scanning for nearby devices...'),
                            );
                          }

                          if (!_isScanning && nearbyDevices.isEmpty) {
                            return Center(
                              child: Column(
                                mainAxisAlignment: MainAxisAlignment.center,
                                children: [
                                  const Icon(
                                    Icons.bluetooth_searching,
                                    size: 48,
                                    color: Colors.grey,
                                  ),
                                  const SizedBox(height: 16),
                                  Text(
                                    'No paired devices in range',
                                    style: Theme.of(
                                      context,
                                    ).textTheme.bodyMedium,
                                  ),
                                  const SizedBox(height: 8),
                                  const Text(
                                    'Make sure devices are powered on',
                                    style: TextStyle(
                                      fontSize: 12,
                                      color: Colors.grey,
                                    ),
                                  ),
                                ],
                              ),
                            );
                          }

                          return ListView.builder(
                            itemCount: nearbyDevices.length,
                            itemBuilder: (context, index) {
                              final device = nearbyDevices[index];
                              final isConnected =
                                  _connectedDevice?.address == device.address;
                              return ListTile(
                                leading: Icon(
                                  Icons.devices,
                                  color: isConnected
                                      ? Colors.green
                                      : Colors.blue,
                                ),
                                title: Text(device.name),
                                subtitle: Text(device.address),
                                trailing: isConnected
                                    ? ElevatedButton(
                                        onPressed: _disconnect,
                                        child: const Text('Disconnect'),
                                      )
                                    : ElevatedButton(
                                        onPressed:
                                            _connectionState?.isConnected !=
                                                    true &&
                                                !_isConnecting
                                            ? () => _connectToDevice(device)
                                            : null,
                                        child: _isConnecting
                                            ? const Row(
                                                mainAxisSize: MainAxisSize.min,
                                                children: [
                                                  SizedBox(
                                                    width: 16,
                                                    height: 16,
                                                    child:
                                                        CircularProgressIndicator(
                                                          strokeWidth: 2,
                                                        ),
                                                  ),
                                                  SizedBox(width: 8),
                                                  Text('Connecting...'),
                                                ],
                                              )
                                            : const Text('Connect'),
                                      ),
                              );
                            },
                          );
                        }(),
                      ),
                    ],
                  ),
                ),
              ),
            ),

            const SizedBox(height: 16), // Message Section
            if (_connectionState?.isConnected == true) ...[
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16.0),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'Send Message',
                        style: Theme.of(context).textTheme.titleMedium,
                      ),
                      const SizedBox(height: 8),
                      Row(
                        children: [
                          Expanded(
                            child: TextField(
                              controller: _messageController,
                              decoration: const InputDecoration(
                                hintText: 'Enter message to send',
                                border: OutlineInputBorder(),
                              ),
                            ),
                          ),
                          const SizedBox(width: 8),
                          ElevatedButton(
                            onPressed: _sendMessage,
                            child: const Text('Send'),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 16),
            ],

            // Received Data
            Expanded(
              flex: 1,
              child: Card(
                child: Padding(
                  padding: const EdgeInsets.all(16.0),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          Text(
                            'Received Data',
                            style: Theme.of(context).textTheme.titleMedium,
                          ),
                          IconButton(
                            onPressed: () {
                              setState(() {
                                _receivedData = '';
                              });
                            },
                            icon: const Icon(Icons.clear),
                          ),
                        ],
                      ),
                      const SizedBox(height: 8),
                      Expanded(
                        child: Container(
                          width: double.infinity,
                          padding: const EdgeInsets.all(8.0),
                          decoration: BoxDecoration(
                            border: Border.all(color: Colors.grey),
                            borderRadius: BorderRadius.circular(4.0),
                          ),
                          child: SingleChildScrollView(
                            child: Text(
                              _receivedData.isEmpty
                                  ? 'No data received'
                                  : _receivedData,
                              style: const TextStyle(fontFamily: 'monospace'),
                            ),
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  @override
  void dispose() {
    _messageController.dispose();
    _discoveryTimer?.cancel();
    super.dispose();
  }
}
