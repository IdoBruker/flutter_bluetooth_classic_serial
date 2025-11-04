#ifndef FLUTTER_PLUGIN_BLUETOOTH_SERVER_H_
#define FLUTTER_PLUGIN_BLUETOOTH_SERVER_H_

#include <flutter/encodable_value.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Bluetooth.Rfcomm.h>
#include <winrt/Windows.Networking.Sockets.h>

#include <memory>
#include <string>
#include <atomic>
#include <functional>

namespace flutter_bluetooth_classic {

template<typename T>
class EventStreamHandler;

class BluetoothConnection;

class BluetoothServer {
public:
  using ConnectionCallback = std::function<void(std::unique_ptr<BluetoothConnection>)>;

  BluetoothServer(
      const std::string& service_name,
      EventStreamHandler<flutter::EncodableValue>* connection_handler,
      EventStreamHandler<flutter::EncodableValue>* data_handler,
      ConnectionCallback on_connection);

  ~BluetoothServer();

  // Start listening for incoming connections
  void StartListening();

  // Stop listening
  void StopListening();

  // Check if server is running
  bool IsListening() const { return is_listening_; }

private:
  // Initialize RFCOMM service provider
  void InitializeServiceProvider();

  // Service information
  std::string service_name_;
  
  // RFCOMM service provider
  winrt::Windows::Devices::Bluetooth::Rfcomm::RfcommServiceProvider service_provider_{nullptr};
  
  // Stream socket listener
  winrt::Windows::Networking::Sockets::StreamSocketListener socket_listener_{nullptr};
  winrt::event_token connection_received_token_{};

  // State
  std::atomic<bool> is_listening_{false};

  // Event handlers (not owned)
  EventStreamHandler<flutter::EncodableValue>* connection_handler_;
  EventStreamHandler<flutter::EncodableValue>* data_handler_;

  // Callback for new connections
  ConnectionCallback on_connection_;
};

}  // namespace flutter_bluetooth_classic

#endif  // FLUTTER_PLUGIN_BLUETOOTH_SERVER_H_
