#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>

class NetworkConfig {
 public:
  NetworkConfig(ESP8266WebServer& server, uint8_t motorPin);
  void begin();
  void handle(bool motorOn);
  bool staConnected() const { return staConnected_; }
  bool apActive() const { return apActive_; }
  const String& hostname() const { return hostname_; }
  String address() const;
  void sendPortal();

 private:
  struct Settings {
    String ssid;
    String password;
    String hostname;
    bool dhcp = true;
    String address;
    String mask;
    String gateway;
    String dns;
  } settings_;

  ESP8266WebServer& server_;
  uint8_t motorPin_;
  DNSServer dnsServer_;
  String hostname_ = "espsette";
  bool fsReady_ = false;
  bool staConnected_ = false;
  bool apActive_ = false;
  bool restartPending_ = false;
  uint32_t restartAfter_ = 0;

  bool load();
  bool save(const Settings& value);
  bool validate(const Settings& value, String& error) const;
  bool connect();
  void startAp();
  void receiveSettings();
  void retry();
};
