#include "home_assistant.h"
#include "config.h"
#include "storage.h"
#include "wifi_manager.h"
#include "web_server.h" 
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// MQTT client
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// MQTT settings
bool mqttEnabled = false;
char mqttClientId[64]; // Increased size slightly
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastHADiscoveryTime = 0;

// MQTT topics
String baseTopic;
String stateTopic;
String commandTopic;
String availabilityTopic;

// Forward declarations
bool reconnectMqtt();
void sendHomeAssistantDiscovery();
void publishState();
void mqttCallback(char* topic, byte* payload, unsigned int length);

void initHomeAssistant() {
  String deviceName = getDeviceName(); // Get unique device name from wifi_manager
  deviceName.replace(" ", "_");
  snprintf(mqttClientId, sizeof(mqttClientId), "%s_%s", MQTT_NODE_ID, deviceName.c_str());

  // Create base topic using the unique device name
  baseTopic = String(MQTT_NODE_ID) + "/" + deviceName;
  stateTopic = baseTopic + "/state";
  commandTopic = baseTopic + "/set";
  availabilityTopic = baseTopic + "/availability";

  // Load MQTT settings from storage if they exist
  if (hasMqttSettings()) {
      setMqttServer(getMqttServer()); // This will attempt connection if server address is valid
  } else {
      Serial.println("MQTT: No saved settings found.");
      mqttEnabled = false;
  }
}

void handleHomeAssistant() {
  // Exit if MQTT is not enabled or configured
  if (!mqttEnabled || !hasMqttSettings()) {
    return;
  }

  // Only proceed if connected to WiFi in client mode
  if (WiFi.status() != WL_CONNECTED) {
      // If MQTT was connected, mark as disconnected
      if (mqttClient.connected()) {
          Serial.println("MQTT: WiFi disconnected, stopping MQTT client.");
          mqttClient.disconnect(); // Gracefully disconnect
      }
      lastMqttReconnectAttempt = 0; // Reset reconnect timer
      return;
  }

  // Handle MQTT connection/reconnection
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastMqttReconnectAttempt > MQTT_RECONNECT_DELAY || lastMqttReconnectAttempt == 0) {
      lastMqttReconnectAttempt = now;
      Serial.println("MQTT: Attempting to reconnect...");
      if (reconnectMqtt()) {
        lastMqttReconnectAttempt = 0; // Success, reset timer
      } else {
         // Keep lastMqttReconnectAttempt as is to retry after delay
      }
    }
  } else {
    // MQTT client loop processing
    mqttClient.loop();

    // Send discovery information periodically (e.g., after reconnect or on interval)
    unsigned long now = millis();
    if (now - lastHADiscoveryTime > HA_DISCOVERY_DELAY || lastHADiscoveryTime == 0) {
        if(mqttClient.connected()) { // Double check connection before publishing
            lastHADiscoveryTime = now;
            sendHomeAssistantDiscovery();
            publishState(); // Publish current state after discovery
        } else {
            lastHADiscoveryTime = 0; // Reset time if connection lost before sending
        }
    }
  }
}

bool reconnectMqtt() {
  if (!hasMqttSettings() || WiFi.status() != WL_CONNECTED) {
    return false; // Cannot connect without settings or WiFi connection
  }

  String mqttServer = getMqttServer();
  int mqttPort = getMqttPort();
  String mqttUser = getMqttUser();
  String mqttPassword = getMqttPassword();

  if (mqttServer.length() == 0) {
      Serial.println("MQTT: Server address is empty.");
      return false;
  }

  mqttClient.setServer(mqttServer.c_str(), mqttPort);
  mqttClient.setCallback(mqttCallback);

  Serial.print("MQTT: Attempting connection to ");
  Serial.print(mqttServer);
  Serial.print(":");
  Serial.print(mqttPort);
  Serial.print(" as ");
  Serial.print(mqttClientId);
  Serial.println("...");

  bool success = false;
  String willMessage = "offline"; // Last Will and Testament message

  if (mqttUser.length() > 0) {
    success = mqttClient.connect(mqttClientId, mqttUser.c_str(), mqttPassword.c_str(),
                           availabilityTopic.c_str(), 0, true, willMessage.c_str());
  } else {
    success = mqttClient.connect(mqttClientId, availabilityTopic.c_str(), 0, true, willMessage.c_str());
  }

  if (success) {
    Serial.println("MQTT: Connected.");

    // Publish online status (retained)
    mqttClient.publish(availabilityTopic.c_str(), "online", true);

    // Subscribe to command topic
    mqttClient.subscribe(commandTopic.c_str());
    Serial.print("MQTT: Subscribed to ");
    Serial.println(commandTopic);

    // Send discovery information immediately after connecting
    lastHADiscoveryTime = 0; // Force discovery send in next handle loop
    // sendHomeAssistantDiscovery(); // Optionally send immediately
    // publishState(); // Optionally send state immediately

    return true;
  } else {
    Serial.print("MQTT: Connection failed, rc=");
    Serial.print(mqttClient.state());
    // Print error message based on state
    if(mqttClient.state() == MQTT_CONNECT_BAD_PROTOCOL) Serial.println(" (Bad protocol)");
    else if(mqttClient.state() == MQTT_CONNECT_BAD_CLIENT_ID) Serial.println(" (Client ID rejected)");
    else if(mqttClient.state() == MQTT_CONNECT_UNAVAILABLE) Serial.println(" (Server unavailable)");
    else if(mqttClient.state() == MQTT_CONNECT_BAD_CREDENTIALS) Serial.println(" (Bad credentials)");
    else if(mqttClient.state() == MQTT_CONNECT_UNAUTHORIZED) Serial.println(" (Unauthorized)");
    else Serial.println(" (Unknown reason)");

    return false;
  }
}

// Call this when MQTT server settings are saved/changed
void setMqttServer(String server) {
  mqttEnabled = server.length() > 0;
  lastMqttReconnectAttempt = 0; // Reset reconnect timer to try immediately if enabled
  lastHADiscoveryTime = 0; // Reset discovery timer

  if (mqttEnabled && WiFi.status() == WL_CONNECTED) {
      Serial.println("MQTT: Settings updated. Disconnecting if already connected...");
      if (mqttClient.connected()) {
          mqttClient.disconnect();
      }
      // reconnectMqtt(); // Let handleHomeAssistant handle the reconnection attempt
  } else if (!mqttEnabled) {
       Serial.println("MQTT: Disabled.");
       if (mqttClient.connected()) {
          mqttClient.disconnect();
      }
  } else {
      Serial.println("MQTT: Enabled, but WiFi not connected. Will attempt connection when WiFi is available.");
  }
}

// Helper to create HA discovery number entities
void createNumberEntity(JsonDocument& deviceDoc, String name, String field, float min_val, float max_val, float step) {
  String deviceName = getDeviceName();
  deviceName.replace(" ", "_");
  String uniqueId = deviceName + "_" + field;
  String entityName = getDeviceName() + " " + name; // Use original device name for display

  DynamicJsonDocument entityDoc(512);
  entityDoc["name"] = entityName;
  entityDoc["unique_id"] = uniqueId;
  entityDoc["state_topic"] = stateTopic;
  entityDoc["value_template"] = "{{ value_json." + field + " }}";
  entityDoc["command_topic"] = commandTopic;
  entityDoc["command_template"] = "{\"" + field + "\":{{ value }}}";
  entityDoc["min"] = min_val; // Use parameter name
  entityDoc["max"] = max_val; // Use parameter name
  entityDoc["step"] = step;
  entityDoc["availability_topic"] = availabilityTopic;
  entityDoc["device"] = deviceDoc; // Link to the main device

  String entityJson;
  serializeJson(entityDoc, entityJson);

  String entityTopic = String(MQTT_DISCOVERY_PREFIX) + "/number/" + uniqueId + "/config";
  Serial.print("MQTT: Publishing discovery for number: ");
  Serial.println(entityName);
  mqttClient.publish(entityTopic.c_str(), entityJson.c_str(), true); // Retain discovery message
}

// Send Home Assistant Discovery messages for all entities
void sendHomeAssistantDiscovery() {
  if (!mqttClient.connected()) {
    return;
  }

  String deviceName = getDeviceName();
  String deviceNameUnderscore = deviceName; // Create a copy for IDs
  deviceNameUnderscore.replace(" ", "_");

  Serial.println("MQTT: Sending Home Assistant discovery information...");

  // Base device information
  DynamicJsonDocument deviceDoc(256);
  deviceDoc["identifiers"] = deviceNameUnderscore;
  deviceDoc["name"] = deviceName; // Use original name for display
  deviceDoc["model"] = "LightTrack";
  deviceDoc["manufacturer"] = "DIY Yari"; // Your name/handle
  deviceDoc["sw_version"] = "1.1-STA"; // Indicate version/mode

  // Main light entity (controls ON/OFF, Brightness, Color)
  DynamicJsonDocument lightDoc(1024);
  String lightUniqueId = deviceNameUnderscore + "_light";
  lightDoc["name"] = deviceName; // Main entity uses device name
  lightDoc["unique_id"] = lightUniqueId;
  lightDoc["state_topic"] = stateTopic;
  lightDoc["command_topic"] = commandTopic;
  lightDoc["schema"] = "json";
  lightDoc["brightness"] = true;
  lightDoc["rgb"] = true;
  lightDoc["availability_topic"] = availabilityTopic;
  lightDoc["device"] = deviceDoc; // Link to the device

  String lightJson;
  serializeJson(lightDoc, lightJson);

  String discoveryTopic = String(MQTT_DISCOVERY_PREFIX) + "/light/" + lightUniqueId + "/config";
   Serial.print("MQTT: Publishing discovery for light: ");
   Serial.println(deviceName);
  mqttClient.publish(discoveryTopic.c_str(), lightJson.c_str(), true);

  // Background mode switch
  DynamicJsonDocument backgroundDoc(512);
  String backgroundUniqueId = deviceNameUnderscore + "_background";
  backgroundDoc["name"] = deviceName + " Background Mode";
  backgroundDoc["unique_id"] = backgroundUniqueId;
  backgroundDoc["state_topic"] = stateTopic;
  backgroundDoc["value_template"] = "{{ value_json.background_mode }}";
  backgroundDoc["command_topic"] = commandTopic;
  backgroundDoc["payload_on"] = "{\"background_mode\":\"ON\"}";
  backgroundDoc["payload_off"] = "{\"background_mode\":\"OFF\"}";
  backgroundDoc["availability_topic"] = availabilityTopic;
  backgroundDoc["device"] = deviceDoc; // Link to the device

  String backgroundJson;
  serializeJson(backgroundDoc, backgroundJson);

  String backgroundTopic = String(MQTT_DISCOVERY_PREFIX) + "/switch/" + backgroundUniqueId + "/config";
   Serial.print("MQTT: Publishing discovery for switch: ");
   Serial.println(backgroundDoc["name"].as<String>());
  mqttClient.publish(backgroundTopic.c_str(), backgroundJson.c_str(), true);

  // Create number entities for other parameters using the helper function
  createNumberEntity(deviceDoc, "Moving Length", "moving_length", 1, NUM_LEDS, 1); // Use NUM_LEDS from config
  createNumberEntity(deviceDoc, "Center Shift", "center_shift", -(NUM_LEDS/2), (NUM_LEDS/2), 1);
  createNumberEntity(deviceDoc, "Additional LEDs", "additional_leds", 0, NUM_LEDS/2, 1);
  createNumberEntity(deviceDoc, "LED Off Delay", "led_off_delay", 1, 60, 1);
  createNumberEntity(deviceDoc, "Update Interval", "update_interval", 5, 100, 1);
  createNumberEntity(deviceDoc, "Moving Intensity", "moving_intensity", 0, 1, 0.01);
  // Reduced max background intensity slightly as 0.07 seemed high, adjust if needed
  createNumberEntity(deviceDoc, "Background Intensity", "stationary_intensity", 0, 0.05, 0.001);

   Serial.println("MQTT: Discovery messages sent.");
}

// Publish the current state of the light and parameters
void publishState() {
  if (!mqttClient.connected()) {
    return;
  }

  CRGB baseColor = getBaseColor();

  DynamicJsonDocument stateDoc(512);
  stateDoc["state"] = isLightOn() ? "ON" : "OFF";
  // Brightness in HA maps to moving intensity here. Clamp value to 0-255.
  stateDoc["brightness"] = constrain((int)(getMovingIntensity() * 255.0), 0, 255);

  JsonObject colorObj = stateDoc.createNestedObject("color");
  colorObj["r"] = baseColor.r;
  colorObj["g"] = baseColor.g;
  colorObj["b"] = baseColor.b;

  // Include RGB values directly as HA JSON schema expects them
  // stateDoc["rgb"] = stateDoc["color"]; // Alternative if schema needs 'rgb'

  stateDoc["background_mode"] = isBackgroundModeActive() ? "ON" : "OFF";
  stateDoc["moving_length"] = getMovingLength();
  stateDoc["center_shift"] = getCenterShift();
  stateDoc["additional_leds"] = getAdditionalLEDs();
  stateDoc["led_off_delay"] = getLedOffDelay();
  stateDoc["update_interval"] = getUpdateInterval();
  stateDoc["moving_intensity"] = getMovingIntensity(); // Include the raw float value too
  stateDoc["stationary_intensity"] = getStationaryIntensity();

  String stateJson;
  serializeJson(stateDoc, stateJson);

  // Serial.print("MQTT: Publishing state: "); // Optional: Log published state
  // Serial.println(stateJson);
  mqttClient.publish(stateTopic.c_str(), stateJson.c_str(), true); // Retain state
}

// Callback function for received MQTT messages
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Create buffer for payload
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0'; // Null-terminate the string

  Serial.print("MQTT: Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(message);

  // Process command using ArduinoJson
  DynamicJsonDocument doc(1024); // Increased size to handle potentially larger JSON from HA
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("MQTT: deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  bool stateChanged = false; // Flag to track if any parameter was changed

  // Process state (ON/OFF)
  if (doc.containsKey("state")) {
    String state = doc["state"].as<String>();
    state.toUpperCase(); // Handle "on" or "ON"
    bool newState = (state == "ON");
    if (isLightOn() != newState) {
        setLightOn(newState);
        setSmartHomeOverride(true); // Assume MQTT command overrides schedule
        stateChanged = true;
        Serial.print("MQTT: Light state set to "); Serial.println(state);
    }
  }

  // Process color (HA sends 'color' object with r, g, b)
  if (doc.containsKey("color")) {
      JsonObject colorObj = doc["color"];
      if (colorObj.containsKey("r") && colorObj.containsKey("g") && colorObj.containsKey("b")) {
          CRGB color = CRGB(colorObj["r"], colorObj["g"], colorObj["b"]);
          setBaseColor(color);
          stateChanged = true;
          Serial.print("MQTT: Base color set to R:"); Serial.print(color.r);
          Serial.print(" G:"); Serial.print(color.g); Serial.print(" B:"); Serial.println(color.b);
      }
  }
  // Also handle legacy 'rgb' if needed
  else if (doc.containsKey("rgb")) {
      JsonObject rgbObj = doc["rgb"];
       if (rgbObj.containsKey("r") && rgbObj.containsKey("g") && rgbObj.containsKey("b")) {
          CRGB color = CRGB(rgbObj["r"], rgbObj["g"], rgbObj["b"]);
          setBaseColor(color);
          stateChanged = true;
          Serial.print("MQTT: Base color set via RGB to R:"); Serial.print(color.r);
          Serial.print(" G:"); Serial.print(color.g); Serial.print(" B:"); Serial.println(color.b);
       }
  }


  // Process brightness (maps to moving_intensity)
  if (doc.containsKey("brightness")) {
    int brightness = doc["brightness"];
    float intensity = constrain(brightness / 255.0f, 0.0f, 1.0f);
    setMovingIntensity(intensity);
    stateChanged = true;
     Serial.print("MQTT: Moving intensity set to "); Serial.println(intensity);
  }

  // Process background mode
  if (doc.containsKey("background_mode")) {
    String mode = doc["background_mode"].as<String>();
    mode.toUpperCase();
    setBackgroundModeActive(mode == "ON");
    stateChanged = true;
    Serial.print("MQTT: Background mode set to "); Serial.println(mode);
  }

  // Process moving_length
  if (doc.containsKey("moving_length")) {
    int length = doc["moving_length"];
    setMovingLength(length);
    stateChanged = true;
    Serial.print("MQTT: Moving length set to "); Serial.println(length);
  }

  // Process center_shift
  if (doc.containsKey("center_shift")) {
    int shift = doc["center_shift"];
    setCenterShift(shift);
    stateChanged = true;
     Serial.print("MQTT: Center shift set to "); Serial.println(shift);
  }

  // Process additional_leds
  if (doc.containsKey("additional_leds")) {
    int leds = doc["additional_leds"];
    setAdditionalLEDs(leds);
    stateChanged = true;
    Serial.print("MQTT: Additional LEDs set to "); Serial.println(leds);
  }

  // Process led_off_delay
  if (doc.containsKey("led_off_delay")) {
    int delay = doc["led_off_delay"];
    setLedOffDelay(delay);
    stateChanged = true;
    Serial.print("MQTT: LED off delay set to "); Serial.println(delay);
  }

  // Process update_interval
  if (doc.containsKey("update_interval")) {
    int interval = doc["update_interval"];
    setUpdateInterval(interval);
    stateChanged = true;
    Serial.print("MQTT: Update interval set to "); Serial.println(interval);
  }

  // Process moving_intensity (allow direct float setting)
  if (doc.containsKey("moving_intensity")) {
    float intensity = doc["moving_intensity"];
    setMovingIntensity(intensity);
    stateChanged = true;
    Serial.print("MQTT: Moving intensity (float) set to "); Serial.println(intensity);
  }

  // Process stationary_intensity (background brightness)
  if (doc.containsKey("stationary_intensity")) {
    float intensity = doc["stationary_intensity"];
    setStationaryIntensity(intensity);
    stateChanged = true;
     Serial.print("MQTT: Stationary intensity set to "); Serial.println(intensity);
  }

  // Publish updated state if anything changed
  if (stateChanged) {
    Serial.println("MQTT: State changed, publishing update.");
    publishState();
    // Optionally save changed parameters to EEPROM/Storage here if needed
    // Example: saveLedParameters();
  }
}