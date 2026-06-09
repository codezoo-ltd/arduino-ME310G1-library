#include <Arduino.h>
#include <ME310.h>
#include <string.h>

// ---------------------------------------------------------
// AWS IoT Core & Network Configuration
// ---------------------------------------------------------
#define APN "simplio.apn"
#define HOSTNAME "your_gateway_address.amazonaws.com"
#define PORT 8883

#define CLIENT_ID "codezoo"
#define CLIENT_USERNAME ""
#define CLIENT_PASSWORD ""

#define ON_OFF 2 /* Select the GPIO to control ON_OFF */
#define MODEM_SERIAL Serial1
#define DEBUG_SERIAL Serial

#define MQTT_PUB "test/reply"
#define MQTT_SUB "test/topic"

using namespace me310;

ME310 myME310;
ME310::return_t rc;

int cID = 1;           // PDP Context Identifier
char ipProt[] = "IP";  // Packet Data Protocol type
int instanceNum = 1;

bool isConnect = false;
String mqttRxBuffer = "";  // Global buffer to accumulate asynchronous URC data

// Helper Function Declarations
bool sendATCommand(const String& cmd, const char* expected, uint32_t timeout);
String sendATCommandRead(const String& cmd, uint32_t timeout);
bool waitForString(const char* expected, uint32_t timeout);

void setup() {
  DEBUG_SERIAL.begin(115200);
  MODEM_SERIAL.begin(115200);
  delay(100);

  myME310.debugMode(true);
  mqttRxBuffer.reserve(256);  // Pre-allocate memory for the URC buffer

  DEBUG_SERIAL.println("\n[INIT] Powering on modem...");
  myME310.powerOn(ON_OFF);
  DEBUG_SERIAL.println("[CLEANUP] Disconnecting MQTTS Session...");
  myME310.mqtt_disconnect(instanceNum, ME310::TOUT_3SEC);
  myME310.mqtt_enable(1, 0, ME310::TOUT_3SEC);
  myME310.context_activation(cID, 0, "", "", ME310::TOUT_3SEC);

  DEBUG_SERIAL.println("======================================");
  DEBUG_SERIAL.println(" ME310G1 AWS IoT MQTTS Secure Test    ");
  DEBUG_SERIAL.println("======================================");

  // Enable verbose error reporting
  myME310.report_mobile_equipment_error(2);

  // Check SIM card status
  rc = myME310.read_enter_pin();
  char* resp = (char*)myME310.buffer_cstr(2);

  if (resp != NULL && strcmp(resp, "OK") == 0) {

    // ---------------------------------------------------------
    // [STEP 1] Define PDP Context & Wait for Network Attach
    // ---------------------------------------------------------
    DEBUG_SERIAL.println("[NET] Defining PDP Context...");
    rc = myME310.define_pdp_context(cID, ipProt, APN);

    if (rc == ME310::RETURN_VALID) {
      DEBUG_SERIAL.println("[NET] Checking LTE Network Registration (CEREG)...");

      while (true) {
        String ceregResp = sendATCommandRead("AT+CEREG?", 2000);
        if (ceregResp.indexOf(",5") != -1 || ceregResp.indexOf(",1") != -1) {
          DEBUG_SERIAL.println("[SUCCESS] LTE Network Attach successful!");
          break;
        }
        DEBUG_SERIAL.println("[WAIT] Registering to network... Retrying in 3 seconds.");
        delay(3000);
      }

      // ---------------------------------------------------------
      // [STEP 2] Context Activation (AT#SGACT=1,1)
      // ---------------------------------------------------------
      DEBUG_SERIAL.println("[NET] Activating data session context...");
      myME310.context_activation(cID, 1);
      delay(1000);

      // ---------------------------------------------------------
      // [STEP 3] NTP Time Synchronization (CRITICAL for AWS TLS)
      // ---------------------------------------------------------
      DEBUG_SERIAL.println("[NTP] Syncing modem clock with Google NTP Server...");
      //NTP Server setup (require of ssl sequence)
      rc = myME310.configure_ntp_parameters(1, 0, 0, "", ME310::TOUT_5SEC);
      if (rc == ME310::RETURN_VALID) {
        DEBUG_SERIAL.println("-> NTP Configuration OK.");
      } else {
        DEBUG_SERIAL.println("-> [ERROR] NTP Configuration Failed. Halting.");
        while (true);
      }

      //+36 ( +9(hour) x 4(15min) )
      rc = myME310.ntp("216.239.35.0", 123, 1, 5, 36, ME310::TOUT_10SEC);
      if (rc == ME310::RETURN_VALID) {
        DEBUG_SERIAL.println("-> NTP Configuration OK.");
      } else {
        DEBUG_SERIAL.println("-> [ERROR] NTP Configuration Failed. Halting.");
        while (true);
      }
      delay(1000);

      // ---------------------------------------------------------
      // [STEP 4] MQTT Instance Enable (AT#MQEN=1,1)
      // ---------------------------------------------------------
      DEBUG_SERIAL.println("[MQTT] Checking MQTT Enable Status...");
      rc = myME310.read_mqtt_enable();
      resp = (char*)myME310.buffer_cstr(1);

      if (resp != NULL && strcmp(resp, "#MQEN: 1,0") == 0) {
        DEBUG_SERIAL.println("[MQTT] Enabling MQTT Instance 1...");
        myME310.mqtt_enable(1, 1);
      }
      delay(1000);

      // ---------------------------------------------------------
      // [STEP 5] AWS MQTTS & SSL Configuration via Native APIs
      // ---------------------------------------------------------
      DEBUG_SERIAL.println("[SSL] Applying AWS IoT Security Parameters via Native Library APIs...");
      //Disable standalone SSL configuration for SSID 1 to prevent conflict with the internal HTTP SSL engine
      myME310.ssl_enable(1, 0, ME310::TOUT_5SEC);

      rc = myME310.mqtt_configure(1, HOSTNAME, PORT, cID, 1, ME310::TOUT_5SEC);
      if (rc != ME310::RETURN_VALID) {
        DEBUG_SERIAL.println("mqtt_configure Error");
      }
      rc = myME310.ssl_configure_security_param(1, 0, 2, ME310::TOUT_5SEC);
      if (rc != ME310::RETURN_VALID) {
        DEBUG_SERIAL.println("ssl_configure_security_param Error");
      }
      rc = myME310.ssl_additional_parameters(1, 3, 1, 1, 1, ME310::TOUT_5SEC);
      if (rc != ME310::RETURN_VALID) {
        DEBUG_SERIAL.println("ssl_additional_parameters Error");
      }
      rc = myME310.ssl_configure_general_param(1, 1, 300, 90, 100, 50, 0, 0, 1, 0, 0, 0, ME310::TOUT_5SEC);
      if (rc != ME310::RETURN_VALID) {
        DEBUG_SERIAL.println("ssl_configure_general_param Error");
      }
      rc = myME310.mqtt_configure_2(1, 60, 1, ME310::TOUT_5SEC);
      if (rc != ME310::RETURN_VALID) {
        DEBUG_SERIAL.println("mqtt_configure_2 Error");
      }
      DEBUG_SERIAL.println("-> Native SSL/TLS APIs Applied Successfully.");

      // ---------------------------------------------------------
      // [STEP 6] Connect to AWS IoT Core (AT#MQCONN)
      // ---------------------------------------------------------
      DEBUG_SERIAL.println("[MQTT] Connecting to AWS IoT Core Broker...");
      rc = myME310.mqtt_connect(1, CLIENT_ID, CLIENT_USERNAME, CLIENT_PASSWORD, ME310::TOUT_1MIN);

      if (rc == ME310::RETURN_VALID) {
        isConnect = true;
        DEBUG_SERIAL.println("[SUCCESS] MQTTS Connected to AWS!");

        sendATCommand("AT#MQCONN?", "OK", 3000);

        // Subscribe to target topic
        DEBUG_SERIAL.println("[MQTT] Subscribing to topic...");
        myME310.mqtt_topic_subscribe(1, MQTT_SUB);
        delay(1000);

        // Publish test messages
        DEBUG_SERIAL.println("[MQTT] Publishing test messages...");
        myME310.mqtt_publish(1, MQTT_PUB, 0, 0, "Hello AWS");
        delay(2000);
        myME310.mqtt_publish(1, MQTT_PUB, 0, 0, "Hello\nAWS123");

        DEBUG_SERIAL.println("\n>>> Setup Complete. Entering Asynchronous URC Listening Mode within setup()...\n");
      } else {
        DEBUG_SERIAL.println("[CLEANUP] Disconnecting MQTTS Session...");
        myME310.mqtt_disconnect(instanceNum, ME310::TOUT_3SEC);
        myME310.mqtt_enable(1, 0, ME310::TOUT_3SEC);
        myME310.context_activation(cID, 0, "", "", ME310::TOUT_3SEC);
        DEBUG_SERIAL.println("\n=== All MQTTS CLEANUP Procedures Done ===");

        //Power off the modem right before leaving the setup sequence
        myME310.powerOff(ON_OFF);
        Serial.println("ME310G1 Modem Power Off");
      }
    }
  } else {
    DEBUG_SERIAL.println("[ERROR] SIM Card initialization failed.");
  }

  // ---------------------------------------------------------
  // Active listening loop located inside setup() for run-once execution
  // ---------------------------------------------------------
  while (true) {
    if (isConnect) {
      // Continuously stream asynchronous URC characters into the buffer
      while (MODEM_SERIAL.available()) {
        mqttRxBuffer += (char)MODEM_SERIAL.read();
      }

      // Check for incoming MQTT payload trigger (#MQRING:)
      int ringIndex = mqttRxBuffer.indexOf("#MQRING:");
      if (ringIndex >= 0) {
        int newLineIndex = mqttRxBuffer.indexOf('\n', ringIndex);
        if (newLineIndex > ringIndex) {

          String ringLine = mqttRxBuffer.substring(ringIndex, newLineIndex);
          ringLine.trim();

          // Wipe the extracted line to free buffer memory
          mqttRxBuffer = mqttRxBuffer.substring(newLineIndex + 1);

          DEBUG_SERIAL.println("\n-------------------------------------");
          DEBUG_SERIAL.println("[URC DETECTED] " + ringLine);

          int colonIndex = ringLine.indexOf(':');
          int firstComma = ringLine.indexOf(',', colonIndex);
          int secondComma = ringLine.indexOf(',', firstComma + 1);

          int msgId = 1;

          if (firstComma > 0 && colonIndex > 0) {
            instanceNum = ringLine.substring(colonIndex + 1, firstComma).toInt();
          }
          if (secondComma > firstComma) {
            msgId = ringLine.substring(firstComma + 1, secondComma).toInt();
          }

          DEBUG_SERIAL.print("[INFO] MQTT Instance : ");
          DEBUG_SERIAL.println(instanceNum);
          DEBUG_SERIAL.print("[INFO] Message ID    : ");
          DEBUG_SERIAL.println(msgId);
          DEBUG_SERIAL.println("-------------------------------------");

          DEBUG_SERIAL.println("[MQTT] Fetching payload via library...");
          DEBUG_SERIAL.println("[MQTT] Fetching RAW payload bypassing the library...");

          myME310.debugMode(false);
          myME310.mqtt_read(instanceNum, msgId, ME310::TOUT_5SEC);

          DEBUG_SERIAL.println("================ MQTT PAYLOAD ================");
          DEBUG_SERIAL.println(myME310.buffer_cstr_raw());
          DEBUG_SERIAL.println("==============================================");

          // ---------------------------------------------------------
          // [STEP 7] Cleanup & Complete Power Down Sequence
          // ---------------------------------------------------------
          DEBUG_SERIAL.println("[CLEANUP] Disconnecting MQTTS Session...");
          myME310.mqtt_disconnect(instanceNum, ME310::TOUT_3SEC);
          myME310.mqtt_enable(1, 0, ME310::TOUT_3SEC);
          myME310.context_activation(cID, 0, "", "", ME310::TOUT_3SEC);
          DEBUG_SERIAL.println("\n=== All MQTTS Secure Procedures Done ===");

          //Power off the modem right before leaving the setup sequence
          myME310.powerOff(ON_OFF);
          Serial.println("ME310G1 Modem Power Off");
          break;  //Successfully break out of the while(true) loop!
        }
      }

      if (mqttRxBuffer.indexOf("\r\nERROR\r\n") >= 0 || mqttRxBuffer.indexOf("\r\nNO CARRIER\r\n") >= 0) {
        DEBUG_SERIAL.print(mqttRxBuffer);
        DEBUG_SERIAL.println("\n[MODEM ERROR OR DISCONNECTED IN SETUP LOOP]");
        mqttRxBuffer = "";
      }
    } else {
      DEBUG_SERIAL.println("[FATAL] Connection state not initialized. Exiting setup loop.");
      break;
    }
  }
}

void loop() {
  //Completely empty as everything executes exactly once inside setup()
}

// ==========================================
// 💡 Bulletproof AT Command Helper Functions
// ==========================================
bool sendATCommand(const String& cmd, const char* expected, uint32_t timeout) {
  while (MODEM_SERIAL.available()) MODEM_SERIAL.read();
  DEBUG_SERIAL.print("\n[TX] ");
  DEBUG_SERIAL.println(cmd);
  MODEM_SERIAL.println(cmd);
  return waitForString(expected, timeout);
}

String sendATCommandRead(const String& cmd, uint32_t timeout) {
  while (MODEM_SERIAL.available()) MODEM_SERIAL.read();
  DEBUG_SERIAL.print("\n[TX] ");
  DEBUG_SERIAL.println(cmd);
  MODEM_SERIAL.println(cmd);

  uint32_t startTime = millis();
  String currentResponse = "";
  currentResponse.reserve(256);

  while (millis() - startTime < timeout) {
    while (MODEM_SERIAL.available()) {
      currentResponse += (char)MODEM_SERIAL.read();
    }
    if (currentResponse.indexOf("\r\nOK\r\n") >= 0 || currentResponse.indexOf("\r\nERROR\r\n") >= 0) {
      DEBUG_SERIAL.print(currentResponse);
      return currentResponse;
    }
  }
  DEBUG_SERIAL.print(currentResponse);
  DEBUG_SERIAL.println("\n[TIMEOUT]");
  return currentResponse;
}

bool waitForString(const char* expected, uint32_t timeout) {
  uint32_t startTime = millis();
  String currentResponse = "";
  currentResponse.reserve(1024);
  String target = String(expected);
  if (target == "OK") target = "\r\nOK\r\n";

  while (millis() - startTime < timeout) {
    while (MODEM_SERIAL.available()) {
      currentResponse += (char)MODEM_SERIAL.read();
    }
    if (currentResponse.indexOf(target) >= 0) {
      DEBUG_SERIAL.print(currentResponse);
      return true;
    }
    if (currentResponse.indexOf("\r\nERROR\r\n") >= 0 || currentResponse.indexOf("\r\nNO CARRIER\r\n") >= 0) {
      DEBUG_SERIAL.print(currentResponse);
      return false;
    }
  }
  DEBUG_SERIAL.print(currentResponse);
  DEBUG_SERIAL.println("\n[TIMEOUT]");
  return false;
}
