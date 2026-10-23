#include "network_config.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

namespace {
constexpr char CONFIG_PATH[] = "/config.json";
constexpr char TEMP_PATH[] = "/config.tmp";
constexpr char BACKUP_PATH[] = "/config.bak";

String escapeHtml(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&': escaped += F("&amp;"); break;
      case '<': escaped += F("&lt;"); break;
      case '>': escaped += F("&gt;"); break;
      case '"': escaped += F("&quot;"); break;
      case '\'': escaped += F("&#39;"); break;
      default: escaped += value[i]; break;
    }
  }
  return escaped;
}

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

NetworkConfig::NetworkConfig(ESP8266WebServer& server) : server_(server) {}

bool NetworkConfig::validate(const Settings& value, String& error) const {
  if (value.ssid.length() == 0 || value.ssid.length() > 32 || value.password.length() > 63) {
    error = F("SSID lub hasło ma nieprawidłową długość.");
    return false;
  }
  if (!validHostname(value.hostname)) {
    error = F("Nazwa hosta: 1–32 znaki ASCII, litery, cyfry lub łącznik.");
    return false;
  }
  if (!value.dhcp) {
    IPAddress address, mask, gateway, dns;
    if (!address.fromString(value.address) || !mask.fromString(value.mask) ||
        !gateway.fromString(value.gateway) || !dns.fromString(value.dns) ||
        address == IPAddress(0, 0, 0, 0) || mask == IPAddress(0, 0, 0, 0)) {
      error = F("Podaj poprawne adresy IPv4, maskę, bramę i DNS.");
      return false;
    }
  }
  return true;
}

bool NetworkConfig::load() {
  if (!fsReady_) return false;
  const char* paths[] = {CONFIG_PATH, BACKUP_PATH};
  for (const char* path : paths) {
    File file = LittleFS.open(path, "r");
    if (!file) continue;
    if (file.size() > 768) { file.close(); continue; }
    StaticJsonDocument<768> doc;
    const DeserializationError result = deserializeJson(doc, file);
    file.close();
    if (result || doc["version"] != 1) continue;
    Settings value;
    value.ssid = doc["ssid"].as<String>();
    value.password = doc["password"].as<String>();
    value.hostname = doc["hostname"].as<String>();
    value.dhcp = doc["dhcp"] | true;
    value.address = doc["address"].as<String>();
    value.mask = doc["mask"].as<String>();
    value.gateway = doc["gateway"].as<String>();
    value.dns = doc["dns"].as<String>();
    String error;
    if (!validate(value, error)) continue;
    settings_ = value;
    hostname_ = value.hostname;
    return true;
  }
  return false;
}

bool NetworkConfig::save(const Settings& value) {
  if (!fsReady_) return false;
  StaticJsonDocument<768> doc;
  doc["version"] = 1;
  doc["ssid"] = value.ssid;
  doc["password"] = value.password;
  doc["hostname"] = value.hostname;
  doc["dhcp"] = value.dhcp;
  doc["address"] = value.address;
  doc["mask"] = value.mask;
  doc["gateway"] = value.gateway;
  doc["dns"] = value.dns;
  if (doc.overflowed()) return false;
  LittleFS.remove(TEMP_PATH);
  File file = LittleFS.open(TEMP_PATH, "w");
  if (!file) return false;
  const size_t expected = measureJson(doc);
  const size_t written = serializeJson(doc, file);
  file.flush();
  file.close();
  if (written != expected) { LittleFS.remove(TEMP_PATH); return false; }
  const bool hadConfig = LittleFS.exists(CONFIG_PATH);
  if (hadConfig) {
    LittleFS.remove(BACKUP_PATH);
    if (!LittleFS.rename(CONFIG_PATH, BACKUP_PATH)) return false;
  }
  if (!LittleFS.rename(TEMP_PATH, CONFIG_PATH)) {
    if (hadConfig) LittleFS.rename(BACKUP_PATH, CONFIG_PATH);
    return false;
  }
  return true;
}

bool NetworkConfig::connect() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname_);
  if (!settings_.dhcp) {
    IPAddress address, mask, gateway, dns;
    address.fromString(settings_.address);
    mask.fromString(settings_.mask);
    gateway.fromString(settings_.gateway);
    dns.fromString(settings_.dns);
    if (!WiFi.config(address, gateway, mask, dns)) return false;
  }
  WiFi.begin(settings_.ssid.c_str(), settings_.password.c_str());
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && (uint32_t)(millis() - started) < 20000) {
    delay(250);
  }
  staConnected_ = WiFi.status() == WL_CONNECTED;
  return staConnected_;
}

void NetworkConfig::startAp() {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  char apName[24];
  snprintf(apName, sizeof(apName), "espsette-%06X", ESP.getChipId());
  apActive_ = WiFi.softAP(apName);
  if (apActive_) dnsServer_.start(53, "*", WiFi.softAPIP());
}

void NetworkConfig::begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  fsReady_ = LittleFS.begin();
  const bool hasConfig = load();
  if (!hasConfig || !connect()) startAp();
  server_.on("/wifi", HTTP_GET, [this]() { sendPortal(); });
  server_.on("/wifi", HTTP_POST, [this]() { receiveSettings(); });
  server_.on("/retry", HTTP_POST, [this]() { retry(); });
}

String NetworkConfig::address() const {
  return apActive_ ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

void NetworkConfig::sendPortal() {
  String page;
  page.reserve(2200);
  page += F("<!doctype html><html lang='pl'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>espsette Wi-Fi</title><style>body{font:1rem system-ui;max-width:32rem;margin:2rem auto;padding:0 1rem;background:#16191e;color:#eee}label{display:block;margin:1rem 0}input{box-sizing:border-box;width:100%;padding:.7rem;font:inherit}input[type=checkbox]{width:auto}button{padding:.8rem 1.2rem;font:inherit}a{color:#8ad}</style><h1>espsette — Wi-Fi</h1>");
  page += apActive_ ? F("<p>Tryb punktu dostępowego. Po zapisie moduł uruchomi się ponownie.</p>") :
                      F("<p>Połączono z siecią lokalną. Po zapisie moduł uruchomi się ponownie.</p>");
  if (!fsReady_) page += F("<p>LittleFS jest niedostępny; nie można zapisać ustawień.</p>");
  page += F("<form method='post' action='/wifi'><label>SSID<input name='ssid' maxlength='32' required value='");
  page += escapeHtml(settings_.ssid);
  page += F("'></label><label>Hasło sieci (puste zachowuje dotychczasowe dla tego SSID)<input type='password' name='password' maxlength='63'></label><label>Nazwa hosta<input name='hostname' maxlength='32' required value='");
  page += escapeHtml(hostname_);
  page += F("'></label><label><input type='checkbox' name='static' value='1'");
  if (!settings_.dhcp) page += F(" checked");
  page += F("> Statyczny adres IPv4</label>");
  const char* labels[] = {"Adres IP", "Maska", "Brama", "DNS"};
  const char* names[] = {"address", "mask", "gateway", "dns"};
  const String* values[] = {&settings_.address, &settings_.mask, &settings_.gateway, &settings_.dns};
  for (uint8_t i = 0; i < 4; ++i) {
    page += F("<label>"); page += labels[i]; page += F("<input name='");
    page += names[i]; page += F("' value='"); page += escapeHtml(*values[i]);
    page += F("'></label>");
  }
  page += F("<button type='submit'>Zapisz i połącz</button></form><form method='post' action='/retry'><button type='submit'>Ponów połączenie</button></form><p><a href='/'>Stan kasety</a></p></html>");
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(200, "text/html; charset=utf-8", page);
}

void NetworkConfig::receiveSettings() {
  Settings value;
  value.ssid = server_.arg("ssid");
  value.password = server_.arg("password");
  if (value.password.length() == 0 && value.ssid == settings_.ssid) value.password = settings_.password;
  value.hostname = server_.arg("hostname");
  value.dhcp = !server_.hasArg("static");
  value.address = server_.arg("address");
  value.mask = server_.arg("mask");
  value.gateway = server_.arg("gateway");
  value.dns = server_.arg("dns");
  String error;
  if (!validate(value, error)) {
    server_.send(400, "text/plain; charset=utf-8", error);
    return;
  }
  if (!save(value)) {
    server_.send(500, "text/plain; charset=utf-8", "Nie udało się zapisać konfiguracji w LittleFS.");
    return;
  }
  settings_ = value;
  hostname_ = value.hostname;
  restartPending_ = true;
  restartAfter_ = millis() + 1500;
  server_.send(200, "text/html; charset=utf-8", "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><p>Zapisano. Moduł uruchomi się ponownie i spróbuje połączyć z Wi-Fi.</p>");
}

void NetworkConfig::retry() {
  restartPending_ = true;
  restartAfter_ = millis() + 1500;
  server_.send(200, "text/html; charset=utf-8", "<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><p>Moduł uruchomi się ponownie i ponowi połączenie.</p>");
}

void NetworkConfig::handle(bool motorOn) {
  if (apActive_) dnsServer_.processNextRequest();
  if (restartPending_ && !motorOn && (int32_t)(millis() - restartAfter_) >= 0) ESP.restart();
}
