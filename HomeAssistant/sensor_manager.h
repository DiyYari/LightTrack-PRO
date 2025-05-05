#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>

// Initialize the sensor serial connection
void initSensor();

// Get the last known sensor distance (thread-safe access)
// Returns distance in units defined by sensor (e.g., mm or cm)
unsigned int getSensorDistance();

// Sensor reading task function (runs infinitely)
// Reads from serial, updates distance, handles protocol.
// Parameter is standard for FreeRTOS tasks, can be NULL here.
void sensorTask(void * parameter);

// Function to perform a single read attempt (used internally by task)
// Returns the newly read distance or the previous value on error/no data.
unsigned int readSensorData();

#endif // SENSOR_MANAGER_H