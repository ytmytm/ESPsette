#include "network_config.h"

#include <ESP8266WiFi.h>
#include <LittleFS.h>

#ifndef ESPSETTE_GPIO_DISABLED
#define ESPSETTE_GPIO_DISABLED 0
#endif
#ifndef ESPSETTE_WIFI_DISABLED
#define ESPSETTE_WIFI_DISABLED 0
#endif

namespace {
constexpr char HOSTNAME_PATH[] = "/hostname.txt";
constexpr char MIGRATION_FLAG[] = "/wm_v1";

bool validHostname(const String& value) {
  if (value.length() == 0 || value.length() > 32) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    const bool alphaNumeric = (c >= 'a' && c <= 'z') ||
                              (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9');
    if (!alphaNumeric && (c != '-' || i == 0 || i == value.length() - 1)) return false;
  }
  return true;
}
}  // namespace

NetworkConfig::NetworkConfig(ESP8266WebServer& server, uint8_t motorPin)
    : server_(server),
      motorPin_(motorPin),
      hostnameParam_("hostname", "Hostname", "ESPsette", 33) {}

bool NetworkConfig::motorBusy() const {
#if ESPSETTE_GPIO_DISABLED
  (void)motorPin_;
  return false;
#else
  return digitalRead(motorPin_) == HIGH;
#endif
}

bool NetworkConfig::apActive() const {
  return portalRunning_ || (WiFi.getMode() == WIFI_AP) ||
         (WiFi.getMode() == WIFI_AP_STA && !staConnected_);
}

String NetworkConfig::address() const {
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    const IPAddress ap = WiFi.softAPIP();
    if (ap != (uint32_t)0) return ap.toString();
  }
  return WiFi.localIP().toString();
}

void NetworkConfig::wipeLegacyWifiConfig() {
  if (!fsReady_) return;
  LittleFS.remove("/config.json");
  LittleFS.remove("/config.tmp");
  LittleFS.remove("/config.bak");
}

void NetworkConfig::loadHostname() {
  if (!fsReady_ || !LittleFS.exists(HOSTNAME_PATH)) return;
  File file = LittleFS.open(HOSTNAME_PATH, "r");
  if (!file) return;
  String value = file.readString();
  file.close();
  value.trim();
  if (validHostname(value)) hostname_ = value;
}

void NetworkConfig::saveHostname(const String& value) {
  if (!fsReady_ || !validHostname(value)) return;
  File file = LittleFS.open(HOSTNAME_PATH, "w");
  if (!file) return;
  file.print(value);
  file.close();
  hostname_ = value;
}

void NetworkConfig::onSaveParams() {
  String value = hostnameParam_.getValue();
  value.trim();
  if (validHostname(value)) saveHostname(value);
}

void NetworkConfig::resetWifiAndReboot() {
  server_.send_P(200, PSTR("text/html"),
                 PSTR("<!doctype html><meta charset=utf-8><p>Clearing Wi-Fi; "
                      "join ESPsette-… AP at http://192.168.4.1/</p>"));
  delay(800);
  wifiManager_.resetSettings();
  ESP.restart();
}

void NetworkConfig::handleWifiGet() {
  // GET is confirm-only; wiping requires POST. OTA is make ota, not this URL.
  server_.send_P(200, PSTR("text/html"),
                 PSTR("<!doctype html><meta charset=utf-8><title>Wi-Fi</title>"
                      "<p>OTA = <code>make ota</code> (keeps Wi-Fi). "
                      "Form forgets saved network only.</p>"
                      "<form method=POST action=/wifi>"
                      "<input type=hidden name=confirm value=1>"
                      "<button>Forget Wi-Fi / setup AP</button></form>"
                      "<p><a href=/>Back</a></p>"));
}

void NetworkConfig::handleWifiPost() {
  if (motorBusy()) {
    server_.send(503, "text/plain", "Reconfigure Wi-Fi after MOTOR stops.");
    return;
  }
  if (server_.arg("confirm") != "1") {
    server_.sendHeader("Location", "/wifi");
    server_.send(303, "text/plain", "");
    return;
  }
  resetWifiAndReboot();
}

void NetworkConfig::begin() {
#if ESPSETTE_WIFI_DISABLED
  fsReady_ = false;
  return;
#else
  fsReady_ = LittleFS.begin();
  wifiManager_.setDebugOutput(false);
  if (fsReady_ && !LittleFS.exists(MIGRATION_FLAG)) {
    wipeLegacyWifiConfig();
    // Force ESPHome-style first-run portal (drop STA saved by old firmware).
    wifiManager_.resetSettings();
    File flag = LittleFS.open(MIGRATION_FLAG, "w");
    if (flag) {
      flag.print('1');
      flag.close();
    }
  }
  loadHostname();

  wifiManager_.setHostname(hostname_.c_str());
  wifiManager_.setConfigPortalTimeout(300);
  wifiManager_.setConnectTimeout(25);
  wifiManager_.setConnectRetries(2);
  wifiManager_.setBreakAfterConfig(true);
  wifiManager_.setTitle("ESPsette Wi-Fi");
  hostnameParam_.setValue(hostname_.c_str(), 32);
  wifiManager_.addParameter(&hostnameParam_);
  wifiManager_.setSaveParamsCallback([this]() { onSaveParams(); });

  char apName[24];
  snprintf(apName, sizeof(apName), "ESPsette-%06X", ESP.getChipId());

  // Like ESPHome: try saved STA; on failure open captive portal with scan UI.
  portalRunning_ = true;
  staConnected_ = wifiManager_.autoConnect(apName);
  portalRunning_ = false;
  onSaveParams();
  WiFi.hostname(hostname_);

  server_.on("/wifi", HTTP_GET, [this]() { handleWifiGet(); });
  server_.on("/wifi", HTTP_POST, [this]() { handleWifiPost(); });
#endif
}

void NetworkConfig::handle(bool motorOn) {
  (void)motorOn;
  if (portalRunning_) wifiManager_.process();
}
