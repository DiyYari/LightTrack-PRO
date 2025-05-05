#include "led_controller.h"
#include "config.h"       // For LED_PIN, NUM_LEDS, CHIPSET, COLOR_ORDER, MIN/MAX_DISTANCE etc.
#include "storage.h"      // For getting current LED parameters (color, intensity, length etc.)
#include "sensor_manager.h" // For getting the current distance

// Global LED array managed by FastLED
CRGB leds[NUM_LEDS];

// Initializes the FastLED library and clears the strip
void initLEDController() {
  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip); // Added color correction
  FastLED.setBrightness(255); // Use global brightness if needed, otherwise control via intensity factors
  FastLED.clear(true); // Clear data and push to strip
  Serial.println("LED Controller: Initialized.");
}


// Main task controlling the LED strip based on sensor data and settings
void ledTask(void * parameter) {
  unsigned int lastSensorDistance = getSensorDistance(); // Initialize last distance
  int lastMovementDirection = 0; // 0 = stationary, 1 = away, -1 = towards
  unsigned long lastMovementTime = millis(); // Time of last detected significant movement

  for (;;) { // Infinite loop for the task
    unsigned long currentMillis = millis();
    unsigned int currentDistance = getSensorDistance(); // Get the latest distance

    // Calculate difference and detect movement direction
    int diff = (int)currentDistance - (int)lastSensorDistance;
    int absDiff = abs(diff);

    // Update movement state if change exceeds noise threshold
    if (absDiff >= NOISE_THRESHOLD) {
      lastMovementTime = currentMillis;
      lastMovementDirection = (diff > 0) ? 1 : -1; // 1 = moving away, -1 = moving towards
      // Serial.print("Movement detected! Dir: "); Serial.println(lastMovementDirection); // Debug
    }

    // Update last known distance
    lastSensorDistance = currentDistance;

    // Determine if the moving light effect should be active based on time since last movement
    bool showMovingEffect = (currentMillis - lastMovementTime <= (unsigned long)getLedOffDelay() * 1000);

    // --- Main Light Logic ---

    if (!isLightOn()) {
      // If the light is globally turned off, ensure the strip is black
      // Use fadeToBlackBy for smoother transitions if needed, otherwise just clear.
       if(leds[0] != CRGB::Black) { // Only clear if not already black
            fill_solid(leds, NUM_LEDS, CRGB::Black);
            FastLED.show(); // Update strip immediately when turned off
       }
       // No need to calculate effects if light is off, delay and loop again
       vTaskDelay(pdMS_TO_TICKS(getUpdateInterval()));
       continue; // Skip the rest of the loop
    }

    // Light is ON, proceed with effects

    // 1. Apply Background Light (if active)
    CRGB baseBgColor = getBaseColor();
    float bgIntensity = getStationaryIntensity();
    if (isBackgroundModeActive() && bgIntensity > 0.0f) {
      fill_solid(leds, NUM_LEDS, baseBgColor.scale8(bgIntensity * 255.0f));
    } else {
      // If no background mode, start with a black strip (or fade existing)
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      // fadeToBlackBy(leds, NUM_LEDS, 40); // Optional: Fade instead of hard clear
    }

    // 2. Apply Moving Light Effect (if motion detected recently)
    if (showMovingEffect) {
      CRGB movingColor = getBaseColor(); // Use base color for moving light
      float movingIntensity = getMovingIntensity();
      int movingLength = getMovingLength();
      int centerShift = getCenterShift();
      int additionalLeds = getAdditionalLEDs(); // Trail/spread

      // Calculate position based on distance (normalized 0.0 to 1.0)
      float proportion = constrain((float)(currentDistance - MIN_DISTANCE) / (MAX_DISTANCE - MIN_DISTANCE), 0.0f, 1.0f);

      // Map proportion to LED index (adjust based on NUM_LEDS and movingLength)
      int baseLedPosition = round(proportion * (NUM_LEDS - 1)); // Simple mapping, adjust if needed

      // Apply center shift
      int centerLED = constrain(baseLedPosition + centerShift, 0, NUM_LEDS - 1);

      // Calculate main beam parameters
      int halfLength = movingLength / 2;
      int startLed = constrain(centerLED - halfLength, 0, NUM_LEDS - 1);
      int endLed = constrain(centerLED + halfLength - (movingLength % 2 == 0 ? 1 : 0), 0, NUM_LEDS - 1); // Inclusive end

      // --- Draw the main moving beam ---
      if (movingLength > 0 && movingIntensity > 0.0f) {
        int fadeWidth = min(halfLength, 5); // Width of fade at edges (e.g., 5 LEDs)

        for (int i = startLed; i <= endLed; ++i) {
          float factor = 1.0f; // Default intensity factor

          // Calculate fade factor based on position within the beam
          if (fadeWidth > 0 && movingLength > 1) {
             int distFromStart = i - startLed;
             int distFromEnd = endLed - i;
             if (distFromStart < fadeWidth) {
                 factor = (float)distFromStart / fadeWidth;
             } else if (distFromEnd < fadeWidth) {
                  factor = (float)distFromEnd / fadeWidth;
             }
          }

          // Apply color, intensity, and fade factor
          // Use blend or add depending on whether background is present
          if (isBackgroundModeActive() && bgIntensity > 0.0f) {
             leds[i] += movingColor.scale8(movingIntensity * factor * 255.0f); // Additive blend
          } else {
             leds[i] = movingColor.scale8(movingIntensity * factor * 255.0f); // Overwrite black background
          }
        }
      }

      // --- Draw the additional trail/spread LEDs ---
      if (lastMovementDirection != 0 && additionalLeds > 0 && movingIntensity > 0.0f) {
        int fadeWidthAdditional = min(additionalLeds / 2, 5); // Fade width for the trail

        for (int i = 1; i <= additionalLeds; ++i) {
          // Calculate index based on movement direction
          int idx = (lastMovementDirection > 0) ? endLed + i : startLed - i;

          // Check bounds
          if (idx < 0 || idx >= NUM_LEDS) continue;

          // Calculate fade factor for the trail
          float factor = 1.0f;
           if (fadeWidthAdditional > 0 && additionalLeds > 1) {
                int distFromMainBeam = i;
                if (distFromMainBeam > (additionalLeds - fadeWidthAdditional)) {
                    factor = (float)(additionalLeds - distFromMainBeam) / fadeWidthAdditional;
                }
           }


          // Apply color, intensity, and fade factor (blend or overwrite)
           if (isBackgroundModeActive() && bgIntensity > 0.0f) {
               leds[idx] += movingColor.scale8(movingIntensity * factor * 0.8f * 255.0f); // Trail slightly dimmer
           } else {
               leds[idx] = movingColor.scale8(movingIntensity * factor * 0.8f * 255.0f);
           }
        }
      }
    } // End if (showMovingEffect)

    // --- Update the physical LED strip ---
    FastLED.show();

    // --- Delay before next iteration ---
    vTaskDelay(pdMS_TO_TICKS(getUpdateInterval())); // Use configured update interval
  } // End infinite loop
}