#include "sensor_manager.h"
#include "config.h" // For SENSOR_BAUD_RATE, HEADER, MIN/MAX_DISTANCE etc.

// #define SIMULATE_SENSOR // Uncomment this line to simulate sensor data for testing

// Global variable to store the latest distance reading.
// Marked 'volatile' because it's written by sensorTask and read by getSensorDistance (potentially different cores/contexts).
volatile unsigned int g_sensorDistance = DEFAULT_DISTANCE;

// Initializes Serial1 for sensor communication
void initSensor() {
  // Pins 20 (RX) and 21 (TX) are defaults for Serial1 on some ESP32 boards, adjust if needed.
  Serial1.begin(SENSOR_BAUD_RATE, SERIAL_8N1, 20, 21);
  Serial.println("Sensor Manager: Serial1 Initialized.");
  // Optional: Add a small delay or flush Serial1 buffer
   delay(100);
   while(Serial1.available()) Serial1.read();
}

// Returns the last valid distance read by sensorTask
unsigned int getSensorDistance() {
  return g_sensorDistance;
}


// Reads data from Serial1 according to the defined sensor protocol
// Returns the new distance if valid, otherwise returns the last known global distance.
unsigned int readSensorData() {
#ifdef SIMULATE_SENSOR
  // --- Sensor Simulation Logic ---
  static unsigned int simulatedDistance = MIN_DISTANCE;
  static int simDirection = 10;
  simulatedDistance += simDirection;
  if (simulatedDistance >= MAX_DISTANCE || simulatedDistance <= MIN_DISTANCE) {
      simDirection *= -1; // Reverse direction at bounds
      simulatedDistance += simDirection; // Ensure it's within bounds after reversing
  }
  simulatedDistance = constrain(simulatedDistance, MIN_DISTANCE, MAX_DISTANCE);
  // Add some simulated noise occasionally
  if(random(10) == 0) return simulatedDistance + random(-NOISE_THRESHOLD/2, NOISE_THRESHOLD/2);
  return simulatedDistance;

#else
  // --- Real Sensor Reading Logic ---
  const int requiredBytes = 7; // Header (2) + Data (5)

  // 1. Check for sufficient data
  if (Serial1.available() < requiredBytes) {
     // Not enough data yet
     return g_sensorDistance; // Return last known value
  }

  // 2. Check for Header bytes (0xAA 0xAA)
  if (Serial1.read() != SENSOR_HEADER) {
     // First byte is not header, potentially misaligned data stream.
     // Flush buffer to resynchronize.
     Serial.println("SENSOR: Sync byte 1 mismatch, flushing...");
     while (Serial1.available()) Serial1.read();
     return g_sensorDistance; // Return last known value
  }

  if (Serial1.read() != SENSOR_HEADER) {
     // Second byte is not header.
     Serial.println("SENSOR: Sync byte 2 mismatch, flushing...");
     while (Serial1.available()) Serial1.read();
     return g_sensorDistance; // Return last known value
  }

  // 3. Read the data payload (5 bytes expected based on original code)
  byte buf[5];
  size_t bytesRead = Serial1.readBytes(buf, 5);

  if (bytesRead < 5) {
     // Failed to read the full payload after header.
     Serial.println("SENSOR: Incomplete payload read.");
      // It's possible more data will arrive, don't flush here unless necessary
     return g_sensorDistance; // Return last known value
  }

  // 4. Process the payload (Assuming format: LowByte, HighByte, Strength?, Checksum?)
  // Original code used buf[1] and buf[2] for distance - adjust if sensor manual differs.
  // Assuming Distance = HighByte << 8 | LowByte
  unsigned int distance = (buf[2] << 8) | buf[1];

  // 5. Validate the distance reading
  if (distance < MIN_DISTANCE || distance > MAX_DISTANCE) {
      // Reading is outside the expected valid range.
      // Serial.printf("SENSOR: Distance out of range (%u)\n", distance); // Optional debug
      return g_sensorDistance; // Return last known value, assuming invalid read
  }

  // Optional: Checksum validation if protocol includes it (e.g., using buf[3], buf[4])
  // byte checksum = calculateChecksum(buf, 4); // Example
  // if (checksum != buf[4]) { /* Handle checksum error */ return g_sensorDistance; }


  // 6. Return the valid distance
  return distance;

#endif // SIMULATE_SENSOR
}

// Task function that continuously reads the sensor
void sensorTask(void * parameter) {
  Serial.println("Sensor Manager: Task Started.");
  for (;;) {
    unsigned int newDistance = readSensorData();

    // Update the global distance variable only if the read was potentially valid
    // (readSensorData returns last known value on error, so this check might be redundant
    // depending on readSensorData's implementation, but harmless)
    if (newDistance >= MIN_DISTANCE && newDistance <= MAX_DISTANCE) {
        // Optional: Apply filtering (e.g., moving average) here if needed
        g_sensorDistance = newDistance;
    } else {
        // Keep the old value if the new reading was invalid (out of range)
        // g_sensorDistance remains unchanged
    }


    // Delay slightly to prevent busy-waiting and allow other tasks to run.
    // The delay depends on how fast the sensor updates and how responsive you need the system to be.
    vTaskDelay(pdMS_TO_TICKS(5)); // e.g., check sensor roughly every 5ms
  }
}