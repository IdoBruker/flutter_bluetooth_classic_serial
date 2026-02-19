#include "bluetooth_classic_registry_enum.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace flutter_bluetooth_classic {
namespace {

std::string WideToUtf8(const std::wstring& ws) {
  if (ws.empty()) {
    return "";
  }
  const int size_needed =
      WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
  if (size_needed <= 0) {
    return "";
  }
  std::string str_to(size_needed, 0);
  WideCharToMultiByte(
      CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), &str_to[0], size_needed, nullptr, nullptr);
  return str_to;
}

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) {
    return L"";
  }
  const int size_needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  if (size_needed <= 0) {
    return L"";
  }
  std::wstring out(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], size_needed);
  return out;
}

std::string ToUpper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return value;
}

std::string GetStringRegValue(HKEY key, const std::wstring& value_name) {
  DWORD type = 0;
  DWORD size = 0;
  LONG rc = RegQueryValueExW(key, value_name.c_str(), nullptr, &type, nullptr, &size);
  if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t)) {
    return "";
  }

  std::vector<wchar_t> buffer(size / sizeof(wchar_t), 0);
  rc = RegQueryValueExW(
      key, value_name.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &size);
  if (rc != ERROR_SUCCESS) {
    return "";
  }

  std::wstring value(buffer.data());
  return WideToUtf8(value);
}

std::string ExtractAddressFromPath(const std::string& path) {
  static const std::regex pattern("&([0-9A-Fa-f]{12})_");
  std::smatch match;
  if (!std::regex_search(path, match, pattern) || match.size() < 2) {
    return "";
  }
  return ToUpper(match[1].str());
}

void EnumerateBthEnumRecursive(
    HKEY root_key,
    const std::wstring& subkey_path,
    std::vector<ClassicDeviceInfo>* devices,
    std::set<std::string>* seen_com_ports,
    int depth) {
  if (depth > 8) {
    return;
  }

  HKEY key = nullptr;
  if (RegOpenKeyExW(root_key, subkey_path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return;
  }

  const std::string friendly_name = GetStringRegValue(key, L"FriendlyName");
  std::string device_desc = GetStringRegValue(key, L"DeviceDesc");
  if (!device_desc.empty()) {
    // Windows prefixes some descriptions with "@...;"
    size_t last_semicolon = device_desc.find_last_of(';');
    if (last_semicolon != std::string::npos && last_semicolon + 1 < device_desc.size()) {
      device_desc = device_desc.substr(last_semicolon + 1);
    }
  }

  HKEY device_params_key = nullptr;
  std::wstring device_params_path = subkey_path + L"\\Device Parameters";
  if (RegOpenKeyExW(root_key, device_params_path.c_str(), 0, KEY_READ, &device_params_key) == ERROR_SUCCESS) {
    std::string port_name = GetStringRegValue(device_params_key, L"PortName");
    RegCloseKey(device_params_key);

    if (!port_name.empty()) {
      port_name = ToUpper(port_name);
      if (seen_com_ports->insert(port_name).second) {
        const std::string path_utf8 = WideToUtf8(subkey_path);
        std::string device_name = !friendly_name.empty() ? friendly_name : device_desc;
        if (device_name.empty()) {
          device_name = port_name;
        }

        ClassicDeviceInfo device;
        device.name = device_name;
        device.address = ExtractAddressFromPath(path_utf8);
        device.com_port = port_name;
        device.device_id = path_utf8;
        device.source = "registry-bthenum";
        device.paired = true;
        device.remembered = true;
        devices->push_back(device);
      }
    }
  }

  DWORD index = 0;
  wchar_t name_buffer[512];
  DWORD name_len = sizeof(name_buffer) / sizeof(name_buffer[0]);
  while (RegEnumKeyExW(
             key, index, name_buffer, &name_len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
    std::wstring child_name(name_buffer, name_len);
    std::wstring child_path = subkey_path + L"\\" + child_name;
    EnumerateBthEnumRecursive(root_key, child_path, devices, seen_com_ports, depth + 1);
    ++index;
    name_len = sizeof(name_buffer) / sizeof(name_buffer[0]);
  }

  RegCloseKey(key);
}

}  // namespace

std::vector<ClassicDeviceInfo> BluetoothClassicRegistryEnumerator::EnumerateClassicSppDevices() {
  std::vector<ClassicDeviceInfo> devices;
  std::set<std::string> seen_com_ports;

  EnumerateBthEnumRecursive(
      HKEY_LOCAL_MACHINE,
      Utf8ToWide("SYSTEM\\CurrentControlSet\\Enum\\BTHENUM"),
      &devices,
      &seen_com_ports,
      0);

  return devices;
}

}  // namespace flutter_bluetooth_classic
