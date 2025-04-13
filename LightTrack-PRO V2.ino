#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FastLED.h>
#include <EEPROM.h>
#include <SPIFFS.h>
#include <math.h>
#include "esp_wifi.h"
#include <time.h> // Used for time management
#include <ArduinoOTA.h>
#include <stdlib.h>

// ------------------------- LED Configuration -------------------------
#define LED_PIN             2
#define NUM_LEDS            300
#define CHIPSET             WS2812B
#define COLOR_ORDER         GRB
CRGB leds[NUM_LEDS];

// ------------------------- Sensor Parameters -------------------------
#define SENSOR_HEADER       0xAA
#define MIN_DISTANCE        20
#define MAX_DISTANCE        1000
#define DEFAULT_DISTANCE    1000
#define NOISE_THRESHOLD     5

// ------------------------- Display Parameters -------------------------
int updateInterval = 20;
float movingIntensity = 0.3;      // Stored as 0.0-1.0
float stationaryIntensity = 0.03; // Stored as 0.0-0.1 (0-10%)
int movingLength = 33;
int centerShift = 0;
int additionalLEDs = 0;
CRGB baseColor = CRGB(255, 200, 50);
int ledOffDelay = 5;
int gradientSoftness = 7; // Gradient configuration

// Global sensor distance
volatile unsigned int g_sensorDistance = DEFAULT_DISTANCE;

// Background Light Mode
volatile bool backgroundModeActive = false;

// ------------------------- Time and Schedule Parameters (Local Time Logic) -------------------------
int startHour   = 20; // Start time (local)
int startMinute = 0;
int endHour     = 8;  // End time (local)
int endMinute   = 30;
bool lightOn    = true; // Current light state (based on schedule or manual override)
unsigned long lastTimeCheck = 0; // For periodic time checks
volatile bool smarthomeOverride = false; // Flag for manual control

// Variables for local time
volatile int clientTimezoneOffsetMinutes = 0; // Client offset from UTC in minutes (East +, West -)
volatile bool isTimeOffsetSet = false;       // Flag indicating offset has been set
// -------------------------------------------------

// ------------------------- EEPROM -------------------------
#define EEPROM_SIZE 132 // was 128, +4 bytes for int offset

// Load settings from EEPROM
void loadSettings() {
  Serial.println("Loading settings from EEPROM...");
  EEPROM.begin(EEPROM_SIZE);
  int offset = 0;

  EEPROM.get(offset, reinterpret_cast<int&>(updateInterval)); offset += sizeof(updateInterval);
  EEPROM.get(offset, reinterpret_cast<int&>(ledOffDelay)); offset += sizeof(ledOffDelay);
  EEPROM.get(offset, reinterpret_cast<float&>(movingIntensity)); offset += sizeof(movingIntensity);
  EEPROM.get(offset, reinterpret_cast<float&>(stationaryIntensity)); offset += sizeof(stationaryIntensity);
  EEPROM.get(offset, reinterpret_cast<int&>(movingLength)); offset += sizeof(movingLength);
  EEPROM.get(offset, reinterpret_cast<int&>(centerShift)); offset += sizeof(centerShift);
  {
    int temp = additionalLEDs;
    EEPROM.get(offset, temp);
    additionalLEDs = temp;
    offset += sizeof(temp);
  }
  EEPROM.get(offset, baseColor); offset += sizeof(baseColor);
  offset += sizeof(float); // Skip speedMultiplier placeholder
  EEPROM.get(offset, startHour); offset += sizeof(startHour);
  EEPROM.get(offset, startMinute); offset += sizeof(startMinute);
  EEPROM.get(offset, endHour); offset += sizeof(endHour);
  EEPROM.get(offset, endMinute); offset += sizeof(endMinute);
  EEPROM.get(offset, gradientSoftness); offset += sizeof(gradientSoftness);
  {
    int temp_tz = 0;
    EEPROM.get(offset, temp_tz);
    if (temp_tz >= -720 && temp_tz <= 840) {
        clientTimezoneOffsetMinutes = temp_tz;
        isTimeOffsetSet = true;
    } else {
        clientTimezoneOffsetMinutes = 0;
        isTimeOffsetSet = false;
    }
    offset += sizeof(temp_tz);
  }
  EEPROM.end();

  // Validate loaded values
  if (updateInterval < 10 || updateInterval > 200) updateInterval = 20;
  if (ledOffDelay < 1 || ledOffDelay > 60) ledOffDelay = 5;
  if (movingIntensity < 0.0 || movingIntensity > 1.0) movingIntensity = 0.3;
  if (stationaryIntensity < 0.0 || stationaryIntensity > 0.1) stationaryIntensity = 0.03; // Max 10%
  if (movingLength < 1 || movingLength > NUM_LEDS) movingLength = 33;
  if (abs(centerShift) > NUM_LEDS/2) centerShift = 0;
  if (additionalLEDs < 0 || additionalLEDs > NUM_LEDS/2) additionalLEDs = 0;
  gradientSoftness = constrain(gradientSoftness, 0, 10);
  startHour = constrain(startHour, 0, 23); startMinute = constrain(startMinute, 0, 59);
  endHour = constrain(endHour, 0, 23); endMinute = constrain(endMinute, 0, 59);

  Serial.println("Settings loaded and validated:");
  Serial.print("Update interval: "); Serial.println(updateInterval);
  Serial.print("LED off delay: "); Serial.println(ledOffDelay);
  Serial.print("Moving intensity: "); Serial.print(movingIntensity * 100.0, 0); Serial.println("%");
  Serial.print("Stationary intensity: "); Serial.print(stationaryIntensity * 100.0, 1); Serial.println("%");
  Serial.print("Moving length: "); Serial.println(movingLength);
  Serial.print("Center shift: "); Serial.println(centerShift);
  Serial.print("Additional LEDs: "); Serial.println(additionalLEDs);
  Serial.print("Gradient Softness: "); Serial.println(gradientSoftness);
  Serial.print("Base color RGB: "); Serial.print(baseColor.r); Serial.print(", ");
  Serial.print(baseColor.g); Serial.print(", "); Serial.println(baseColor.b);
  Serial.printf("Schedule: %02d:%02d - %02d:%02d (Local Time)\n", startHour, startMinute, endHour, endMinute);
  Serial.print("Client Timezone Offset: "); Serial.print(clientTimezoneOffsetMinutes); Serial.print(" minutes from UTC");
  if (!isTimeOffsetSet) Serial.print(" (Default/Not Set)");
  Serial.println();
}

// Save settings to EEPROM
void saveSettings() {
  Serial.println("Saving settings to EEPROM...");
  EEPROM.begin(EEPROM_SIZE);
  int offset = 0;

  EEPROM.put(offset, updateInterval); offset += sizeof(updateInterval);
  EEPROM.put(offset, ledOffDelay); offset += sizeof(ledOffDelay);
  EEPROM.put(offset, movingIntensity); offset += sizeof(movingIntensity);
  EEPROM.put(offset, stationaryIntensity); offset += sizeof(stationaryIntensity);
  EEPROM.put(offset, movingLength); offset += sizeof(movingLength);
  EEPROM.put(offset, centerShift); offset += sizeof(centerShift);
  {
    int temp = additionalLEDs;
    EEPROM.put(offset, temp); offset += sizeof(temp);
  }
  EEPROM.put(offset, baseColor); offset += sizeof(baseColor);
  offset += sizeof(float); // Skip speedMultiplier placeholder
  EEPROM.put(offset, startHour); offset += sizeof(startHour);
  EEPROM.put(offset, startMinute); offset += sizeof(startMinute);
  EEPROM.put(offset, endHour); offset += sizeof(endHour);
  EEPROM.put(offset, endMinute); offset += sizeof(endMinute);
  EEPROM.put(offset, gradientSoftness); offset += sizeof(gradientSoftness);
  {
    int temp_tz = clientTimezoneOffsetMinutes;
    EEPROM.put(offset, temp_tz);
    offset += sizeof(temp_tz);
  }

  boolean result = EEPROM.commit();
  EEPROM.end();

  Serial.print("Settings saved to EEPROM: ");
  Serial.println(result ? "OK" : "FAILED");
}

// ------------------------- Web Server -------------------------
WebServer server(80);

// HTTP handler prototypes
void handleRoot();
void handleSetInterval();
void handleSetLedOffDelay();
void handleSetBaseColor();
void handleSetMovingIntensity();
void handleSetStationaryIntensity();
void handleSetMovingLength();
void handleSetAdditionalLEDs();
void handleSetCenterShift();
void handleSetGradientSoftness();
void handleSetTime();
void handleSetSchedule();
void handleNotFound();
void handleSmartHomeOn();
void handleSmartHomeOff();
void handleSmartHomeClear();
void handleToggleBackgroundMode();
void handleGetCurrentTime(); // NEW: Handler for getting current time
void updateTime();

// OTA setup prototype
void setupOTA();

// ------------------------- Smart Home Integration Endpoints -------------------------
void handleSmartHomeOn() {
  lightOn = true;
  smarthomeOverride = true;
  server.send(200, "text/plain", "Smart Home Override: ON");
}
void handleSmartHomeOff() {
  lightOn = false;
  smarthomeOverride = true;
  server.send(200, "text/plain", "Smart Home Override: OFF");
}

// Modified to use local time for recalculation
void handleSmartHomeClear() {
  smarthomeOverride = false;
  Serial.println("Smart Home Override Cleared.");
  // Immediately check schedule after override is cleared, USING LOCAL TIME
  time_t nowUtc = time(nullptr);
  if (nowUtc < 1000000000UL || !isTimeOffsetSet) { // If time/offset not set
    Serial.println("Time or TZ Offset not set. Defaulting light ON.");
    lightOn = true;
  } else {
    // Calculate client's local time
    time_t clientLocalEpoch = nowUtc + (clientTimezoneOffsetMinutes * 60);
    struct tm timeinfo_local;
    gmtime_r(&clientLocalEpoch, &timeinfo_local); // Use gmtime_r to convert calculated local epoch

    int currentTotalMinutes = timeinfo_local.tm_hour * 60 + timeinfo_local.tm_min;
    int startTotalMinutes = startHour * 60 + startMinute;
    int endTotalMinutes = endHour * 60 + endMinute;

    // Schedule checking logic (same, but with local time)
    if (startTotalMinutes <= endTotalMinutes)
      lightOn = (currentTotalMinutes >= startTotalMinutes && currentTotalMinutes < endTotalMinutes);
    else // Schedule crosses midnight
      lightOn = (currentTotalMinutes >= startTotalMinutes || currentTotalMinutes < endTotalMinutes);

    Serial.print("Schedule re-evaluated using local time. Light should be: ");
    Serial.println(lightOn ? "ON" : "OFF");
    Serial.printf(" (Based on Est. Local Time: %02d:%02d)\n", timeinfo_local.tm_hour, timeinfo_local.tm_min);
  }
  server.send(200, "text/plain", "Smart Home Override: CLEARED");
}


// ------------------------- Mode Handlers -------------------------
void handleToggleBackgroundMode() {
  backgroundModeActive = !backgroundModeActive;
  Serial.print("Background mode toggled: "); Serial.println(backgroundModeActive ? "ON" : "OFF");
  server.sendHeader("Location", "/");
  server.send(303);
}

// ------------------------- Sensor Reading Function -------------------------
unsigned int readSensorData() {
#ifndef SIMULATE_SENSOR
  if (Serial1.available() < 7) return g_sensorDistance;
  if (Serial1.read() != SENSOR_HEADER) {
    while (Serial1.available()) Serial1.read();
    return g_sensorDistance;
  }
  if (Serial1.read() != SENSOR_HEADER) {
    while (Serial1.available()) Serial1.read();
    return g_sensorDistance;
  }
  byte buf[5];
  size_t bytesRead = Serial1.readBytes(buf, 5);
  if (bytesRead < 5) {
      while (Serial1.available()) Serial1.read();
      return g_sensorDistance;
  }
  unsigned int distance = (buf[2] << 8) | buf[1];
  if (distance < MIN_DISTANCE || distance > MAX_DISTANCE) return g_sensorDistance;
  return distance;
#else
  static unsigned int simulatedDistance = MIN_DISTANCE;
  simulatedDistance += 10;
  if (simulatedDistance > MAX_DISTANCE) simulatedDistance = MIN_DISTANCE;
  return simulatedDistance;
#endif
}

// ------------------------- RTOS Tasks -------------------------
void sensorTask(void * parameter) {
  Serial.println("Sensor Task started");
  for (;;) {
    unsigned int newDistance = readSensorData();
    g_sensorDistance = newDistance;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void ledTask(void * parameter) {
  static unsigned int lastSensor = g_sensorDistance;
  static int lastMovementDirection = 0;
  static unsigned long lastMovementTime = millis();

  FastLED.clear();
  FastLED.show();
  vTaskDelay(pdMS_TO_TICKS(1000)); // Initial delay

  Serial.println("LED Task initialized and starting main loop");

  for (;;) {
    unsigned long currentMillis = millis();
    unsigned int currentDistance = g_sensorDistance;
    int diff = (int)currentDistance - (int)lastSensor;
    int absDiff = abs(diff);

    // Movement detection
    if (absDiff >= NOISE_THRESHOLD) {
        if (currentMillis - lastMovementTime > 50 || (diff > 0 && lastMovementDirection < 0) || (diff < 0 && lastMovementDirection > 0)) {
           lastMovementTime = currentMillis;
           lastMovementDirection = (diff > 0) ? 1 : -1;
        }
    }
    lastSensor = currentDistance;

    bool drawMovingPart = (currentMillis - lastMovementTime <= ledOffDelay * 1000);

    // --- Background Fill ---
    if (!lightOn) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
    } else if (backgroundModeActive) {
        // Use stationaryIntensity (0.0 to 0.1)
        uint8_t r = max((uint8_t)1, (uint8_t)(baseColor.r * stationaryIntensity));
        uint8_t g = max((uint8_t)1, (uint8_t)(baseColor.g * stationaryIntensity));
        uint8_t b = max((uint8_t)1, (uint8_t)(baseColor.b * stationaryIntensity));
        fill_solid(leds, NUM_LEDS, CRGB(r, g, b));
    } else {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
    }

    // --- Moving Beam Drawing ---
    if (lightOn && drawMovingPart) {
        float prop = constrain((float)(currentDistance - MIN_DISTANCE) / (MAX_DISTANCE - MIN_DISTANCE), 0.0, 1.0);
        int ledPosition = round(prop * (NUM_LEDS - 1));
        int centerLED = constrain(ledPosition + centerShift, 0, NUM_LEDS - 1);

        // Use movingIntensity (0.0 to 1.0)
        CRGB fullBrightColor = CRGB((uint8_t)(baseColor.r * movingIntensity),
                                   (uint8_t)(baseColor.g * movingIntensity),
                                   (uint8_t)(baseColor.b * movingIntensity));

        int direction = lastMovementDirection;
        if (direction == 0) direction = 1; // Default direction if no movement detected yet

        int halfMainLength = movingLength / 2;
        int totalLightLength = movingLength + additionalLEDs;
        if (totalLightLength <= 0) totalLightLength = 1;

        int leftEdge, rightEdge;
        if (direction > 0) { // Moving away
            leftEdge = centerLED - halfMainLength;
            rightEdge = leftEdge + movingLength - 1 + additionalLEDs;
        } else { // Moving towards
            rightEdge = centerLED + halfMainLength;
            leftEdge = rightEdge - movingLength + 1 - additionalLEDs;
        }

        // Clamp edges to valid LED indices
        leftEdge = max(0, leftEdge);
        rightEdge = min(NUM_LEDS - 1, rightEdge);

        // Calculate effective gradient parameters using gradientSoftness
        int effectiveFadeWidth = map(gradientSoftness, 0, 10, 1, 10);
        float effectiveFadeExponent = 1.0 + (gradientSoftness / 10.0) * 2.0;

        int actualBeamPixelLength = rightEdge - leftEdge + 1;
        effectiveFadeWidth = constrain(effectiveFadeWidth, 1, max(1, actualBeamPixelLength / 2));

        // Draw the beam with gradient
        for (int i = leftEdge; i <= rightEdge; i++) {
            int posInBeam;
            if (direction > 0) {
                posInBeam = i - leftEdge;
            } else {
                posInBeam = rightEdge - i;
            }

            float factor = 1.0f;

            // Apply fade if needed
            if (gradientSoftness > 0 && totalLightLength > 1) {
                if (posInBeam < effectiveFadeWidth) {
                    float normalizedPos = (effectiveFadeWidth > 0) ? (float)(posInBeam + 1) / (effectiveFadeWidth + 1) : 1.0f;
                    factor = pow(normalizedPos, effectiveFadeExponent);
                }
                else if (posInBeam >= (totalLightLength - effectiveFadeWidth)) {
                    int posFromEnd = totalLightLength - 1 - posInBeam;
                    float normalizedPos = (effectiveFadeWidth > 0) ? (float)(posFromEnd + 1) / (effectiveFadeWidth + 1) : 1.0f;
                    factor = pow(normalizedPos, effectiveFadeExponent);
                }
            }

            factor = constrain(factor, 0.0f, 1.0f);

            // Set LED color if factor is significant
            if (factor > 0.01f) {
                CRGB beamColor;
                beamColor.r = (uint8_t)(fullBrightColor.r * factor);
                beamColor.g = (uint8_t)(fullBrightColor.g * factor);
                beamColor.b = (uint8_t)(fullBrightColor.b * factor);

                // Blend with background if background mode is active
                if (backgroundModeActive) {
                    uint8_t bgR = max((uint8_t)1, (uint8_t)(baseColor.r * stationaryIntensity));
                    uint8_t bgG = max((uint8_t)1, (uint8_t)(baseColor.g * stationaryIntensity));
                    uint8_t bgB = max((uint8_t)1, (uint8_t)(baseColor.b * stationaryIntensity));

                    leds[i].r = max(beamColor.r, leds[i].r);
                    leds[i].g = max(beamColor.g, leds[i].g);
                    leds[i].b = max(beamColor.b, leds[i].b);
                    leds[i].r = max(leds[i].r, bgR);
                    leds[i].g = max(leds[i].g, bgG);
                    leds[i].b = max(leds[i].b, bgB);
                } else {
                    leds[i] = beamColor; // Just set the beam color
                }
            }
        } // End of pixel loop
    } // End of drawMovingPart

    FastLED.show();
    vTaskDelay(pdMS_TO_TICKS(updateInterval));
  } // End of infinite loop
}

void webServerTask(void * parameter) {
  Serial.println("Web Server Task started");
  for (;;) {
    server.handleClient();
    ArduinoOTA.handle(); // Handle OTA updates here
    vTaskDelay(pdMS_TO_TICKS(2)); // Small delay for web server handling
  }
}

// ------------------------- Time Functions (Local Time Logic) -------------------------

// Modified: Sets UTC time AND saves client TZ offset
void handleSetTime() {
  bool timeUpdated = false;
  bool tzUpdated = false;

  // Handling Timezone (TZ) offset
  if (server.hasArg("tz")) {
    int tz_offset_minutes = server.arg("tz").toInt();
    // Check validity and save if changed
    if (tz_offset_minutes >= -720 && tz_offset_minutes <= 840) { // -12:00 to +14:00
        if (!isTimeOffsetSet || clientTimezoneOffsetMinutes != tz_offset_minutes) {
            clientTimezoneOffsetMinutes = tz_offset_minutes;
            isTimeOffsetSet = true;
            tzUpdated = true;
            Serial.print("Client timezone offset received and set to (minutes): ");
            Serial.println(clientTimezoneOffsetMinutes);
            saveSettings(); // Save settings, including new TZ
        }
    } else {
        Serial.print("Received invalid timezone offset (minutes): ");
        Serial.println(tz_offset_minutes);
    }
  }

  // Handling time setting (Epoch UTC)
  if (server.hasArg("epoch")) {
    unsigned long epoch = strtoul(server.arg("epoch").c_str(), NULL, 10);
    Serial.print("Received Epoch: "); Serial.println(epoch);

    if (epoch > 946684800UL) { // Check Epoch validity (after 1 Jan 2000)
      struct timeval tv;
      tv.tv_sec = epoch; // Set UTC time
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
      Serial.print("System time set via settimeofday() to (UTC): "); Serial.println(epoch);
      timeUpdated = true;
    } else {
       Serial.print("Received invalid epoch value: "); Serial.println(epoch);
    }
  }

  // After setting time or TZ, immediately check schedule
  if (timeUpdated || tzUpdated) {
    updateTime(); // Call for immediate reaction to time/TZ change
  }

  // Form response
  String response = "";
  if(timeUpdated) response += "Time OK";
  if(tzUpdated) response += (response.length() > 0 ? ", TZ OK" : "TZ OK");
  if(response.length() == 0) response = "No change";

  server.send(200, "text/plain", response);
}

// Modified: Uses saved TZ offset to calculate local time
void updateTime() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastTimeCheck >= 1000) {
    lastTimeCheck = currentMillis;
    time_t nowUtc = time(nullptr); // Get current UTC time

    // Periodically check if time has been set
    static bool lastTimeValid = false;
    if (nowUtc > 1000000000UL && !lastTimeValid) {
      lastTimeValid = true;
      Serial.println("NTP time received successfully!");
    }

    // Check that time is synchronized and offset is set
    if (nowUtc < 1000000000UL || !isTimeOffsetSet) {
        // Default ON if time/TZ not set, but only if not overridden
        if (!smarthomeOverride) {
            lightOn = true;
        }
        return; // Exit if we cannot check schedule
    }

    // Calculate client's local time
    time_t clientLocalEpoch = nowUtc + (clientTimezoneOffsetMinutes * 60);
    struct tm timeinfo_local;
    gmtime_r(&clientLocalEpoch, &timeinfo_local); // Convert local epoch into tm structure

    // Compare with schedule (using local hours/minutes)
    int currentTotalMinutes = timeinfo_local.tm_hour * 60 + timeinfo_local.tm_min;
    int startTotalMinutes = startHour * 60 + startMinute;
    int endTotalMinutes = endHour * 60 + endMinute;
    bool shouldBeOn;

    if (startTotalMinutes <= endTotalMinutes) { // Schedule within the same day
      shouldBeOn = (currentTotalMinutes >= startTotalMinutes && currentTotalMinutes < endTotalMinutes);
    } else { // Schedule crosses midnight
      shouldBeOn = (currentTotalMinutes >= startTotalMinutes || currentTotalMinutes < endTotalMinutes);
    }

    // Update light state if schedule changes it AND no manual override
    if (!smarthomeOverride && (lightOn != shouldBeOn)) {
        lightOn = shouldBeOn;
        Serial.print("Schedule updated light state to: "); Serial.println(lightOn ? "ON" : "OFF");
        Serial.printf(" (Based on Est. Local Time: %02d:%02d)\n", timeinfo_local.tm_hour, timeinfo_local.tm_min);
    }
  }
}

// NEW: Handler to return current estimated local time as JSON
void handleGetCurrentTime() {
  char timeStr[20] = "N/A"; // Default if time/tz not set
  time_t nowUtc = time(nullptr);

  if (nowUtc > 1000000000UL && isTimeOffsetSet) {
      time_t clientLocalEpoch = nowUtc + (clientTimezoneOffsetMinutes * 60);
      struct tm timeinfo_client;
      gmtime_r(&clientLocalEpoch, &timeinfo_client); // Use gmtime_r for thread safety
      strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo_client);
  } else if (nowUtc > 1000000000UL && !isTimeOffsetSet) {
      // Time synced, but TZ offset missing
      strcpy(timeStr, "TZ Not Set");
  } else {
      // Time not synced yet
      strcpy(timeStr, "Sync Pend");
  }

  String json = "{\"time\":\"";
  json += timeStr;
  json += "\"}";
  server.send(200, "application/json", json);
}


// (handleSetSchedule, handleNotFound - no changes needed)
void handleSetSchedule() {
  if (server.hasArg("startHour") && server.hasArg("startMinute") &&
      server.hasArg("endHour") && server.hasArg("endMinute")) {
    startHour = server.arg("startHour").toInt();
    startMinute = server.arg("startMinute").toInt();
    endHour = server.arg("endHour").toInt();
    endMinute = server.arg("endMinute").toInt();
    startHour = constrain(startHour, 0, 23);
    startMinute = constrain(startMinute, 0, 59);
    endHour = constrain(endHour, 0, 23);
    endMinute = constrain(endMinute, 0, 59);

    Serial.print("Schedule set via HTTP: ");
    Serial.printf("%02d:%02d - %02d:%02d (Local)\n", startHour, startMinute, endHour, endMinute);
    saveSettings(); // Save the new schedule to EEPROM
    updateTime(); // Immediately check if the new schedule changes the light state
  }
  server.sendHeader("Location", "/"); // Redirect back to the root page
  server.send(303);
}
void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}


// ------------------------- WiFi Setup -------------------------
void setupWiFi() {
  WiFi.mode(WIFI_AP);
  IPAddress local_IP(192, 168, 4, 22);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);

  // Generate Random Suffix
  String uniqueID = "";
  for (int i = 0; i < 3; i++) {
    char randomLetter = 'A' + random(0, 26); // Generate A-Z
    uniqueID += randomLetter;
  }

  String deviceName = "LightTrack " + uniqueID; // Use random suffix
  WiFi.softAP(deviceName.c_str(), "12345678");
  Serial.print("WiFi AP Name: "); Serial.println(deviceName);

  // Reduce WiFi power slightly (optional)
  esp_wifi_set_max_tx_power(21); // Corresponds to +8.5dBm on C3

  Serial.println("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // Register HTTP handlers
  server.on("/", handleRoot);
  server.on("/setInterval", handleSetInterval);
  server.on("/setLedOffDelay", handleSetLedOffDelay);
  server.on("/setBaseColor", handleSetBaseColor);
  server.on("/setMovingIntensity", handleSetMovingIntensity);
  server.on("/setStationaryIntensity", handleSetStationaryIntensity);
  server.on("/setMovingLength", handleSetMovingLength);
  server.on("/setAdditionalLEDs", handleSetAdditionalLEDs);
  server.on("/setCenterShift", handleSetCenterShift);
  server.on("/setGradientSoftness", handleSetGradientSoftness);
  server.on("/setTime", handleSetTime);
  server.on("/setSchedule", handleSetSchedule);
  server.on("/smarthome/on", handleSmartHomeOn);
  server.on("/smarthome/off", handleSmartHomeOff);
  server.on("/smarthome/clear", handleSmartHomeClear);
  server.on("/toggleNightMode", handleToggleBackgroundMode);
  server.on("/getCurrentTime", handleGetCurrentTime); // NEW: Register time endpoint
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Web server started.");
}

// ------------------------- Web Interface Handler -------------------------
void handleRoot() {
  char scheduleStartStr[6];
  sprintf(scheduleStartStr, "%02d:%02d", startHour, startMinute);
  char scheduleEndStr[6];
  sprintf(scheduleEndStr, "%02d:%02d", endHour, endMinute);

  int movingIntensityPercent = (int)(movingIntensity * 100.0 + 0.5);
  float stationaryIntensityPercent = stationaryIntensity * 100.0;

  // Use standard string concatenation for HTML
  String html = "";
  html += "<html>";
  html += "<head>";
  html += "<title>LED Control Panel</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no'>";
  html += "<style>";
  html += "body { margin: 0; padding: 0; background-color: #333; color: white; font-family: Arial, sans-serif; }";
  html += ".container { text-align: center; width: 90%; max-width: 800px; margin: auto; padding-top: 20px; }";
  html += ".lighttrack { font-size: 1.4em; }";
  html += ".footer { font-size: 1em; margin-top: 20px; }";
  html += "input[type=range] { -webkit-appearance: none; width: 100%; height: 25px; background: transparent; margin: 5px 0; }";
  html += "input[type=range]:focus { outline: none; }";
  html += "input[type=range]::-webkit-slider-runnable-track { height: 8px; background: #F5F5DC; border-radius: 4px; }";
  html += "input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; height: 25px; width: 25px; background: #fff; border: 2px solid #ccc; border-radius: 50%; margin-top: -9px; }";
  html += "input[type=range]::-moz-range-track { height: 8px; background: #F5F5DC; border-radius: 4px; }";
  html += "input[type=range]::-moz-range-thumb { height: 25px; width: 25px; background: #fff; border: 2px solid #ccc; border-radius: 50%; }";
  html += "input[type=color] { width: 100px; height: 100px; border: none; display: block; margin: 10px auto; }";
  html += "input[type=time] { font-size: 1.2em; margin: 5px; }";
  html += "button { font-size: 1em; margin: 5px; padding: 10px; }";
  html += "hr { border: none; height: 1px; background: #555; margin: 25px 0; }";
  html += "p { margin-bottom: 2px; margin-top: 10px; }";
  html += ".current-time { font-size: 0.9em; color: #ccc; margin-top: 10px; }"; // NEW Style for current time display
  html += "</style>";
  // ----- MODIFIED JAVASCRIPT -----
  html += "<script>";
  html += "function setDeviceTime() {";
  html += "  var now = new Date();";
  html += "  var epoch = Math.floor(now.getTime()/1000);";
  html += "  var tz = -now.getTimezoneOffset();"; // Send offset (minutes EAST of UTC)
  html += "  fetch('/setTime?epoch='+epoch+'&tz='+tz).then(response => response.text()).then(text => {";
  html += "    console.log('Set time response:', text);";
  html += "    updateTimeDisplay();"; // Update time display after setting
  html += "  });";
  html += "}";

  html += "function updateTimeDisplay() {";
  html += "  fetch('/getCurrentTime').then(response => response.json()).then(data => {";
  html += "    document.getElementById('currentTimeDisplay').innerText = data.time;";
  html += "  }).catch(error => console.error('Error fetching time:', error));";
  html += "}";

  html += "function changeBaseColor(hex) {";
  html += "var r = parseInt(hex.substring(1,3),16);";
  html += "var g = parseInt(hex.substring(3,5),16);";
  html += "var b = parseInt(hex.substring(5,7),16);";
  html += "fetch('/setBaseColor?r=' + r + '&g=' + g + '&b=' + b); }";
  html += "function setMovingIntensity(val) { fetch('/setMovingIntensity?value=' + val); }";
  html += "function setStationaryIntensityValue(val) { fetch('/setStationaryIntensity?value=' + val); }";
  html += "function setMovingLength(val) { fetch('/setMovingLength?value=' + val); }";
  html += "function setAdditionalLEDs(val) { fetch('/setAdditionalLEDs?value=' + val); }";
  html += "function setCenterShift(val) { fetch('/setCenterShift?value=' + val); }";
  html += "function setLedOffDelay(val) { fetch('/setLedOffDelay?value=' + val); }";
  html += "function setSchedule(startTime, endTime) {"; // Schedule change still reloads page
  html += "var sParts = startTime.split(':');";
  html += "var eParts = endTime.split(':');";
  html += "fetch('/setSchedule?startHour=' + sParts[0] + '&startMinute=' + sParts[1] +";
  html += "'&endHour=' + eParts[0] + '&endMinute=' + eParts[1]).then(()=>location.reload()); }";
  html += "function toggleBackgroundMode() { fetch('/toggleNightMode').then(()=>location.reload()); }"; // Reload needed to update button text
  html += "function setGradientSoftness(val) { fetch('/setGradientSoftness?value=' + val); }";

  html += "// Update time every 5 seconds";
  html += "setInterval(updateTimeDisplay, 5000);";
  html += "// Periodically re-sync TZ (e.g., every hour)";
  html += "setInterval(setDeviceTime, 3600000);";
  html += "</script>";
  // ----- END OF MODIFIED JAVASCRIPT -----
  html += "</head>";
  html += "<body onload='setDeviceTime()'>"; // Call setDeviceTime on load, which will then call updateTimeDisplay
  html += "<div class='container'>";
  html += "<h1 class='lighttrack'>LED Control Panel</h1>";

  html += "<input type='color' id='baseColorPicker' value='#";
  html += String((baseColor.r < 16 ? "0" : "") + String(baseColor.r, HEX));
  html += String((baseColor.g < 16 ? "0" : "") + String(baseColor.g, HEX));
  html += String((baseColor.b < 16 ? "0" : "") + String(baseColor.b, HEX));
  html += "' onchange='changeBaseColor(this.value)'>";

  html += "<p>Moving Light Intensity: <span id='movingIntensityValue'>"; html += String(movingIntensityPercent); html += "</span>%</p>";
  html += "<input type='range' min='0' max='100' step='1' value='"; html += String(movingIntensityPercent); html += "' oninput='document.getElementById(\"movingIntensityValue\").innerText = this.value' onchange='setMovingIntensity(this.value)'>";

  html += "<p>Moving Light Length: <span id='movingLengthValue'>"; html += String(movingLength); html += "</span></p>";
  html += "<input type='range' min='1' max='"; html += String(NUM_LEDS); html += "' step='1' value='"; html += String(movingLength); html += "' oninput='document.getElementById(\"movingLengthValue\").innerText = this.value' onchange='setMovingLength(this.value)'>";

  html += "<p>Additional LEDs (direction): <span id='additionalLEDsValue'>"; html += String(additionalLEDs); html += "</span></p>";
  html += "<input type='range' min='0' max='"; html += String(NUM_LEDS/2); html += "' step='1' value='"; html += String(additionalLEDs); html += "' oninput='document.getElementById(\"additionalLEDsValue\").innerText = this.value' onchange='setAdditionalLEDs(this.value)'>";

  html += "<p>Gradient Softness (0=Hard, 10=Soft): <span id='gradientSoftnessValue'>"; html += String(gradientSoftness); html += "</span></p>";
  html += "<input type='range' min='0' max='10' step='1' value='"; html += String(gradientSoftness); html += "' oninput='document.getElementById(\"gradientSoftnessValue\").innerText = this.value' onchange='setGradientSoftness(this.value)'>";

  html += "<p>Center Shift (LEDs): <span id='centerShiftValue'>"; html += String(centerShift); html += "</span></p>";
  html += "<input type='range' min='-"; html += String(NUM_LEDS/2); html += "' max='"; html += String(NUM_LEDS/2); html += "' step='1' value='"; html += String(centerShift); html += "' oninput='document.getElementById(\"centerShiftValue\").innerText = this.value' onchange='setCenterShift(this.value)'>";

  html += "<p>LED Off Delay (seconds): <span id='ledOffDelayValue'>"; html += String(ledOffDelay); html += "</span></p>";
  html += "<input type='range' min='1' max='60' step='1' value='"; html += String(ledOffDelay); html += "' oninput='document.getElementById(\"ledOffDelayValue\").innerText = this.value' onchange='setLedOffDelay(this.value)'>";

  html += "<hr>";

  html += "<p>Background Light Mode:</p>";
  html += "<button onclick='toggleBackgroundMode()'>"; html += (backgroundModeActive ? "Turn Off" : "Turn On"); html += " Background</button>";

  html += "<p>Background Light Intensity: <span id='stationaryIntensityValue'>"; html += String(stationaryIntensityPercent, 1); html += "</span>%</p>";
  html += "<input type='range' min='0' max='10' step='0.1' value='"; html += String(stationaryIntensityPercent, 1); html += "' oninput='document.getElementById(\"stationaryIntensityValue\").innerText = parseFloat(this.value).toFixed(1)' onchange='setStationaryIntensityValue(this.value)'>";

  html += "<hr>";

  html += "<p>Schedule Window (Local Time):</p>";
  html += "<div style='display: flex; justify-content: center; gap: 15px;'>";
  html += "<input type='time' id='scheduleStartInput' value='"; html += String(scheduleStartStr); html += "' onchange='setSchedule(this.value, document.getElementById(\"scheduleEndInput\").value)'>";
  html += "<input type='time' id='scheduleEndInput' value='"; html += String(scheduleEndStr); html += "' onchange='setSchedule(document.getElementById(\"scheduleStartInput\").value, this.value)'>";
  html += "</div>";
  // ----- NEW HTML FOR TIME DISPLAY -----
  html += "<div class='current-time'>Est. Local Time: <span id='currentTimeDisplay'>Loading...</span></div>";
  // ------------------------------------

  html += "<div class='footer'>DIY Yari</div>";
  html += "</div>"; // container
  html += "</body>";
  html += "</html>";

  server.send(200, "text/html", html);
}


// ------------------------- HTTP Handlers (Settings) -------------------------
// (No changes needed for these handlers)
void handleSetInterval() {
  if (server.hasArg("value")) {
    updateInterval = server.arg("value").toInt();
    if(updateInterval < 10) updateInterval = 10;
    Serial.print("Update interval set to: "); Serial.println(updateInterval);
    saveSettings();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}
void handleSetLedOffDelay() {
  if (server.hasArg("value")) {
    ledOffDelay = server.arg("value").toInt();
    ledOffDelay = constrain(ledOffDelay, 1, 60);
    Serial.print("LED off delay set to: "); Serial.println(ledOffDelay);
    saveSettings();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}
void handleSetBaseColor() {
  if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
    baseColor = CRGB(server.arg("r").toInt(), server.arg("g").toInt(), server.arg("b").toInt());
    Serial.print("Base color set to RGB: "); Serial.print(baseColor.r); Serial.print(", "); Serial.print(baseColor.g); Serial.print(", "); Serial.println(baseColor.b);
    saveSettings();
  }
 // No redirect needed for color change - async update
  server.send(200, "text/plain", "OK");
}
void handleSetMovingLength() {
  if (server.hasArg("value")) {
    movingLength = server.arg("value").toInt();
    movingLength = constrain(movingLength, 1, NUM_LEDS);
    Serial.print("Moving length set to: "); Serial.println(movingLength);
    saveSettings();
  }
 // No redirect needed - async update
  server.send(200, "text/plain", "OK");
}
void handleSetAdditionalLEDs() {
  if (server.hasArg("value")) {
    additionalLEDs = server.arg("value").toInt();
    additionalLEDs = constrain(additionalLEDs, 0, NUM_LEDS / 2);
    Serial.print("Additional LEDs set to: "); Serial.println(additionalLEDs);
    saveSettings();
  }
 // No redirect needed - async update
  server.send(200, "text/plain", "OK");
}
void handleSetCenterShift() {
  if (server.hasArg("value")) {
    centerShift = server.arg("value").toInt();
    centerShift = constrain(centerShift, -NUM_LEDS / 2, NUM_LEDS / 2);
    Serial.print("Center shift set to: "); Serial.println(centerShift);
    saveSettings();
  }
 // No redirect needed - async update
  server.send(200, "text/plain", "OK");
}
// Intensity Handlers Updated
void handleSetMovingIntensity() {
  if (server.hasArg("value")) {
    float val_percent = server.arg("value").toFloat();
    movingIntensity = constrain(val_percent / 100.0, 0.0, 1.0);
    Serial.print("Moving intensity set to: "); Serial.print(movingIntensity * 100.0, 0); Serial.println("%");
    saveSettings();
  }
 // No redirect needed - async update
  server.send(200, "text/plain", "OK");
}
void handleSetStationaryIntensity() {
  if (server.hasArg("value")) {
    float val_percent = server.arg("value").toFloat();
    stationaryIntensity = constrain(val_percent / 100.0, 0.0, 0.1);
    Serial.print("Stationary intensity set to: "); Serial.print(stationaryIntensity * 100.0, 1); Serial.println("%");
    saveSettings();
  }
 // No redirect needed - async update
  server.send(200, "text/plain", "OK");
}
// Gradient Handler (Exists)
void handleSetGradientSoftness() {
  if (server.hasArg("value")) {
    gradientSoftness = server.arg("value").toInt();
    gradientSoftness = constrain(gradientSoftness, 0, 10);
    Serial.print("Gradient Softness set to: "); Serial.println(gradientSoftness);
    saveSettings();
  }
 // No redirect needed - async update
  server.send(200, "text/plain", "OK");
}


// ------------------------- OTA Setup Function -------------------------
void setupOTA() {
  ArduinoOTA.onStart([]() {
    String type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  // Use the same device name for OTA hostname
  String uniqueID = "";
  uint64_t chipid = ESP.getEfuseMac();
  randomSeed((unsigned long)chipid ^ (unsigned long)(chipid >> 32));
  for (int i = 0; i < 3; i++) { uniqueID += (char)('A' + random(0, 26)); }
  String hostname = "LightTrack-" + uniqueID;

  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.begin();
  Serial.print("OTA Initialized. Hostname: "); Serial.println(hostname);
}

// ------------------------- Setup -------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n--------------------------------------");
  Serial.println("ESP32-C3 LightTrack Starting (Local Time Schedule)");
  Serial.println("--------------------------------------");

  // Initialize random number generator
  uint64_t chipid = ESP.getEfuseMac();
  randomSeed((unsigned long)chipid ^ (unsigned long)(chipid >> 32));
  
  // Initialize LEDs immediately
  Serial.println("Initializing LED Strip (FastLED)...");
  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(255);
  FastLED.clear(); 
  leds[0] = CRGB::White;
  FastLED.show();
  
  // Initialize SPIFFS and load settings
  Serial.println("Initializing SPIFFS & EEPROM...");
  if (!SPIFFS.begin(true)) {
    Serial.println("!!! Failed to mount SPIFFS. Formatting...");
    if (!SPIFFS.begin(true)) {
      Serial.println("!!! SPIFFS Format failed. Continuing anyway.");
    }
  }
  loadSettings();
  
  // Initialize sensor
  Serial.println("Initializing Radar Sensor (Serial1)...");
  Serial1.begin(256000, SERIAL_8N1, 20, 21);
  
  // Set up WiFi
  Serial.println("Setting up WiFi AP Mode...");
  setupWiFi();

  // Start NTP without blocking
  Serial.println("Setting up NTP time synchronization (async)...");
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");

  // Set up OTA
  Serial.println("Setting up OTA Updates...");
  setupOTA();
  
  // Turn off indicator LED
  leds[0] = CRGB::Black;
  FastLED.show();
  
  // Create tasks
  Serial.println("Creating RTOS Tasks...");
  xTaskCreatePinnedToCore(sensorTask, "Sensor Task", 2048, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(ledTask, "LED Task", 8192, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(webServerTask, "WebServer Task", 4096, NULL, 1, NULL, 0);

  Serial.println("--------------------------------------");
  Serial.println("Setup Complete. System Running.");
  Serial.print("Connect to WiFi AP '"); Serial.print(WiFi.softAPSSID()); 
  Serial.print("' with password '12345678'");
  Serial.print(" and browse to http://"); Serial.println(WiFi.softAPIP());
  Serial.println("--------------------------------------");
}

// ------------------------- Loop -------------------------
void loop() {
  updateTime(); // Check schedule using local time calculation

  // Optional status logging
  static unsigned long lastLoopLog = 0;
  if (millis() - lastLoopLog > 15000) { // Log status every 15 seconds
      lastLoopLog = millis();
      Serial.println("--- Status Update ---");
      Serial.print("Uptime: "); Serial.print(millis()/1000); Serial.println(" s");

      time_t now = time(nullptr);
      if (now < 1000000000UL) {
          Serial.println("System Time (UTC): Not Synced");
          Serial.println("Est. Local Time: N/A");
      } else {
          struct tm timeinfo_utc;
          gmtime_r(&now, &timeinfo_utc);
          char utc_buf[25]; strftime(utc_buf, sizeof(utc_buf), "%Y-%m-%d %H:%M:%S", &timeinfo_utc);
          Serial.print("System Time (UTC): "); Serial.println(utc_buf);

          if (isTimeOffsetSet) {
              time_t clientLocalEpoch = now + (clientTimezoneOffsetMinutes * 60);
              struct tm timeinfo_client;
              gmtime_r(&clientLocalEpoch, &timeinfo_client);
              char local_buf[20]; strftime(local_buf, sizeof(local_buf), "%H:%M:%S", &timeinfo_client);
              Serial.print("Est. Local Time: "); Serial.print(local_buf);
              Serial.print(" (Offset: "); Serial.print(clientTimezoneOffsetMinutes); Serial.println(" min)");
          } else {
              Serial.println("Est. Local Time: Timezone Not Set");
          }
      }

      Serial.print("Schedule: "); Serial.printf("%02d:%02d - %02d:%02d (Local)\n", startHour, startMinute, endHour, endMinute);
      Serial.print("Light status (lightOn): "); Serial.println(lightOn ? "ON" : "OFF");
      Serial.print("Background Mode: "); Serial.println(backgroundModeActive ? "ON" : "OFF");
      Serial.print("SmartHome Override: "); Serial.println(smarthomeOverride ? "YES" : "NO");
      Serial.print("Moving Intensity: "); Serial.print(movingIntensity * 100.0, 0); Serial.println("%");
      Serial.print("Stationary Intensity: "); Serial.print(stationaryIntensity * 100.0, 1); Serial.println("%");
      Serial.print("Gradient Softness: "); Serial.println(gradientSoftness);
      Serial.print("Free heap: "); Serial.println(ESP.getFreeHeap());
      Serial.println("---------------------");
  }

  vTaskDelay(pdMS_TO_TICKS(1000)); // Check schedule roughly every second
}
