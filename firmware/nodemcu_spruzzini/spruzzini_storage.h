#pragma once
#include <flash_hal.h>

// The core exclusively owns EEPROM 0..1023. The application uses LittleFS.
static_assert(sizeof(Impostazioni) == 24, "Unexpected v8 EEPROM layout");
bool memoriaPronta = false;
String erroreMemoria;
const char* fileImpostazioni = "/spruzzini-v8.dat";
struct RecordSpruzzini {
  uint32_t magic, version;
  uint8_t dati[24];
  uint32_t crc;
};

bool impostazioniValide(const uint8_t* bytes) {
  // Validate raw boolean bytes BEFORE constructing an object with bool fields.
  if (bytes[0] > 1 || bytes[12] > 1 || bytes[13] > 1) return false;
  int32_t ora, minuti, ore;
  uint32_t ultima;
  memcpy(&ora, bytes + 4, 4); memcpy(&minuti, bytes + 8, 4);
  memcpy(&ore, bytes + 16, 4); memcpy(&ultima, bytes + 20, 4);
  return ora >= 0 && ora <= 23 && minuti >= 0 && minuti <= 120 &&
    (!bytes[12] || minuti > 0) && ore >= 1 && ore <= 24 &&
    (ultima == 0 || (ultima / 1000000 >= 2020 && ultima / 1000000 <= 2099 &&
      (ultima / 10000) % 100 >= 1 && (ultima / 10000) % 100 <= 12 &&
      (ultima / 100) % 100 >= 1 && (ultima / 100) % 100 <= 31 && ultima % 100 <= 23));
}

bool leggiRecord(const char* path, RecordSpruzzini& record) {
  File file = LittleFS.open(path, "r");
  if (!file || file.size() != sizeof(record)) return false;
  const size_t count = file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record));
  file.close();
  return count == sizeof(record) && record.magic == 0x53505238 && record.version == 1 &&
    record.crc == devicecore::crc32(&record, offsetof(RecordSpruzzini, crc)) &&
    impostazioniValide(record.dati);
}

bool salvaImpostazioni(const Impostazioni& candidate) {
  if (!memoriaPronta) { erroreMemoria = "Memoria impostazioni non disponibile"; return false; }
  RecordSpruzzini record{};
  record.magic = 0x53505238; record.version = 1;
  memcpy(record.dati, &candidate, sizeof(candidate));
  if (!impostazioniValide(record.dati)) { erroreMemoria = "Impostazioni non valide"; return false; }
  record.crc = devicecore::crc32(&record, offsetof(RecordSpruzzini, crc));
  File file = LittleFS.open("/spruzzini-v8.tmp", "w");
  if (!file) { erroreMemoria = "Apertura file impostazioni fallita"; return false; }
  const size_t count = file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record));
  file.flush(); file.close();
  RecordSpruzzini check{};
  if (count != sizeof(record) || !leggiRecord("/spruzzini-v8.tmp", check) ||
      memcmp(&record, &check, sizeof(record)) || !LittleFS.rename("/spruzzini-v8.tmp", fileImpostazioni)) {
    erroreMemoria = "Salvataggio impostazioni fallito"; return false;
  }
  erroreMemoria = "";
  return true;
}

bool partizioneVuota() {
  // Never autoformat a populated/corrupted filesystem. Only erased flash is initialized.
  if (!FS_PHYS_SIZE || FS_PHYS_ADDR >= ESP.getFlashChipRealSize() ||
      FS_PHYS_SIZE > ESP.getFlashChipRealSize() - FS_PHYS_ADDR) return false;
  uint32_t block[64];
  for (uint32_t offset = 0; offset < FS_PHYS_SIZE; offset += sizeof(block)) {
    const size_t length = min(size_t(sizeof(block)), size_t(FS_PHYS_SIZE - offset));
    if (!ESP.flashRead(FS_PHYS_ADDR + offset, block, length)) return false;
    for (size_t i = 0; i < length / 4; ++i) if (block[i] != 0xFFFFFFFF) return false;
    yield();
  }
  return true;
}

void caricaDati() {
  LittleFS.setConfig(LittleFSConfig(false));
  memoriaPronta = LittleFS.begin();
  if (!memoriaPronta && partizioneVuota()) memoriaPronta = LittleFS.format() && LittleFS.begin();
  if (!memoriaPronta) {
    dati.reset(); erroreMemoria = "LittleFS non leggibile: nessuna formattazione automatica"; return;
  }
  RecordSpruzzini record{};
  if (leggiRecord(fileImpostazioni, record)) { memcpy(&dati, record.dati, sizeof(dati)); return; }
  if (LittleFS.exists(fileImpostazioni)) {
    dati.reset(); erroreMemoria = "Impostazioni danneggiate: avvio automatico non configurato"; return;
  }
  // setup() has opened the historical EEPROM before deviceCore.begin().
  uint8_t legacy[24];
  for (size_t i = 0; i < sizeof(legacy); ++i) legacy[i] = EEPROM.read(i);
  if (impostazioniValide(legacy)) memcpy(&dati, legacy, sizeof(dati));
  else dati.reset();
  if (!salvaImpostazioni(dati)) dati.reset();
}

void salvaDati() {
  if (!salvaImpostazioni(dati)) Serial.println(erroreMemoria);
}
