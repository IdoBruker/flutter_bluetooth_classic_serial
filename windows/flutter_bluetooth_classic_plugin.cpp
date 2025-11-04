#include "flutter_bluetooth_classic_plugin.h"

#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <memory>
#include <sstream>

#include <winrt/base.h>

#include "include/flutter_bluetooth_classic/flutter_bluetooth_classic_plugin_c_api.h"

namespace flutter_bluetooth_classic {

// Channel names - must match Dart implementation
constexpr char kMethodChannelName[] = "com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic";
constexpr char kStateChannelName[] = "com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic_state";
constexpr char kConnectionChannelName[] = "com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic_connection";
constexpr char kDataChannelName[] = "com.flutter_bluetooth_classic.plugin/flutter_bluetooth_classic_data";

// Static registration
void FlutterBluetoothClassicPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  
  auto method_channel = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
      registrar->messenger(), kMethodChannelName,
      &flutter::StandardMethodCodec::GetInstance());

  auto state_channel = std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
      registrar->messenger(), kStateChannelName,
      &flutter::StandardMethodCodec::GetInstance());

  auto connection_channel = std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
      registrar->messenger(), kConnectionChannelName,
      &flutter::StandardMethodCodec::GetInstance());

  auto data_channel = std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
      registrar->messenger(), kDataChannelName,
      &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<FlutterBluetoothClassicPlugin>(
      registrar,
      std::move(method_channel),
      std::move(state_channel),
      std::move(connection_channel),
      std::move(data_channel));

  registrar->AddPlugin(std::move(plugin));
}

FlutterBluetoothClassicPlugin::FlutterBluetoothClassicPlugin(
    flutter::PluginRegistrarWindows* registrar,
    std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> method_channel,
    std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>> state_channel,
    std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>> connection_channel,
    std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>> data_channel)
    : method_channel_(std::move(method_channel)),
      state_channel_(std::move(state_channel)),
      connection_channel_(std::move(connection_channel)),
      data_channel_(std::move(data_channel)),
      registrar_(registrar) {

  // Create stream handlers and transfer ownership to channels
  auto state_handler = std::make_unique<EventStreamHandler<>>();
  auto connection_handler = std::make_unique<EventStreamHandler<>>();
  auto data_handler = std::make_unique<EventStreamHandler<>>();

  // Keep raw pointers for later use
  state_handler_ = state_handler.get();
  connection_handler_ = connection_handler.get();
  data_handler_ = data_handler.get();

  // Set stream handlers for event channels (transfers ownership)
  state_channel_->SetStreamHandler(std::move(state_handler));
  connection_channel_->SetStreamHandler(std::move(connection_handler));
  data_channel_->SetStreamHandler(std::move(data_handler));

  // Set method call handler
  method_channel_->SetMethodCallHandler(
      [this](const auto& call, auto result) {
        HandleMethodCall(call, std::move(result));
      });

  // Note: WinRT apartment initialization is handled per-thread as needed
  // Flutter's platform thread already has STA initialized, so we don't initialize it here
  
  // Create Bluetooth manager
  bluetooth_manager_ = std::make_unique<BluetoothManager>(
      state_handler_,
      connection_handler_,
      data_handler_);
}

FlutterBluetoothClassicPlugin::~FlutterBluetoothClassicPlugin() {
  // Cleanup will happen automatically via unique_ptr
  // COM/WinRT cleanup is handled by main.cpp via CoUninitialize()
}

void FlutterBluetoothClassicPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

  const std::string& method = method_call.method_name();

  if (method == "isBluetoothSupported") {
    bluetooth_manager_->IsBluetoothSupported(std::move(result));
  }
  else if (method == "isBluetoothEnabled") {
    bluetooth_manager_->IsBluetoothEnabled(std::move(result));
  }
  else if (method == "enableBluetooth") {
    // Windows doesn't support programmatic Bluetooth enabling
    result->Error("UNSUPPORTED",
                  "Cannot enable Bluetooth programmatically on Windows. "
                  "Please enable it manually in system settings.");
  }
  else if (method == "getPairedDevices") {
    bluetooth_manager_->GetPairedDevices(std::move(result));
  }
  else if (method == "startDiscovery") {
    bluetooth_manager_->StartDiscovery(std::move(result));
  }
  else if (method == "stopDiscovery") {
    bluetooth_manager_->StopDiscovery(std::move(result));
  }
  else if (method == "connect") {
    const auto* args = std::get_if<flutter::EncodableMap>(method_call.arguments());
    if (!args) {
      result->Error("INVALID_ARGUMENT", "Arguments must be a map");
      return;
    }

    auto address_it = args->find(flutter::EncodableValue("address"));
    if (address_it == args->end()) {
      result->Error("INVALID_ARGUMENT", "Device address is required");
      return;
    }

    const auto* address = std::get_if<std::string>(&address_it->second);
    if (!address) {
      result->Error("INVALID_ARGUMENT", "Device address must be a string");
      return;
    }

    bluetooth_manager_->Connect(*address, std::move(result));
  }
  else if (method == "listen") {
    const auto* args = std::get_if<flutter::EncodableMap>(method_call.arguments());
    std::string app_name = "FlutterBluetoothClassic";
    
    if (args) {
      auto app_name_it = args->find(flutter::EncodableValue("appName"));
      if (app_name_it != args->end()) {
        const auto* name = std::get_if<std::string>(&app_name_it->second);
        if (name && !name->empty()) {
          app_name = *name;
        }
      }
    }

    bluetooth_manager_->Listen(app_name, std::move(result));
  }
  else if (method == "disconnect") {
    bluetooth_manager_->Disconnect(std::move(result));
  }
  else if (method == "stopListen") {
    bluetooth_manager_->StopListen(std::move(result));
  }
  else if (method == "sendData") {
    const auto* args = std::get_if<flutter::EncodableMap>(method_call.arguments());
    if (!args) {
      result->Error("INVALID_ARGUMENT", "Arguments must be a map");
      return;
    }

    auto data_it = args->find(flutter::EncodableValue("data"));
    if (data_it == args->end()) {
      result->Error("INVALID_ARGUMENT", "Data is required");
      return;
    }

    // Flutter sends Uint8List which arrives as std::vector<uint8_t>
    const auto* data = std::get_if<std::vector<uint8_t>>(&data_it->second);
    if (!data) {
      result->Error("INVALID_ARGUMENT", "Data must be a byte array");
      return;
    }

    bluetooth_manager_->SendData(*data, std::move(result));
  }
  else {
    result->NotImplemented();
  }
}

}  // namespace flutter_bluetooth_classic

// C API implementation
void FlutterBluetoothClassicPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  flutter_bluetooth_classic::FlutterBluetoothClassicPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}

// Wrapper for Flutter's generated code
void FlutterBluetoothClassicPluginRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  FlutterBluetoothClassicPluginCApiRegisterWithRegistrar(registrar);
}
