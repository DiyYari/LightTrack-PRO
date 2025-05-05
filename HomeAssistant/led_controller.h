#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include <FastLED.h> // Required for CRGB type awareness

// Initialize LED controller (hardware setup)
void initLEDController();

// Main LED task function (runs infinitely)
// Parameter is standard for FreeRTOS tasks, can be NULL here.
void ledTask(void * parameter);

#endif // LED_CONTROLLER_H