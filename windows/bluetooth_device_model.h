#ifndef FLUTTER_PLUGIN_BLUETOOTH_DEVICE_MODEL_H_
#define FLUTTER_PLUGIN_BLUETOOTH_DEVICE_MODEL_H_

#include <flutter/encodable_value.h>

#include <string>

namespace flutter_bluetooth_classic {

struct ClassicDeviceInfo {
  std::string name;
  std::string address;
  std::string com_port;
  std::string device_id;
  std::string source;
  std::string connect_key;
  bool paired = false;
  bool connected = false;
  bool remembered = false;

  flutter::EncodableMap ToEncodableMap() const {
    flutter::EncodableMap device_map;
    device_map[flutter::EncodableValue("name")] = flutter::EncodableValue(name);
    device_map[flutter::EncodableValue("address")] = flutter::EncodableValue(address);
    device_map[flutter::EncodableValue("paired")] = flutter::EncodableValue(paired);
    device_map[flutter::EncodableValue("connected")] = flutter::EncodableValue(connected);
    device_map[flutter::EncodableValue("remembered")] = flutter::EncodableValue(remembered);
    device_map[flutter::EncodableValue("comPort")] = flutter::EncodableValue(com_port);
    device_map[flutter::EncodableValue("source")] = flutter::EncodableValue(source);
    device_map[flutter::EncodableValue("deviceId")] = flutter::EncodableValue(device_id);
    return device_map;
  }
};

}  // namespace flutter_bluetooth_classic

#endif  // FLUTTER_PLUGIN_BLUETOOTH_DEVICE_MODEL_H_
