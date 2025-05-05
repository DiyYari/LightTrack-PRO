#include "wifi_manager.h"
#include "config.h"
#include "storage.h" // Assumes saveWiFiSettings, getWiFiSsid, etc. exist
#include "web_server.h" // For server instance access
#include <WiFi.h>
#include "esp_wifi.h" // For esp_wifi_set_max_tx_power

// Store the unique device name generated
static String deviceName = "";
// Store credentials loaded from storage or set via web
static String currentSsid = "";
static String currentPassword = "";

// --- Public Functions ---

// Gets the unique device name (used for AP, Hostname, MQTT Client ID)
String getDeviceName() {
  if (deviceName.length() == 0) {
      // Generate unique suffix based on MAC address part
      uint8_t mac[6];
      esp_read_mac(mac, ESP_MAC_WIFI_STA); // Read station MAC
      char macSuffix[7]; // Last 3 bytes of MAC + null terminator
      snprintf(macSuffix, sizeof(macSuffix), "%02X%02X%02X", mac[3], mac[4], mac[5]);
      deviceName = String(AP_SSID_PREFIX) + "_" + String(macSuffix);
  }
  return deviceName;
}

// Attempts to connect to WiFi using stored/provided credentials
bool connectToWiFi(const String& ssid, const String& password) {
    if (ssid.length() == 0) {
        Serial.println("WIFI: No SSID configured.");
        return false;
    }

    Serial.print("WIFI: Connecting to SSID: ");
    Serial.println(ssid);

    // Set mode to Station + AP for connection attempts and fallback
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (millis() - startTime > WIFI_CONNECT_TIMEOUT) {
            Serial.println("\nWIFI: Connection timed out!");
            WiFi.disconnect(false); // Don't erase credentials from SDK memory
            return false;
        }
    }

    Serial.println("\nWIFI: Connected!");
    Serial.print("WIFI: IP Address: ");
    Serial.println(WiFi.localIP());
    currentSsid = ssid; // Update current credentials on successful connection
    currentPassword = password;
    return true;
}

// Starts the Access Point
void startAccessPoint() {
  Serial.println("WIFI: Starting Access Point (AP) mode.");
  WiFi.mode(WIFI_AP_STA); // Keep STA mode active in case connection works later

  // Configure AP IP address etc.
  IPAddress local_IP(192, 168, 4, 1); // Standard AP IP
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);

  // Start the AP with the unique device name
  String apName = getDeviceName();
  bool apStarted = WiFi.softAP(apName.c_str(), AP_PASSWORD);

  if(apStarted) {
      Serial.print("WIFI: AP Started. SSID: ");
      Serial.print(apName);
      Serial.print(" | Password: ");
      Serial.print(AP_PASSWORD);
      Serial.print(" | IP: ");
      Serial.println(WiFi.softAPIP());

      // Optionally reduce WiFi power in AP mode if range isn't critical
      // esp_wifi_set_max_tx_power(20); // Lower power (~5dBm), check SDK docs for values
  } else {
      Serial.println("WIFI: Failed to start Access Point!");
  }
}


// --- Setup and Handlers ---

// Initialize WiFi: Load settings, try to connect, start AP if fails
void setupWiFi() {
  WiFi.persistent(false); // Prevent SDK from saving credentials automatically
  WiFi.disconnect(true);  // Disconnect and clear SDK memory from previous sessions
  delay(100);

  getDeviceName(); // Ensure device name is generated
  Serial.print("WIFI: Device Name: "); Serial.println(deviceName);

  // Load saved credentials from storage (if they exist)
  if (hasWiFiSettings()) { // Assumed function from storage.h/cpp
      currentSsid = getStoredWiFiSsid();   // Assumed function
      currentPassword = getStoredWiFiPassword(); // Assumed function
      if (connectToWiFi(currentSsid, currentPassword)) {
          // Successfully connected using saved credentials
          // Optionally reduce WiFi power now if connection is stable
          // esp_wifi_set_max_tx_power(40); // Moderate power (~10dBm)
          return; // Done with WiFi setup
      } else {
          // Connection failed with saved credentials
          Serial.println("WIFI: Failed to connect using saved credentials.");
          currentSsid = ""; // Clear potentially invalid credentials
          currentPassword = "";
          // Proceed to start AP mode below
      }
  } else {
      Serial.println("WIFI: No saved credentials found.");
      currentSsid = "";
      currentPassword = "";
      // Proceed to start AP mode
  }

  // Start AP mode as fallback or if no credentials were saved/valid
  startAccessPoint();
}


// Web Handler for WiFi Settings Page (/wifi)
void handleWiFiSettings() {
  String html = "<!DOCTYPE html><html><head><title>WiFi Settings</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
     "<style>"
      "body{background-color:#282c34;color:#abb2bf;font-family:sans-serif;margin:0;padding:15px;}"
      ".container{max-width:600px;margin:auto;}"
      "h1{color:#61afef;text-align:center;}"
      "label{display:block;margin-top:15px;margin-bottom:5px;color:#c678dd;}"
      "input[type=text], input[type=password]{width:calc(100% - 22px);background-color:#3e4451;color:#abb2bf;border:1px solid #5c6370;padding:10px;font-size:1em;border-radius:4px;}"
      "input[type=submit]{background-color:#98c379;color:#282c34;border:none;padding:10px 20px;margin-top:20px;cursor:pointer;font-size:1em;border-radius:4px;}"
      "input[type=submit]:hover{background-color:#a9d18e;}"
      ".form-group{margin-bottom:20px;padding:15px;background-color:#323842;border-radius:5px;}"
      "a{color:#61afef;text-decoration:none;display:block;text-align:center;margin-top:20px;}"
      "a:hover{text-decoration:underline;}"
      ".status, .scan-results{background-color:#3e4451;padding:10px;margin-bottom:20px;border-radius:4px;}"
      ".scan-results ul { list-style: none; padding: 0; } "
      ".scan-results li { padding: 5px 0; border-bottom: 1px solid #5c6370; cursor: pointer; }"
      ".scan-results li:last-child { border-bottom: none; }"
      ".scan-results li:hover { background-color: #5c6370; }"
      ".hidden { display: none; }"
    "</style>"
     "<script>"
      "function selectSSID(ssid) { document.getElementById('ssid').value = ssid; }"
      "function startScan() { document.getElementById('scanBtn').innerText = 'Scanning...'; document.getElementById('scanBtn').disabled = true; fetch('/wifi?scan=1').then(response => response.text()).then(html => { document.body.innerHTML = html; }).catch(err => { console.error('Scan failed:', err); alert('WiFi scan failed.'); document.getElementById('scanBtn').innerText = 'Scan Networks'; document.getElementById('scanBtn').disabled = false; }); }"
    "</script>"
    "</head><body>"
    "<div class='container'>"
      "<h1>WiFi Settings</h1>";

  // Display current status
  html += "<div class='status'>";
  if (WiFi.status() == WL_CONNECTED) {
    html += "Status: Connected to <strong>" + WiFi.SSID() + "</strong> (IP: " + WiFi.localIP().toString() + ")";
  } else {
    html += "Status: Disconnected.";
    if (WiFi.getMode() & WIFI_AP) {
        html += " Access Point mode is active (SSID: " + getDeviceName() + ")";
    }
  }
   html += "</div>";


   // WiFi Connection Form
   html += "<form action='/savewifi' method='post'>"
        "<div class='form-group'>"
        "<label for='ssid'>Network Name (SSID):</label>"
        "<input type='text' id='ssid' name='ssid' value='" + currentSsid + "' required placeholder='Select from scan or type SSID'>" // Pre-fill with current SSID
        "<label for='password'>Password:</label>"
        "<input type='password' id='password' name='password' placeholder='Enter WiFi password'>" // Don't pre-fill password
        "<input type='submit' value='Save & Connect'>"
         "</div>"
      "</form>";

    // WiFi Scan Section
    html += "<div class='scan-results'>";
    html += "<h2>Available Networks</h2>";
    html += "<button id='scanBtn' onclick='startScan()'>Scan Networks</button>";
    html += "<ul id='networkList'>";

    // Check if scan was requested
    if (server.hasArg("scan")) {
        Serial.println("WIFI: Starting network scan...");
        int n = WiFi.scanNetworks();
        Serial.print("WIFI: Scan finished, found "); Serial.print(n); Serial.println(" networks.");
        if (n == 0) {
            html += "<li>No networks found.</li>";
        } else {
            for (int i = 0; i < n; ++i) {
                // Display SSID, Signal Strength, and Security Type
                html += "<li onclick='selectSSID(\"" + WiFi.SSID(i) + "\")'>";
                html += WiFi.SSID(i);
                html += " (" + String(WiFi.RSSI(i)) + " dBm)";
                html += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " [Open]" : " [Secure]";
                html += "</li>";
            }
        }
    } else {
         html += "<li style='color:#aaa;'>Click 'Scan Networks' to see available networks.</li>";
    }
    html += "</ul></div>"; // End scan-results


  html += "<a href='/'>← Back to main page</a>"
    "</div></body></html>";

  server.send(200, "text/html", html);
}

// Web Handler for Saving WiFi Settings (/savewifi)
void handleWiFiSave() {
  String newSsid = server.arg("ssid");
  String newPassword = server.arg("password");

  Serial.println("WIFI: Received new WiFi credentials via web.");
  Serial.print("WIFI: SSID: "); Serial.println(newSsid);
  // Avoid printing password to Serial: Serial.print(" Password: "); Serial.println(newPassword);

  if (newSsid.length() > 0) {
      // Save the new credentials to persistent storage
      Serial.println("WIFI: Saving credentials...");
      saveWiFiSettings(newSsid.c_str(), newPassword.c_str()); // Assumed function in storage.h/cpp

      // Attempt to connect with the new credentials
      connectToWiFi(newSsid, newPassword);

  } else {
      Serial.println("WIFI: Received empty SSID, cannot save or connect.");
      // Optionally clear saved settings here if desired
      // clearWiFiSettings(); // Assumed function
  }

  // Redirect back to the WiFi settings page to show status
  server.sendHeader("Location", "/wifi");
  server.send(303);
}