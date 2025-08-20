
// === Controller Irrigazione Automatico v7 - 2025 ===
// Codice reso anonimo per condivisione su GitHub
// Tutti i dati sensibili (SSID, Password WiFi, Token Telegram, API Key Meteo) 
// sono stati rimossi o sostituiti con placeholder.

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <time.h>
#include <ESP8266HTTPClient.h>

// === CONFIGURAZIONE WI-FI (inserire i propri dati) ===
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// === CONFIGURAZIONE TELEGRAM ===
#define BOT_TOKEN "YOUR_TELEGRAM_BOT_TOKEN"
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// === CONFIGURAZIONE OPENWEATHERMAP ===
String apiKey = "YOUR_OPENWEATHERMAP_API_KEY";
String cityID = "YOUR_CITY_ID";   // esempio: 3173435 per Milano
String units = "metric";
String language = "it";

// === STRUTTURA DATI EEPROM ===
struct Impostazioni {
  bool avvioAutomatico;
  int oraAvvio;
  int minutiDurata;
  bool avvioSmart;
  int orePrecedentiPioggia;
};

struct Dati {
  Impostazioni impostazioni;
  long timestampUltimaEsecuzioneCiclo;
} dati;

// === PINOUT ===
const int numSettori = 5;
const int pinSettori[numSettori] = { 16, 5, 4, 0, 2 }; // D0, D1, D2, D3, D4
const int pinPulsante = 15;  // Pulsante fisico
const int pinLed = 13;       // LED lampeggiante (D7)

// === VARIABILI GLOBALI ===
bool cicloAttivo = false;
int settoreAttivo = -1;
unsigned long millisUltimoLampeggio = 0;
bool statoLed = false;
time_t timestampUltimaPioggia = 0;
String chatAttiva = "";  // chat corrente (verrà sostituita da quella autorizzata)

// === FUNZIONI UTILI ===
long timestampPerCiclo() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  return (long)(t->tm_year * 10000L + (t->tm_mon + 1) * 100L + t->tm_mday);
}

void iniziaSequenzaIrrigazione(int durata) {
  cicloAttivo = true;
  dati.timestampUltimaEsecuzioneCiclo = timestampPerCiclo();
  Serial.println("💧 Avvio ciclo irrigazione per " + String(durata) + " minuti.");
  // Attiva settore 0 (esempio semplificato)
  digitalWrite(pinSettori[0], HIGH);
  delay(durata * 60000UL);
  digitalWrite(pinSettori[0], LOW);
  cicloAttivo = false;
  Serial.println("✅ Ciclo completato.");
}

// === SETUP ===
void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connesso");

  // Impostazione pin
  pinMode(pinPulsante, INPUT_PULLUP);
  pinMode(pinLed, OUTPUT);
  for (int i = 0; i < numSettori; i++) {
    pinMode(pinSettori[i], OUTPUT);
    digitalWrite(pinSettori[i], LOW);
  }

  // Imposta NTP
  configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");
  Serial.println("⏱ Orario sincronizzato.");

  // Placeholder impostazioni
  dati.impostazioni.avvioAutomatico = true;
  dati.impostazioni.oraAvvio = 2;
  dati.impostazioni.minutiDurata = 10;
  dati.impostazioni.avvioSmart = true;
  dati.impostazioni.orePrecedentiPioggia = 6;
  dati.timestampUltimaEsecuzioneCiclo = 0;
}

// === LOOP ===
void loop() {
  // LED lampeggiante ogni 0.5s
  if (millis() - millisUltimoLampeggio > 500) {
    statoLed = !statoLed;
    digitalWrite(pinLed, statoLed);
    millisUltimoLampeggio = millis();
  }

  // Lettura pulsante fisico (debounce semplice)
  if (digitalRead(pinPulsante) == LOW) {
    Serial.println("🔘 Pulsante premuto -> avvio irrigazione manuale.");
    iniziaSequenzaIrrigazione(dati.impostazioni.minutiDurata);
    delay(1000); // evita pressioni multiple
  }

  // Controllo avvio automatico (semplificato)
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  if (!cicloAttivo && dati.impostazioni.avvioAutomatico) {
    if (t->tm_hour == dati.impostazioni.oraAvvio && 
        dati.timestampUltimaEsecuzioneCiclo != timestampPerCiclo()) {

      if (dati.impostazioni.avvioSmart && 
          (now - timestampUltimaPioggia < dati.impostazioni.orePrecedentiPioggia * 3600UL)) {
        Serial.println("⛔️ Irrigazione annullata: pioggia nelle ultime " + String(dati.impostazioni.orePrecedentiPioggia) + " ore.");
        dati.timestampUltimaEsecuzioneCiclo = timestampPerCiclo();
      } else {
        iniziaSequenzaIrrigazione(dati.impostazioni.minutiDurata);
      }
    }
  }
}
