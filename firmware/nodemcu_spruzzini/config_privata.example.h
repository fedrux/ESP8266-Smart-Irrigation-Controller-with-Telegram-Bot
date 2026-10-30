#pragma once
// Copy to config_privata.h and replace every example value before installation.
// This file is public; config_privata.h and compiled images must remain private.
const char* ssid = "YOUR_PRIMARY_WIFI";
const char* password = "YOUR_PRIMARY_WIFI_PASSWORD";
const char* botToken = "YOUR_TELEGRAM_BOT_TOKEN";
const char* openWeatherMapApiKey = "YOUR_OPENWEATHERMAP_API_KEY";
const char* city = "YOUR_CITY,it";

const char* WIFI_SECONDARY_SSID = "YOUR_SECONDARY_WIFI";
const char* WIFI_SECONDARY_PASSWORD = "YOUR_SECONDARY_WIFI_PASSWORD";
const char* ADMIN_USER = "YOUR_ADMIN_USERNAME";
const char* ADMIN_PASSWORD = "CHANGE_THIS_ADMIN_PASSWORD";
const char* SETUP_AP_PASSWORD = "CHANGE_THIS_RECOVERY_PASSWORD";

const int NUM_CHAT_AUTORIZZATE = 2;
const String chatConsentite[NUM_CHAT_AUTORIZZATE] = {
  "YOUR_TELEGRAM_CHAT_ID_1", "YOUR_TELEGRAM_CHAT_ID_2"
};
String chatAttiva = chatConsentite[0];

// Public defaults use DHCP. Replace these EXAMPLE addresses only if using static IP.
const bool USE_DHCP = true;
const uint8_t STATIC_IP[4] = {192, 168, 1, 100};
const uint8_t GATEWAY[4] = {192, 168, 1, 1};
const uint8_t SUBNET[4] = {255, 255, 255, 0};
const uint8_t DNS_SERVER[4] = {192, 168, 1, 1};
