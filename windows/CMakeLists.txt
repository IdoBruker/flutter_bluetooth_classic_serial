# The Flutter tooling requires that developers have CMake 3.14 or later
# installed. You should not increase this version, as doing so will cause
# the plugin to fail to compile for some customers of the plugin.
cmake_minimum_required(VERSION 3.14)

# Project-level configuration.
set(PROJECT_NAME "flutter_bluetooth_classic")
project(${PROJECT_NAME} LANGUAGES CXX)

# This value is used when generating builds using this plugin, so it must
# not be changed.
set(PLUGIN_NAME "flutter_bluetooth_classic_serial_plugin")

# Require C++17 for WinRT support
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define the plugin library target.
add_library(${PLUGIN_NAME} SHARED
  "flutter_bluetooth_classic_plugin.cpp"
  "bluetooth_manager.cpp"
  "bluetooth_connection.cpp"
  "bluetooth_server.cpp"
)

# Apply standard build settings
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
)

# Symbols are hidden by default to reduce the chance of accidental conflicts
# between plugins. This should not be removed; any symbols that should be
# exported should be explicitly exported with the FLUTTER_PLUGIN_EXPORT macro.
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)

# Plugin-specific definitions and compile options
target_compile_definitions(${PLUGIN_NAME} PRIVATE 
  FLUTTER_PLUGIN_IMPL
  WINVER=0x0A00
  _WIN32_WINNT=0x0A00
  NTDDI_VERSION=0x0A000000
)

# Enable C++/WinRT support and disable non-critical warnings
target_compile_options(${PLUGIN_NAME} PRIVATE 
  /await      # Enable coroutines for WinRT
  /EHsc       # Enable C++ exception handling
  /wd4244     # Disable conversion warnings
)

# Include directories
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_include_directories(${PLUGIN_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")

# Link Flutter plugin wrapper
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter flutter_wrapper_plugin)

# Link Windows Bluetooth libraries
target_link_libraries(${PLUGIN_NAME} PRIVATE
  windowsapp.lib
  ws2_32.lib
)

# List of absolute paths to libraries that should be bundled with the plugin.
# This list could contain prebuilt libraries, or libraries created by an
# external build triggered from this build file.
set(flutter_bluetooth_classic_bundled_libraries
  ""
  PARENT_SCOPE
)
