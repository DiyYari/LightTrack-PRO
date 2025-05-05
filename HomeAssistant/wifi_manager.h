#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

// Setup WiFi
void setupWiFi();

// Get the device name (SSID)
String getDeviceName();

void handleWiFiSettings();
void handleWiFiSave();

#endif // WIFI_MANAGER_H