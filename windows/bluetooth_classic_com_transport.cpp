#include "bluetooth_classic_com_transport.h"

#include <windows.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "flutter_bluetooth_classic_plugin.h"

namespace flutter_bluetooth_classic {
namespace {

std::string LastErrorMessage(const std::string& prefix) {
  const DWORD error = GetLastError();
  char* buffer = nullptr;
  FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr,
      error,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&buffer),
      0,
      nullptr);

  std::string result = prefix + " (error=" + std::to_string(error) + ")";
  if (buffer != nullptr) {
    result += ": ";
    result += buffer;
    LocalFree(buffer);
  }
  return result;
}

}  // namespace

BluetoothClassicComTransport::BluetoothClassicComTransport(
    const std::string& com_port,
    const std::string& device_address,
    EventStreamHandler<flutter::EncodableValue>* connection_handler,
    EventStreamHandler<flutter::EncodableValue>* data_handler)
    : com_port_(com_port),
      device_address_(device_address),
      connection_handler_(connection_handler),
      data_handler_(data_handler) {}

BluetoothClassicComTransport::~BluetoothClassicComTransport() {
  Close();
}

bool BluetoothClassicComTransport::Open(std::string* error_message) {
  if (is_connected_) {
    return true;
  }

  const std::string com_path = ToWindowsComPath(com_port_);
  HANDLE handle = CreateFileA(
      com_path.c_str(),
      GENERIC_READ | GENERIC_WRITE,
      0,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);

  if (handle == INVALID_HANDLE_VALUE) {
    if (error_message != nullptr) {
      *error_message = LastErrorMessage("COM_OPEN_FAILED");
    }
    return false;
  }

  DCB dcb;
  SecureZeroMemory(&dcb, sizeof(dcb));
  dcb.DCBlength = sizeof(dcb);
  if (!GetCommState(handle, &dcb)) {
    if (error_message != nullptr) {
      *error_message = LastErrorMessage("COM_GET_STATE_FAILED");
    }
    CloseHandle(handle);
    return false;
  }

  dcb.BaudRate = CBR_115200;
  dcb.ByteSize = 8;
  dcb.StopBits = ONESTOPBIT;
  dcb.Parity = NOPARITY;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  dcb.fRtsControl = RTS_CONTROL_ENABLE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;

  if (!SetCommState(handle, &dcb)) {
    if (error_message != nullptr) {
      *error_message = LastErrorMessage("COM_SET_STATE_FAILED");
    }
    CloseHandle(handle);
    return false;
  }

  COMMTIMEOUTS timeouts;
  SecureZeroMemory(&timeouts, sizeof(timeouts));
  timeouts.ReadIntervalTimeout = 50;
  timeouts.ReadTotalTimeoutConstant = 50;
  timeouts.ReadTotalTimeoutMultiplier = 1;
  timeouts.WriteTotalTimeoutConstant = 1000;
  timeouts.WriteTotalTimeoutMultiplier = 1;
  SetupComm(handle, 4096, 4096);
  SetCommTimeouts(handle, &timeouts);

  PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

  serial_handle_ = handle;
  should_stop_ = false;
  is_connected_ = true;
  disconnect_reported_ = false;
  SendConnectionState(true, "CONNECTED: COM(" + com_port_ + ")");
  StartReadLoop();
  StartWriteLoop();
  return true;
}

void BluetoothClassicComTransport::WriteData(const std::vector<uint8_t>& data) {
  if (!is_connected_ || serial_handle_ == nullptr) {
    throw std::runtime_error("COM transport not connected");
  }

  {
    std::lock_guard<std::mutex> lock(write_mutex_);
    constexpr size_t kMaxPendingWrites = 256;
    if (pending_writes_.size() >= kMaxPendingWrites) {
      throw std::runtime_error("COM send queue is full");
    }
    pending_writes_.push_back(data);
  }
  write_cv_.notify_one();
}

void BluetoothClassicComTransport::Close() {
  should_stop_ = true;
  write_cv_.notify_all();
  {
    std::lock_guard<std::mutex> lock(write_mutex_);
    pending_writes_.clear();
  }

  HANDLE handle = reinterpret_cast<HANDLE>(serial_handle_);
  if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
    CloseHandle(handle);
  }
  serial_handle_ = nullptr;

  if (read_thread_.joinable()) {
    read_thread_.join();
  }
  if (write_thread_.joinable()) {
    write_thread_.join();
  }

  if (is_connected_) {
    is_connected_ = false;
    ReportDisconnected("DISCONNECTED");
  }
}

std::string BluetoothClassicComTransport::ToWindowsComPath(const std::string& com_port) const {
  if (com_port.rfind("\\\\.\\", 0) == 0) {
    return com_port;
  }
  return "\\\\.\\" + com_port;
}

void BluetoothClassicComTransport::StartReadLoop() {
  read_thread_ = std::thread([this]() {
    ReadLoop();
  });
}

void BluetoothClassicComTransport::StartWriteLoop() {
  write_thread_ = std::thread([this]() {
    WriteLoop();
  });
}

void BluetoothClassicComTransport::ReadLoop() {
  std::vector<uint8_t> buffer(1024);
  while (!should_stop_ && is_connected_) {
    HANDLE handle = reinterpret_cast<HANDLE>(serial_handle_);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
      break;
    }

    DWORD bytes_read = 0;
    BOOL ok = ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr);
    if (!ok) {
      if (!should_stop_) {
        is_connected_ = false;
        ReportDisconnected(LastErrorMessage("DISCONNECTED"));
      }
      break;
    }

    if (bytes_read > 0) {
      std::vector<uint8_t> data(buffer.begin(), buffer.begin() + bytes_read);
      SendData(data);
    } else {
      Sleep(2);
    }
  }
}

void BluetoothClassicComTransport::WriteLoop() {
  while (!should_stop_) {
    std::vector<uint8_t> next_payload;
    {
      std::unique_lock<std::mutex> lock(write_mutex_);
      write_cv_.wait(lock, [this]() { return should_stop_ || !pending_writes_.empty(); });
      if (should_stop_) {
        break;
      }
      next_payload = std::move(pending_writes_.front());
      pending_writes_.pop_front();
    }

    HANDLE handle = reinterpret_cast<HANDLE>(serial_handle_);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
      break;
    }

    const uint8_t* cursor = next_payload.data();
    size_t remaining = next_payload.size();
    while (remaining > 0 && !should_stop_) {
      DWORD bytes_written = 0;
      BOOL ok = WriteFile(handle, cursor, static_cast<DWORD>(remaining), &bytes_written, nullptr);
      if (!ok || bytes_written == 0) {
        if (!should_stop_) {
          is_connected_ = false;
          ReportDisconnected(LastErrorMessage("WRITE_ERROR"));
        }
        return;
      }
      cursor += bytes_written;
      remaining -= bytes_written;
    }
  }
}

void BluetoothClassicComTransport::ReportDisconnected(const std::string& status) {
  bool expected = false;
  if (disconnect_reported_.compare_exchange_strong(expected, true)) {
    SendConnectionState(false, status);
  }
}

void BluetoothClassicComTransport::SendConnectionState(bool is_connected, const std::string& status) {
  flutter::EncodableMap connection_map;
  connection_map[flutter::EncodableValue("isConnected")] = flutter::EncodableValue(is_connected);
  connection_map[flutter::EncodableValue("deviceAddress")] = flutter::EncodableValue(device_address_);
  connection_map[flutter::EncodableValue("status")] = flutter::EncodableValue(status);
  connection_map[flutter::EncodableValue("transport")] = flutter::EncodableValue("COM");
  connection_map[flutter::EncodableValue("comPort")] = flutter::EncodableValue(com_port_);
  connection_handler_->Success(flutter::EncodableValue(connection_map));
}

void BluetoothClassicComTransport::SendData(const std::vector<uint8_t>& data) {
  flutter::EncodableMap data_map;
  data_map[flutter::EncodableValue("deviceAddress")] = flutter::EncodableValue(device_address_);
  data_map[flutter::EncodableValue("comPort")] = flutter::EncodableValue(com_port_);

  flutter::EncodableList data_list;
  data_list.reserve(data.size());
  for (uint8_t byte : data) {
    data_list.push_back(flutter::EncodableValue(static_cast<int>(byte)));
  }
  data_map[flutter::EncodableValue("data")] = flutter::EncodableValue(data_list);
  data_handler_->Success(flutter::EncodableValue(data_map));
}

}  // namespace flutter_bluetooth_classic
