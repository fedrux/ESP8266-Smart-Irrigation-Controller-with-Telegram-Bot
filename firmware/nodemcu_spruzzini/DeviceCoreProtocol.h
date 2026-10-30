#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Arduino-independent storage and OTA boundaries, also exercised by native tests.
namespace devicecore {
constexpr size_t kPackageHeaderSize = 256;
constexpr size_t kIdentitySize = 124;
inline uint32_t crc32(const void* data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  while (length--) {
    crc ^= *bytes++;
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}
inline uint32_t read32(const uint8_t* v) {
  return uint32_t(v[0]) | (uint32_t(v[1]) << 8) | (uint32_t(v[2]) << 16) | (uint32_t(v[3]) << 24);
}
inline bool fieldValid(const char* value, size_t capacity) {
  const char* end = static_cast<const char*>(memchr(value, 0, capacity));
  if (!end || end == value) return false;
  for (const char* p = value; p != end; ++p)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-')) return false;
  return true;
}
inline bool parseIp(const char* text, uint8_t output[4]) {
  uint8_t candidate[4];
  for (unsigned part = 0; part < 4; ++part) {
    unsigned number = 0, digits = 0;
    while (*text >= '0' && *text <= '9') {
      number = number * 10 + (*text++ - '0');
      if (++digits > 3 || number > 255) return false;
    }
    if (!digits || (part < 3 ? *text++ != '.' : *text != 0)) return false;
    candidate[part] = uint8_t(number);
  }
  memcpy(output, candidate, 4);
  return true;
}
inline uint32_t ipNumber(const uint8_t ip[4]) {
  return (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) | (uint32_t(ip[2]) << 8) | ip[3];
}
inline bool validStatic(const uint8_t ip[4], const uint8_t gateway[4],
                        const uint8_t subnet[4], const uint8_t dns[4]) {
  const uint32_t mask = ipNumber(subnet), hostMask = ~mask;
  const uint32_t address = ipNumber(ip), router = ipNumber(gateway);
  if (!mask || hostMask < 3 || (hostMask & (hostMask + 1))) return false;
  if (ip[0] == 0 || ip[0] == 127 || ip[0] >= 224 || gateway[0] == 0 ||
      gateway[0] == 127 || gateway[0] >= 224 || dns[0] == 0 || dns[0] == 127 || dns[0] >= 224) return false;
  return (address & mask) == (router & mask) && address != router &&
         (address & hostMask) && (address & hostMask) != hostMask &&
         (router & hostMask) && (router & hostMask) != hostMask;
}
// magic[8], target[32], app[48], version[32], sizeLE[4], md5[33], reserved[95], crcLE[4].
// MD5 checks transport integrity, not authorship.
inline const char* validatePackage(const uint8_t* h, const char* target, const char* app) {
  if (memcmp(h, "EDCOTA2\0", 8)) return "Formato non riconosciuto: serve un pacchetto .edc";
  if (crc32(h, 252) != read32(h + 252)) return "Intestazione danneggiata";
  if (!fieldValid(reinterpret_cast<const char*>(h + 8), 32) ||
      !fieldValid(reinterpret_cast<const char*>(h + 40), 48) ||
      !fieldValid(reinterpret_cast<const char*>(h + 88), 32)) return "Identita firmware non valida";
  if (strcmp(reinterpret_cast<const char*>(h + 8), target)) return "Firmware per un'altra scheda";
  if (strcmp(reinterpret_cast<const char*>(h + 40), app)) return "Firmware per un'altra applicazione";
  if (!read32(h + 120)) return "Firmware vuoto";
  for (unsigned i = 124; i < 156; ++i)
    if (!((h[i] >= '0' && h[i] <= '9') || (h[i] >= 'a' && h[i] <= 'f'))) return "Checksum non valido";
  if (h[156]) return "Checksum non valido";
  return nullptr;
}
}
