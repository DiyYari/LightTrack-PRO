#include "storage.h"
#include "config.h"
#include <Preferences.h> // Use Preferences for WiFi/MQTT
#include <EEPROM.h>      // Use EEPROM for LED/Schedule (as originally designed)

// --- Preferences Object ---
Preferences preferences;
const char* PREFERENCES_NAMESPACE = "lighttrack"; // Namespace for Preferences

// --- Internal Variables Holding Current Settings ---

// Display Parameters (loaded from EEPROM, defaults from config.h)
static int updateInterval = DEFAULT_UPDATE_INTERVAL;
static float movingIntensity = DEFAULT_MOVING_INTENSITY;
static float stationaryIntensity = DEFAULT_STATIONARY_INTENSITY;
static int movingLength = DEFAULT_MOVING_LENGTH;
static int centerShift = DEFAULT_CENTER_SHIFT;
static int additionalLEDs = DEFAULT_ADDITIONAL_LEDS;
static CRGB baseColor = DEFAULT_BASE_COLOR;
// static float speedMultiplier = DEFAULT_SPEED_MULTIPLIER; // Marked as unused
static int ledOffDelay = DEFAULT_LED_OFF_DELAY;

// Time and Schedule Parameters (loaded from EEPROM, defaults from config.h)
static int startHour = DEFAULT_START_HOUR;
static int startMinute = DEFAULT_START_MINUTE;
static int endHour = DEFAULT_END_HOUR;
static int endMinute = DEFAULT_END_MINUTE;

// State Parameters (runtime state, not saved persistently by default)
static bool lightOn = true;             // Default to ON until schedule/MQTT overrides
static bool backgroundModeActive = false; // Default to OFF

// WiFi settings (loaded from Preferences)
static String current_wifi_ssid = "";
static String current_wifi_password = "";

// MQTT settings (loaded from Preferences)
static String current_mqtt_server = "";
static int current_mqtt_port = MQTT_PORT; // Default from config.h
static String current_mqtt_user = "";
static String current_mqtt_password = "";


// --- Initialization ---

void initStorage() {
  // Initialize EEPROM for LED/Schedule parameters
  // Make sure EEPROM_SIZE in config.h is large enough!
  if (!EEPROM.begin(EEPROM_SIZE)) {
      Serial.println("STORAGE: Failed to initialise EEPROM!");
      // Handle error? Maybe reset EEPROM?
  } else {
       Serial.println("STORAGE: EEPROM Initialized.");
       loadEepromSettings(); // Load LED/Schedule settings from EEPROM
  }

  // Initialize Preferences for WiFi/MQTT settings
  preferences.begin(PREFERENCES_NAMESPACE, false); // 'false' for read/write mode
  Serial.println("STORAGE: Preferences Initialized.");

  // Load WiFi settings from Preferences into current variables
  current_wifi_ssid = preferences.getString("wifi_ssid", "");
  current_wifi_password = preferences.getString("wifi_password", "");
  if(current_wifi_ssid.length() > 0) {
      Serial.println("STORAGE: Loaded WiFi settings from Preferences.");
  } else {
       Serial.println("STORAGE: No WiFi settings found in Preferences.");
  }

  // Load MQTT settings from Preferences into current variables
  current_mqtt_server = preferences.getString("mqtt_server", "");
  current_mqtt_port = preferences.getInt("mqtt_port", MQTT_PORT); // Use default if not found
  current_mqtt_user = preferences.getString("mqtt_user", "");
  current_mqtt_password = preferences.getString("mqtt_password", "");
   if(current_mqtt_server.length() > 0) {
      Serial.println("STORAGE: Loaded MQTT settings from Preferences.");
  } else {
      Serial.println("STORAGE: No MQTT settings found in Preferences.");
  }
}


// --- EEPROM Load/Save for LED/Schedule ---

// Load settings from EEPROM into static variables
void loadEepromSettings() {
  Serial.println("STORAGE: Loading settings from EEPROM...");
  int offset = 0;
  byte checkValue; // Read a check value first (optional but good practice)
  // EEPROM.get(offset, checkValue); offset += sizeof(checkValue);
  // if (checkValue != EEPROM_CHECK_VALUE) { Serial.println("EEPROM check value mismatch, using defaults."); return; }

  // Read values, ensuring types match 'put' operations
  EEPROM.get(offset, updateInterval); offset += sizeof(updateInterval);
  EEPROM.get(offset, ledOffDelay); offset += sizeof(ledOffDelay);
  EEPROM.get(offset, movingIntensity); offset += sizeof(movingIntensity);
  EEPROM.get(offset, stationaryIntensity); offset += sizeof(stationaryIntensity);
  EEPROM.get(offset, movingLength); offset += sizeof(movingLength);
  EEPROM.get(offset, centerShift); offset += sizeof(centerShift);
  EEPROM.get(offset, additionalLEDs); offset += sizeof(additionalLEDs); // Assuming 'int' was used in put
  EEPROM.get(offset, baseColor); offset += sizeof(baseColor);
  // offset += sizeof(speedMultiplier); // Skip loading speedMultiplier as it wasn't saved
  EEPROM.get(offset, startHour); offset += sizeof(startHour);
  EEPROM.get(offset, startMinute); offset += sizeof(startMinute);
  EEPROM.get(offset, endHour); offset += sizeof(endHour);
  EEPROM.get(offset, endMinute); offset += sizeof(endMinute);

  Serial.println("STORAGE: EEPROM Settings loaded.");
  // Optional: Print loaded values for debugging
  // Serial.printf(" - Update Interval: %d\n", updateInterval);
  // ... etc ...
}

// Save current static variables to EEPROM
void saveEepromSettings() {
  Serial.println("STORAGE: Saving settings to EEPROM...");
  int offset = 0;
  // byte checkValue = EEPROM_CHECK_VALUE; // Optional check value
  // EEPROM.put(offset, checkValue); offset += sizeof(checkValue);

  EEPROM.put(offset, updateInterval); offset += sizeof(updateInterval);
  EEPROM.put(offset, ledOffDelay); offset += sizeof(ledOffDelay);
  EEPROM.put(offset, movingIntensity); offset += sizeof(movingIntensity);
  EEPROM.put(offset, stationaryIntensity); offset += sizeof(stationaryIntensity);
  EEPROM.put(offset, movingLength); offset += sizeof(movingLength);
  EEPROM.put(offset, centerShift); offset += sizeof(centerShift);
  EEPROM.put(offset, additionalLEDs); offset += sizeof(additionalLEDs); // Save as int
  EEPROM.put(offset, baseColor); offset += sizeof(baseColor);
  // offset += sizeof(speedMultiplier); // Skip saving speedMultiplier - it seems unused
  EEPROM.put(offset, startHour); offset += sizeof(startHour);
  EEPROM.put(offset, startMinute); offset += sizeof(startMinute);
  EEPROM.put(offset, endHour); offset += sizeof(endHour);
  EEPROM.put(offset, endMinute); offset += sizeof(endMinute);

  if (EEPROM.commit()) {
    Serial.println("STORAGE: EEPROM commit successful.");
  } else {
    Serial.println("STORAGE: ERROR: EEPROM commit failed!");
  }
}


// --- Getters (Return current values from static variables) ---

// Display Parameters
int getUpdateInterval() { return updateInterval; }
int getLedOffDelay() { return ledOffDelay; }
float getMovingIntensity() { return movingIntensity; }
float getStationaryIntensity() { return stationaryIntensity; }
int getMovingLength() { return movingLength; }
int getCenterShift() { return centerShift; }
int getAdditionalLEDs() { return additionalLEDs; }
CRGB getBaseColor() { return baseColor; }
// float getSpeedMultiplier() { return speedMultiplier; } // Unused
int getStartHour() { return startHour; }
int getStartMinute() { return startMinute; }
int getEndHour() { return endHour; }
int getEndMinute() { return endMinute; }

// State Parameters
bool isLightOn() { return lightOn; }
bool isBackgroundModeActive() { return backgroundModeActive; }


// --- Setters (Update static variables AND save EEPROM settings immediately) ---
// Warning: Frequent saves wear out EEPROM. Consider a less frequent save strategy.

// Display Parameters
void setUpdateInterval(int value) {
  if (value == updateInterval) return;
  updateInterval = constrain(value, 5, 100); // Add constraints if appropriate
  saveEepromSettings();
}

void setLedOffDelay(int value) {
   if (value == ledOffDelay) return;
   ledOffDelay = constrain(value, 1, 60);
   saveEepromSettings();
}

void setMovingIntensity(float value) {
  if (value == movingIntensity) return;
  movingIntensity = constrain(value, 0.0f, 1.0f);
  saveEepromSettings();
}

void setStationaryIntensity(float value) {
  // Clean up very small values, constrain to reasonable max
  value = constrain(value, 0.0f, 0.1f); // Max 10% brightness for background? Adjust as needed.
  if (abs(value - stationaryIntensity) < 0.0001f) return; // Avoid saving tiny float changes
  stationaryIntensity = value;
  saveEepromSettings();
}

void setMovingLength(int value) {
  if (value == movingLength) return;
  movingLength = constrain(value, 1, NUM_LEDS); // Use NUM_LEDS from config.h
  saveEepromSettings();
}

void setCenterShift(int value) {
  if (value == centerShift) return;
  centerShift = constrain(value, -(NUM_LEDS / 2), (NUM_LEDS / 2));
  saveEepromSettings();
}

void setAdditionalLEDs(int value) {
  if (value == additionalLEDs) return;
  additionalLEDs = constrain(value, 0, NUM_LEDS / 2);
  saveEepromSettings();
}

void setBaseColor(CRGB color) {
  // Use memcmp for CRGB comparison
  if (memcmp(&color, &baseColor, sizeof(CRGB)) == 0) return;
  baseColor = color;
  saveEepromSettings();
}

// void setSpeedMultiplier(float value) { speedMultiplier = value; saveEepromSettings(); } // Unused

// Schedule Parameters
void setStartHour(int value) {
  if (value == startHour) return;
  startHour = constrain(value, 0, 23);
  saveEepromSettings();
}

void setStartMinute(int value) {
  if (value == startMinute) return;
  startMinute = constrain(value, 0, 59);
  saveEepromSettings();
}

void setEndHour(int value) {
   if (value == endHour) return;
   endHour = constrain(value, 0, 23);
   saveEepromSettings();
}

void setEndMinute(int value) {
   if (value == endMinute) return;
   endMinute = constrain(value, 0, 59);
   saveEepromSettings();
}

// State Parameters (Do not save state persistently here)
void setLightOn(bool value) {
  lightOn = value;
}

void setBackgroundModeActive(bool value) {
  backgroundModeActive = value;
}

void toggleBackgroundMode() {
  backgroundModeActive = !backgroundModeActive;
  // Note: State is not saved automatically. If persistence is needed, call a save function.
}


// --- WiFi Settings (Uses Preferences) ---

void saveWiFiSettings(const char* ssid, const char* password) {
  current_wifi_ssid = String(ssid);
  current_wifi_password = String(password);

  // Save to Preferences
  preferences.putString("wifi_ssid", current_wifi_ssid);
  preferences.putString("wifi_password", current_wifi_password);
  Serial.println("STORAGE: WiFi settings saved to Preferences.");
}

// Get currently loaded SSID
String getWiFiSsid() {
  return current_wifi_ssid;
}

// Get currently loaded Password
String getWiFiPassword() {
  return current_wifi_password;
}

// Get stored SSID (used at init)
String getStoredWiFiSsid() {
     return preferences.getString("wifi_ssid", "");
}

// Get stored Password (used at init)
String getStoredWiFiPassword() {
     return preferences.getString("wifi_password", "");
}

// Check if SSID exists in Preferences
bool hasWiFiSettings() {
  return preferences.isKey("wifi_ssid") && preferences.getString("wifi_ssid", "").length() > 0;
}


// --- MQTT Settings (Uses Preferences) ---

void saveMqttSettings(const char* server, int port, const char* user, const char* password) {
  current_mqtt_server = String(server);
  current_mqtt_port = port;
  current_mqtt_user = String(user);
  current_mqtt_password = String(password);

  // Save to Preferences
  preferences.putString("mqtt_server", current_mqtt_server);
  preferences.putInt("mqtt_port", current_mqtt_port);
  preferences.putString("mqtt_user", current_mqtt_user);
  preferences.putString("mqtt_password", current_mqtt_password);
  Serial.println("STORAGE: MQTT settings saved to Preferences.");
}

String getMqttServer() {
  return current_mqtt_server;
}

int getMqttPort() {
  // Return the current value, which defaults to MQTT_PORT or loaded value
  return current_mqtt_port;
}

String getMqttUser() {
  return current_mqtt_user;
}

String getMqttPassword() {
  return current_mqtt_password;
}

// Check if MQTT Server setting exists and is not empty in Preferences
bool hasMqttSettings() {
  return preferences.isKey("mqtt_server") && preferences.getString("mqtt_server", "").length() > 0;
}