#include "bluetooth_server.h"
#include "bluetooth_connection.h"
#include "flutter_bluetooth_classic_plugin.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winerror.h>
#include <future>
#include <thread>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Bluetooth::Rfcomm;
using namespace Windows::Networking::Sockets;

namespace flutter_bluetooth_classic {

BluetoothServer::BluetoothServer(
    const std::string& service_name,
    EventStreamHandler<flutter::EncodableValue>* connection_handler,
    EventStreamHandler<flutter::EncodableValue>* data_handler,
    ConnectionCallback on_connection)
    : service_name_(service_name),
      connection_handler_(connection_handler),
      data_handler_(data_handler),
      on_connection_(on_connection) {
}

BluetoothServer::~BluetoothServer() {
  StopListening();
}

void BluetoothServer::StartListening() {
  if (is_listening_) {
    return;
  }

  try {
    InitializeServiceProvider();
    is_listening_ = true;
  }
  catch (hresult_error const&) {
    is_listening_ = false;
    throw;
  }
}

void BluetoothServer::StopListening() {
  if (!is_listening_) {
    return;
  }

  is_listening_ = false;

  // Unregister connection received handler
  if (socket_listener_ && connection_received_token_) {
    socket_listener_.ConnectionReceived(connection_received_token_);
  }

  // Close listener
  try {
    if (socket_listener_) {
      socket_listener_.Close();
      socket_listener_ = nullptr;
    }

    if (service_provider_) {
      service_provider_.StopAdvertising();
      service_provider_ = nullptr;
    }
  }
  catch (...) {
    // Ignore errors during cleanup
  }
}

void BluetoothServer::InitializeServiceProvider() {
  // Create RFCOMM service provider for Serial Port Profile (SPP)
  // SPP UUID: 00001101-0000-1000-8000-00805F9B34FB
  RfcommServiceId spp_service_id = RfcommServiceId::SerialPort();
  
  // Run on background thread to avoid STA issues
  auto provider_async = RfcommServiceProvider::CreateAsync(spp_service_id);
  
  std::promise<decltype(provider_async.get())> promise;
  auto future = promise.get_future();
  std::thread worker([op = std::move(provider_async), &promise]() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try {
      auto result = op.get();
      promise.set_value(result);
    } catch (...) {
      try {
        promise.set_exception(std::current_exception());
      } catch (...) {}
    }
    winrt::uninit_apartment();
  });
  
  worker.join();  // Use join instead of detach to ensure proper cleanup
  service_provider_ = future.get();

  if (!service_provider_) {
    throw hresult_error(E_FAIL, L"Failed to create RFCOMM service provider");
  }

  // Create socket listener
  socket_listener_ = StreamSocketListener();

  // Register for incoming connections
  connection_received_token_ = socket_listener_.ConnectionReceived(
      [this](StreamSocketListener const& listener, 
             StreamSocketListenerConnectionReceivedEventArgs const& args) {
        
        try {
          // Get the socket from the connection
          StreamSocket socket = args.Socket();

          // Extract device address from remote host name
          std::wstring remote_host_wide = socket.Information().RemoteAddress().DisplayName().c_str();
          std::string device_address(remote_host_wide.begin(), remote_host_wide.end());

          // Create connection object
          auto connection = std::make_unique<BluetoothConnection>(
              socket,
              device_address,
              connection_handler_,
              data_handler_);

          // Notify via callback
          if (on_connection_) {
            on_connection_(std::move(connection));
          }
        }
        catch (hresult_error const& ex) {
          // Send error event
          flutter::EncodableMap connection_map;
          connection_map[flutter::EncodableValue("isConnected")] = flutter::EncodableValue(false);
          connection_map[flutter::EncodableValue("deviceAddress")] = flutter::EncodableValue("unknown");
          
          std::wstring msg_wide = ex.message().c_str();
          std::string msg(msg_wide.begin(), msg_wide.end());
          connection_map[flutter::EncodableValue("status")] = flutter::EncodableValue("ERROR: " + msg);
          
          connection_handler_->Success(flutter::EncodableValue(connection_map));
        }
      });

  // Bind to the RFCOMM service provider
  auto bind_async = socket_listener_.BindServiceNameAsync(
      service_provider_.ServiceId().AsString(),
      SocketProtectionLevel::BluetoothEncryptionAllowNullAuthentication);
  
  // Run on background thread to avoid STA issues (returns void)
  std::promise<void> bind_promise;
  auto bind_future = bind_promise.get_future();
  std::thread bind_worker([op = std::move(bind_async), &bind_promise]() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try {
      op.get();
      bind_promise.set_value();
    } catch (...) {
      try {
        bind_promise.set_exception(std::current_exception());
      } catch (...) {}
    }
    winrt::uninit_apartment();
  });
  
  bind_worker.join();  // Use join instead of detach to ensure proper cleanup
  bind_future.get();

  // Set SDP attributes for the service
  try {
    using namespace Windows::Storage::Streams;
    
    DataWriter sdp_writer;
    
    // Service name attribute (0x0100)
    std::wstring service_name_wide(service_name_.begin(), service_name_.end());
    sdp_writer.WriteByte(0x09);  // Data element type: string
    sdp_writer.WriteByte(static_cast<uint8_t>(service_name_wide.length()));
    sdp_writer.UnicodeEncoding(UnicodeEncoding::Utf8);
    sdp_writer.WriteString(hstring(service_name_wide));

    IBuffer sdp_buffer = sdp_writer.DetachBuffer();
    service_provider_.SdpRawAttributes().Insert(0x0100, sdp_buffer);
  }
  catch (...) {
    // SDP attributes are optional, continue without them
  }

  // Start advertising
  service_provider_.StartAdvertising(socket_listener_);
}

}  // namespace flutter_bluetooth_classic
