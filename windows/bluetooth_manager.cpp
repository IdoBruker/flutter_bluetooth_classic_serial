#include "bluetooth_manager.h"
#include "bluetooth_connection.h"
#include "bluetooth_server.h"
#include "flutter_bluetooth_classic_plugin.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <sstream>
#include <iomanip>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Rfcomm;
using namespace Windows::Devices::Enumeration;
using namespace Windows::Devices::Radios;
using namespace Windows::Networking::Sockets;

namespace flutter_bluetooth_classic {

BluetoothManager::BluetoothManager(
    EventStreamHandler<flutter::EncodableValue>* state_handler,
    EventStreamHandler<flutter::EncodableValue>* connection_handler,
    EventStreamHandler<flutter::EncodableValue>* data_handler)
    : state_handler_(state_handler),
      connection_handler_(connection_handler),
      data_handler_(data_handler) {
  
  // Initialize Bluetooth radio
  InitializeBluetoothRadio();
}

BluetoothManager::~BluetoothManager() {
  // Stop discovery if running
  if (device_watcher_) {
    if (device_watcher_.Status() == DeviceWatcherStatus::Started ||
        device_watcher_.Status() == DeviceWatcherStatus::EnumerationCompleted) {
      device_watcher_.Stop();
    }
    
    // Unregister event handlers
    device_watcher_.Added(watcher_added_token_);
    device_watcher_.Updated(watcher_updated_token_);
    device_watcher_.Removed(watcher_removed_token_);
    device_watcher_.EnumerationCompleted(watcher_completed_token_);
  }

  // Disconnect active connection
  {
    std::lock_guard<std::mutex> lock(connection_mutex_);
    if (active_connection_) {
      active_connection_->Close();
      active_connection_.reset();
    }
  }

  // Stop server
  {
    std::lock_guard<std::mutex> lock(server_mutex_);
    if (bluetooth_server_) {
      bluetooth_server_->StopListening();
      bluetooth_server_.reset();
    }
  }

  // Unregister radio state handler
  if (bluetooth_radio_ && radio_state_token_) {
    bluetooth_radio_.StateChanged(radio_state_token_);
  }
}

void BluetoothManager::InitializeBluetoothRadio() {
  try {
    // Get all radios (run on background thread to avoid STA issues)
    auto radios_async = Radio::GetRadiosAsync();
    auto radios = RunOnBackgroundThread<decltype(radios_async.get())>(radios_async);

    // Find Bluetooth radio
    for (const auto& radio : radios) {
      if (radio.Kind() == RadioKind::Bluetooth) {
        bluetooth_radio_ = radio;
        
        // Register for state changes
        radio_state_token_ = bluetooth_radio_.StateChanged(
            [this](Radio const& radio, auto const&) {
              flutter::EncodableMap state_map;
              bool is_enabled = (radio.State() == RadioState::On);
              
              std::string status;
              switch (radio.State()) {
                case RadioState::On:
                  status = "ON";
                  break;
                case RadioState::Off:
                  status = "OFF";
                  break;
                case RadioState::Disabled:
                  status = "DISABLED";
                  break;
                default:
                  status = "UNKNOWN";
                  break;
              }

              state_map[flutter::EncodableValue("isEnabled")] = flutter::EncodableValue(is_enabled);
              state_map[flutter::EncodableValue("status")] = flutter::EncodableValue(status);
              
              state_handler_->Success(flutter::EncodableValue(state_map));
            });
        
        break;
      }
    }
  }
  catch (hresult_error const&) {
    // Failed to get Bluetooth radio
  }
}

void BluetoothManager::IsBluetoothSupported(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  result->Success(flutter::EncodableValue(bluetooth_radio_ != nullptr));
}

void BluetoothManager::IsBluetoothEnabled(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (!bluetooth_radio_) {
    result->Success(flutter::EncodableValue(false));
    return;
  }

  bool is_enabled = (bluetooth_radio_.State() == RadioState::On);
  result->Success(flutter::EncodableValue(is_enabled));
}

void BluetoothManager::GetPairedDevices(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  
  try {
    // Get paired Bluetooth devices
    hstring selector = BluetoothDevice::GetDeviceSelectorFromPairingState(true);
    auto devices_async = DeviceInformation::FindAllAsync(selector);
    auto devices = RunOnBackgroundThread<decltype(devices_async.get())>(devices_async);

    flutter::EncodableList device_list;
    
    for (const auto& device_info : devices) {
      flutter::EncodableMap device_map;
      
      // Get device name
      std::wstring name_wide = device_info.Name().c_str();
      std::string name(name_wide.begin(), name_wide.end());
      device_map[flutter::EncodableValue("name")] = flutter::EncodableValue(name);

      // Get Bluetooth address
      try {
        auto bt_device_async = BluetoothDevice::FromIdAsync(device_info.Id());
        auto bt_device = RunOnBackgroundThread<decltype(bt_device_async.get())>(bt_device_async);
        
        if (bt_device) {
          std::string address = BluetoothAddressToString(bt_device.BluetoothAddress());
          device_map[flutter::EncodableValue("address")] = flutter::EncodableValue(address);
          device_map[flutter::EncodableValue("paired")] = flutter::EncodableValue(true);
          
          device_list.push_back(flutter::EncodableValue(device_map));
        }
      }
      catch (...) {
        // Skip devices that can't be accessed
        continue;
      }
    }

    result->Success(flutter::EncodableValue(device_list));
  }
  catch (hresult_error const& ex) {
    std::wstring msg_wide = ex.message().c_str();
    std::string msg(msg_wide.begin(), msg_wide.end());
    result->Error("BLUETOOTH_ERROR", "Failed to get paired devices: " + msg);
  }
}

void BluetoothManager::StartDiscovery(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  
  try {
    // Stop existing watcher if any
    if (device_watcher_) {
      if (device_watcher_.Status() == DeviceWatcherStatus::Started ||
          device_watcher_.Status() == DeviceWatcherStatus::EnumerationCompleted) {
        device_watcher_.Stop();
      }
    }

    // Create device selector for Bluetooth devices (Classic, not BLE)
    hstring selector = BluetoothDevice::GetDeviceSelector();
    
    // Create device watcher
    device_watcher_ = DeviceInformation::CreateWatcher(selector);

    // Register event handlers
    watcher_added_token_ = device_watcher_.Added(
        {this, &BluetoothManager::OnDeviceAdded});
    
    watcher_updated_token_ = device_watcher_.Updated(
        {this, &BluetoothManager::OnDeviceUpdated});
    
    watcher_removed_token_ = device_watcher_.Removed(
        {this, &BluetoothManager::OnDeviceRemoved});
    
    watcher_completed_token_ = device_watcher_.EnumerationCompleted(
        {this, &BluetoothManager::OnEnumerationCompleted});

    // Start discovery
    device_watcher_.Start();

    result->Success(flutter::EncodableValue(true));
  }
  catch (hresult_error const& ex) {
    std::wstring msg_wide = ex.message().c_str();
    std::string msg(msg_wide.begin(), msg_wide.end());
    result->Error("BLUETOOTH_ERROR", "Failed to start discovery: " + msg);
  }
}

void BluetoothManager::StopDiscovery(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  
  if (device_watcher_) {
    if (device_watcher_.Status() == DeviceWatcherStatus::Started ||
        device_watcher_.Status() == DeviceWatcherStatus::EnumerationCompleted) {
      device_watcher_.Stop();
    }
  }

  result->Success(flutter::EncodableValue(true));
}

void BluetoothManager::Connect(
    const std::string& address,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  
  // Move the result to a shared_ptr so it can be safely captured by the thread
  auto result_ptr = std::shared_ptr<flutter::MethodResult<flutter::EncodableValue>>(std::move(result));
  
  // Run entire connection process on background thread to avoid blocking UI
  std::thread([this, address, result_ptr]() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    
    try {
      // Disconnect any existing connection
      {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        if (active_connection_) {
          active_connection_->Close();
          active_connection_.reset();
        }
      }

      // Convert address string to Bluetooth address
      uint64_t bt_address = StringToBluetoothAddress(address);

      // Get Bluetooth device from address
      auto bt_device_async = BluetoothDevice::FromBluetoothAddressAsync(bt_address);
      auto bt_device = bt_device_async.get();

      if (!bt_device) {
        result_ptr->Error("DEVICE_NOT_FOUND", "Device with address " + address + " not found");
        winrt::uninit_apartment();
        return;
      }

      // Get RFCOMM services
      auto services_async = bt_device.GetRfcommServicesAsync(BluetoothCacheMode::Uncached);
      auto services_result = services_async.get();

      if (services_result.Services().Size() == 0) {
        result_ptr->Error("NO_SERVICES", "No RFCOMM services found on device");
        winrt::uninit_apartment();
        return;
      }

      // Use Serial Port Profile (SPP) UUID: 00001101-0000-1000-8000-00805F9B34FB
      RfcommServiceId spp_service_id = RfcommServiceId::SerialPort();
      RfcommDeviceService rfcomm_service{nullptr};

      // Find SPP service
      for (const auto& service : services_result.Services()) {
        if (service.ServiceId().Uuid() == spp_service_id.Uuid()) {
          rfcomm_service = service;
          break;
        }
      }

      // If SPP not found, use first available service
      if (!rfcomm_service && services_result.Services().Size() > 0) {
        rfcomm_service = services_result.Services().GetAt(0);
      }

      if (!rfcomm_service) {
        result_ptr->Error("NO_SPP_SERVICE", "Serial Port Profile service not found");
        winrt::uninit_apartment();
        return;
      }

      // Create socket and connect
      StreamSocket socket;
      auto connect_async = socket.ConnectAsync(
          rfcomm_service.ConnectionHostName(),
          rfcomm_service.ConnectionServiceName());
      
      connect_async.get();

      // Create connection object
      {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        active_connection_ = std::make_unique<BluetoothConnection>(
            socket,
            address,
            connection_handler_,
            data_handler_);
      }

      result_ptr->Success(flutter::EncodableValue(true));
    }
    catch (hresult_error const& ex) {
      std::wstring msg_wide = ex.message().c_str();
      std::string msg(msg_wide.begin(), msg_wide.end());
      result_ptr->Error("CONNECTION_FAILED", "Failed to connect: " + msg);
      
      // Send connection error event
      flutter::EncodableMap connection_map;
      connection_map[flutter::EncodableValue("isConnected")] = flutter::EncodableValue(false);
      connection_map[flutter::EncodableValue("deviceAddress")] = flutter::EncodableValue(address);
      connection_map[flutter::EncodableValue("status")] = flutter::EncodableValue("ERROR: " + msg);
      connection_handler_->Success(flutter::EncodableValue(connection_map));
    }
    
    winrt::uninit_apartment();
  }).detach();  // Detach thread so it runs independently without blocking
}

void BluetoothManager::Listen(
    const std::string& app_name,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  
  try {
    // Stop existing server if any
    {
      std::lock_guard<std::mutex> lock(server_mutex_);
      if (bluetooth_server_) {
        bluetooth_server_->StopListening();
        bluetooth_server_.reset();
      }
    }

    // Create server with callback for incoming connections
    auto on_connection = [this](std::unique_ptr<BluetoothConnection> connection) {
      std::lock_guard<std::mutex> lock(connection_mutex_);
      
      // Close existing connection if any
      if (active_connection_) {
        active_connection_->Close();
      }
      
      active_connection_ = std::move(connection);
    };

    {
      std::lock_guard<std::mutex> lock(server_mutex_);
      bluetooth_server_ = std::make_unique<BluetoothServer>(
          app_name,
          connection_handler_,
          data_handler_,
          on_connection);
      
      bluetooth_server_->StartListening();
    }

    result->Success(flutter::EncodableValue(true));
  }
  catch (hresult_error const& ex) {
    std::wstring msg_wide = ex.message().c_str();
    std::string msg(msg_wide.begin(), msg_wide.end());
    result->Error("LISTEN_FAILED", "Failed to start listening: " + msg);
  }
}

void BluetoothManager::Disconnect(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  
  std::lock_guard<std::mutex> lock(connection_mutex_);
  if (active_connection_) {
    active_connection_->Close();
    active_connection_.reset();
  }

  result->Success(flutter::EncodableValue(true));
}

void BluetoothManager::StopListen(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  
  std::lock_guard<std::mutex> lock(server_mutex_);
  if (bluetooth_server_) {
    bluetooth_server_->StopListening();
    bluetooth_server_.reset();
  }

  result->Success(flutter::EncodableValue(true));
}

void BluetoothManager::SendData(
    const std::vector<uint8_t>& data,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  
  std::lock_guard<std::mutex> lock(connection_mutex_);
  
  if (!active_connection_ || !active_connection_->IsConnected()) {
    result->Error("NOT_CONNECTED", "Not connected to any device");
    return;
  }

  try {
    active_connection_->WriteData(data);
    result->Success(flutter::EncodableValue(true));
  }
  catch (hresult_error const& ex) {
    std::wstring msg_wide = ex.message().c_str();
    std::string msg(msg_wide.begin(), msg_wide.end());
    result->Error("SEND_FAILED", "Failed to send data: " + msg);
  }
}

// Helper methods
std::string BluetoothManager::BluetoothAddressToString(uint64_t address) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  
  // Format as XX:XX:XX:XX:XX:XX
  for (int i = 5; i >= 0; i--) {
    ss << std::setw(2) << ((address >> (i * 8)) & 0xFF);
    if (i > 0) ss << ":";
  }
  
  std::string result = ss.str();
  // Convert to uppercase
  std::transform(result.begin(), result.end(), result.begin(), ::toupper);
  return result;
}

uint64_t BluetoothManager::StringToBluetoothAddress(const std::string& address) {
  uint64_t result = 0;
  std::string addr_no_colons = address;
  
  // Remove colons
  addr_no_colons.erase(
      std::remove(addr_no_colons.begin(), addr_no_colons.end(), ':'),
      addr_no_colons.end());
  
  // Parse hex string
  std::stringstream ss;
  ss << std::hex << addr_no_colons;
  ss >> result;
  
  return result;
}

// Device watcher callbacks
void BluetoothManager::OnDeviceAdded(
    DeviceWatcher const& sender,
    DeviceInformation const& device_info) {
  
  try {
    // Get Bluetooth device to get address
    auto bt_device_async = BluetoothDevice::FromIdAsync(device_info.Id());
    auto bt_device = RunOnBackgroundThread<decltype(bt_device_async.get())>(bt_device_async);
    
    if (!bt_device) return;

    flutter::EncodableMap device_map;
    
    std::wstring name_wide = device_info.Name().c_str();
    std::string name(name_wide.begin(), name_wide.end());
    device_map[flutter::EncodableValue("name")] = flutter::EncodableValue(name);
    
    std::string address = BluetoothAddressToString(bt_device.BluetoothAddress());
    device_map[flutter::EncodableValue("address")] = flutter::EncodableValue(address);
    
    device_map[flutter::EncodableValue("paired")] = 
        flutter::EncodableValue(device_info.Pairing().IsPaired());

    flutter::EncodableMap event_map;
    event_map[flutter::EncodableValue("event")] = flutter::EncodableValue("deviceFound");
    event_map[flutter::EncodableValue("device")] = flutter::EncodableValue(device_map);
    
    state_handler_->Success(flutter::EncodableValue(event_map));
  }
  catch (...) {
    // Ignore errors for individual devices
  }
}

void BluetoothManager::OnDeviceUpdated(
    DeviceWatcher const& sender,
    DeviceInformationUpdate const& device_info_update) {
  // Not needed for now
}

void BluetoothManager::OnDeviceRemoved(
    DeviceWatcher const& sender,
    DeviceInformationUpdate const& device_info_update) {
  // Not needed for now
}

void BluetoothManager::OnEnumerationCompleted(
    DeviceWatcher const& sender,
    IInspectable const& obj) {
  // Discovery enumeration completed
}

}  // namespace flutter_bluetooth_classic
