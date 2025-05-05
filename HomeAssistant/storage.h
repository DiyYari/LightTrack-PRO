#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <FastLED.h> // Needed for CRGB type

// --- Initialization ---

// Initialize storage (EEPROM and Preferences)
void initStorage();


// --- Settings Loading/Saving (EEPROM part - primarily for LED/Schedule params) ---

// Load settings currently stored in EEPROM (called by initStorage)
void loadEepromSettings();

// Save settings currently stored in EEPROM
// WARNING: Called frequently by setters, consider using saveAllSettings less often.
void saveEepromSettings();

// Aliases for saving specific groups (they both currently save all EEPROM settings)
// These are placeholders; could be implemented to save only specific parts if needed.
#define saveLedParameters saveEepromSettings
#define saveScheduleSettings saveEepromSettings


// --- Parameter Getters (Retrieve current values) ---

// Display Parameters
int getUpdateInterval();
int getLedOffDelay();
float getMovingIntensity();
float getStationaryIntensity(); // Background light intensity
int getMovingLength();
int getCenterShift();
int getAdditionalLEDs(); // Spread/trail LEDs
CRGB getBaseColor();
float getSpeedMultiplier(); // Currently unused? Not saved/loaded.

// Schedule Parameters
int getStartHour();
int getStartMinute();
int getEndHour();
int getEndMinute();

// State Parameters
bool isLightOn();           // Current calculated/set light state (ON/OFF)
bool isBackgroundModeActive(); // Is background light mode enabled?


// --- Parameter Setters (Update current values and save) ---
// Note: Most setters immediately save to EEPROM.

// Display Parameters
void setUpdateInterval(int value);
void setLedOffDelay(int value);
void setMovingIntensity(float value);
void setStationaryIntensity(float value);
void setMovingLength(int value);
void setCenterShift(int value);
void setAdditionalLEDs(int value);
void setBaseColor(CRGB color);
void setSpeedMultiplier(float value); // Currently unused? Not saved/loaded.

// Schedule Parameters
void setStartHour(int value);
void setStartMinute(int value);
void setEndHour(int value);
void setEndMinute(int value);

// State Parameters
void setLightOn(bool value); // Note: Doesn't save state to EEPROM/Prefs
void setBackgroundModeActive(bool value); // Note: Doesn't save state
void toggleBackgroundMode(); // Toggles state, doesn't save


// --- WiFi Settings (Uses Preferences) ---

// Save WiFi credentials to Preferences
void saveWiFiSettings(const char* ssid, const char* password);

// Get currently loaded/set WiFi SSID
String getWiFiSsid(); // Renamed from getWiFiSSID for consistency

// Get currently loaded/set WiFi Password
String getWiFiPassword();

// Get WiFi SSID stored in Preferences (Used during init)
String getStoredWiFiSsid();

// Get WiFi Password stored in Preferences (Used during init)
String getStoredWiFiPassword();

// Check if SSID is stored in Preferences
bool hasWiFiSettings();


// --- MQTT Settings (Uses Preferences) ---

// Save MQTT connection details to Preferences
void saveMqttSettings(const char* server, int port, const char* user, const char* password);

// Get currently loaded/set MQTT Server address
String getMqttServer();

// Get currently loaded/set MQTT Port
int getMqttPort();

// Get currently loaded/set MQTT Username
String getMqttUser();

// Get currently loaded/set MQTT Password
String getMqttPassword();

// Check if MQTT server address is stored in Preferences
bool hasMqttSettings();

#endif // STORAGE_H