# 🌱 ESP8266 Smart Irrigation Controller with Telegram Bot  

An open-source **smart irrigation system** based on **ESP8266 NodeMCU**, allowing you to manage up to **5 irrigation zones** with automatic scheduling, manual button trigger, and full **Telegram Bot** remote control.  
The system integrates with **OpenWeatherMap** for **smart weather-based irrigation skipping**, helping save water and protect your plants.  

---

## 🚀 Key Features  

- ✅ Automatic irrigation at a scheduled time  
- ✅ Manual control via Telegram commands  
- ✅ Single-zone activation with custom duration  
- ✅ Physical push button to start a full cycle  
- ✅ Status LED with 0.5s heartbeat blinking  
- ✅ **Smart Weather Mode**: skip irrigation if it rained in the last *X hours*  
- ✅ EEPROM storage for all user settings  
- ✅ Daily protection to avoid repeated cycles  

---

## 📡 Available Telegram Commands  

- `/start` → Welcome message + command list  
- `/settings` → Guided setup (time, duration, auto start, weather smart mode)  
- `/cycle` → Start a full irrigation cycle (all zones in sequence)  
- `/zone` → Activate a single irrigation zone with custom duration  
- `/stop` → Immediately stop ongoing irrigation  
- `/status` → Display current configuration and system status  
- `/reset` → Reset all settings to defaults  
- `/weather` → Fetch weather forecast (JSON from OpenWeatherMap)  

---

## 🛠️ Required Hardware  

- ESP8266 NodeMCU board  
- Up to 5 relays (one per irrigation valve)  
- Push button (connected to dedicated pin)  
- Status LED (connected to dedicated pin)  
- Power supply (5V or 12V depending on solenoid valves)  
- (Optional) additional sensors (e.g., rain, soil moisture)  

---

## ⚡ Wiring Overview  

- **Relay zone pins** → { D0, D1, D2, D3, D4 } → Control solenoid valves  
- **Push button** → D5 → Manual start of irrigation cycle  
- **Status LED** → D6 → Blinks every 0.5s  

---

## 🌍 Smart Weather Feature  

The system connects to **OpenWeatherMap API** to check:  
- Rain forecast at scheduled start time  
- Rain that occurred in the last *X hours* (user configurable)  

If rain is detected → **Irrigation cycle is skipped** and marked as executed for that day.  

---

