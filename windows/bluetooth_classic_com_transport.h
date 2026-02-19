#ifndef FLUTTER_PLUGIN_BLUETOOTH_CLASSIC_COM_TRANSPORT_H_
#define FLUTTER_PLUGIN_BLUETOOTH_CLASSIC_COM_TRANSPORT_H_

#include <flutter/encodable_value.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace flutter_bluetooth_classic {

template <typename T>
class EventStreamHandler;

class BluetoothClassicComTransport {
 public:
  BluetoothClassicComTransport(
      const std::string& com_port,
      const std::string& device_address,
      EventStreamHandler<flutter::EncodableValue>* connection_handler,
      EventStreamHandler<flutter::EncodableValue>* data_handler);

  ~BluetoothClassicComTransport();

  bool Open(std::string* error_message);
  void WriteData(const std::vector<uint8_t>& data);
  bool IsConnected() const { return is_connected_; }
  std::string GetDeviceAddress() const { return device_address_; }
  std::string GetComPort() const { return com_port_; }
  void Close();

 private:
  std::string ToWindowsComPath(const std::string& com_port) const;
  void StartReadLoop();
  void ReadLoop();
  void SendConnectionState(bool is_connected, const std::string& status);
  void SendData(const std::vector<uint8_t>& data);

  void* serial_handle_ = nullptr;
  std::string com_port_;
  std::string device_address_;
  std::atomic<bool> is_connected_{false};
  std::atomic<bool> should_stop_{false};
  std::thread read_thread_;
  std::mutex write_mutex_;
  EventStreamHandler<flutter::EncodableValue>* connection_handler_ = nullptr;
  EventStreamHandler<flutter::EncodableValue>* data_handler_ = nullptr;
};

}  // namespace flutter_bluetooth_classic

#endif  // FLUTTER_PLUGIN_BLUETOOTH_CLASSIC_COM_TRANSPORT_H_
