#ifndef HOME_ASSISTANT_H
#define HOME_ASSISTANT_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Initialize HomeAssistant integration
void initHomeAssistant();

// Handle HomeAssistant communication
void handleHomeAssistant();

// Set MQTT server
void setMqttServer(String server);

// Helper function to create number entities for HomeAssistant
void createNumberEntity(JsonDocument& deviceDoc, String name, String field, float min, float max, float step);

#endif // HOME_ASSISTANT_H