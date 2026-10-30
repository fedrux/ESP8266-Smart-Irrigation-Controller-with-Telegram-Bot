#include "ESPDeviceCore.h"
#include "DeviceAdminPageGzip.h"
#if defined(ESP8266)
extern "C" {
#include <user_interface.h>
}
#else
#include <esp_system.h>
#endif

namespace {
constexpr uint32_t kTrialMs = 120000;
const char* safe(const char* value) { return value ? value : ""; }
String quoted(const String& value) {
  String result = "\"";
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = value[i];
    if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
    else if (c < 32) { char escape[7]; snprintf(escape, sizeof(escape), "\\u%04x", c); result += escape; }
    else result += char(c);
  }
  return result + '"';
}
String resetReason() {
#if defined(ESP8266)
  return ESP.getResetReason();
#else
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "Accensione";
    case ESP_RST_SW: return "Riavvio software";
    case ESP_RST_PANIC: return "Errore software";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "Watchdog";
    case ESP_RST_BROWNOUT: return "Alimentazione insufficiente";
    case ESP_RST_DEEPSLEEP: return "Risveglio";
    default: return "Altro (" + String(int(esp_reset_reason())) + ")";
  }
#endif
}
String updaterError() {
#if defined(ESP8266)
  return Update.getErrorString();
#else
  return String(Update.errorString());
#endif
}
}

ESPDeviceCore::ESPDeviceCore(const DeviceCoreConfig& config) : config_(config) {
  network_.magic = kStorageMagic;
  strlcpy(network_.primarySsid, safe(config.primarySsid), sizeof(network_.primarySsid));
  strlcpy(network_.primaryPassword, safe(config.primaryPassword), sizeof(network_.primaryPassword));
  strlcpy(network_.secondarySsid, safe(config.secondarySsid), sizeof(network_.secondarySsid));
  strlcpy(network_.secondaryPassword, safe(config.secondaryPassword), sizeof(network_.secondaryPassword));
  network_.useDhcp = config.useDhcp;
  memcpy(network_.staticIp, config.staticIp, 4);
  memcpy(network_.gateway, config.gateway, 4);
  memcpy(network_.subnet, config.subnet, 4);
  memcpy(network_.dns, config.dns, 4);
}

void ESPDeviceCore::begin() {
  // The application owns Serial: it may already be a sensor or battery UART.
#if defined(ESP8266)
  EEPROM.begin(kStorageSize);
  storageReady_ = EEPROM.length() == kStorageSize;
#else
  storageReady_ = EEPROM.begin(kStorageSize);
#endif
  loadStoredConfig();
  if (!validNetwork(network_)) { network_.useDhcp = 1; lastError_ = "Configurazione IP iniziale non valida: uso DHCP"; }
  for (unsigned i = 0; i < 4; ++i) {
#if defined(ESP8266)
    uint32_t randomValue = os_random();
#else
    uint32_t randomValue = esp_random();
#endif
    char part[9]; snprintf(part, sizeof(part), "%08lx", static_cast<unsigned long>(randomValue)); csrfToken_ += part;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  const char* headers[] = {"X-CSRF-Token"};
  server_.collectHeaders(static_cast<const char**>(headers), size_t(1));
  registerRoutes();
  server_.begin();
  lastLoopAt_ = millis();
  connectWifi();
}

void ESPDeviceCore::loop() {
  const uint32_t now = millis();
  uptimeMs_ += uint32_t(now - lastLoopAt_); lastLoopAt_ = now;
  server_.handleClient();
  if (uploadActive_ && uint32_t(now - uploadAt_) > 30000) {
    failUpload("Caricamento fermo da 30 secondi: aggiornamento annullato"); uploadSeen_ = false;
  }
  if (!uploadActive_) serviceWifi();
  if (restartAt_ && int32_t(now - restartAt_) >= 0) ESP.restart();
}

void ESPDeviceCore::setStatusProvider(String (*provider)()) { statusProvider_ = provider; }
String ESPDeviceCore::statusText() const { return statusProvider_ ? statusProvider_() : status_; }
bool ESPDeviceCore::restart() { scheduleRestart(); return true; }
void ESPDeviceCore::scheduleRestart() { restartAt_ = millis() + 1200; }
bool ESPDeviceCore::setupMode() const { return setupMode_; }
DeviceWebServer& ESPDeviceCore::server() { return server_; }

bool ESPDeviceCore::validNetwork(const NetworkConfig& value) const {
  if (!memchr(value.primarySsid, 0, sizeof(value.primarySsid)) ||
      !memchr(value.primaryPassword, 0, sizeof(value.primaryPassword)) ||
      !memchr(value.secondarySsid, 0, sizeof(value.secondarySsid)) ||
      !memchr(value.secondaryPassword, 0, sizeof(value.secondaryPassword)) || value.useDhcp > 1) return false;
  if (strlen(value.primarySsid) > 32 || strlen(value.secondarySsid) > 32) return false;
  return value.useDhcp || devicecore::validStatic(value.staticIp, value.gateway, value.subnet, value.dns);
}

bool ESPDeviceCore::loadStoredConfig() {
  if (!storageReady_) { lastError_ = "Memoria configurazione non disponibile"; return false; }
  StorageRecord record{}; EEPROM.get(512, record);
  if (record.magic == 0x45444332 && record.version == 2 &&
      record.crc == devicecore::crc32(&record, offsetof(StorageRecord, crc)) && validNetwork(record.network)) {
    network_ = record.network; return true;
  }
  NetworkConfig legacy{}; EEPROM.get(0, legacy);
  if (legacy.magic == kStorageMagic && validNetwork(legacy)) { network_ = legacy; return true; }
  return false;
}

bool ESPDeviceCore::saveStoredConfig() {
  if (!storageReady_) return false;
  StorageRecord record{};
  record.magic = 0x45444332; record.version = 2; record.network = network_;
  record.crc = devicecore::crc32(&record, offsetof(StorageRecord, crc));
  EEPROM.put(512, record);
  // Keep a validated v1-compatible copy as a recovery path.
  EEPROM.put(0, network_);
  return EEPROM.commit();
}

void ESPDeviceCore::connectWifi() {
  WiFi.disconnect();
  WiFi.mode(setupMode_ ? WIFI_AP_STA : WIFI_STA);
  bool configured;
  if (network_.useDhcp) configured = WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
  else configured = WiFi.config(IPAddress(network_.staticIp), IPAddress(network_.gateway),
                                IPAddress(network_.subnet), IPAddress(network_.dns));
  if (!configured) lastError_ = "Impossibile applicare la configurazione IP";
  const char* ssid = networkIndex_ ? network_.secondarySsid : network_.primarySsid;
  const char* password = networkIndex_ ? network_.secondaryPassword : network_.primaryPassword;
  attemptAt_ = millis(); connecting_ = true; ++attempts_;
  status_ = "Connessione " + String(networkIndex_ ? "alla rete secondaria" : "alla rete principale");
  if (ssid[0]) WiFi.begin(ssid, password);
  else attemptAt_ -= max(uint32_t(1000), config_.wifiTimeoutMs);
}

void ESPDeviceCore::startSetupAp() {
  if (setupMode_) return;
  const size_t length = strlen(safe(config_.apPassword));
  if (length < 8 || length > 63) { lastError_ = "Password di recupero non valida: servono 8-63 caratteri"; return; }
  WiFi.mode(WIFI_AP_STA);
  String suffix = WiFi.macAddress(); suffix.replace(":", ""); suffix = suffix.substring(suffix.length() - 4);
  setupMode_ = WiFi.softAP(("ESP-Setup-" + suffix).c_str(), config_.apPassword);
  if (!setupMode_) lastError_ = "Avvio rete di recupero fallito";
}

void ESPDeviceCore::serviceWifi() {
  const uint32_t now = millis();
  if (rollbackScheduled_ && uint32_t(now - rollbackAt_) >= 700) { rollbackTrial(); return; }
  if (trialScheduled_) {
    if (uint32_t(now - trialAt_) < 700) return;
    trialScheduled_ = false; startSetupAp(); wasConnected_ = false;
    networkIndex_ = 0; failedNetworks_ = 0; connectWifi();
  }
  if (trial_ && uint32_t(now - trialAt_) >= kTrialMs) { rollbackTrial(); return; }
  if (WiFi.status() == WL_CONNECTED) {
    if (!wasConnected_) {
      wasConnected_ = true; connecting_ = false; connectedAt_ = now; failedNetworks_ = 0;
      status_ = "Wi-Fi collegato";
    }
    if (setupMode_ && !trial_ && uint32_t(now - connectedAt_) > 30000) {
      WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); setupMode_ = false;
    }
    return;
  }
  if (wasConnected_) {
    wasConnected_ = false; ++disconnects_; networkIndex_ = 0; connectWifi(); return;
  }
  if (connecting_ && uint32_t(now - attemptAt_) >= max(uint32_t(1000), config_.wifiTimeoutMs)) {
    connecting_ = false; ++failedNetworks_; networkIndex_ = 1 - networkIndex_;
    if (failedNetworks_ >= 2) {
      startSetupAp(); status_ = "Reti assenti: recupero locale e nuovi tentativi";
      lastError_ = "Connessione Wi-Fi non riuscita";
      failedNetworks_ = 0; retryAt_ = now + 10000;
    } else retryAt_ = now;
  }
  if (!connecting_ && int32_t(now - retryAt_) >= 0) connectWifi();
}

void ESPDeviceCore::rollbackTrial() {
  network_ = previous_; trial_ = false; trialScheduled_ = false; rollbackScheduled_ = false; wasConnected_ = false;
  lastError_ = "Prova non confermata: ripristinata la rete precedente";
  networkIndex_ = 0; failedNetworks_ = 0; connectWifi();
}

bool ESPDeviceCore::authorized() {
  if (strlen(safe(config_.adminUser)) && strlen(safe(config_.adminPassword)) &&
      server_.authenticate(config_.adminUser, config_.adminPassword)) return true;
  server_.requestAuthentication(); return false;
}
bool ESPDeviceCore::validCsrf() {
  return server_.header("X-CSRF-Token") == csrfToken_ ||
         (server_.hasArg("csrf_token") && server_.arg("csrf_token") == csrfToken_);
}
bool ESPDeviceCore::requireWrite() {
  if (!authorized()) return false;
  if (!validCsrf()) { reply(403, "Sessione scaduta: ricarica la pagina"); return false; }
  return true;
}
void ESPDeviceCore::reply(int code, const String& message) {
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(code, "application/json; charset=utf-8", "{\"message\":" + quoted(message) + "}");
}

void ESPDeviceCore::registerRoutes() {
  // Deliberately leave / and the application's 404 handler untouched.
  server_.on("/admin", HTTP_GET, [this] { sendAdminPage(); });
  server_.on("/admin/api/status", HTTP_GET, [this] { if (authorized()) sendStatus(); });
  server_.on("/admin/api/restart", HTTP_POST, [this] {
    if (!requireWrite()) return;
    if (trial_ || uploadActive_) { reply(409, "Completa prima la prova o l'aggiornamento"); return; }
    reply(200, "Riavvio in corso"); scheduleRestart();
  });
  server_.on("/admin/api/config", HTTP_POST, [this] { handleConfig(); });
  server_.on("/admin/api/config/confirm", HTTP_POST, [this] { handleConfirm(); });
  server_.on("/admin/api/config/cancel", HTTP_POST, [this] {
    if (!requireWrite()) return;
    if (trial_) { reply(200, "Ripristino della rete precedente"); rollbackScheduled_ = true; rollbackAt_ = millis(); }
    else reply(409, "Nessuna prova in corso");
  });
  server_.on("/admin/api/scan", HTTP_POST, [this] { handleScan(); });
  server_.on("/admin/api/update", HTTP_POST, [this] { handleUpdate(); }, [this] { handleUpload(); });
}

void ESPDeviceCore::sendStatus() {
  const auto* fw = config_.firmware;
  const bool connected = WiFi.status() == WL_CONNECTED;
  String body; body.reserve(1600);
  body = "{\"device\":" + quoted(safe(config_.deviceName));
  body += ",\"core_version\":\"1.1.1\",\"board\":\"" ESP_DEVICE_BOARD_TAG "\"";
  body += ",\"application_id\":" + quoted(fw ? fw->application : "");
  body += ",\"firmware_version\":" + quoted(fw ? fw->version : "Non dichiarata");
  body += ",\"status\":" + quoted(status_) + ",\"application_status\":" + quoted(statusText());
  body += ",\"wifi_connected\":" + String(connected ? "true" : "false");
  body += ",\"wifi_ssid\":" + quoted(connected ? WiFi.SSID() : "");
  body += ",\"ip\":" + quoted(connected ? WiFi.localIP().toString() : "");
  body += ",\"wifi_rssi\":" + (connected ? String(WiFi.RSSI()) : String("null"));
  body += ",\"uptime_s\":" + String(static_cast<unsigned long>(uptimeMs_ / 1000));
  body += ",\"free_heap\":" + String(ESP.getFreeHeap());
  body += ",\"reset_reason\":" + quoted(resetReason());
  body += ",\"disconnects\":" + String(disconnects_) + ",\"connection_attempts\":" + String(attempts_);
  body += ",\"last_error\":" + quoted(lastError_) + ",\"setup_mode\":" + String(setupMode_ ? "true" : "false");
  body += ",\"setup_ip\":" + quoted(setupMode_ ? WiFi.softAPIP().toString() : "");
  body += ",\"trial_pending\":" + String(trial_ ? "true" : "false");
  body += ",\"trial_seconds_left\":" + String(trial_ ? (kTrialMs - min(kTrialMs, uint32_t(millis() - trialAt_))) / 1000 : 0);
  body += ",\"csrf_token\":" + quoted(csrfToken_);
  body += ",\"config\":{\"primary_ssid\":" + quoted(network_.primarySsid);
  body += ",\"secondary_ssid\":" + quoted(network_.secondarySsid);
  body += ",\"dhcp\":" + String(network_.useDhcp ? "true" : "false");
  body += ",\"static_ip\":" + quoted(IPAddress(network_.staticIp).toString());
  body += ",\"gateway\":" + quoted(IPAddress(network_.gateway).toString());
  body += ",\"subnet\":" + quoted(IPAddress(network_.subnet).toString());
  body += ",\"dns\":" + quoted(IPAddress(network_.dns).toString()) + "}}";
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(200, "application/json; charset=utf-8", body);
}

void ESPDeviceCore::sendAdminPage() {
  if (!authorized()) return;
  server_.sendHeader("Cache-Control", "no-store");
  server_.sendHeader("X-Content-Type-Options", "nosniff");
  server_.sendHeader("Content-Encoding", "gzip");
  server_.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(DEVICE_ADMIN_GZIP), sizeof(DEVICE_ADMIN_GZIP));
}

void ESPDeviceCore::handleConfig() {
  if (!requireWrite()) return;
  if (trial_ || uploadActive_ || restartAt_) { reply(409, "Un'altra operazione e in corso"); return; }
  NetworkConfig candidate = network_;
  const String primary = server_.arg("primary_ssid"), secondary = server_.arg("secondary_ssid");
  const String password1 = server_.arg("primary_password"), password2 = server_.arg("secondary_password");
  if ((!primary.length() && !secondary.length()) || primary.length() > 32 || secondary.length() > 32 ||
      password1.length() > 63 || password2.length() > 63) { reply(400, "Inserisci almeno una rete; nome massimo 32 byte, password massimo 63"); return; }
  // A changed SSID must not silently inherit another network's password.
  if (primary != candidate.primarySsid || server_.hasArg("primary_open")) candidate.primaryPassword[0] = 0;
  if (secondary != candidate.secondarySsid || server_.hasArg("secondary_open")) candidate.secondaryPassword[0] = 0;
  strlcpy(candidate.primarySsid, primary.c_str(), sizeof(candidate.primarySsid));
  strlcpy(candidate.secondarySsid, secondary.c_str(), sizeof(candidate.secondarySsid));
  if (password1.length()) strlcpy(candidate.primaryPassword, password1.c_str(), sizeof(candidate.primaryPassword));
  if (password2.length()) strlcpy(candidate.secondaryPassword, password2.c_str(), sizeof(candidate.secondaryPassword));
  candidate.useDhcp = server_.hasArg("dhcp");
  if (!candidate.useDhcp && (!devicecore::parseIp(server_.arg("static_ip").c_str(), candidate.staticIp) ||
      !devicecore::parseIp(server_.arg("gateway").c_str(), candidate.gateway) ||
      !devicecore::parseIp(server_.arg("subnet").c_str(), candidate.subnet) ||
      !devicecore::parseIp(server_.arg("dns").c_str(), candidate.dns))) { reply(400, "Indirizzo IP non valido"); return; }
  if (!validNetwork(candidate)) { reply(400, "Controlla IP, gateway, maschera e DNS: IP e gateway devono essere nella stessa rete"); return; }
  previous_ = network_; network_ = candidate; trial_ = true; trialScheduled_ = true; trialAt_ = millis();
  lastError_ = "";
  reply(202, "Prova avviata: conferma dal nuovo indirizzo entro 120 secondi. Senza conferma ripristino la rete precedente.");
}

void ESPDeviceCore::handleConfirm() {
  if (!requireWrite()) return;
  if (!trial_ || trialScheduled_ || rollbackScheduled_ || uint32_t(millis() - trialAt_) >= kTrialMs) { reply(409, "Nessuna prova confermabile"); return; }
  // Confirmation through the LAN proves that the new IP is actually reachable.
  if (WiFi.status() != WL_CONNECTED || server_.client().localIP() != WiFi.localIP()) {
    reply(409, "Apri /admin dal nuovo indirizzo della rete Wi-Fi per confermare"); return;
  }
  if (!saveStoredConfig()) { reply(500, "Salvataggio fallito: la configurazione precedente resta quella di recupero"); return; }
  trial_ = false; connectedAt_ = millis(); reply(200, "Nuova configurazione confermata e salvata");
}

void ESPDeviceCore::handleScan() {
  if (!requireWrite()) return;
  if (trial_ || uploadActive_) { reply(409, "Attendi la fine della prova o dell'aggiornamento"); return; }
  const int count = WiFi.scanComplete();
  if (count == -1) { reply(202, "Ricerca in corso"); return; }
  if (count < 0) { WiFi.scanNetworks(true); reply(202, "Ricerca avviata"); return; }
  String body = "{\"networks\":[";
  for (int i = 0; i < min(count, 20); ++i) {
    if (i) body += ',';
    body += "{\"ssid\":" + quoted(WiFi.SSID(i)) + ",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  body += "]}"; WiFi.scanDelete(); server_.send(200, "application/json; charset=utf-8", body);
}

void ESPDeviceCore::failUpload(const String& message) {
  updateError_ = message; lastError_ = message;
  if (uploadActive_) {
#if defined(ESP8266)
    // end(false) could commit a complete image; an impossible MD5 forces discard.
    Update.setMD5("00000000000000000000000000000000"); Update.end(false);
#else
    Update.abort();
#endif
  }
  uploadActive_ = false; uploadComplete_ = false;
}

void ESPDeviceCore::handleUpload() {
  HTTPUpload& upload = server_.upload();
  uploadAt_ = millis();
  if (upload.status == UPLOAD_FILE_START) {
    if (uploadSeen_) { failUpload("Carica un solo pacchetto alla volta"); return; }
    uploadSeen_ = true; headerBytes_ = uploadedBytes_ = firmwareBytes_ = 0; updateError_ = ""; uploadComplete_ = false;
    if (!strlen(safe(config_.adminUser)) || !strlen(safe(config_.adminPassword)) ||
        !server_.authenticate(safe(config_.adminUser), safe(config_.adminPassword)) ||
        server_.header("X-CSRF-Token") != csrfToken_) { failUpload("Autenticazione o sessione non valida"); return; }
    if (trial_ || restartAt_) { failUpload("Completa prima la prova Wi-Fi o il riavvio"); return; }
    if (!config_.firmware || !devicecore::fieldValid(config_.firmware->application, 48) ||
        !devicecore::fieldValid(config_.firmware->version, 32)) { failUpload("Identita firmware assente: aggiorna prima via USB"); return; }
    if (!upload.filename.endsWith(".edc")) { failUpload("Seleziona un pacchetto .edc preparato per questo dispositivo"); return; }
  } else if (upload.status == UPLOAD_FILE_WRITE && updateError_.isEmpty()) {
    size_t offset = 0;
    if (headerBytes_ < sizeof(uploadHeader_)) {
      const size_t count = min(sizeof(uploadHeader_) - headerBytes_, upload.currentSize);
      memcpy(uploadHeader_ + headerBytes_, upload.buf, count); headerBytes_ += count; offset += count;
      if (headerBytes_ == sizeof(uploadHeader_)) {
        const char* error = devicecore::validatePackage(uploadHeader_, ESP_DEVICE_BOARD_TAG, config_.firmware->application);
        if (error) { failUpload(error); return; }
        firmwareBytes_ = devicecore::read32(uploadHeader_ + 120);
        updateVersion_ = reinterpret_cast<const char*>(uploadHeader_ + 88);
        if (!Update.begin(firmwareBytes_)) { failUpload("Spazio o partizione OTA non disponibile: " + updaterError()); return; }
        uploadActive_ = true;
        if (!Update.setMD5(reinterpret_cast<const char*>(uploadHeader_ + 124))) { failUpload("Checksum del pacchetto non valido"); return; }
      }
    }
    const size_t bytes = upload.currentSize - offset;
    if (bytes) {
      if (!uploadActive_ || bytes > firmwareBytes_ - uploadedBytes_) { failUpload("Dimensione del pacchetto non valida"); return; }
      if (Update.write(upload.buf + offset, bytes) != bytes) { failUpload("Scrittura fallita: " + updaterError()); return; }
      uploadedBytes_ += bytes;
    }
  } else if (upload.status == UPLOAD_FILE_END && updateError_.isEmpty()) {
    if (headerBytes_ != sizeof(uploadHeader_) || !uploadActive_ || uploadedBytes_ != firmwareBytes_) {
      failUpload("Caricamento incompleto"); return;
    }
    // Do not activate the image until the authenticated final request handler.
    uploadComplete_ = true;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    failUpload("Caricamento interrotto: firmware precedente conservato"); uploadSeen_ = false;
  }
}

void ESPDeviceCore::handleUpdate() {
  if (!requireWrite()) { failUpload("Aggiornamento non autorizzato"); uploadSeen_ = false; return; }
  if (!uploadSeen_ || !uploadComplete_ || !uploadActive_ || !updateError_.isEmpty()) {
    reply(400, updateError_.isEmpty() ? "Pacchetto mancante o incompleto" : updateError_);
    failUpload(updateError_.isEmpty() ? "Pacchetto mancante o incompleto" : updateError_); uploadSeen_ = false; return;
  }
  if (!Update.end(false)) {
    failUpload("Verifica firmware fallita: " + updaterError()); reply(400, updateError_); uploadSeen_ = false; return;
  }
  uploadActive_ = false; uploadSeen_ = false; uploadComplete_ = false;
  reply(200, "Firmware verificato. Riavvio e controllo versione " + updateVersion_); scheduleRestart();
}
