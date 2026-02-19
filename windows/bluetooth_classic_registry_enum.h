#ifndef FLUTTER_PLUGIN_BLUETOOTH_CLASSIC_REGISTRY_ENUM_H_
#define FLUTTER_PLUGIN_BLUETOOTH_CLASSIC_REGISTRY_ENUM_H_

#include <vector>

#include "bluetooth_device_model.h"

namespace flutter_bluetooth_classic {

class BluetoothClassicRegistryEnumerator {
 public:
  std::vector<ClassicDeviceInfo> EnumerateClassicSppDevices();
};

}  // namespace flutter_bluetooth_classic

#endif  // FLUTTER_PLUGIN_BLUETOOTH_CLASSIC_REGISTRY_ENUM_H_
