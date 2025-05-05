#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <FastLED.h>

// ------------------------- LED Configuration -------------------------
#define LED_PIN             2
#define NUM_LEDS            300
#define CHIPSET             WS2812B
#define COLOR_ORDER         GRB

// ------------------------- Sensor Parameters -------------------------
#define SENSOR_HEADER       0xAA
#define MIN_DISTANCE        20
#define MAX_DISTANCE        1000
#define DEFAULT_DISTANCE    1000
#define NOISE_THRESHOLD     5

// ------------------------- Default Display Parameters -------------------------
#define DEFAULT_UPDATE_INTERVAL     20
#define DEFAULT_MOVING_INTENSITY    0.3
#define DEFAULT_STATIONARY_INTENSITY 0.0
#define DEFAULT_MOVING_LENGTH       33
#define DEFAULT_CENTER_SHIFT        0
#define DEFAULT_ADDITIONAL_LEDS     0
#define DEFAULT_BASE_COLOR          CRGB(255, 200, 50)
#define DEFAULT_SPEED_MULTIPLIER    2.0
#define DEFAULT_LED_OFF_DELAY       5

// ------------------------- Default Time and Schedule Parameters -------------------------
#define DEFAULT_START_HOUR    20
#define DEFAULT_START_MINUTE  0
#define DEFAULT_END_HOUR      8
#define DEFAULT_END_MINUTE    30

// ------------------------- EEPROM -------------------------
// Make sure EEPROM_SIZE is large enough for all saved settings (WiFi, MQTT, LED params etc.)
#define EEPROM_SIZE 128 // Increased size example, adjust as needed

// ------------------------- WiFi Settings -------------------------
#define AP_SSID_PREFIX "LightTrack" // Prefix for the AP name
#define AP_PASSWORD "12345678"      // Password for the Access Point
#define WIFI_CONNECT_TIMEOUT 15000  // 15 seconds timeout for WiFi connection attempt

// ------------------------- HomeAssistant MQTT Settings -------------------------
#define MQTT_PORT 1883
#define MQTT_RECONNECT_DELAY 5000  // 5 seconds
#define MQTT_DISCOVERY_PREFIX "homeassistant"
#define MQTT_NODE_ID "lighttrack"
#define HA_DISCOVERY_DELAY 10000   // 10 seconds delay between discovery messages

// ------------------------- Serial settings for sensor -------------------------
#define SENSOR_BAUD_RATE 256000

#endif // CONFIG_H