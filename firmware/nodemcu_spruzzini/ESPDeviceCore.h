#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include "DeviceCoreProtocol.h"
#if defined(ESP8266)
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <Updater.h>
using DeviceWebServer = ESP8266WebServer;
#define ESP_DEVICE_BOARD_TAG "esp8266"
#else
#include <WebServer.h>
#include <WiFi.h>
#include <Update.h>
using DeviceWebServer = WebServer;
#if CONFIG_IDF_TARGET_ESP32C3
#define ESP_DEVICE_BOARD_TAG "esp32c3"
#elif CONFIG_IDF_TARGET_ESP32S3
#define ESP_DEVICE_BOARD_TAG "esp32s3"
#elif CONFIG_IDF_TARGET_ESP32S2
#define ESP_DEVICE_BOARD_TAG "esp32s2"
#elif CONFIG_IDF_TARGET_ESP32C6
#define ESP_DEVICE_BOARD_TAG "esp32c6"
#elif CONFIG_IDF_TARGET_ESP32
#define ESP_DEVICE_BOARD_TAG "esp32"
#else
#define ESP_DEVICE_BOARD_TAG "unsupported"
#endif
#endif

struct DeviceFirmwareIdentity {
  char magic[12];
  char board[32];
  char application[48];
  char version[32];
};
static_assert(sizeof(DeviceFirmwareIdentity) == devicecore::kIdentitySize, "Identity layout changed");
// Referenced by config.firmware and extracted from the image by tools/package_firmware.py.
#define ESP_DEVICE_FIRMWARE(name, app, ver) \
  const DeviceFirmwareIdentity name = {"EDCIDENT2", ESP_DEVICE_BOARD_TAG, app, ver}

struct DeviceCoreConfig {
  const char* deviceName;
  const char* primarySsid;
  const char* primaryPassword;
  const char* secondarySsid;
  const char* secondaryPassword;
  const char* adminUser;
  const char* adminPassword;
  const char* apPassword;
  uint32_t wifiTimeoutMs = 15000;
  bool useDhcp = true;
  uint8_t staticIp[4] = {0, 0, 0, 0};
  uint8_t gateway[4] = {0, 0, 0, 0};
  uint8_t subnet[4] = {255, 255, 255, 0};
  uint8_t dns[4] = {0, 0, 0, 0};
  const DeviceFirmwareIdentity* firmware = nullptr;
};

class ESPDeviceCore {
 public:
  explicit ESPDeviceCore(const DeviceCoreConfig& config);
  void begin();
  void loop();
  void setStatusProvider(String (*provider)());
  String statusText() const;
  bool restart();
  bool setupMode() const;
  DeviceWebServer& server();
 private:
  // The v1 layout at offset zero is retained for migration.
  struct NetworkConfig {
    uint32_t magic;
    char primarySsid[64];
    char primaryPassword[64];
    char secondarySsid[64];
    char secondaryPassword[64];
    uint8_t useDhcp;
    uint8_t staticIp[4], gateway[4], subnet[4], dns[4];
  };
  struct StorageRecord {
    uint32_t magic, version;
    NetworkConfig network;
    uint32_t crc;
  };
  static constexpr uint32_t kStorageMagic = 0x45535044;
  static constexpr size_t kStorageSize = 1024;
  DeviceCoreConfig config_;
  NetworkConfig network_{}, previous_{};
  DeviceWebServer server_{80};
  String csrfToken_, status_, lastError_, updateError_, updateVersion_;
  String (*statusProvider_)() = nullptr;
  uint32_t attemptAt_ = 0, retryAt_ = 0, connectedAt_ = 0, trialAt_ = 0;
  uint32_t disconnects_ = 0, attempts_ = 0, restartAt_ = 0, lastLoopAt_ = 0;
  uint32_t uploadAt_ = 0;
  uint64_t uptimeMs_ = 0;
  uint8_t networkIndex_ = 0, failedNetworks_ = 0;
  bool setupMode_ = false, connecting_ = false, wasConnected_ = false;
  bool trial_ = false, trialScheduled_ = false, storageReady_ = false;
  bool rollbackScheduled_ = false;
  uint32_t rollbackAt_ = 0;
  bool uploadActive_ = false, uploadSeen_ = false, uploadComplete_ = false;
  uint8_t uploadHeader_[devicecore::kPackageHeaderSize]{};
  size_t headerBytes_ = 0, uploadedBytes_ = 0, firmwareBytes_ = 0;
  bool authorized();
  bool validCsrf();
  bool requireWrite();
  bool loadStoredConfig();
  bool saveStoredConfig();
  bool validNetwork(const NetworkConfig& value) const;
  void connectWifi();
  void serviceWifi();
  void startSetupAp();
  void rollbackTrial();
  void registerRoutes();
  void sendAdminPage();
  void sendStatus();
  void handleConfig();
  void handleConfirm();
  void handleScan();
  void handleUpdate();
  void handleUpload();
  void failUpload(const String& message);
  void reply(int code, const String& message);
  void scheduleRestart();
};
