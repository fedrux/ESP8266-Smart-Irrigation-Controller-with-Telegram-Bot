#pragma once
#include "spruzzini_web.h"
#include <ESP8266mDNS.h>
extern "C" {
#include <user_interface.h>
}

void iniziaSequenzaIrrigazione(int durataMinuti);
void avviaSettoreSingolo(int numeroSettore, int durataMinuti);
String orario();
constexpr char NOME_RETE[] = "nodemcu-spruzzini";
ESP_DEVICE_FIRMWARE(firmwareIdentity, "nodemcu-spruzzini", "8.1.1");
DeviceCoreConfig configurazioneCore() {
  DeviceCoreConfig config{NOME_RETE, ssid, password,
    WIFI_SECONDARY_SSID, WIFI_SECONDARY_PASSWORD, ADMIN_USER, ADMIN_PASSWORD, SETUP_AP_PASSWORD};
  config.useDhcp = USE_DHCP;
  memcpy(config.staticIp, STATIC_IP, 4); memcpy(config.gateway, GATEWAY, 4);
  memcpy(config.subnet, SUBNET, 4); memcpy(config.dns, DNS_SERVER, 4);
  config.firmware = &firmwareIdentity;
  return config;
}
ESPDeviceCore deviceCore(configurazioneCore());
String tokenSpruzzini;
bool notificaAvvioInviata = false;
bool primaConnessioneVista = false;
uint32_t primaConnessioneMs = 0;
bool mdnsPronto = false;
uint32_t ultimoTentativoMdns = 0;

bool irrigazioneAttiva() { return cicloAttivo || settoreSingoloAttivo; }
String statoApplicazione() {
  if (!erroreMemoria.isEmpty()) return "Impostazioni: " + erroreMemoria;
  if (cicloAttivo) return "Ciclo completo: settore " + String(settoreCorrente + 1);
  if (settoreSingoloAttivo) return "Settore singolo " + String(settoreAttivoSingolo + 1);
  return dati.impostazioniSet ? "Irrigazione a riposo; programma configurato" : "Irrigazione a riposo; programma da configurare";
}
bool autenticato() {
  auto& web = deviceCore.server();
  if (web.authenticate(ADMIN_USER, ADMIN_PASSWORD)) return true;
  web.requestAuthentication(); return false;
}
void rispostaSpruzzini(int code, const String& message) {
  StaticJsonDocument<512> doc;
  doc["success"] = code >= 200 && code < 300; doc["message"] = message;
  String body; serializeJson(doc, body);
  deviceCore.server().sendHeader("Cache-Control", "no-store");
  deviceCore.server().send(code, "application/json", body);
}
bool autorizzaScrittura() {
  if (!autenticato()) return false;
  auto& web = deviceCore.server();
  if (tokenSpruzzini.isEmpty() || web.header("X-CSRF-Token") != tokenSpruzzini) {
    rispostaSpruzzini(403, "Sessione non valida: ricarica la pagina"); return false;
  }
  return true;
}
bool numeroArgomento(const char* name, int minimum, int maximum, int& output) {
  String value = deviceCore.server().arg(name);
  if (value.isEmpty() || value.length() > 3) return false;
  int number = 0;
  for (size_t i = 0; i < value.length(); ++i) {
    if (value[i] < '0' || value[i] > '9') return false;
    number = number * 10 + value[i] - '0';
  }
  if (number < minimum || number > maximum) return false;
  output = number; return true;
}
void inviaStatoSpruzzini() {
  if (!autenticato()) return;
  StaticJsonDocument<2048> doc;
  doc["application"] = "nodemcu-spruzzini"; doc["version"] = "8.1.1";
  doc["hostname"] = String(NOME_RETE) + ".local";
  doc["ip"] = WiFi.localIP().toString();
  doc["csrf_token"] = tokenSpruzzini; doc["status"] = statoApplicazione();
  doc["application_ready"] = notificaAvvioInviata;
  doc["cycle_active"] = cicloAttivo; doc["single_active"] = settoreSingoloAttivo;
  doc["sector"] = cicloAttivo ? settoreCorrente + 1 : (settoreSingoloAttivo ? settoreAttivoSingolo + 1 : 0);
  doc["remaining_seconds"] = 0;
  if (irrigazioneAttiva()) {
    uint32_t duration = uint32_t(cicloAttivo ? durataSettoreMinuti : durataSettoreSingoloMinuti) * 60000UL;
    uint32_t elapsed = uint32_t(millis() - (cicloAttivo ? inizioSettoreMillis : inizioSettoreSingoloMillis));
    doc["remaining_seconds"] = elapsed >= duration ? 0 : (duration - elapsed + 999) / 1000;
  }
  doc["configured"] = dati.impostazioniSet; doc["automatic"] = dati.avvioAutomatico;
  doc["start_hour"] = dati.oraAvvio; doc["minutes"] = dati.minutiDurata;
  doc["smart"] = dati.avvioSmart; doc["rain_hours_saved"] = dati.orePioggia;
  doc["rain_hours_effective"] = orePrecedentiPioggia;
  doc["last_cycle"] = dati.timestampUltimaEsecuzioneCiclo;
  doc["last_rain"] = timestampUltimaPioggia;
  doc["storage_ready"] = memoriaPronta; doc["storage_error"] = erroreMemoria;
  time_t now = time(nullptr); struct tm info{}; localtime_r(&now, &info);
  doc["clock_valid"] = info.tm_year > 116;
  char clock[32]{};
  if (info.tm_year > 116) strftime(clock, sizeof(clock), "%d/%m/%Y %H:%M:%S", &info);
  doc["clock"] = clock;
  JsonArray sectors = doc.createNestedArray("outputs");
  for (int i = 0; i < numSettori; ++i) sectors.add(digitalRead(pinSettori[i]) == LOW);
  String body; serializeJson(doc, body);
  deviceCore.server().sendHeader("Cache-Control", "no-store");
  deviceCore.server().send(200, "application/json", body);
}
void comandoSpruzzini() {
  if (!autorizzaScrittura()) return;
  auto& web = deviceCore.server();
  const String action = web.arg("action");
  if (action == "stop") {
    // Same v8 stop request; the existing loop performs the actual cycle stop.
    if (cicloAttivo) richiestaStop = true;
    else if (settoreSingoloAttivo) {
      digitalWrite(pinSettori[settoreAttivoSingolo], HIGH); settoreSingoloAttivo = false;
    }
    rispostaSpruzzini(200, "Richiesta di arresto ricevuta"); return;
  }
  if (irrigazioneAttiva()) { rispostaSpruzzini(409, "Ferma prima l'irrigazione in corso"); return; }
  if (!memoriaPronta || ((action == "cycle" || action == "sector") && !erroreMemoria.isEmpty())) {
    rispostaSpruzzini(503, "Verifica prima la memoria delle impostazioni"); return;
  }
  if (action == "cycle" || action == "sector") {
    if (!notificaAvvioInviata) { rispostaSpruzzini(503, "Attendi l'inizializzazione di rete e orario"); return; }
    int minutes, sector;
    if (!numeroArgomento("minutes", 1, 120, minutes) ||
        (action == "sector" && !numeroArgomento("sector", 1, numSettori, sector))) {
      rispostaSpruzzini(400, "Durata o settore non validi"); return;
    }
    chatAttiva = chatConsentite[0]; stato = IDLE;
    if (action == "cycle") iniziaSequenzaIrrigazione(minutes);
    else avviaSettoreSingolo(sector - 1, minutes);
    rispostaSpruzzini(200, action == "cycle" ? "Ciclo completo avviato" : "Settore avviato"); return;
  }
  if (action == "settings" || action == "reset") {
    Impostazioni candidate = dati;
    if (action == "reset") candidate.reset();
    else {
      int automatic, smart;
      if (!numeroArgomento("automatic", 0, 1, automatic) || !numeroArgomento("smart", 0, 1, smart) ||
          !numeroArgomento("hour", 0, 23, candidate.oraAvvio) ||
          !numeroArgomento("minutes", 1, 120, candidate.minutiDurata) ||
          !numeroArgomento("rain_hours", 1, 24, candidate.orePioggia)) {
        rispostaSpruzzini(400, "Impostazioni non valide"); return;
      }
      candidate.avvioAutomatico = automatic; candidate.avvioSmart = smart; candidate.impostazioniSet = true;
    }
    if (!salvaImpostazioni(candidate)) { rispostaSpruzzini(500, "Impostazioni non salvate"); return; }
    dati = candidate; stato = IDLE;
    rispostaSpruzzini(200, action == "reset" ? "Impostazioni ripristinate" : "Impostazioni salvate"); return;
  }
  rispostaSpruzzini(400, "Comando non riconosciuto");
}
void inviaAsset(const char* type, const uint8_t* bytes, size_t length) {
  if (!autenticato()) return;
  auto& web = deviceCore.server();
  web.sendHeader("Cache-Control", "no-store"); web.sendHeader("Content-Encoding", "gzip");
  web.sendHeader("X-Content-Type-Options", "nosniff");
  web.sendHeader("Content-Security-Policy", "default-src 'self'; frame-ancestors 'none'; object-src 'none'");
  web.send_P(200, type, reinterpret_cast<const char*>(bytes), length);
}
void preparaInterfaccia() {
  for (unsigned i = 0; i < 4; ++i) { char part[9]; snprintf(part, sizeof(part), "%08lx", static_cast<unsigned long>(os_random())); tokenSpruzzini += part; }
  auto& web = deviceCore.server();
  // Reject maintenance before parsing multipart data: uploads must never hold a live relay on.
  web.addHook([](const String& method, const String& url, WiFiClient* connection, DeviceWebServer::ContentTypeFunction) {
    if (method == "POST" && irrigazioneAttiva() &&
        (url == "/admin/api/update" || url == "/admin/api/restart" || url == "/admin/api/config")) {
      const String body = "{\"message\":\"Ferma prima l'irrigazione in corso\"}";
      connection->print("HTTP/1.1 409 Conflict\r\nContent-Type: application/json\r\nConnection: close\r\nCache-Control: no-store\r\nContent-Length: " + String(body.length()) + "\r\n\r\n" + body);
      return DeviceWebServer::CLIENT_MUST_STOP;
    }
    return DeviceWebServer::CLIENT_REQUEST_CAN_CONTINUE;
  });
  web.on("/", HTTP_GET, [] { inviaAsset("text/html; charset=utf-8", SPRUZZINI_INDEX, sizeof(SPRUZZINI_INDEX)); });
  web.on("/css/custom.css", HTTP_GET, [] { inviaAsset("text/css; charset=utf-8", SPRUZZINI_CSS, sizeof(SPRUZZINI_CSS)); });
  web.on("/js/spruzzini.js", HTTP_GET, [] { inviaAsset("text/javascript; charset=utf-8", SPRUZZINI_JS, sizeof(SPRUZZINI_JS)); });
  web.on("/api/spruzzini/status", HTTP_GET, inviaStatoSpruzzini);
  web.on("/api/spruzzini/command", HTTP_POST, comandoSpruzzini);
  deviceCore.setStatusProvider(statoApplicazione);
}
void servizioRete() {
  if (WiFi.status() != WL_CONNECTED) {
    if (mdnsPronto) MDNS.close();
    mdnsPronto = false;
    return;
  }
  if (!mdnsPronto && uint32_t(millis() - ultimoTentativoMdns) >= 2000) {
    ultimoTentativoMdns = millis();
    mdnsPronto = MDNS.begin(NOME_RETE);
    if (mdnsPronto) MDNS.addService("http", "tcp", 80);
  }
  if (mdnsPronto) MDNS.update();
  if (WiFi.status() == WL_CONNECTED && !notificaAvvioInviata) {
    if (!primaConnessioneVista) { primaConnessioneVista = true; primaConnessioneMs = millis(); }
    if (uint32_t(millis() - primaConnessioneMs) < 1000) return;
    // Same initial NTP wait as v8, without delaying core recovery while Wi-Fi is absent.
    Serial.println(orario());
    notificaAvvioInviata = true;
    bot.sendMessage(chatConsentite[0], "Avvio v8 con interfaccia condivisa\n" + benvenuto, "");
  }
}
