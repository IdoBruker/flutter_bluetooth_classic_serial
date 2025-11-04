#ifndef FLUTTER_PLUGIN_BLUETOOTH_CONNECTION_H_
#define FLUTTER_PLUGIN_BLUETOOTH_CONNECTION_H_

#include <flutter/encodable_value.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Rfcomm.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Storage.Streams.h>

#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <functional>

namespace flutter_bluetooth_classic {

template<typename T>
class EventStreamHandler;

class BluetoothConnection {
public:
  BluetoothConnection(
      winrt::Windows::Networking::Sockets::StreamSocket socket,
      const std::string& device_address,
      EventStreamHandler<flutter::EncodableValue>* connection_handler,
      EventStreamHandler<flutter::EncodableValue>* data_handler);

  ~BluetoothConnection();

  // Write data to the connection
  void WriteData(const std::vector<uint8_t>& data);

  // Check if connection is active
  bool IsConnected() const { return is_connected_; }

  // Get device address
  std::string GetDeviceAddress() const { return device_address_; }

  // Close the connection
  void Close();

private:
  // Start reading data from the socket
  void StartReadLoop();

  // Read loop running in background thread
  void ReadLoop();

  // Send connection state to Flutter
  void SendConnectionState(bool is_connected, const std::string& status);

  // Send received data to Flutter
  void SendData(const std::vector<uint8_t>& data);

  // Socket and streams
  winrt::Windows::Networking::Sockets::StreamSocket socket_{nullptr};
  winrt::Windows::Storage::Streams::DataReader data_reader_{nullptr};
  winrt::Windows::Storage::Streams::DataWriter data_writer_{nullptr};

  // Device information
  std::string device_address_;

  // Connection state
  std::atomic<bool> is_connected_{false};
  std::atomic<bool> should_stop_{false};

  // Read thread
  std::thread read_thread_;

  // Event handlers (not owned)
  EventStreamHandler<flutter::EncodableValue>* connection_handler_;
  EventStreamHandler<flutter::EncodableValue>* data_handler_;
};

}  // namespace flutter_bluetooth_classic

#endif  // FLUTTER_PLUGIN_BLUETOOTH_CONNECTION_H_
