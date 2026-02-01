#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint flutter_bluetooth_classic_serial.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'flutter_bluetooth_classic_serial'
  s.version          = '1.0.3'
  s.summary          = 'A Flutter plugin for Bluetooth Classic communication'
  s.description      = <<-DESC
A Flutter plugin for Bluetooth Classic communication on Android, iOS, and Windows platforms. Supports device discovery, connection management, and data transmission.
                       DESC
  s.homepage         = 'https://github.com/C0DE-IN/flutter_bluetooth_classic_serial'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'Flutter Bluetooth Classic' => 'contact@flutter-bluetooth-classic.com' }
  s.source           = { :path => '.' }
  s.source_files     = 'Classes/**/*'
  s.dependency 'FlutterMacOS'
  s.platform         = :osx, '10.15'
  s.swift_version    = '5.0'

  # macOS-specific frameworks
  s.frameworks = 'IOBluetooth'

  # Flutter engine dependency
  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES' }
end

