#ifndef FLUTTER_PLUGIN_BLUETOOTH_MANAGER_H_
#define FLUTTER_PLUGIN_BLUETOOTH_MANAGER_H_

#include <flutter/method_result.h>
#include <flutter/encodable_value.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Rfcomm.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Storage.Streams.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <future>
#include <thread>
#include <type_traits>

namespace flutter_bluetooth_classic {

// Forward declarations
template<typename T>
class EventStreamHandler;
class BluetoothConnection;
class BluetoothServer;

class BluetoothManager {
public:
  BluetoothManager(
      EventStreamHandler<flutter::EncodableValue>* state_handler,
      EventStreamHandler<flutter::EncodableValue>* connection_handler,
      EventStreamHandler<flutter::EncodableValue>* data_handler);

  ~BluetoothManager();

  // Platform method implementations
  void IsBluetoothSupported(
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  void IsBluetoothEnabled(
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  void GetPairedDevices(
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  void StartDiscovery(
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  void StopDiscovery(
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  void Connect(
      const std::string& address,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  void Listen(
      const std::string& app_name,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  void Disconnect(
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  void StopListen(
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  void SendData(
      const std::vector<uint8_t>& data,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

private:
  // Helper methods
  void InitializeBluetoothRadio();
  std::string BluetoothAddressToString(uint64_t address);
  uint64_t StringToBluetoothAddress(const std::string& address);
  
  // Template helper to run async operations on a background thread
  template<typename TResult, typename TAsync>
  TResult RunOnBackgroundThread(TAsync async_operation) {
    std::promise<TResult> promise;
    auto future = promise.get_future();
    
    std::thread worker([op = std::move(async_operation), &promise]() {
      winrt::init_apartment(winrt::apartment_type::multi_threaded);
      try {
        if constexpr (std::is_void_v<TResult>) {
          op.get();
          promise.set_value();
        } else {
          auto result = op.get();
          promise.set_value(result);
        }
      } catch (...) {
        try {
          promise.set_exception(std::current_exception());
        } catch (...) {}
      }
      winrt::uninit_apartment();
    });
    
    worker.join();  // Use join instead of detach to ensure proper cleanup
    return future.get();
  }
  
  // Device watcher callbacks
  void OnDeviceAdded(
      winrt::Windows::Devices::Enumeration::DeviceWatcher const& sender,
      winrt::Windows::Devices::Enumeration::DeviceInformation const& deviceInfo);
  
  void OnDeviceUpdated(
      winrt::Windows::Devices::Enumeration::DeviceWatcher const& sender,
      winrt::Windows::Devices::Enumeration::DeviceInformationUpdate const& deviceInfoUpdate);

  void OnDeviceRemoved(
      winrt::Windows::Devices::Enumeration::DeviceWatcher const& sender,
      winrt::Windows::Devices::Enumeration::DeviceInformationUpdate const& deviceInfoUpdate);

  void OnEnumerationCompleted(
      winrt::Windows::Devices::Enumeration::DeviceWatcher const& sender,
      winrt::Windows::Foundation::IInspectable const& obj);

  // Stream handlers (not owned)
  EventStreamHandler<flutter::EncodableValue>* state_handler_;
  EventStreamHandler<flutter::EncodableValue>* connection_handler_;
  EventStreamHandler<flutter::EncodableValue>* data_handler_;

  // Bluetooth radio
  winrt::Windows::Devices::Radios::Radio bluetooth_radio_{nullptr};
  winrt::event_token radio_state_token_{};

  // Device watcher
  winrt::Windows::Devices::Enumeration::DeviceWatcher device_watcher_{nullptr};
  winrt::event_token watcher_added_token_{};
  winrt::event_token watcher_updated_token_{};
  winrt::event_token watcher_removed_token_{};
  winrt::event_token watcher_completed_token_{};

  // Active connection
  std::unique_ptr<BluetoothConnection> active_connection_;
  std::mutex connection_mutex_;

  // Server for incoming connections
  std::unique_ptr<BluetoothServer> bluetooth_server_;
  std::mutex server_mutex_;
};

}  // namespace flutter_bluetooth_classic

#endif  // FLUTTER_PLUGIN_BLUETOOTH_MANAGER_H_
