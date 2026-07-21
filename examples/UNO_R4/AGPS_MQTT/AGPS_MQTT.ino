#include <Arduino.h>
#include <ME310.h>

#define ON_OFF 2 /* Select the GPIO to control ON_OFF */
#define MODEM_SERIAL Serial1
#define DEBUG_SERIAL Serial

using namespace me310;

ME310 myME310;
ME310::return_t rc;

#define HOSTNAME "broker.emqx.io"
#define PORT 1883
int cID = 1;                      // PDP Context Identifier
#define CLIENT_ID "czme310g1001"  //unique value
#define CLIENT_USERNAME ""
#define CLIENT_PASSWORD ""
#define PUB_TOPIC "czme310/topic/put"

// Function Declarations
bool sendAT(String cmd, String expected, uint32_t timeout = 2000, String* response = nullptr);
String getCSVElement(String data, char separator, int index);
double nmeaToDecimalDegrees(String nmeaString);
void checkAndSetupAGPS();
void connectNetworkAndSyncTime();
void setupMQTT();
void transferGPSCoordinates();
void cleanupAndPowerOff();

void setup() {
  DEBUG_SERIAL.begin(115200);
  MODEM_SERIAL.begin(115200);
  delay(100);

  myME310.debugMode(true);
  myME310.powerOn(ON_OFF);

  DEBUG_SERIAL.println("=== ME310G1-W3 AGPS & MQTT 3-Cycle Publisher ===");

  // 1. AGPS Initialization Procedure
  checkAndSetupAGPS();

  // 2. Network Connection and NTP Time Synchronization for AGPS
  connectNetworkAndSyncTime();

  // 3. MQTT Configuration and Connection
  setupMQTT();

  // 4. GPS Coordinate Acquisition and MQTT Transmission (Executes 3 cycles)
  for (int i = 1; i <= 3; i++) {
    DEBUG_SERIAL.println("\n--------------------------------------------------");
    DEBUG_SERIAL.print(">>> [GPS Transfer Cycle ");
    DEBUG_SERIAL.print(i);
    DEBUG_SERIAL.println(" / 3 Started] <<<");
    DEBUG_SERIAL.println("--------------------------------------------------");

    transferGPSCoordinates();

    // Skip delay on the last iteration and proceed to cleanup immediately
    if (i < 3) {
      DEBUG_SERIAL.println("-> Waiting 3 seconds before next cycle...");
      delay(3000);
    }
  }

  // 5. Safe Session Disconnection and Power-Down
  cleanupAndPowerOff();
}

void loop() {
  // Empty loop as the 3-cycle routine completes inside setup()
}

// ----------------------------------------------------
// 1. AGPS Setup and Module Reboot Control
// ----------------------------------------------------
void checkAndSetupAGPS() {
  while (MODEM_SERIAL.available() > 0) {
    MODEM_SERIAL.read();
    delay(1); // Drain remaining bytes thoroughly
  }

  String res = "";
  DEBUG_SERIAL.println("[Step 1-1] Checking AGPS configuration status...");
  sendAT("AT$AGNSS?", "OK", 2000, &res);

  if (res.indexOf("$AGNSS: 0,1,1") != -1) {
    DEBUG_SERIAL.println("-> AGPS is already enabled. Proceeding immediately.");
    return;
  }

  DEBUG_SERIAL.println("-> AGPS configuration required. Initializing settings...");
  sendAT("AT$AGNSS=0,1", "OK");
  sendAT("AT$AGNSSCFG=0,0,0", "OK");
  sendAT("AT$AGNSS?", "OK");

  DEBUG_SERIAL.println("[Step 1-5] Rebooting module to apply AGPS settings (AT#REBOOT)...");
  MODEM_SERIAL.println("AT#REBOOT");
  delay(5000);

  while (true) {
    if (sendAT("AT", "OK", 1000)) {
      DEBUG_SERIAL.println("-> Module reboot complete and responding.");
      break;
    }
    DEBUG_SERIAL.println("-> Awaiting module response...");
    delay(1000);
  }
}

// ----------------------------------------------------
// 2. Network Connection and NTP Synchronization
// ----------------------------------------------------
void connectNetworkAndSyncTime() {
  String res = "";
  DEBUG_SERIAL.println("[Step 2-1] Final AGPS verification post-reboot...");
  sendAT("AT$AGNSS?", "$AGNSS: 0,1,1", 3000);

  DEBUG_SERIAL.println("[Step 2-2] Checking network registration status...");
  while (true) {
    sendAT("AT+CEREG?", "OK", 2000, &res);
    if (res.indexOf("+CEREG: 0,1") != -1 || res.indexOf("+CEREG: 0,5") != -1) {
      DEBUG_SERIAL.println("-> Cellular network connected successfully!");
      break;
    }
    delay(2000);
  }

  DEBUG_SERIAL.println("[Step 2-3] Requesting PDP context activation (IP allocation)...");
  DEBUG_SERIAL.println("Activate context");
  myME310.context_activation(cID, 1,ME310::TOUT_5SEC);
  sendAT("AT#NTPCFG=1,0", "OK");

  DEBUG_SERIAL.println("[Step 2-5] Synchronizing NTP time...");
  sendAT("AT#NTP=\"216.239.35.0\",123,1,5,36", "OK", 10000);
}

// ----------------------------------------------------
// 3. Initial MQTT Setup and Server Connection
// ----------------------------------------------------
void setupMQTT() {
  char *resp;

  DEBUG_SERIAL.println("mqtt enable read");
  rc = myME310.read_mqtt_enable();
  DEBUG_SERIAL.println(myME310.buffer_cstr(1));
  resp = (char*)myME310.buffer_cstr(1);

  if (resp != NULL && (strcmp(resp, "#MQEN: 1,0")) == 0) {
    DEBUG_SERIAL.println("mqtt enable");
    rc = myME310.mqtt_enable(1, 1);
    DEBUG_SERIAL.println(myME310.buffer_cstr(1));
    rc = myME310.read_mqtt_enable();
    DEBUG_SERIAL.println(myME310.buffer_cstr(1));
  }

  DEBUG_SERIAL.print("mqtt configure:");
  rc = myME310.mqtt_configure(1, HOSTNAME, PORT, cID);
  DEBUG_SERIAL.println(myME310.buffer_cstr(1));
  delay(1000);

  if (rc == ME310::RETURN_VALID) {
    DEBUG_SERIAL.print("mqtt connect: ");
    rc = myME310.mqtt_connect(1, CLIENT_ID, CLIENT_USERNAME, CLIENT_PASSWORD, ME310::TOUT_1MIN);
    DEBUG_SERIAL.println(myME310.buffer_cstr(1));

    if (rc == ME310::RETURN_VALID) {
      // MQTT Topic Publish
      DEBUG_SERIAL.print("MQTT Publish: ");
      myME310.mqtt_publish(1, PUB_TOPIC, 1, 0, "AGPS Publish Ready");
      DEBUG_SERIAL.println(myME310.buffer_cstr(1));
    }
  }
}

// ----------------------------------------------------
// 4. GPS Coordinate Acquisition, NMEA Parsing, and MQTT Publish
// ----------------------------------------------------
void transferGPSCoordinates() {
  String res = "";

  // Change RF priority to GPS (Wait 3 seconds)
  DEBUG_SERIAL.println("-> Setting RF priority to GPS (Waiting 3s)...");
  sendAT("AT$GPSCFG=3,0", "OK");
  delay(3000);

  // Turn on GPS engine (Start AGPS positioning)
  sendAT("AT$GPSP=1", "OK");

  // Wait for GNSS position fix via AT$GNSSACP
  DEBUG_SERIAL.println("-> Waiting for GPS position fix...");
  String rawLat = "";
  String rawLon = "";

  while (true) {
    sendAT("AT$GNSSACP", "OK", 3000, &res);

    int p_start = res.indexOf("$GNSSACP:");
    if (p_start != -1) {
      int p_end = res.indexOf("\r", p_start);
      String line = res.substring(p_start, p_end);

      // Extract 8th parameter (Index 7: Fix Status)
      String fixStatus = getCSVElement(line, ',', 7);
      int fixVal = fixStatus.toInt();

      DEBUG_SERIAL.print("-> Current Fix Status: ");
      DEBUG_SERIAL.println(fixVal);

      // Extract coordinates if 2D Fix (2) or 3D Fix (3) or higher is achieved
      if (fixVal >= 2) {
        rawLat = getCSVElement(line, ',', 2);  // Raw Latitude
        rawLon = getCSVElement(line, ',', 3);  // Raw Longitude
        DEBUG_SERIAL.println("-> [SUCCESS] Raw NMEA coordinates acquired!");
        break;
      }
    }
    delay(2000);
  }

  // Change RF priority back to LTE (Wait 3 seconds)
  sendAT("AT$GPSP=0", "OK");
  DEBUG_SERIAL.println("-> Setting RF priority to LTE (Waiting 3s)...");
  sendAT("AT$GPSCFG=3,1", "OK");
  delay(3000);

  // Convert NMEA format to Decimal Degrees (DD)
  double decimalLat = nmeaToDecimalDegrees(rawLat);
  double decimalLon = nmeaToDecimalDegrees(rawLon);

  DEBUG_SERIAL.print("Raw Latitude: ");
  DEBUG_SERIAL.println(rawLat);
  DEBUG_SERIAL.print("Converted Latitude (DD): ");
  DEBUG_SERIAL.println(decimalLat, 6);
  DEBUG_SERIAL.print("Raw Longitude: ");
  DEBUG_SERIAL.println(rawLon);
  DEBUG_SERIAL.print("Converted Longitude (DD): ");
  DEBUG_SERIAL.println(decimalLon, 6);

  // Format payload string with 6 decimal places precision
  String payload = String(decimalLat, 6) + "," + String(decimalLon, 6);
  myME310.mqtt_publish(1, PUB_TOPIC, 1, 0, payload.c_str());

  // Publish DD coordinates via MQTT
  DEBUG_SERIAL.println("-> Publishing converted DD coordinates to MQTT broker...");
}

// ----------------------------------------------------
// 5. Session Cleanup and Safe Shutdown
// ----------------------------------------------------
void cleanupAndPowerOff() {
  DEBUG_SERIAL.println("\n==================================================");
  DEBUG_SERIAL.println("=== [CLEANUP] 3 Cycles Complete: Initiating Shutdown ===");
  DEBUG_SERIAL.println("==================================================");

  myME310.mqtt_disconnect(1, ME310::TOUT_3SEC);
  myME310.mqtt_enable(1, 0, ME310::TOUT_3SEC);
  myME310.context_activation(cID, 0, "", "", ME310::TOUT_3SEC);
  DEBUG_SERIAL.println("\n=== All MQTT Public Procedures Done ===");

  // Power off the modem right before leaving the setup sequence
  myME310.powerOff(ON_OFF);
  DEBUG_SERIAL.println("ME310G1 Modem Power Off");
  DEBUG_SERIAL.println("\n=== All tasks and power-off procedures completed successfully. ===");

  while (true) {
    // Halt further execution
    delay(1000);
  }
}

// ----------------------------------------------------
// 🛠️ Utility & Helper Functions
// ----------------------------------------------------

// Converts NMEA formatted coordinates string to Decimal Degrees (DD)
double nmeaToDecimalDegrees(String nmeaString) {
  int dotIndex = nmeaString.indexOf('.');
  if (dotIndex == -1) return 0.0;

  int minIndex = dotIndex - 2;

  String degStr = nmeaString.substring(0, minIndex);
  String minStr = nmeaString.substring(minIndex);

  int degrees = degStr.toInt();
  double minutes = minStr.toFloat();

  double decimalDegrees = degrees + (minutes / 60.0);

  if (nmeaString.endsWith("S") || nmeaString.endsWith("W")) {
    decimalDegrees *= -1.0;
  }

  return decimalDegrees;
}

// Helper function to send AT commands and await expected response
bool sendAT(String cmd, String expected, uint32_t timeout, String* response) {
  while (MODEM_SERIAL.available()) MODEM_SERIAL.read();

  DEBUG_SERIAL.print("[TX]: ");
  DEBUG_SERIAL.println(cmd);
  MODEM_SERIAL.println(cmd);

  String resp = "";
  uint32_t startMilli = millis();

  while (millis() - startMilli < timeout) {
    while (MODEM_SERIAL.available()) {
      char c = MODEM_SERIAL.read();
      resp += c;
    }
    if (resp.indexOf(expected) != -1) {
      if (response) *response = resp;
      DEBUG_SERIAL.print("[RX]: ");
      DEBUG_SERIAL.println(resp);
      return true;
    }
  }

  if (response) *response = resp;
  DEBUG_SERIAL.print("[RX (TIMEOUT)]: ");
  DEBUG_SERIAL.println(resp);
  return false;
}

// Utility function to extract CSV element by index
String getCSVElement(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = { 0, -1 };
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}