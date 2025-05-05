#include "web_server.h"
#include "config.h"
#include "storage.h" // Assumes functions for saving/loading all params
#include "sensor_manager.h"
#include "wifi_manager.h" // Include for WiFi settings handlers
#include "home_assistant.h" // Include for MQTT settings handlers
#include <time.h>
#include <WiFi.h>
#include <stdio.h>
#include <ArduinoJson.h> // Include for JSON handling

// Web Server instance
WebServer server(80);

// Smart home override flag (volatile as it can be changed by MQTT callback/interrupt)
volatile bool smarthomeOverride = false;

// Forward declarations for HTTP handlers
void handleRoot();
void handleSetInterval();
void handleSetLedOffDelay();
void handleSetBaseColor();
void handleSetMovingIntensity();
void handleSetStationaryIntensity();
void handleSetMovingLength();
void handleSetAdditionalLEDs();
void handleSetCenterShift();
void handleSetTime();
void handleSetSchedule();
void handleNotFound();
void handleDebugPage();
void handleGetSensorData();
void handleSmartHomeOn();
void handleSmartHomeOff();
void handleSmartHomeClear();
void handleToggleBackgroundMode();
void handleMqttSettings(); // Defined in web_server.cpp now
void handleMqttSave();    // Defined in web_server.cpp now
void handleWiFiSettings(); // Declaration for the handler in wifi_manager.cpp
void handleWiFiSave();     // Declaration for the handler in wifi_manager.cpp
void handleSaveAll();      // Handler to save all settings

// --- Smart Home Override Control ---
bool isSmartHomeOverride() {
  return smarthomeOverride;
}

// Used by MQTT callback or other external controls
void setSmartHomeOverride(bool override) {
    smarthomeOverride = override;
}

// Clears the override, allowing schedule to take control again
void clearSmartHomeOverride() {
  smarthomeOverride = false;
  // Force re-evaluation of time-based state in the next loop iteration
  // (or call the updateTimeBasedState() logic directly if safe)
  Serial.println("WEB: Smart Home override cleared.");
}


// --- Web Server Initialization ---
void initWebServer() {
  // Register HTTP handlers for settings
  server.on("/", HTTP_GET, handleRoot);
  server.on("/setInterval", HTTP_POST, handleSetInterval);
  server.on("/setLedOffDelay", HTTP_POST, handleSetLedOffDelay);
  server.on("/setBaseColor", HTTP_POST, handleSetBaseColor);
  server.on("/setMovingIntensity", HTTP_POST, handleSetMovingIntensity);
  server.on("/setStationaryIntensity", HTTP_POST, handleSetStationaryIntensity);
  server.on("/setMovingLength", HTTP_POST, handleSetMovingLength);
  server.on("/setAdditionalLEDs", HTTP_POST, handleSetAdditionalLEDs);
  server.on("/setCenterShift", HTTP_POST, handleSetCenterShift);
  server.on("/setTime", HTTP_GET, handleSetTime); // Use GET for simple time sync
  server.on("/setSchedule", HTTP_POST, handleSetSchedule);
  server.on("/toggleNightMode", HTTP_POST, handleToggleBackgroundMode); // Use POST for actions

  // Smart Home override endpoints (using POST)
  server.on("/smarthome/on", HTTP_POST, handleSmartHomeOn);
  server.on("/smarthome/off", HTTP_POST, handleSmartHomeOff);
  server.on("/smarthome/clear", HTTP_POST, handleSmartHomeClear);

  // Debugging and data endpoints
  server.on("/debug", HTTP_GET, handleDebugPage);
  server.on("/getSensorData", HTTP_GET, handleGetSensorData); // Endpoint for AJAX updates

  // WiFi Settings (handlers are in wifi_manager.cpp but routes defined here)
  server.on("/wifi", HTTP_GET, handleWiFiSettings);
  server.on("/savewifi", HTTP_POST, handleWiFiSave);

  // MQTT Settings (handlers defined below)
  server.on("/mqtt", HTTP_GET, handleMqttSettings);
  server.on("/savemqtt", HTTP_POST, handleMqttSave);

  // Save All Settings
  server.on("/saveall", HTTP_POST, handleSaveAll);

  // Not Found Handler
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Web Server: Started on port 80.");
}

// Web Server Task: Handles incoming client requests
void webServerTask(void * parameter) {
  for (;;) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(2)); // Small delay to yield CPU
  }
}

// --- Specific Page Handlers ---

// Returns a JSON with the current sensor value for the debug graph
void handleGetSensorData() {
  // Use ArduinoJson for cleaner JSON creation
  StaticJsonDocument<128> doc; // Small doc, adjust size if needed
  doc["current"] = getSensorDistance();
  doc["noise_threshold"] = NOISE_THRESHOLD; // Assuming this is still from config.h

  String jsonOutput;
  serializeJson(doc, jsonOutput);

  server.send(200, "application/json", jsonOutput);
}

// Debug page with a live graph
void handleDebugPage() {
  // Using F-strings (PROGMEM) for large HTML strings to save RAM
  String html = FPSTR(
    "<html><head><title>Sensor Debug</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no'>"
    "<style>"
      "body { font-family: Arial, sans-serif; background-color: #282c34; color: #abb2bf; padding: 15px; margin: 0; }"
      "h1 { color: #61afef; text-align: center; }"
      ".chart-container { width: 95%; max-width: 600px; margin: 20px auto; }"
      "canvas { background-color: #21252b; border: 1px solid #3e4451; width: 100%; height: 250px; display: block; }"
      ".data { font-size: 1.1em; margin: 10px 0; text-align: center; color: #98c379; }"
      "a { color: #c678dd; text-decoration: none; display: block; text-align: center; margin-top: 20px; }"
      "a:hover { text-decoration: underline; }"
    "</style>"
    "<script>"
      "let chart, dataPoints = [], maxDataPoints = 100, chartInstance = null;"
      "function initChart() {"
        "const ctx = document.getElementById('sensorChart').getContext('2d');"
        "chart = { canvas: ctx.canvas };" // Simplified chart object
        "setInterval(updateData, 200);" // Update interval (ms)
        "drawChart();" // Initial draw
      "}"
      "function updateData() {"
        "fetch('/getSensorData')"
          ".then(response => response.json())"
          ".then(data => {"
            "document.getElementById('currentValue').textContent = data.current;"
            "const now = new Date();"
            "dataPoints.push({x: now, y: data.current});" // Store time and value
            "if (dataPoints.length > maxDataPoints) dataPoints.shift();"
            "drawChart(data.noise_threshold);"
          "}).catch(error => console.error('Error fetching sensor data:', error));"
      "}"
      // Simple Canvas drawing function (replace with a library like Chart.js for better graphs if needed)
      "function drawChart(noiseThreshold) {"
           "const ctx = document.getElementById('sensorChart').getContext('2d');"
           "const canvas = ctx.canvas;"
           "ctx.clearRect(0, 0, canvas.width, canvas.height);"
           "if (dataPoints.length === 0) return;"

           "// Determine min/max Y values dynamically"
           "let minY = dataPoints[0].y, maxY = dataPoints[0].y;"
           "dataPoints.forEach(p => { minY = Math.min(minY, p.y); maxY = Math.max(maxY, p.y); });"
           "minY = Math.max(0, minY - 50); // Add some padding, ensure min is not negative"
           "maxY = maxY + 50; // Add some padding"
           "const rangeY = maxY - minY;"

           "// Determine min/max X values (time)"
           "const minX = dataPoints[0].x;"
           "const maxX = dataPoints[dataPoints.length - 1].x;"
           "const rangeX = maxX - minX;"

           "const padding = { top: 20, right: 20, bottom: 30, left: 40 };"
           "const plotWidth = canvas.width - padding.left - padding.right;"
           "const plotHeight = canvas.height - padding.top - padding.bottom;"

           "// Draw axes"
           "ctx.strokeStyle = '#5c6370';"
           "ctx.lineWidth = 1;"
           "ctx.beginPath(); ctx.moveTo(padding.left, padding.top); ctx.lineTo(padding.left, canvas.height - padding.bottom); ctx.stroke(); // Y axis"
           "ctx.beginPath(); ctx.moveTo(padding.left, canvas.height - padding.bottom); ctx.lineTo(canvas.width - padding.right, canvas.height - padding.bottom); ctx.stroke(); // X axis"

            "// Draw Y labels and grid lines"
            "ctx.fillStyle = '#abb2bf';"
            "ctx.font = '10px Arial';"
            "ctx.textAlign = 'right';"
            "const yGridLines = 5;"
            "for (let i = 0; i <= yGridLines; i++) {"
                "const val = minY + (rangeY / yGridLines) * i;"
                "const y = canvas.height - padding.bottom - (plotHeight / yGridLines) * i;"
                "ctx.fillText(Math.round(val), padding.left - 5, y + 3);"
                "ctx.strokeStyle = '#3e4451'; // Grid line color"
                "ctx.beginPath(); ctx.moveTo(padding.left, y); ctx.lineTo(canvas.width - padding.right, y); ctx.stroke();"
            "}"

            "// Draw Noise Threshold line"
            "if (noiseThreshold !== undefined && rangeY > 0) {"
               "const yNoise = canvas.height - padding.bottom - ((noiseThreshold - minY) / rangeY * plotHeight);"
               "if (yNoise >= padding.top && yNoise <= canvas.height - padding.bottom) {"
                  "ctx.strokeStyle = '#e5c07b';" // Yellowish color for threshold
                  "ctx.lineWidth = 1;"
                  "ctx.setLineDash([5, 3]);"
                  "ctx.beginPath(); ctx.moveTo(padding.left, yNoise); ctx.lineTo(canvas.width - padding.right, yNoise); ctx.stroke();"
                  "ctx.setLineDash([]);"
                  "ctx.fillStyle = '#e5c07b';"
                  "ctx.fillText('Noise', canvas.width - padding.right, yNoise - 5);"
               "}"
            "}"


           "// Draw data line"
           "if (dataPoints.length > 1 && rangeY > 0 && rangeX > 0) {"
                "ctx.strokeStyle = '#61afef'; // Blueish color for data line"
                "ctx.lineWidth = 2;"
                "ctx.beginPath();"
                "dataPoints.forEach((p, index) => {"
                    "const x = padding.left + ((p.x - minX) / rangeX * plotWidth);"
                    "const y = canvas.height - padding.bottom - ((p.y - minY) / rangeY * plotHeight);"
                    "if (index === 0) { ctx.moveTo(x, y); } else { ctx.lineTo(x, y); }"
                "});"
                "ctx.stroke();"
           "}"
    "}"
    "</script>"
    "</head><body onload='initChart()'>"
      "<h1>Sensor Debug</h1>"
      "<div class='data'>Current Value: <span id='currentValue'>-</span></div>"
      "<div class='chart-container'><canvas id='sensorChart' width='560' height='250'></canvas></div>" // Adjusted canvas size
      "<a href='/'>← Return to main page</a>"
    "</body></html>"
  );
  server.send(200, "text/html", html);
}

// --- Smart Home Integration Endpoints ---
void handleSmartHomeOn() {
  setLightOn(true);
  setSmartHomeOverride(true); // Set override flag
  Serial.println("WEB: Smart Home Override: ON");
  server.send(200, "text/plain", "OK: Smart Home Override ON");
}

void handleSmartHomeOff() {
  setLightOn(false);
  setSmartHomeOverride(true); // Set override flag
  Serial.println("WEB: Smart Home Override: OFF");
  server.send(200, "text/plain", "OK: Smart Home Override OFF");
}

void handleSmartHomeClear() {
  clearSmartHomeOverride(); // Clear the override flag
  // Time-based state will be re-evaluated in the main loop
  server.send(200, "text/plain", "OK: Smart Home Override CLEARED");
}

// --- Settings Handlers ---

// Toggle Background Light Mode Handler
void handleToggleBackgroundMode() {
  toggleBackgroundMode();
  Serial.print("WEB: Background mode "); Serial.println(isBackgroundModeActive() ? "enabled" : "disabled");
  // No redirect needed if called via AJAX, otherwise redirect:
  server.sendHeader("Location", "/");
  server.send(303);
}

// Set system time based on browser's time (epoch + timezone offset)
void handleSetTime() {
  if (server.hasArg("epoch")) {
    unsigned long epoch = server.arg("epoch").toInt();
    // Timezone offset from browser is in minutes WEST of UTC.
    // C time functions expect seconds EAST of UTC.
    int tz_offset_minutes = 0;
    if (server.hasArg("tz")) {
       tz_offset_minutes = server.arg("tz").toInt();
    }
    // Adjust epoch using the timezone offset. Note the sign change.
    // time_t adjusted_epoch = epoch - (tz_offset_minutes * 60); // This might be wrong logic for settimeofday

    // settimeofday expects seconds since UTC epoch. Browser provides UTC epoch.
    if (epoch > 1000000000UL) { // Basic check for valid epoch
      struct timeval tv;
      tv.tv_sec = epoch;
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
      Serial.print("WEB: Time set via browser to epoch: "); Serial.println(epoch);
      // Optionally set Timezone string if needed elsewhere, TZ offset doesn't set system TZ
      // configTzTime(...); // Might need to reconfigure timezone string here if needed

      // Force re-evaluation of time-based state
       clearSmartHomeOverride(); // Clear override when time is manually set
    } else {
         Serial.println("WEB: Invalid epoch received for time setting.");
    }
  }
  server.send(200, "text/plain", "OK");
}

// Set the schedule window (start/end times)
void handleSetSchedule() {
  bool changed = false;
  if (server.hasArg("startHour") && server.hasArg("startMinute")) {
    setStartHour(server.arg("startHour").toInt());
    setStartMinute(server.arg("startMinute").toInt());
    changed = true;
  }
  if (server.hasArg("endHour") && server.hasArg("endMinute")) {
    setEndHour(server.arg("endHour").toInt());
    setEndMinute(server.arg("endMinute").toInt());
    changed = true;
  }
  if(changed) {
    Serial.printf("WEB: Schedule updated to %02d:%02d - %02d:%02d\n",
        getStartHour(), getStartMinute(), getEndHour(), getEndMinute());
    saveScheduleSettings(); // Assumed function in storage.h/cpp
  }
  server.sendHeader("Location", "/"); // Redirect back to main page
  server.send(303);
}

// Generic Not Found handler
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  Serial.println("WEB: 404 Not Found: " + server.uri());
}

// Handlers for individual LED parameter changes (using POST)
void handleSetInterval() {
  if (server.hasArg("value")) {
    setUpdateInterval(server.arg("value").toInt());
    Serial.print("WEB: Update Interval set to "); Serial.println(server.arg("value"));
    saveLedParameters(); // Assumed function
  }
  // Send simple OK for AJAX or redirect for form submit
  // server.send(200, "text/plain", "OK");
  server.sendHeader("Location", "/"); server.send(303);
}

void handleSetLedOffDelay() {
  if (server.hasArg("value")) {
    setLedOffDelay(server.arg("value").toInt());
     Serial.print("WEB: LED Off Delay set to "); Serial.println(server.arg("value"));
     saveLedParameters(); // Assumed function
  }
  server.sendHeader("Location", "/"); server.send(303);
}

void handleSetBaseColor() {
  if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
    CRGB color = CRGB(
      server.arg("r").toInt(),
      server.arg("g").toInt(),
      server.arg("b").toInt()
    );
    setBaseColor(color);
    Serial.printf("WEB: Base Color set to R:%d G:%d B:%d\n", color.r, color.g, color.b);
    saveLedParameters(); // Assumed function
  }
   server.sendHeader("Location", "/"); server.send(303);
}

void handleSetMovingIntensity() {
  if (server.hasArg("value")) {
    setMovingIntensity(server.arg("value").toFloat());
     Serial.print("WEB: Moving Intensity set to "); Serial.println(server.arg("value"));
     saveLedParameters(); // Assumed function
  }
   server.sendHeader("Location", "/"); server.send(303);
}

void handleSetStationaryIntensity() {
  if (server.hasArg("value")) {
    // Input value from slider might be 0-7, needs conversion back to 0-0.07
    // Let's assume the JS sends the correct 0.0 to 1.0 (or similar) float value directly
    setStationaryIntensity(server.arg("value").toFloat());
     Serial.print("WEB: Stationary Intensity set to "); Serial.println(server.arg("value"));
     saveLedParameters(); // Assumed function
  }
   server.sendHeader("Location", "/"); server.send(303);
}

void handleSetMovingLength() {
  if (server.hasArg("value")) {
    setMovingLength(server.arg("value").toInt());
     Serial.print("WEB: Moving Length set to "); Serial.println(server.arg("value"));
     saveLedParameters(); // Assumed function
  }
  server.sendHeader("Location", "/"); server.send(303);
}

void handleSetAdditionalLEDs() {
  if (server.hasArg("value")) {
    setAdditionalLEDs(server.arg("value").toInt());
    Serial.print("WEB: Additional LEDs set to "); Serial.println(server.arg("value"));
    saveLedParameters(); // Assumed function
  }
   server.sendHeader("Location", "/"); server.send(303);
}

void handleSetCenterShift() {
  if (server.hasArg("value")) {
    setCenterShift(server.arg("value").toInt());
     Serial.print("WEB: Center Shift set to "); Serial.println(server.arg("value"));
     saveLedParameters(); // Assumed function
  }
  server.sendHeader("Location", "/"); server.send(303);
}

// Save All Settings Handler
void handleSaveAll() {
  Serial.println("WEB: Received request to save all settings.");
  // Call individual save functions from storage.h/.cpp
  saveWiFiSettings(getWiFiSsid().c_str(), getWiFiPassword().c_str()); // <--- ИСПРАВЛЕНО .c_str()
  saveMqttSettings(getMqttServer().c_str(), getMqttPort(), getMqttUser().c_str(), getMqttPassword().c_str()); // <--- ИСПРАВЛЕНО .c_str()
  saveLedParameters();
  saveScheduleSettings();
  // Add any other settings groups here
  Serial.println("WEB: All settings saved to persistent storage.");
  server.send(200, "text/plain", "OK: All settings saved.");
  // Optionally add a redirect back or confirmation message
  // server.sendHeader("Location", "/"); server.send(303);
}

// --- Main Control Panel Page ---
void handleRoot() {
  char scheduleStartStr[6];
  sprintf(scheduleStartStr, "%02d:%02d", getStartHour(), getStartMinute());

  char scheduleEndStr[6];
  sprintf(scheduleEndStr, "%02d:%02d", getEndHour(), getEndMinute());

  CRGB baseColor = getBaseColor();
  char baseColorHex[8];
  sprintf(baseColorHex, "#%02X%02X%02X", baseColor.r, baseColor.g, baseColor.b);

  // Start building HTML using String concatenation (consider PROGMEM/F-strings for large pages)
  String html = "<!DOCTYPE html><html><head><title>LightTrack Control</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
      "body{background-color:#282c34;color:#abb2bf;font-family:sans-serif;margin:0;padding:15px;}"
      ".container{max-width:800px;margin:auto;}"
      "h1{color:#61afef;text-align:center;}"
      "label{display:block;margin-top:15px;margin-bottom:5px;color:#c678dd;}"
      "input[type=range]{width:100%;cursor:pointer;height:20px;}"
      "input[type=color]{width:80px;height:40px;border:1px solid #5c6370;cursor:pointer;vertical-align:middle;margin-left:10px;}"
      "input[type=time]{background-color:#3e4451;color:#abb2bf;border:1px solid #5c6370;padding:5px;font-size:1em;margin:0 5px;}"
      "button, input[type=submit]{background-color:#98c379;color:#282c34;border:none;padding:10px 15px;margin:10px 5px;cursor:pointer;font-size:1em;border-radius:4px;}"
      "button:hover, input[type=submit]:hover{background-color:#a9d18e;}"
      ".value-display{color:#e5c07b;font-weight:bold;margin-left:10px;}"
      ".color-preview{display:inline-block;width:30px;height:30px;border:1px solid #5c6370;margin-left:10px;vertical-align:middle;}"
      ".form-group{margin-bottom:20px;padding:15px;background-color:#323842;border-radius:5px;}"
      ".nav-links{margin:20px 0;text-align:center;}"
      ".nav-links a{color:#61afef;text-decoration:none;margin:0 10px;}"
      ".nav-links a:hover{text-decoration:underline;}"
      ".status{background-color:#3e4451;padding:10px;margin-bottom:20px;border-radius:4px;text-align:center;}"
      "hr{border:none;height:1px;background-color:#5c6370;margin:30px 0;}"
    "</style>"
    "<script>"
      // Update display value when range slider changes
      "function updateRangeValue(id, value) { document.getElementById(id).innerText = value; }"
      // Update color preview
      "function updateColorPreview(hex) { document.getElementById('colorPreview').style.backgroundColor = hex; }"
      // Debounce function to limit frequent saves on range change
      "const debounce = (func, delay) => { let timeoutId; return function(...args) { clearTimeout(timeoutId); timeoutId = setTimeout(() => { func.apply(this, args); }, delay); }; };"

      // Functions to submit changes via POST requests (using Fetch API)
      "async function postData(url, data) { try { const response = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded', }, body: new URLSearchParams(data) }); if (!response.ok) { console.error('Request failed:', response.statusText); alert('Failed to update setting.'); } } catch (error) { console.error('Fetch error:', error); alert('Error sending request.'); } }"

      "const debouncedSaveColor = debounce((r, g, b) => postData('/setBaseColor', {r, g, b}), 500);"
      "function changeBaseColor(hex) { updateColorPreview(hex); const r = parseInt(hex.substring(1,3),16); const g = parseInt(hex.substring(3,5),16); const b = parseInt(hex.substring(5,7),16); debouncedSaveColor(r, g, b); }"
      "const debouncedSaveFloat = debounce((url, value) => postData(url, {value}), 500);"
      "const debouncedSaveInt = debounce((url, value) => postData(url, {value}), 500);"

      "function setMovingIntensity(val) { updateRangeValue('movingIntensityValue', val); debouncedSaveFloat('/setMovingIntensity', val); }"
      "function setMovingLength(val) { updateRangeValue('movingLengthValue', val); debouncedSaveInt('/setMovingLength', val); }"
      "function setAdditionalLEDs(val) { updateRangeValue('additionalLEDsValue', val); debouncedSaveInt('/setAdditionalLEDs', val); }"
      "function setCenterShift(val) { updateRangeValue('centerShiftValue', val); debouncedSaveInt('/setCenterShift', val); }"
      "function setLedOffDelay(val) { updateRangeValue('ledOffDelayValue', val); debouncedSaveInt('/setLedOffDelay', val); }"
      "function setStationaryIntensity(val) { updateRangeValue('stationaryIntensityValue', val); debouncedSaveFloat('/setStationaryIntensity', val); }" // Assuming slider value is correct float

      "function setSchedule() { const start = document.getElementById('scheduleStartInput').value.split(':'); const end = document.getElementById('scheduleEndInput').value.split(':'); postData('/setSchedule', {startHour: start[0], startMinute: start[1], endHour: end[0], endMinute: end[1]}); }"
      "function toggleBackgroundMode() { postData('/toggleNightMode', {}).then(()=>location.reload()); }" // Reload after toggle
      "function setDeviceTime() { const now = new Date(); const epoch = Math.floor(now.getTime()/1000); const tz = -now.getTimezoneOffset(); fetch('/setTime?epoch='+epoch+'&tz='+tz); console.log('Time sync request sent.'); }"
      "function saveAllSettings() { if(confirm('Save all current settings to persistent storage?')) { postData('/saveall', {}); alert('All settings saved!'); } }"
    "</script>"
    "</head><body onload='setDeviceTime()'>" // Sync time on load
    "<div class='container'>"
      "<h1>LightTrack Control Panel</h1>";

  // WiFi Status
  html += "<div class='status'>";
  if (WiFi.status() == WL_CONNECTED) {
    html += "WiFi: Connected to " + WiFi.SSID() + " | IP: " + WiFi.localIP().toString();
  } else if (WiFi.getMode() & WIFI_AP) {
     html += "WiFi: AP Mode Active (SSID: " + getDeviceName() + ") | IP: " + WiFi.softAPIP().toString();
     html += " | <a href='/wifi' style='color:#e5c07b;'>Connect to WiFi</a>";
  } else {
     html += "WiFi: Disconnected | <a href='/wifi' style='color:#e5c07b;'>WiFi Settings</a>";
  }
   html += "</div>";


  // --- LED Settings ---
  html += "<div class='form-group'>";
  html += "<label for='baseColorPicker'>Base Color:<span class='color-preview' id='colorPreview' style='background-color:" + String(baseColorHex) + ";'></span></label>";
  html += "<input type='color' id='baseColorPicker' value='" + String(baseColorHex) + "' oninput='changeBaseColor(this.value)'>"; // Use oninput for live preview

  html += "<label for='movingIntensity'>Moving Light Intensity:<span class='value-display' id='movingIntensityValue'>" + String(getMovingIntensity()) + "</span></label>";
  html += "<input type='range' id='movingIntensity' min='0' max='1' step='0.01' value='" + String(getMovingIntensity()) + "' oninput='setMovingIntensity(this.value)'>";

  html += "<label for='movingLength'>Moving Light Length (LEDs):<span class='value-display' id='movingLengthValue'>" + String(getMovingLength()) + "</span></label>";
  html += "<input type='range' id='movingLength' min='1' max='" + String(NUM_LEDS) + "' step='1' value='" + String(getMovingLength()) + "' oninput='setMovingLength(this.value)'>";

  html += "<label for='additionalLEDs'>Additional LEDs (spread):<span class='value-display' id='additionalLEDsValue'>" + String(getAdditionalLEDs()) + "</span></label>";
  html += "<input type='range' id='additionalLEDs' min='0' max='" + String(NUM_LEDS / 2) + "' step='1' value='" + String(getAdditionalLEDs()) + "' oninput='setAdditionalLEDs(this.value)'>";

  html += "<label for='centerShift'>Center Shift (LEDs):<span class='value-display' id='centerShiftValue'>" + String(getCenterShift()) + "</span></label>";
  html += "<input type='range' id='centerShift' min='-" + String(NUM_LEDS / 2) + "' max='" + String(NUM_LEDS / 2) + "' step='1' value='" + String(getCenterShift()) + "' oninput='setCenterShift(this.value)'>";

  html += "<label for='ledOffDelay'>LED Off Delay (seconds):<span class='value-display' id='ledOffDelayValue'>" + String(getLedOffDelay()) + "</span></label>";
  html += "<input type='range' id='ledOffDelay' min='1' max='60' step='1' value='" + String(getLedOffDelay()) + "' oninput='setLedOffDelay(this.value)'>";
  html += "</div>";

  // --- Background Mode ---
   html += "<div class='form-group'>";
   html += "<label>Background Light</label>";
   html += "<button onclick='toggleBackgroundMode()'>" + String(isBackgroundModeActive() ? "Disable" : "Enable") + " Background Light</button>";
   if (isBackgroundModeActive()) {
      html += "<label for='stationaryIntensity' style='margin-top:10px;'>Background Intensity:<span class='value-display' id='stationaryIntensityValue'>" + String(getStationaryIntensity()) + "</span></label>";
      // Adjust range/step for background intensity as needed. Example: 0 to 0.1
      html += "<input type='range' id='stationaryIntensity' min='0' max='0.1' step='0.001' value='" + String(getStationaryIntensity()) + "' oninput='setStationaryIntensity(this.value)'>";
   }
   html += "</div>";

  // --- Schedule ---
  html += "<div class='form-group'>";
  html += "<label>Schedule Window (Active Time)</label>";
  html += "<input type='time' id='scheduleStartInput' value='" + String(scheduleStartStr) + "'>";
  html += "<span> to </span>";
  html += "<input type='time' id='scheduleEndInput' value='" + String(scheduleEndStr) + "'>";
  html += "<button onclick='setSchedule()'>Set Schedule</button>";
   html += "<div><small>(Light will be ON between these times unless overridden by Smart Home)</small></div>";
  html += "</div>";

  // --- Navigation and Actions ---
  html += "<div class='nav-links'>";
  html += "<a href='/wifi'>WiFi Settings</a> | ";
  html += "<a href='/mqtt'>MQTT Settings</a> | ";
  html += "<a href='/debug'>Sensor Debug</a>";
  html += "</div>";

  html += "<div style='text-align:center; margin-top:20px;'>";
  html += "<button onclick='saveAllSettings()'>Save All Settings</button>";
  html += "<button onclick=\"postData('/smarthome/clear', {})\">Resume Schedule</button>";
  html += "</div>";

  html += "<div style='text-align:center; font-size:0.8em; margin-top:30px; color:#5c6370;'>LightTrack by DIY Yari</div>";
  html += "</div></body></html>"; // End container and page

  server.send(200, "text/html", html);
}


// --- MQTT Settings Page ---
void handleMqttSettings() {
  String html = "<!DOCTYPE html><html><head><title>MQTT Settings</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
     "<style>"
      "body{background-color:#282c34;color:#abb2bf;font-family:sans-serif;margin:0;padding:15px;}"
      ".container{max-width:600px;margin:auto;}"
      "h1{color:#61afef;text-align:center;}"
      "label{display:block;margin-top:15px;margin-bottom:5px;color:#c678dd;}"
      "input[type=text], input[type=password], input[type=number]{width:calc(100% - 22px);background-color:#3e4451;color:#abb2bf;border:1px solid #5c6370;padding:10px;font-size:1em;border-radius:4px;}"
      "input[type=submit]{background-color:#98c379;color:#282c34;border:none;padding:10px 20px;margin-top:20px;cursor:pointer;font-size:1em;border-radius:4px;}"
      "input[type=submit]:hover{background-color:#a9d18e;}"
       ".form-group{margin-bottom:20px;padding:15px;background-color:#323842;border-radius:5px;}"
       "a{color:#61afef;text-decoration:none;display:block;text-align:center;margin-top:20px;}"
       "a:hover{text-decoration:underline;}"
       ".note{color:#e5c07b;background-color:#3e4451;border:1px solid #e5c07b;padding:10px;margin:15px 0;border-radius:5px;font-size:0.9em;}"
    "</style>"
    "</head><body>"
    "<div class='container'>"
      "<h1>MQTT Settings</h1>";

   // Note about WiFi connection requirement
   if (WiFi.status() != WL_CONNECTED) {
       html += "<div class='note'><strong>Note:</strong> Device must be connected to your WiFi network for MQTT to work. <a href='/wifi' style='color:#e5c07b;'>Configure WiFi</a></div>";
   }

   html += "<form action='/savemqtt' method='post'>"
        "<div class='form-group'>"
        "<label for='server'>MQTT Server Address:</label>"
        "<input type='text' id='server' name='server' value='" + getMqttServer() + "' required placeholder='e.g., 192.168.1.100 or mqtt.example.com'>"
        "<label for='port'>Port:</label>"
        "<input type='number' id='port' name='port' value='" + String(getMqttPort()) + "' required placeholder='e.g., 1883'>"
        "<label for='user'>Username (optional):</label>"
        "<input type='text' id='user' name='user' value='" + getMqttUser() + "'>"
        "<label for='password'>Password (optional):</label>"
        "<input type='password' id='password' name='password' value='" + getMqttPassword() + "'>" // Type password hides input
        "<br>"
        "<input type='submit' value='Save MQTT Settings'>"
        "</div>" // End form-group
      "</form>"
      "<a href='/'>← Back to main page</a>"
    "</div></body></html>";

  server.send(200, "text/html", html);
}

// MQTT Save Handler
void handleMqttSave() {
  bool settingsChanged = false;
  String mqttServer = server.arg("server");
  int mqttPort = server.hasArg("port") ? server.arg("port").toInt() : MQTT_PORT; // Use default from config if missing
  String mqttUser = server.arg("user");
  String mqttPassword = server.arg("password");

  // Basic validation: Check if server address changed
  if (mqttServer != getMqttServer() || mqttPort != getMqttPort() || mqttUser != getMqttUser() || mqttPassword != getMqttPassword()) {
      settingsChanged = true;
      Serial.println("WEB: Saving new MQTT settings.");
      saveMqttSettings(mqttServer.c_str(), mqttPort, mqttUser.c_str(), mqttPassword.c_str()); // Assumed function in storage.h/cpp

      // Apply new settings and trigger reconnection attempt
      setMqttServer(mqttServer); // This function handles enabling/disabling and triggering reconnect
  } else {
      Serial.println("WEB: MQTT settings submitted, but no changes detected.");
  }

  // Redirect back to MQTT settings page
  server.sendHeader("Location", "/mqtt");
  server.send(303);
}