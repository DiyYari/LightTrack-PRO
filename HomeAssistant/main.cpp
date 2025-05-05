#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <Preferences.h> // Used by storage.h usually

#include "config.h"
#include "wifi_manager.h" // Manages WiFi connection (STA + AP)
#include "led_controller.h" // Manages LED strip
#include "sensor_manager.h" // Manages distance sensor
#include "web_server.h"     // Manages the web interface
#include "storage.h"        // Manages saving/loading settings
#include "home_assistant.h" // Manages MQTT and HA integration

// Task handles
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t ledTaskHandle = NULL;
TaskHandle_t serverTaskHandle = NULL;
TaskHandle_t debugTaskHandle = NULL; // Optional debug task

// Debug Task: Periodically prints sensor distance
void debugTask(void * parameter) {
  for (;;) {
    Serial.print("DEBUG: Sensor distance: ");
    Serial.println(getSensorDistance());
    vTaskDelay(pdMS_TO_TICKS(5000)); // Print every 5 seconds
  }
}

// Time update logic: checks schedule and sets light state if not overridden
void updateTimeBasedState() {
  static unsigned long lastTimeCheck = 0;
  const unsigned long checkInterval = 1000; // Check every second

  unsigned long currentMillis = millis();

  if (currentMillis - lastTimeCheck >= checkInterval) {
    lastTimeCheck = currentMillis;
    time_t nowSec = time(nullptr); // Get current time

    // Check if time is synchronized (greater than a reasonable value like year 2000 epoch)
    if (nowSec < 1000000000UL) {
      // Time not set or invalid, maybe keep light ON as default?
      // Or handle this state differently if needed.
      // setLightOn(true); // Example: Default ON if time is unknown
      return; // Don't process schedule if time is not valid
    }

    // Only apply schedule if Smart Home hasn't overridden the state
    if (!isSmartHomeOverride()) {
      struct tm timeinfo;
      getLocalTime(&timeinfo); // Get broken-down time (hours, minutes)

      int currentTotalMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
      int startTotalMinutes = getStartHour() * 60 + getStartMinute();
      int endTotalMinutes = getEndHour() * 60 + getEndMinute();

      bool shouldBeOn;
      // Handle schedule across midnight (e.g., 20:00 to 08:00)
      if (startTotalMinutes <= endTotalMinutes) {
        // Normal case: Start time is before End time (e.g., 08:00 to 20:00)
        shouldBeOn = (currentTotalMinutes >= startTotalMinutes && currentTotalMinutes < endTotalMinutes);
      } else {
        // Overnight case: Start time is after End time (e.g., 20:00 to 08:00)
        shouldBeOn = (currentTotalMinutes >= startTotalMinutes || currentTotalMinutes < endTotalMinutes);
      }

      // Apply the calculated state
      if(isLightOn() != shouldBeOn) {
         setLightOn(shouldBeOn);
         Serial.print("TIME: Schedule applied. Light turned "); Serial.println(shouldBeOn ? "ON" : "OFF");
      }
    }
  }
}

// Setup OTA (Over-The-Air Updates)
void setupOTA() {
  ArduinoOTA.setHostname(getDeviceName().c_str()); // Use the unique device name
  // ArduinoOTA.setPassword("ota_password"); // Optional: Set password

  ArduinoOTA.onStart([]() {
    String type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
    Serial.println("OTA: Start updating " + type);
    // Optional: Stop tasks that might interfere (e.g., LED updates)
    // if (ledTaskHandle != NULL) vTaskSuspend(ledTaskHandle);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA: End");
    // Optional: Resume tasks if stopped
    // if (ledTaskHandle != NULL) vTaskResume(ledTaskHandle);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("OTA: Ready");
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nLightTrack Starting...");

  // Initialize SPIFFS for web server files (if used)
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS: Mount Failed!");
    // Decide how to handle this - maybe device can't fully work?
  } else {
    Serial.println("SPIFFS: Mounted.");
  }

  // Initialize storage (must be done early to load settings)
  initStorage();
  Serial.println("Storage: Initialized.");

  // Initialize LED controller (load parameters from storage)
  initLEDController();
  Serial.println("LEDs: Initialized.");

  // Initialize sensor interface
  initSensor();
  Serial.println("Sensor: Initialized.");

  // Initialize time (using default TZ, will be updated by web UI or NTP if connected)
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov"); // Basic NTP setup
  Serial.println("Time: Configured (waiting for sync).");

  // Setup WiFi (tries to connect using saved creds, starts AP if fails)
  // This also generates the unique device name
  setupWiFi();
  Serial.println("WiFi: Setup process initiated.");

  // Initialize web server (starts serving pages)
  initWebServer();
  Serial.println("Web Server: Initialized.");

  // Initialize HomeAssistant integration (loads MQTT settings, prepares client)
  initHomeAssistant();
  Serial.println("Home Assistant: Initialized.");

  // Setup OTA only after WiFi is potentially connected
  setupOTA();

  // Create tasks with specific core affinities if needed
  // Core 0: Often used for WiFi/BT stack - good for timing critical tasks like LEDs
  // Core 1: Often used for application logic, network tasks
  Serial.println("Tasks: Creating...");
  xTaskCreatePinnedToCore(sensorTask, "Sensor Task", 2048, NULL, 2, &sensorTaskHandle, 1); // Core 1
  xTaskCreatePinnedToCore(ledTask, "LED Task", 4096, NULL, 1, &ledTaskHandle, 0);       // Core 0 (timing critical)
  xTaskCreatePinnedToCore(webServerTask, "WebServer Task", 4096, NULL, 1, &serverTaskHandle, 1); // Core 1
  // xTaskCreatePinnedToCore(debugTask, "Debug Task", 2048, NULL, 1, &debugTaskHandle, 1); // Uncomment for debug output (Core 1)

  Serial.println("LightTrack: Setup complete. System running.");
}

void loop() {
  // Handle OTA updates
  ArduinoOTA.handle();

  // Handle MQTT connection and communication
  handleHomeAssistant();

  // Update light state based on time schedule (if not overridden)
  updateTimeBasedState();

  // Allow other tasks to run. The main loop handles less frequent tasks.
  vTaskDelay(pdMS_TO_TICKS(100)); // Reduced delay to keep OTA responsive
}