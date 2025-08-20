#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;  // UTC+1 (Italia)
const int daylightOffset_sec = 3600;

// ====== CONFIGURAZIONE ======
const char* ssid = "";
const char* password = "";
const char* botToken = ":";
const char* city = "Milan,it";                                            // città e paese
const char* openWeatherMapApiKey = "";    // API key meteo
// ===== CHAT AUTORIZZATE =====
const int NUM_CHAT_AUTORIZZATE = 2;
const String chatConsentite[NUM_CHAT_AUTORIZZATE] = {
  "",  // <- Sostituisci o aggiungi altri ID Telegram qui
  ""
};

String chatAttiva = "";

String benvenuto = "👋 Benvenuto nel sistema di irrigazione automatica!\n\n"
                   "Puoi controllare e programmare l'irrigazione direttamente da qui. Ecco i comandi disponibili:\n\n"
                   "🚿 /settore – attiva un singolo settore a scelta\n"
                   "🔧 /impostazioni – imposta ora, durata e avvio smart meteo\n"
                   "📊 /stato – visualizza lo stato attuale del sistema\n"
                   "💦 /ciclo – avvia subito un'irrigazione manuale\n"
                   "⛔️ /stop – interrompe immediatamente un ciclo in corso\n"
                   "🔄 /reset – ripristina le impostazioni salvate\n"
                   "🌤 /meteo – mostra i dati meteo grezzi (JSON)\n\n"
                   "ℹ️ Il sistema gestisce fino a 5 settori in sequenza\n"
                   "🌦 L'avvio smart impedisce l'irrigazione se è prevista pioggia\n"
                   "🕒 L'orario è sincronizzato tramite NTP\n\n"
                   "✅ Tutti i comandi funzionano anche mentre il sistema è attivo.";

const int numSettori = 5;

const int pinSettori[numSettori] = { D0, D1, D2, D3, D4 };
const int pinPulsante = D8;
const int pinLed = D6;  // D6

bool statoLed = false;
unsigned long ultimoLampeggio = 0;
const unsigned long intervalloLampeggio = 250;  // 500 ms


WiFiClientSecure client;
UniversalTelegramBot bot(botToken, client);

unsigned long lastCheckTime = 0;
const unsigned long interval = 1000;

bool isChatAutorizzata(String chat_id) {
  for (int i = 0; i < NUM_CHAT_AUTORIZZATE; i++) {
    if (chat_id == chatConsentite[i]) return true;
  }
  return false;
}

enum StatoBot {
  IDLE,
  ATTESA_AUTOMATICO,
  ATTESA_ORA,
  ATTESA_MINUTI,
  ATTESA_SMART,
  ATTESA_MINUTI_CICLO,
  ATTESTA_NUMERO_SETTORE,
  ATTESTA_MINUTI_SETTORE,
  ATTESA_ORE_SMART
};

struct Impostazioni {
  bool avvioAutomatico = true;
  int oraAvvio = 0;
  int minutiDurata = 0;
  bool impostazioniSet = false;
  bool avvioSmart = false;
  int orePioggia = 6;  // default 6 ore
  unsigned long timestampUltimaEsecuzioneCiclo = 0;

  void reset() {
    oraAvvio = 0;
    minutiDurata = 0;
    avvioSmart = false;
    orePioggia = 6;
    avvioAutomatico = true;
    timestampUltimaEsecuzioneCiclo = 0;
    impostazioniSet = false;
  }
};

Impostazioni dati;
StatoBot stato = IDLE;
int tempOra;


int settoreCorrente = -1;
unsigned long inizioSettoreMillis = 0;
int durataSettoreMinuti = 0;
bool cicloAttivo = false;
bool richiestaStop = false;

int settoreSelezionato = -1;
bool settoreSingoloAttivo = false;
int settoreAttivoSingolo = -1;
unsigned long inizioSettoreSingoloMillis = 0;
int durataSettoreSingoloMinuti = 0;

// Variabili globali
unsigned long orePrecedentiPioggia = 6;    // 6 ore
unsigned long timestampUltimaPioggia = 0;  // salva l'orario dell'ultima pioggia rilevata




void controllaPulsante() {
  if (digitalRead(pinPulsante)) {
    bot.sendMessage("404952185", "🔘 Pulsante fisico premuto\nAvvio ciclo manuale per " + String(dati.minutiDurata) + " minuti", "");
    iniziaSequenzaIrrigazione(dati.minutiDurata);
  }
}

void avviaSettoreSingolo(int numeroSettore, int durataMinuti) {
  if (numeroSettore < 0 || numeroSettore >= numSettori) return;

  for (int i = 0; i < numSettori; i++) {
    digitalWrite(pinSettori[i], HIGH);  // spegne tutti
  }

  digitalWrite(pinSettori[numeroSettore], LOW);

  settoreAttivoSingolo = numeroSettore;
  durataSettoreSingoloMinuti = durataMinuti;
  inizioSettoreSingoloMillis = millis();
  settoreSingoloAttivo = true;

  bot.sendMessage(chatAttiva, "🚿 Settore " + String(numeroSettore + 1) + " attivato per " + String(durataMinuti) + " minuti", "");
}






// ===== EEPROM =====
void salvaDati() {
  EEPROM.put(0, dati);
  EEPROM.commit();
}

void caricaDati() {
  EEPROM.get(0, dati);
}

// ===== METEO =====
bool pioggiaPrevista() {
  WiFiClient httpClient;
  HTTPClient http;

  String url = "http://api.openweathermap.org/data/2.5/weather?q=";
  url += city;
  url += "&appid=";
  url += openWeatherMapApiKey;
  url += "&units=metric";

  http.begin(httpClient, url);  // ✅ Nuovo modo
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);

    http.end();
    if (error) return false;

    const char* meteo = doc["weather"][0]["main"];
    if (String(meteo).indexOf("Rain") >= 0 || String(meteo).indexOf("Drizzle") >= 0) {
      return true;
    } else {
      return false;
    }
  }
  http.end();
  return false;
}
// Funzione da chiamare periodicamente per aggiornare l'ultima pioggia rilevata
void aggiornaUltimaPioggia() {
  if (pioggiaPrevista()) {
    timestampUltimaPioggia = time(nullptr);  // salva il momento della pioggia
  }
}

String recuperaMeteoJson() {
  WiFiClient httpClient;
  HTTPClient http;

  String url = "http://api.openweathermap.org/data/2.5/weather?q=";
  url += city;
  url += "&appid=";
  url += openWeatherMapApiKey;
  url += "&units=metric";

  http.begin(httpClient, url);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    http.end();
    return payload;
  } else {
    http.end();
    return "❌ Errore HTTP: " + String(httpCode);
  }
}


String orario() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("❌ Errore nel recupero dell'orario!");
    return "Errore time.h";
  } else {
    Serial.print("🕒 Orario sincronizzato: ");
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%A, %d %B %Y %H:%M:%S", &timeinfo);
    Serial.println(buffer);
    return String(buffer);
  }
}

long int timestampPerCiclo() {
  //siccome il ciclo si avvia alle ore XX per non ripetersi deve esserci nel timestamp fino alle ore
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char dataOra[15];  // 10 caratteri + null terminator
  strftime(dataOra, sizeof(dataOra), "%Y%m%d%H", t);
  //bot.sendMessage(chatAttiva, String(dataOra), "");
  return atol(dataOra);
}



// ===== IRRIGAZIONE =====
void iniziaSequenzaIrrigazione(int durataMinuti) {
  if (durataMinuti <= 0) durataMinuti = dati.minutiDurata;

  dati.timestampUltimaEsecuzioneCiclo = timestampPerCiclo();
  salvaDati();
  durataSettoreMinuti = durataMinuti;
  settoreCorrente = 0;
  richiestaStop = false;
  cicloAttivo = true;

  inizioSettoreMillis = millis();
  digitalWrite(pinSettori[settoreCorrente], LOW);
  bot.sendMessage(chatAttiva, "🚿 Avvio settore 1", "");
}



// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  caricaDati();

  for (int i = 0; i < numSettori; i++) {
    pinMode(pinSettori[i], OUTPUT);
    digitalWrite(pinSettori[i], HIGH);  // spegni tutti i relè
  }
  pinMode(pinPulsante, INPUT);


  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  client.setInsecure();
  Serial.println("WiFi connesso");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("⏱ Inizializzazione NTP...");
  delay(1000);

  Serial.println(orario());

  bot.sendMessage("404952185", "Avvio v8\n" + benvenuto, "");
  pinMode(pinLed, OUTPUT);
  digitalWrite(pinLed, LOW);  // spegni inizialmente
}



// ===== LOOP PRINCIPALE =====
void loop() {


  controllaPulsante();  // nuovo controllo pulsante con debounce


  if (millis() - lastCheckTime > interval) {
    lastCheckTime = millis();
    int numNuovi = bot.getUpdates(bot.last_message_received + 1);

    while (numNuovi) {
      for (int i = 0; i < numNuovi; i++) {
        String msg = bot.messages[i].text;
        String chat_id = bot.messages[i].chat_id;
        bot.last_message_received = bot.messages[i].update_id;

        if (!isChatAutorizzata(chat_id)) {
          bot.sendMessage(chat_id, "⛔️ Accesso negato. Chat non autorizzata.", "");
        } else if (msg == "/impostazioni") {
          stato = ATTESA_AUTOMATICO;
          chatAttiva = chat_id;
          bot.sendMessage(chat_id, "⚙️ Vuoi abilitare l'avvio automatico ogni giorno? (sì/no)", "");
        } else if (stato == ATTESA_AUTOMATICO && chat_id == chatAttiva) {
          String risposta = msg;
          risposta.toLowerCase();
          dati.avvioAutomatico = (risposta == "si" || risposta == "sì" || risposta == "s");
          stato = ATTESA_ORA;
          bot.sendMessage(chat_id, "🕒 A che ora vuoi avviare il ciclo? (0-23)", "");
        } else if (stato == ATTESA_ORA && chat_id == chatAttiva) {
          int ora = msg.toInt();
          if (ora >= 0 && ora <= 23) {
            tempOra = ora;
            stato = ATTESA_MINUTI;
            bot.sendMessage(chat_id, "⏱ Per quanti minuti vuoi irrigare?", "");
          } else {
            bot.sendMessage(chat_id, "❌ Ora non valida. Inserisci un valore tra 0 e 23.", "");
          }
        } else if (stato == ATTESA_MINUTI && chat_id == chatAttiva) {
          int durata = msg.toInt();
          if (durata > 0 && durata <= 120) {
            dati.oraAvvio = tempOra;
            dati.minutiDurata = durata;
            stato = ATTESA_SMART;
            bot.sendMessage(chat_id, "🌦 Vuoi attivare l'avvio smart con meteo? (sì/no)", "");
          } else {
            bot.sendMessage(chat_id, "❌ Durata non valida. Massimo 120 minuti.", "");
          }
        } else if (stato == ATTESA_SMART && chat_id == chatAttiva) {
          String risposta = msg;
          risposta.toLowerCase();
          dati.avvioSmart = (risposta == "si" || risposta == "sì" || risposta == "s");
          // Passa al nuovo stato per chiedere le ore
          stato = ATTESA_ORE_SMART;
          bot.sendMessage(chat_id, "⏳ Impedisci ciclo se ha piovuto nelle precedenti X ore (es. 6):", "");
        } else if (stato == ATTESA_ORE_SMART && chat_id == chatAttiva) {
          int ore = msg.toInt();
          if (ore > 0 && ore <= 24) {
            dati.orePioggia = ore;
            dati.impostazioniSet = true;
            salvaDati();
            stato = IDLE;

            String conferma = "✅ Impostazioni salvate:\n";
            conferma += "🔁 Avvio automatico: " + String(dati.avvioAutomatico ? "Attivo" : "Disattivo") + "\n";
            conferma += "🕒 Ora avvio: " + String(dati.oraAvvio) + "\n";
            conferma += "⏱ Durata: " + String(dati.minutiDurata) + " min\n";
            conferma += "🌦 Smart meteo: " + String(dati.avvioSmart ? "Attivo" : "Disattivo") + "\n";
            conferma += "⏳ Ore da considerare per pioggia: " + String(dati.orePioggia);
            bot.sendMessage(chat_id, conferma, "");
          } else {
            bot.sendMessage(chat_id, "❌ Numero ore non valido. Inserisci un numero tra 1 e 24.", "");
          }
        } else if (msg == "/ciclo") {
          stato = ATTESA_MINUTI_CICLO;
          chatAttiva = chat_id;
          bot.sendMessage(chat_id, "⏱ Per quanti minuti vuoi irrigare ora?", "");
        } else if (stato == ATTESA_MINUTI_CICLO && chat_id == chatAttiva) {
          int minuti = msg.toInt();
          if (minuti > 0 && minuti <= 120) {
            stato = IDLE;
            iniziaSequenzaIrrigazione(minuti);
            //bot.sendMessage(chat_id, "✅ Irrigazione manuale completata", "");
          } else {
            bot.sendMessage(chat_id, "❌ Durata non valida. Max 120 minuti.", "");
          }
        } else if (msg == "/stop") {
          if (cicloAttivo) {
            richiestaStop = true;
            bot.sendMessage(chat_id, "🛑 Comando ricevuto: sto interrompendo il ciclo completo...", "");
          } else if (settoreSingoloAttivo) {
            digitalWrite(pinSettori[settoreAttivoSingolo], HIGH);
            settoreSingoloAttivo = false;
            bot.sendMessage(chat_id, "🛑 Settore singolo interrotto", "");
          } else {
            bot.sendMessage(chat_id, "ℹ️ Nessuna irrigazione attiva.", "");
          }
        } else if (msg == "/stato") {
          if (dati.impostazioniSet) {
            String info = "📋 Impostazioni correnti:\n";
            info += "🔁 Avvio automatico: " + String(dati.avvioAutomatico ? "Attivo" : "Disattivo") + "\n";
            info += "🕒 Ora avvio: " + String(dati.oraAvvio) + "\n";
            info += "⏱ Durata: " + String(dati.minutiDurata) + " min\n";
            info += "🌦 Smart meteo: " + String(dati.avvioSmart ? "Attivo" : "Disattivo") + "\n";
            info += "🕒 Avvio automatico impedito se pioggia nelle ultime: " + String(dati.orePioggia) + " ore\n";
            info += "🕒 Orario: " + orario() + "\n";
            info += "🚿 Ultimo Ciclo: " + String(dati.timestampUltimaEsecuzioneCiclo) + "\n";

            if (cicloAttivo) {
              info += "⚠️ Ciclo in corso\n";
              if (settoreCorrente >= 0 && settoreCorrente < numSettori) {
                info += "🔌 Settore attivo: " + String(settoreCorrente + 1) + "\n";
              }
            } else {
              info += "✅ Nessun ciclo in corso\n";
            }

            bot.sendMessage(chat_id, info, "");
          } else {
            bot.sendMessage(chat_id, "⚠️ Nessuna impostazione salvata.", "");
          }
        } else if (msg == "/start") {
          bot.sendMessage(chat_id, benvenuto, "Markdown");
        } else if (msg == "/reset") {
          dati.reset();  // oppure imposta a mano ogni campo se non hai aggiunto reset()
          salvaDati();
          stato = IDLE;
          bot.sendMessage(chat_id, "🔄 Impostazioni ripristinate ai valori predefiniti.\nOra premi /impostazioni", "");
        } else if (msg == "/meteo") {
          String jsonMeteo = recuperaMeteoJson();
          if (jsonMeteo.length() > 4096) {
            bot.sendMessage(chat_id, "⚠️ Risposta troppo lunga. Mostro solo l'inizio:\n\n" + jsonMeteo.substring(0, 4090), "");
          } else {
            bot.sendMessage(chat_id, "🌦 Risposta meteo JSON:\n\n" + jsonMeteo, "");
          }
        } else if (msg == "/settore") {
          stato = ATTESTA_NUMERO_SETTORE;
          chatAttiva = chat_id;
          bot.sendMessage(chat_id, "🔢 Quale settore vuoi attivare? (1-" + String(numSettori) + ")", "");
        } else if (stato == ATTESTA_NUMERO_SETTORE && chat_id == chatAttiva) {
          int num = msg.toInt();
          if (num >= 1 && num <= numSettori) {
            settoreSelezionato = num - 1;
            stato = ATTESTA_MINUTI_SETTORE;
            bot.sendMessage(chat_id, "⏱ Per quanti minuti vuoi attivare il settore " + String(num) + "?", "");
          } else {
            bot.sendMessage(chat_id, "❌ Settore non valido. Inserisci un numero da 1 a " + String(numSettori), "");
          }
        } else if (stato == ATTESTA_MINUTI_SETTORE && chat_id == chatAttiva) {
          int minuti = msg.toInt();
          if (minuti > 0 && minuti <= 120) {
            stato = IDLE;
            avviaSettoreSingolo(settoreSelezionato, minuti);
          } else {
            bot.sendMessage(chat_id, "❌ Durata non valida. Inserisci un numero tra 1 e 120.", "");
          }
        }
      }
      numNuovi = bot.getUpdates(bot.last_message_received + 1);
    }





    // === AVVIO AUTOMATICO (solo ora esatta e ogni 60s) ===
    if (dati.impostazioniSet && dati.avvioAutomatico) {
      time_t now = time(nullptr);
      struct tm* t = localtime(&now);

      // Aggiorna l'ultima pioggia prima di decidere se avviare il ciclo
      aggiornaUltimaPioggia();

      if (cicloAttivo) {
        Serial.println("ciclo attivo");
      } else {
        if (t->tm_hour == dati.oraAvvio) {
          if (dati.timestampUltimaEsecuzioneCiclo != timestampPerCiclo()) {

            // Controllo smart: se ha piovuto nelle ultime X ore, non avviare
            if (dati.avvioSmart && (now - timestampUltimaPioggia < orePrecedentiPioggia * 3600UL)) {
              bot.sendMessage(chatAttiva, "⛔️ Irrigazione annullata: pioggia nelle ultime " + String(orePrecedentiPioggia) + " ore.", "");
              dati.timestampUltimaEsecuzioneCiclo = timestampPerCiclo();  // evita tentativi ripetuti
              salvaDati();                                                // salva le modifiche su EEPROM
            } else {
              iniziaSequenzaIrrigazione(dati.minutiDurata);
            }
          }
        } else {
          Serial.println("⛔️ Irrigazione annullata: ciclo già eseguito oggi. timestampUltimaEsecuzioneCiclo = " + String(dati.timestampUltimaEsecuzioneCiclo));
        }
      }
    }
  }

  if (cicloAttivo) {
    if (richiestaStop) {
      digitalWrite(pinSettori[settoreCorrente], HIGH);
      cicloAttivo = false;
      richiestaStop = false;
      bot.sendMessage(chatAttiva, "🛑 Irrigazione interrotta manualmente", "");
      return;
    }

    unsigned long durataMillis = durataSettoreMinuti * 60UL * 1000UL;
    if (millis() - inizioSettoreMillis >= durataMillis) {
      digitalWrite(pinSettori[settoreCorrente], HIGH);  // spegni attuale

      settoreCorrente++;
      if (settoreCorrente >= numSettori) {
        cicloAttivo = false;
        bot.sendMessage(chatAttiva, "✅ Irrigazione completata!", "");
      } else {
        //spengo e cambio settore
        digitalWrite(pinSettori[settoreCorrente], LOW);
        inizioSettoreMillis = millis();
        bot.sendMessage(chatAttiva, "🚿 Avvio settore " + String(settoreCorrente + 1), "");
      }
    }
  }

  if (settoreSingoloAttivo) {
    unsigned long durataMillis = durataSettoreSingoloMinuti * 60000UL;
    if (millis() - inizioSettoreSingoloMillis >= durataMillis) {
      digitalWrite(pinSettori[settoreAttivoSingolo], HIGH);
      settoreSingoloAttivo = false;
      bot.sendMessage(chatAttiva, "✅ Settore " + String(settoreAttivoSingolo + 1) + " disattivato (fine tempo)", "");
    }
  }

  if (millis() - ultimoLampeggio >= intervalloLampeggio) {
    ultimoLampeggio = millis();
    statoLed = !statoLed;
    digitalWrite(pinLed, statoLed ? HIGH : LOW);
  }
}

//////////////////////////////////////////////////////////
