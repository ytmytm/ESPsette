#pragma once

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

// ESPHome-like onboarding: WiFiManager captive portal (scan + SSID/password),
// credentials in WM flash storage; hostname in LittleFS.
class NetworkConfig {
 public:
  NetworkConfig(ESP8266WebServer& server, uint8_t motorPin);
  void begin();
  void handle(bool motorOn);
  bool staConnected() const { return staConnected_; }
  bool apActive() const;
  const String& hostname() const { return hostname_; }
  String address() const;
  void openConfigPortal();

 private:
  ESP8266WebServer& server_;
  uint8_t motorPin_;
  WiFiManager wifiManager_;
  WiFiManagerParameter hostnameParam_;
  String hostname_ = "espsette";
  bool fsReady_ = false;
  bool staConnected_ = false;
  bool portalRunning_ = false;

  void wipeLegacyWifiConfig();
  void loadHostname();
  void saveHostname(const String& value);
  bool motorBusy() const;
  void onSaveParams();
};
