#include "bluetooth_connection.h"
#include "flutter_bluetooth_classic_plugin.h"

#include <winrt/Windows.Foundation.h>
#include <winerror.h>
#include <future>
#include <thread>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;

namespace flutter_bluetooth_classic {

BluetoothConnection::BluetoothConnection(
    StreamSocket socket,
    const std::string& device_address,
    EventStreamHandler<flutter::EncodableValue>* connection_handler,
    EventStreamHandler<flutter::EncodableValue>* data_handler)
    : socket_(socket),
      device_address_(device_address),
      connection_handler_(connection_handler),
      data_handler_(data_handler),
      is_connected_(true) {
  
  try {
    // Get input and output streams
    data_reader_ = DataReader(socket_.InputStream());
    data_writer_ = DataWriter(socket_.OutputStream());
    
    // Configure reader for efficient reading
    data_reader_.InputStreamOptions(InputStreamOptions::Partial);

    // Send connection success event
    SendConnectionState(true, "CONNECTED");

    // Start reading data
    StartReadLoop();
  }
  catch (hresult_error const& ex) {
    is_connected_ = false;
    std::wstring msg_wide = ex.message().c_str();
    std::string msg(msg_wide.begin(), msg_wide.end());
    SendConnectionState(false, "ERROR: " + msg);
  }
}

BluetoothConnection::~BluetoothConnection() {
  Close();
}

void BluetoothConnection::WriteData(const std::vector<uint8_t>& data) {
  if (!is_connected_ || !data_writer_) {
    throw hresult_error(E_FAIL, L"Not connected");
  }

  try {
    // Write data to buffer
    data_writer_.WriteBytes(data);
    
    // Flush to socket (run on background thread to avoid STA issues)
    auto store_async = data_writer_.StoreAsync();
    
    std::promise<void> promise;
    auto future = promise.get_future();
    std::thread worker([op = std::move(store_async), &promise]() {
      winrt::init_apartment(winrt::apartment_type::multi_threaded);
      try {
        op.get();
        promise.set_value();
      } catch (...) {
        try {
          promise.set_exception(std::current_exception());
        } catch (...) {}
      }
      winrt::uninit_apartment();
    });
    
    worker.join();  // Use join instead of detach to ensure proper cleanup
    future.get();   // Get result or rethrow exception
  }
  catch (hresult_error const& ex) {
    is_connected_ = false;
    std::wstring msg_wide = ex.message().c_str();
    std::string msg(msg_wide.begin(), msg_wide.end());
    SendConnectionState(false, "WRITE_ERROR: " + msg);
    throw;
  }
}

void BluetoothConnection::Close() {
  if (!is_connected_) {
    return;
  }

  is_connected_ = false;
  should_stop_ = true;

  // Close socket first to unblock any pending reads on the read thread
  // This must be done BEFORE joining the thread to prevent deadlock
  try {
    if (socket_) {
      socket_.Close();
      socket_ = nullptr;
    }
  }
  catch (...) {
    // Ignore errors during socket close
  }

  // Now wait for read thread to finish (it will exit because socket is closed)
  if (read_thread_.joinable()) {
    read_thread_.join();
  }

  // Clean up streams (safe to do now that read thread is done)
  try {
    if (data_reader_) {
      data_reader_.Close();
      data_reader_ = nullptr;
    }

    if (data_writer_) {
      data_writer_.Close();
      data_writer_ = nullptr;
    }
  }
  catch (...) {
    // Ignore errors during cleanup
  }

  // Send disconnection event
  SendConnectionState(false, "DISCONNECTED");
}

void BluetoothConnection::StartReadLoop() {
  // Start read thread
  read_thread_ = std::thread([this]() {
    ReadLoop();
  });
}

void BluetoothConnection::ReadLoop() {
  // Initialize WinRT apartment for this background thread
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  
  const uint32_t buffer_size = 1024;
  
  while (is_connected_ && !should_stop_) {
    try {
      // Load data from socket
      auto load_async = data_reader_.LoadAsync(buffer_size);
      uint32_t bytes_read = load_async.get();

      if (bytes_read == 0) {
        // Connection closed
        is_connected_ = false;
        SendConnectionState(false, "DISCONNECTED: Remote device closed connection");
        break;
      }

      // Read bytes from buffer
      std::vector<uint8_t> data(bytes_read);
      data_reader_.ReadBytes(data);

      // Send data to Flutter
      SendData(data);
    }
    catch (hresult_error const& ex) {
      if (is_connected_) {
        is_connected_ = false;
        std::wstring msg_wide = ex.message().c_str();
        std::string msg(msg_wide.begin(), msg_wide.end());
        SendConnectionState(false, "DISCONNECTED: " + msg);
      }
      break;
    }
    catch (...) {
      if (is_connected_) {
        is_connected_ = false;
        SendConnectionState(false, "DISCONNECTED: Unknown error");
      }
      break;
    }
  }
  
  // Uninitialize apartment before thread exits
  winrt::uninit_apartment();
}

void BluetoothConnection::SendConnectionState(bool is_connected, const std::string& status) {
  flutter::EncodableMap connection_map;
  connection_map[flutter::EncodableValue("isConnected")] = flutter::EncodableValue(is_connected);
  connection_map[flutter::EncodableValue("deviceAddress")] = flutter::EncodableValue(device_address_);
  connection_map[flutter::EncodableValue("status")] = flutter::EncodableValue(status);
  
  connection_handler_->Success(flutter::EncodableValue(connection_map));
}

void BluetoothConnection::SendData(const std::vector<uint8_t>& data) {
  flutter::EncodableMap data_map;
  data_map[flutter::EncodableValue("deviceAddress")] = flutter::EncodableValue(device_address_);
  
  // Convert bytes to list of ints for Flutter
  flutter::EncodableList data_list;
  data_list.reserve(data.size());
  for (uint8_t byte : data) {
    data_list.push_back(flutter::EncodableValue(static_cast<int>(byte)));
  }
  
  data_map[flutter::EncodableValue("data")] = flutter::EncodableValue(data_list);
  
  data_handler_->Success(flutter::EncodableValue(data_map));
}

}  // namespace flutter_bluetooth_classic
