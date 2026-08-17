#include "bluetooth_classic_registry_enum.h"

#include <windows.h>

#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>

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

std::string ExtractComPortFromText(const std::string& text) {
  static const std::regex pattern("\\((COM[0-9]+)\\)", std::regex::icase);
  std::smatch match;
  if (std::regex_search(text, match, pattern) && match.size() >= 2) {
    return ToUpper(match[1].str());
  }
  static const std::regex bare("\\b(COM[0-9]+)\\b", std::regex::icase);
  if (std::regex_search(text, match, bare) && match.size() >= 2) {
    return ToUpper(match[1].str());
  }
  return "";
}

std::string ResolveFriendlyNameFromDevNode(HKEY root_key, const std::string& address_12_hex) {
  if (address_12_hex.empty()) {
    return "";
  }

  const std::wstring base_path =
      Utf8ToWide("SYSTEM\\CurrentControlSet\\Enum\\BTHENUM\\Dev_" + ToUpper(address_12_hex));
  HKEY base_key = nullptr;
  if (RegOpenKeyExW(root_key, base_path.c_str(), 0, KEY_READ, &base_key) != ERROR_SUCCESS) {
    return "";
  }

  std::string resolved_name;
  DWORD index = 0;
  wchar_t name_buffer[512];
  DWORD name_len = sizeof(name_buffer) / sizeof(name_buffer[0]);
  while (RegEnumKeyExW(
             base_key, index, name_buffer, &name_len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
    HKEY child_key = nullptr;
    if (RegOpenKeyExW(base_key, name_buffer, 0, KEY_READ, &child_key) == ERROR_SUCCESS) {
      resolved_name = GetStringRegValue(child_key, L"FriendlyName");
      RegCloseKey(child_key);
      if (!resolved_name.empty()) {
        break;
      }
    }

    ++index;
    name_len = sizeof(name_buffer) / sizeof(name_buffer[0]);
  }

  RegCloseKey(base_key);
  return resolved_name;
}

std::string ResolveNameFromBthPort(HKEY root_key, const std::string& address_12_hex) {
  if (address_12_hex.empty()) {
    return "";
  }

  const std::wstring path = Utf8ToWide(
      "SYSTEM\\CurrentControlSet\\Services\\BTHPORT\\Parameters\\Devices\\" + ToUpper(address_12_hex));
  HKEY key = nullptr;
  if (RegOpenKeyExW(root_key, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return "";
  }

  DWORD type = 0;
  DWORD size = 0;
  LONG rc = RegQueryValueExW(key, L"Name", nullptr, &type, nullptr, &size);
  if (rc != ERROR_SUCCESS || size == 0) {
    RegCloseKey(key);
    return "";
  }

  std::vector<BYTE> buffer(size + 2, 0);
  rc = RegQueryValueExW(key, L"Name", nullptr, &type, buffer.data(), &size);
  RegCloseKey(key);
  if (rc != ERROR_SUCCESS) {
    return "";
  }

  if (type == REG_SZ || type == REG_EXPAND_SZ) {
    return WideToUtf8(reinterpret_cast<wchar_t*>(buffer.data()));
  }

  // Windows stores Classic names as UTF-16LE REG_BINARY, often without a terminator.
  const size_t wchar_count = size / sizeof(wchar_t);
  if (wchar_count == 0) {
    return "";
  }
  std::wstring name(reinterpret_cast<wchar_t*>(buffer.data()), wchar_count);
  while (!name.empty() && name.back() == L'\0') {
    name.pop_back();
  }
  return WideToUtf8(name);
}

std::string ResolveBestDeviceName(
    HKEY root_key,
    const std::string& address,
    const std::string& friendly_name,
    const std::string& device_desc,
    const std::string& port_name) {
  std::string device_name = ResolveFriendlyNameFromDevNode(root_key, address);
  device_name = BluetoothClassicRegistryEnumerator::PreferBetterName(
      device_name, ResolveNameFromBthPort(root_key, address));
  device_name = BluetoothClassicRegistryEnumerator::PreferBetterName(device_name, friendly_name);
  device_name = BluetoothClassicRegistryEnumerator::PreferBetterName(device_name, device_desc);
  if (device_name.empty()) {
    device_name = port_name;
  }
  return device_name;
}

void AddComDevice(
    std::vector<ClassicDeviceInfo>* devices,
    std::set<std::string>* seen_com_ports,
    const std::string& port_name,
    const std::string& address,
    const std::string& name,
    const std::string& device_id,
    const std::string& source) {
  if (port_name.empty() || !seen_com_ports->insert(port_name).second) {
    return;
  }

  ClassicDeviceInfo device;
  device.name = name.empty() ? port_name : name;
  device.address = address;
  device.com_port = port_name;
  device.device_id = device_id;
  device.source = source;
  device.paired = true;
  device.remembered = true;
  devices->push_back(device);
}

void EnumerateBthEnumRecursive(
    HKEY root_key,
    const std::wstring& subkey_path,
    std::vector<ClassicDeviceInfo>* devices,
    std::set<std::string>* seen_com_ports,
    const std::string& source,
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
      const std::string path_utf8 = WideToUtf8(subkey_path);
      const std::string address = BluetoothClassicRegistryEnumerator::ExtractAddressFromHardwareId(path_utf8);
      const std::string device_name =
          ResolveBestDeviceName(root_key, address, friendly_name, device_desc, port_name);
      AddComDevice(devices, seen_com_ports, port_name, address, device_name, path_utf8, source);
    }
  }

  DWORD index = 0;
  wchar_t name_buffer[512];
  DWORD name_len = sizeof(name_buffer) / sizeof(name_buffer[0]);
  while (RegEnumKeyExW(
             key, index, name_buffer, &name_len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
    std::wstring child_name(name_buffer, name_len);
    std::wstring child_path = subkey_path + L"\\" + child_name;
    EnumerateBthEnumRecursive(root_key, child_path, devices, seen_com_ports, source, depth + 1);
    ++index;
    name_len = sizeof(name_buffer) / sizeof(name_buffer[0]);
  }

  RegCloseKey(key);
}

void EnumerateSetupApiPorts(std::vector<ClassicDeviceInfo>* devices, std::set<std::string>* seen_com_ports) {
  HDEVINFO dev_info = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
  if (dev_info == INVALID_HANDLE_VALUE) {
    return;
  }

  SP_DEVINFO_DATA device_data;
  device_data.cbSize = sizeof(device_data);
  for (DWORD index = 0; SetupDiEnumDeviceInfo(dev_info, index, &device_data); ++index) {
    wchar_t instance_id[512] = {};
    if (!SetupDiGetDeviceInstanceIdW(
            dev_info, &device_data, instance_id, sizeof(instance_id) / sizeof(instance_id[0]), nullptr)) {
      continue;
    }
    const std::string instance_utf8 = WideToUtf8(instance_id);
    const std::string address = BluetoothClassicRegistryEnumerator::ExtractAddressFromHardwareId(instance_utf8);

    wchar_t friendly[512] = {};
    SetupDiGetDeviceRegistryPropertyW(
        dev_info,
        &device_data,
        SPDRP_FRIENDLYNAME,
        nullptr,
        reinterpret_cast<PBYTE>(friendly),
        sizeof(friendly),
        nullptr);
    wchar_t description[512] = {};
    SetupDiGetDeviceRegistryPropertyW(
        dev_info,
        &device_data,
        SPDRP_DEVICEDESC,
        nullptr,
        reinterpret_cast<PBYTE>(description),
        sizeof(description),
        nullptr);

    std::string port_name;
    HKEY params_key = SetupDiOpenDevRegKey(dev_info, &device_data, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (params_key != INVALID_HANDLE_VALUE) {
      port_name = ToUpper(GetStringRegValue(params_key, L"PortName"));
      RegCloseKey(params_key);
    }
    if (port_name.empty()) {
      port_name = ExtractComPortFromText(WideToUtf8(friendly));
    }
    if (port_name.empty()) {
      continue;
    }

    const std::string device_name = ResolveBestDeviceName(
        HKEY_LOCAL_MACHINE, address, WideToUtf8(friendly), WideToUtf8(description), port_name);
    AddComDevice(devices, seen_com_ports, port_name, address, device_name, instance_utf8, "setupapi-ports");
  }

  SetupDiDestroyDeviceInfoList(dev_info);
}

}  // namespace

std::string BluetoothClassicRegistryEnumerator::ExtractAddressFromHardwareId(const std::string& path) {
  static const std::regex amp_pattern("&([0-9A-Fa-f]{12})_");
  static const std::regex dev_pattern("Dev_([0-9A-Fa-f]{12})", std::regex::icase);
  std::smatch match;
  if (std::regex_search(path, match, amp_pattern) && match.size() >= 2) {
    return ToUpper(match[1].str());
  }
  if (std::regex_search(path, match, dev_pattern) && match.size() >= 2) {
    return ToUpper(match[1].str());
  }
  return "";
}

bool BluetoothClassicRegistryEnumerator::IsGenericSerialName(const std::string& name) {
  if (name.empty()) {
    return true;
  }
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (lower.find("standard serial") != std::string::npos) {
    return true;
  }
  if (lower.find("bluetooth link") != std::string::npos) {
    return true;
  }
  if (lower.rfind("com", 0) == 0 && lower.size() <= 6) {
    return true;
  }
  return false;
}

std::string BluetoothClassicRegistryEnumerator::PreferBetterName(
    const std::string& current,
    const std::string& candidate) {
  if (candidate.empty()) {
    return current;
  }
  if (current.empty() || (IsGenericSerialName(current) && !IsGenericSerialName(candidate))) {
    return candidate;
  }
  return current;
}

std::vector<ClassicDeviceInfo> BluetoothClassicRegistryEnumerator::EnumerateClassicSppDevices() {
  std::vector<ClassicDeviceInfo> devices;
  std::set<std::string> seen_com_ports;

  EnumerateBthEnumRecursive(
      HKEY_LOCAL_MACHINE,
      Utf8ToWide("SYSTEM\\CurrentControlSet\\Enum\\BTHENUM"),
      &devices,
      &seen_com_ports,
      "registry-bthenum",
      0);
  EnumerateBthEnumRecursive(
      HKEY_LOCAL_MACHINE,
      Utf8ToWide("SYSTEM\\CurrentControlSet\\Enum\\BTHMODEM"),
      &devices,
      &seen_com_ports,
      "registry-bthmodem",
      0);
  EnumerateSetupApiPorts(&devices, &seen_com_ports);

  return devices;
}

}  // namespace flutter_bluetooth_classic
