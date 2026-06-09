#include <Arduino.h>
#include <ME310.h>
#include <string.h>

// ---------------------------------------------------------
// Public EMQX MQTT Broker Configuration (Non-SSL)
// ---------------------------------------------------------
#define APN "simplio.apn"
#define HOSTNAME "broker.emqx.io"
#define PORT 1883

#define CLIENT_ID "czme310g1001"
#define CLIENT_USERNAME ""
#define CLIENT_PASSWORD ""

#define ON_OFF 2 /* Select the GPIO to control ON_OFF */
#define MDMSerial Serial1
#define DEBUG_SERIAL Serial

#define SUB_TOPIC "czme310/topic/get"
#define PUB_TOPIC "czme310/topic/put"

using namespace me310;

ME310 myME310;
ME310::return_t rc; 

int cID = 1;          // PDP Context Identifier
char ipProt[] = "IP"; // Packet Data Protocol type
int instanceNum = 1;

bool isConnect = false;
String mqttRxBuffer = ""; // Global buffer to accumulate asynchronous URC data

void setup() {
  DEBUG_SERIAL.begin(115200);
  MDMSerial.begin(115200);
  delay(100);

  myME310.debugMode(true);
  myME310.powerOn(ON_OFF);

  DEBUG_SERIAL.println("[CLEANUP] Disconnecting MQTT Session...");
  myME310.mqtt_disconnect(instanceNum, ME310::TOUT_3SEC);
  myME310.mqtt_enable(1, 0, ME310::TOUT_3SEC);
  myME310.context_activation(cID, 0, "", "", ME310::TOUT_3SEC);
  DEBUG_SERIAL.println("\n=== All MQTT Public Procedures Done ===");  
 
  DEBUG_SERIAL.println("Telit Test AT MQTT command");
  DEBUG_SERIAL.println("ME310 ON");
  DEBUG_SERIAL.println("AT Command");

  // Enable verbose error reporting (AT+CMEE=2)
  myME310.report_mobile_equipment_error(2); 
  
  // Check SIM card status
  rc = myME310.read_enter_pin(); 
  char *resp = (char *)myME310.buffer_cstr(2);
  
  if (resp != NULL && strcmp(resp, "OK") == 0) {
    
    // ---------------------------------------------------------
    // [STEP 1] Define PDP Context & Wait for Network Attach
    // ---------------------------------------------------------
    DEBUG_SERIAL.println("Define PDP Context");
    rc = myME310.define_pdp_context(cID, ipProt, APN); 
    DEBUG_SERIAL.print(rc);
    
    if (rc == ME310::RETURN_VALID) {
      myME310.read_define_pdp_context(); 
      DEBUG_SERIAL.print("pdp context read: ");
      DEBUG_SERIAL.println(myME310.buffer_cstr(1)); 

      DEBUG_SERIAL.print("gprs network registration status: ");
      rc = myME310.read_gprs_network_registration_status(); 
      
      if (rc == ME310::RETURN_VALID) {
        resp = (char *)myME310.buffer_cstr(1);
        DEBUG_SERIAL.println(resp);
        
        // Loop until registered to the GPRS/LTE network (+CGREG: 0,1 or 0,5)
        while (resp != NULL) {
          if ((strcmp(resp, "+CGREG: 0,1") != 0) && (strcmp(resp, "+CGREG: 0,5") != 0)) {
            delay(3000);
            rc = myME310.read_gprs_network_registration_status();
            if (rc != ME310::RETURN_VALID) {
              DEBUG_SERIAL.println("ERROR");
              break;
            }
            resp = (char *)myME310.buffer_cstr(1);
            DEBUG_SERIAL.println(resp);
          } else {
            break;
          }
        }
      }

      // ---------------------------------------------------------
      // [STEP 2] Context Activation (AT#SGACT=1,1)
      // ---------------------------------------------------------
      DEBUG_SERIAL.println("Activate context");
      myME310.context_activation(cID, 1,ME310::TOUT_5SEC); 
      DEBUG_SERIAL.println(myME310.buffer_cstr(1));

      // ---------------------------------------------------------
      // [STEP 3] MQTT Instance Enable (AT#MQEN=1,1)
      // ---------------------------------------------------------
      DEBUG_SERIAL.println("mqtt enable read");
      rc = myME310.read_mqtt_enable(); 
      DEBUG_SERIAL.println(myME310.buffer_cstr(1));
      resp = (char *)myME310.buffer_cstr(1);
      
      if (resp != NULL && (strcmp(resp, "#MQEN: 1,0")) == 0) {
        DEBUG_SERIAL.println("mqtt enable");
        rc = myME310.mqtt_enable(1, 1); 
        DEBUG_SERIAL.println(myME310.buffer_cstr(1));
        rc = myME310.read_mqtt_enable();
        DEBUG_SERIAL.println(myME310.buffer_cstr(1));
      }
      delay(1000);

      // ---------------------------------------------------------
      // [STEP 4] MQTT Engine Configuration (Non-SSL Default)
      // ---------------------------------------------------------
      DEBUG_SERIAL.print("mqtt configure:");
      rc = myME310.mqtt_configure(1, HOSTNAME, PORT, cID); 
      DEBUG_SERIAL.println(myME310.buffer_cstr(1));
      delay(1000);

      // ---------------------------------------------------------
      // [STEP 5] Connect to Public EMQX Broker
      // ---------------------------------------------------------
      if (rc == ME310::RETURN_VALID) {
        DEBUG_SERIAL.print("mqtt connect: ");
        rc = myME310.mqtt_connect(1, CLIENT_ID, CLIENT_USERNAME, CLIENT_PASSWORD, ME310::TOUT_1MIN); 
        DEBUG_SERIAL.println(myME310.buffer_cstr(1));

        if (rc == ME310::RETURN_VALID) {
          isConnect = true;

          // MQTT Topic Subscribe
          DEBUG_SERIAL.print("MQTT Topic Subscribe: ");
          myME310.mqtt_topic_subscribe(1, SUB_TOPIC); 
          DEBUG_SERIAL.println(myME310.buffer_cstr(1));

          // MQTT Topic Publish
          DEBUG_SERIAL.print("MQTT Publish: ");
          myME310.mqtt_publish(1, PUB_TOPIC, 1, 0, "message");
          DEBUG_SERIAL.println(myME310.buffer_cstr(1));
          
          DEBUG_SERIAL.println("\n>>> Setup Complete. Entering Asynchronous URC Listening Mode within setup()...\n");
        }
      }
    }
  } else {
    DEBUG_SERIAL.println((String) "Error: " + rc + " Error string: " + myME310.buffer_cstr(2));
    myME310.powerOff(ON_OFF);
    DEBUG_SERIAL.println("ME310G1 Modem Power Off");
  }

  // ---------------------------------------------------------
  // Active listening loop located inside setup() for run-once execution
  // ---------------------------------------------------------
  while (true) {
    if (isConnect) {
      // Continuously stream asynchronous URC characters into the buffer
      while (MDMSerial.available()) {
        mqttRxBuffer += (char)MDMSerial.read();
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

          DEBUG_SERIAL.print("[INFO] MQTT Instance : "); DEBUG_SERIAL.println(instanceNum);
          DEBUG_SERIAL.print("[INFO] Message ID    : "); DEBUG_SERIAL.println(msgId);
          DEBUG_SERIAL.println("-------------------------------------");

          DEBUG_SERIAL.println("[MQTT] Fetching payload via library...");
          myME310.debugMode(false);
          myME310.mqtt_read(instanceNum, msgId, ME310::TOUT_5SEC);

          DEBUG_SERIAL.println("================ MQTT PAYLOAD ================");
          DEBUG_SERIAL.println(myME310.buffer_cstr_raw());
          DEBUG_SERIAL.println("==============================================");

          // ---------------------------------------------------------
          // [STEP 6] Cleanup & Complete Power Down Sequence
          // ---------------------------------------------------------
          DEBUG_SERIAL.println("[CLEANUP] Disconnecting MQTT Session...");
          myME310.mqtt_disconnect(instanceNum, ME310::TOUT_3SEC);
          myME310.mqtt_enable(1, 0, ME310::TOUT_3SEC);
          myME310.context_activation(cID, 0, "", "", ME310::TOUT_3SEC);
          DEBUG_SERIAL.println("\n=== All MQTT Public Procedures Done ===");

          // Power off the modem right before leaving the setup sequence
          myME310.powerOff(ON_OFF);
          DEBUG_SERIAL.println("ME310G1 Modem Power Off");
          break;  // Successfully break out of the while(true) loop!
        }
      }

      if (mqttRxBuffer.indexOf("\r\nERROR\r\n") >= 0 || mqttRxBuffer.indexOf("\r\nNO CARRIER\r\n") >= 0) {
        DEBUG_SERIAL.print(mqttRxBuffer);
        DEBUG_SERIAL.println("\n[MODEM ERROR OR DISCONNECTED IN SETUP LOOP]");
        mqttRxBuffer = "";
      }
    } else {
      DEBUG_SERIAL.println("[FATAL] Connection state not initialized. Exiting setup loop.");
      myME310.powerOff(ON_OFF);
      DEBUG_SERIAL.println("ME310G1 Modem Power Off");     

      break;
    }
  }
}

void loop() {
  // Completely empty as everything executes exactly once inside setup()
}