#ifndef FLUTTER_PLUGIN_FLUTTER_BLUETOOTH_CLASSIC_PLUGIN_H_
#define FLUTTER_PLUGIN_FLUTTER_BLUETOOTH_CLASSIC_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/event_channel.h>
#include <flutter/event_sink.h>
#include <flutter/standard_method_codec.h>

#include <memory>
#include <mutex>

#include "bluetooth_manager.h"

namespace flutter_bluetooth_classic {

// EventStreamHandler for managing event channels
template<typename T = flutter::EncodableValue>
class EventStreamHandler : public flutter::StreamHandler<T> {
public:
  EventStreamHandler() = default;
  virtual ~EventStreamHandler() = default;

  // Notify listeners with data
  void Success(const T& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sink_) {
      sink_->Success(event);
    }
  }

  void Error(const std::string& error_code,
             const std::string& error_message,
             const T* error_details = nullptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sink_) {
      sink_->Error(error_code, error_message, error_details);
    }
  }

protected:
  std::unique_ptr<flutter::StreamHandlerError<T>> OnListenInternal(
      const T* arguments,
      std::unique_ptr<flutter::EventSink<T>>&& events) override {
    std::lock_guard<std::mutex> lock(mutex_);
    sink_ = std::move(events);
    return nullptr;
  }

  std::unique_ptr<flutter::StreamHandlerError<T>> OnCancelInternal(
      const T* arguments) override {
    std::lock_guard<std::mutex> lock(mutex_);
    sink_.reset();
    return nullptr;
  }

private:
  std::unique_ptr<flutter::EventSink<T>> sink_;
  std::mutex mutex_;
};

class FlutterBluetoothClassicPlugin : public flutter::Plugin {
public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);

  FlutterBluetoothClassicPlugin(
      flutter::PluginRegistrarWindows* registrar,
      std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> method_channel,
      std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>> state_channel,
      std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>> connection_channel,
      std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>> data_channel);

  virtual ~FlutterBluetoothClassicPlugin();

  // Disallow copy and assign.
  FlutterBluetoothClassicPlugin(const FlutterBluetoothClassicPlugin&) = delete;
  FlutterBluetoothClassicPlugin& operator=(const FlutterBluetoothClassicPlugin&) = delete;

private:
  // Method call handler
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  // Channels
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> method_channel_;
  std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>> state_channel_;
  std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>> connection_channel_;
  std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>> data_channel_;

  // Stream handler pointers (raw pointers as Flutter owns them via unique_ptr)
  EventStreamHandler<>* state_handler_;
  EventStreamHandler<>* connection_handler_;
  EventStreamHandler<>* data_handler_;

  // Bluetooth manager
  std::unique_ptr<BluetoothManager> bluetooth_manager_;

  // Plugin registrar
  flutter::PluginRegistrarWindows* registrar_;
};

}  // namespace flutter_bluetooth_classic

#endif  // FLUTTER_PLUGIN_FLUTTER_BLUETOOTH_CLASSIC_PLUGIN_H_
