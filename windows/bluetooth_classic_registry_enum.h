#ifndef FLUTTER_PLUGIN_BLUETOOTH_CLASSIC_REGISTRY_ENUM_H_
#define FLUTTER_PLUGIN_BLUETOOTH_CLASSIC_REGISTRY_ENUM_H_

#include <string>
#include <vector>

#include "bluetooth_device_model.h"

namespace flutter_bluetooth_classic {

class BluetoothClassicRegistryEnumerator {
 public:
  std::vector<ClassicDeviceInfo> EnumerateClassicSppDevices();

  static std::string ExtractAddressFromHardwareId(const std::string& path);
  static bool IsGenericSerialName(const std::string& name);
  static std::string PreferBetterName(const std::string& current, const std::string& candidate);
};

}  // namespace flutter_bluetooth_classic

#endif  // FLUTTER_PLUGIN_BLUETOOTH_CLASSIC_REGISTRY_ENUM_H_
