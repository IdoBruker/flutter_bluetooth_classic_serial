#include "bluetooth_manager.h"
#include "bluetooth_classic_com_transport.h"
#include "bluetooth_classic_registry_enum.h"
#include "bluetooth_connection.h"
#include "bluetooth_server.h"
#include "flutter_bluetooth_classic_plugin.h"

#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

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
    if (active_com_connection_) {
      active_com_connection_->Close();
      active_com_connection_.reset();
    }
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
    std::vector<ClassicDeviceInfo> devices = BuildMergedClassicDeviceList();
    CacheKnownDevices(devices);
    flutter::EncodableList device_list;

    std::string connected_address;
    std::string connected_com_port;
    {
      std::lock_guard<std::mutex> lock(connection_mutex_);
      if (active_com_connection_ && active_com_connection_->IsConnected()) {
        connected_com_port = NormalizeComPort(active_com_connection_->GetComPort());
      }
      if (active_connection_ && active_connection_->IsConnected()) {
        connected_address = NormalizeAddress(active_connection_->GetDeviceAddress());
      }
    }

    for (auto& device : devices) {
      if (!connected_address.empty() && NormalizeAddress(device.address) == connected_address) {
        device.connected = true;
      }
      if (!connected_com_port.empty() && NormalizeComPort(device.com_port) == connected_com_port) {
        device.connected = true;
      }
      device_list.push_back(flutter::EncodableValue(device.ToEncodableMap()));
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
      std::vector<ClassicDeviceInfo> devices = BuildMergedClassicDeviceList();
      CacheKnownDevices(devices);

      std::string request_key = NormalizeAddress(address);
      if (request_key.empty()) {
        request_key = "COM:" + NormalizeComPort(address);
      }
      if (request_key.rfind("COM:", 0) != 0 && request_key.find(':') == std::string::npos) {
        request_key = "COM:" + NormalizeComPort(address);
      }

      ClassicDeviceInfo target;
      bool has_target = false;
      {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        auto known = known_devices_by_key_.find(request_key);
        if (known != known_devices_by_key_.end()) {
          target = known->second;
          has_target = true;
        }
      }

      if (!has_target) {
        target.address = NormalizeAddress(address);
        target.connect_key = request_key;
      }

      std::string com_error;
      std::string winrt_error;
      bool connected = false;
      {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        if (active_com_connection_) {
          active_com_connection_->Close();
          active_com_connection_.reset();
        }
        if (active_connection_) {
          active_connection_->Close();
          active_connection_.reset();
        }

        if (!target.com_port.empty()) {
          connected = ConnectViaComLocked(target, &com_error);
        }
        if (!connected) {
          const std::string winrt_address =
              !target.address.empty() ? target.address : NormalizeAddress(address);
          connected = ConnectViaWinRtLocked(winrt_address, &winrt_error);
        }
      }

      if (connected) {
        result_ptr->Success(flutter::EncodableValue(true));
      } else {
        std::string reason = "COM_NOT_FOUND_OR_FAILED";
        if (!com_error.empty() && !winrt_error.empty()) {
          reason = "COM_OPEN_FAILED; WINRT_FALLBACK_FAILED";
        } else if (!winrt_error.empty()) {
          reason = "WINRT_FALLBACK_FAILED";
        } else if (!com_error.empty()) {
          reason = "COM_OPEN_FAILED";
        }
        result_ptr->Error(
            "CONNECTION_FAILED",
            "Failed to connect (" + reason + "). COM=[" + com_error + "] WINRT=[" + winrt_error + "]");
      }
    } catch (hresult_error const& ex) {
      std::wstring msg_wide = ex.message().c_str();
      std::string msg(msg_wide.begin(), msg_wide.end());
      result_ptr->Error("CONNECTION_FAILED", "Failed to connect: " + msg);
    } catch (std::exception const& ex) {
      result_ptr->Error("CONNECTION_FAILED", "Failed to connect: " + std::string(ex.what()));
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
      if (active_com_connection_) {
        active_com_connection_->Close();
        active_com_connection_.reset();
      }

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
  if (active_com_connection_) {
    active_com_connection_->Close();
    active_com_connection_.reset();
  }
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

  const bool winrt_connected = active_connection_ && active_connection_->IsConnected();
  const bool com_connected = active_com_connection_ && active_com_connection_->IsConnected();
  if (!winrt_connected && !com_connected) {
    result->Error("NOT_CONNECTED", "Not connected to any device");
    return;
  }

  try {
    if (com_connected) {
      active_com_connection_->WriteData(data);
    } else {
      active_connection_->WriteData(data);
    }
    result->Success(flutter::EncodableValue(true));
  } catch (hresult_error const& ex) {
    std::wstring msg_wide = ex.message().c_str();
    std::string msg(msg_wide.begin(), msg_wide.end());
    result->Error("SEND_FAILED", "Failed to send data: " + msg);
  } catch (std::exception const& ex) {
    result->Error("SEND_FAILED", "Failed to send data: " + std::string(ex.what()));
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
  std::string addr_no_colons = NormalizeAddress(address);
  addr_no_colons.erase(std::remove(addr_no_colons.begin(), addr_no_colons.end(), ':'), addr_no_colons.end());
  
  // Parse hex string
  std::stringstream ss;
  ss << std::hex << addr_no_colons;
  ss >> result;
  
  return result;
}

std::string BluetoothManager::NormalizeAddress(const std::string& address) {
  std::string hex_only;
  hex_only.reserve(address.size());
  for (char c : address) {
    if (std::isxdigit(static_cast<unsigned char>(c))) {
      hex_only.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
  }

  if (hex_only.size() != 12) {
    return "";
  }

  std::stringstream formatted;
  for (size_t i = 0; i < hex_only.size(); ++i) {
    formatted << hex_only[i];
    if (i % 2 == 1 && i < hex_only.size() - 1) {
      formatted << ":";
    }
  }
  return formatted.str();
}

std::string BluetoothManager::NormalizeComPort(const std::string& com_port) {
  std::string normalized = com_port;
  if (normalized.rfind("COM:", 0) == 0) {
    normalized = normalized.substr(4);
  }
  if (normalized.rfind("\\\\.\\", 0) == 0) {
    normalized = normalized.substr(4);
  }
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return normalized;
}

std::vector<ClassicDeviceInfo> BluetoothManager::BuildMergedClassicDeviceList() {
  std::unordered_map<std::string, ClassicDeviceInfo> merged;

  BluetoothClassicRegistryEnumerator registry_enumerator;
  auto registry_devices = registry_enumerator.EnumerateClassicSppDevices();
  for (auto& registry_device : registry_devices) {
    registry_device.address = NormalizeAddress(registry_device.address);
    registry_device.com_port = NormalizeComPort(registry_device.com_port);
    if (registry_device.connect_key.empty()) {
      if (!registry_device.address.empty()) {
        registry_device.connect_key = registry_device.address;
      } else if (!registry_device.com_port.empty()) {
        registry_device.connect_key = "COM:" + registry_device.com_port;
      }
    }
    if (registry_device.connect_key.empty()) {
      continue;
    }
    merged[registry_device.connect_key] = registry_device;
  }

  hstring selector = BluetoothDevice::GetDeviceSelectorFromPairingState(true);
  auto devices_async = DeviceInformation::FindAllAsync(selector);
  auto devices = RunOnBackgroundThread<decltype(devices_async.get())>(devices_async);
  for (const auto& device_info : devices) {
    ClassicDeviceInfo device;
    std::wstring name_wide = device_info.Name().c_str();
    device.name = std::string(name_wide.begin(), name_wide.end());
    device.paired = true;
    device.remembered = true;
    device.source = "winrt-classic";

    std::wstring id_wide = device_info.Id().c_str();
    device.device_id = std::string(id_wide.begin(), id_wide.end());

    try {
      auto bt_device_async = BluetoothDevice::FromIdAsync(device_info.Id());
      auto bt_device = RunOnBackgroundThread<decltype(bt_device_async.get())>(bt_device_async);
      if (bt_device) {
        device.address = NormalizeAddress(BluetoothAddressToString(bt_device.BluetoothAddress()));
      }
    } catch (...) {
      // Keep device even if WinRT object cannot be materialized.
    }

    if (device.address.empty()) {
      continue;
    }
    device.connect_key = device.address;

    auto it = merged.find(device.connect_key);
    if (it == merged.end()) {
      merged[device.connect_key] = device;
      continue;
    }
    ClassicDeviceInfo& existing = it->second;
    if (existing.name.empty() && !device.name.empty()) {
      existing.name = device.name;
    }
    if (existing.device_id.empty()) {
      existing.device_id = device.device_id;
    }
    existing.paired = existing.paired || device.paired;
    existing.remembered = existing.remembered || device.remembered;
    if (existing.source != "winrt-classic") {
      existing.source = "registry+winrt";
    }
  }

  std::vector<ClassicDeviceInfo> result;
  result.reserve(merged.size());
  for (auto& entry : merged) {
    ClassicDeviceInfo device = entry.second;
    if (device.address.empty() && !device.com_port.empty()) {
      device.address = "COM:" + device.com_port;
    }
    if (device.connect_key.empty()) {
      device.connect_key = entry.first;
    }
    result.push_back(device);
  }
  return result;
}

void BluetoothManager::CacheKnownDevices(const std::vector<ClassicDeviceInfo>& devices) {
  std::lock_guard<std::mutex> lock(connection_mutex_);
  known_devices_by_key_.clear();
  for (const auto& device : devices) {
    if (!device.connect_key.empty()) {
      known_devices_by_key_[device.connect_key] = device;
    }
    if (!device.address.empty()) {
      known_devices_by_key_[NormalizeAddress(device.address)] = device;
    }
    if (!device.com_port.empty()) {
      known_devices_by_key_["COM:" + NormalizeComPort(device.com_port)] = device;
    }
  }
}

bool BluetoothManager::ConnectViaComLocked(const ClassicDeviceInfo& device, std::string* error_message) {
  if (device.com_port.empty()) {
    if (error_message != nullptr) {
      *error_message = "COM_NOT_FOUND";
    }
    return false;
  }

  auto connection = std::make_unique<BluetoothClassicComTransport>(
      device.com_port, !device.address.empty() ? device.address : "COM:" + device.com_port, connection_handler_, data_handler_);

  std::string open_error;
  if (!connection->Open(&open_error)) {
    if (error_message != nullptr) {
      *error_message = open_error.empty() ? "COM_OPEN_FAILED" : open_error;
    }
    return false;
  }
  active_com_connection_ = std::move(connection);
  return true;
}

bool BluetoothManager::ConnectViaWinRtLocked(const std::string& address, std::string* error_message) {
  const std::string normalized_address = NormalizeAddress(address);
  if (normalized_address.empty()) {
    if (error_message != nullptr) {
      *error_message = "INVALID_BT_ADDRESS";
    }
    return false;
  }

  uint64_t bt_address = StringToBluetoothAddress(normalized_address);
  auto bt_device_async = BluetoothDevice::FromBluetoothAddressAsync(bt_address);
  auto bt_device = bt_device_async.get();
  if (!bt_device) {
    if (error_message != nullptr) {
      *error_message = "DEVICE_NOT_FOUND";
    }
    return false;
  }

  auto services_async = bt_device.GetRfcommServicesAsync(BluetoothCacheMode::Uncached);
  auto services_result = services_async.get();
  if (services_result.Services().Size() == 0) {
    if (error_message != nullptr) {
      *error_message = "NO_SERVICES";
    }
    return false;
  }

  RfcommServiceId spp_service_id = RfcommServiceId::SerialPort();
  RfcommDeviceService rfcomm_service{nullptr};
  for (const auto& service : services_result.Services()) {
    if (service.ServiceId().Uuid() == spp_service_id.Uuid()) {
      rfcomm_service = service;
      break;
    }
  }
  if (!rfcomm_service && services_result.Services().Size() > 0) {
    rfcomm_service = services_result.Services().GetAt(0);
  }
  if (!rfcomm_service) {
    if (error_message != nullptr) {
      *error_message = "NO_SPP_SERVICE";
    }
    return false;
  }

  StreamSocket socket;
  auto connect_async =
      socket.ConnectAsync(rfcomm_service.ConnectionHostName(), rfcomm_service.ConnectionServiceName());
  connect_async.get();

  active_connection_ =
      std::make_unique<BluetoothConnection>(socket, normalized_address, connection_handler_, data_handler_);
  return true;
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

    ClassicDeviceInfo device;
    std::wstring name_wide = device_info.Name().c_str();
    device.name = std::string(name_wide.begin(), name_wide.end());
    device.address = NormalizeAddress(BluetoothAddressToString(bt_device.BluetoothAddress()));
    device.paired = device_info.Pairing().IsPaired();
    device.remembered = true;
    device.source = "winrt-discovery";
    device.connect_key = device.address;

    std::lock_guard<std::mutex> lock(connection_mutex_);
    auto known = known_devices_by_key_.find(device.connect_key);
    if (known != known_devices_by_key_.end()) {
      device.com_port = known->second.com_port;
    }
    known_devices_by_key_[device.connect_key] = device;

    flutter::EncodableMap event_map;
    event_map[flutter::EncodableValue("event")] = flutter::EncodableValue("deviceFound");
    event_map[flutter::EncodableValue("device")] = flutter::EncodableValue(device.ToEncodableMap());
    
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
