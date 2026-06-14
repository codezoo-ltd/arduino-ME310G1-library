#include <Arduino.h>
#include <ME310.h>
#include <string.h>
#include <Arduino_FreeRTOS.h>

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

// FreeRTOS task handles declaration
TaskHandle_t xMQTTTaskHandle = NULL;
TaskHandle_t xCommandTaskHandle = NULL;

// Task function prototype declarations
void vMQTTTask(void *pvParameters);
void vCommandTask(void *pvParameters);

void setup() {
  DEBUG_SERIAL.begin(115200);
  MDMSerial.begin(115200);
  
  // Wait for UNO R4 Native USB serial to stabilize
  while(!DEBUG_SERIAL) {
    delay(10);
  }
  delay(1000);

  mqttRxBuffer.reserve(256);

  DEBUG_SERIAL.println("======================================");
  DEBUG_SERIAL.println(" ME310 FreeRTOS Suspend/Resume Test   ");
  DEBUG_SERIAL.println("======================================");
  DEBUG_SERIAL.println("[SYSTEM] Allocating All Tasks Upfront...");
  DEBUG_SERIAL.flush();

  // 1. Create MQTT Master Task (Allocate 5KB stack)
  xTaskCreate(
    vMQTTTask,          
    "MQTT_Task",        
    1280,               // 1280 * 4 = 5,120 Bytes
    NULL,               
    2,                  
    &xMQTTTaskHandle    
  );

  // 2. Pre-create control task (Allocate 768 Bytes)
  xTaskCreate(
    vCommandTask,       
    "Cmd_Task",         
    192,                // 192 * 4 = 768 Bytes
    NULL,               
    1,                  
    &xCommandTaskHandle 
  );

  // Force suspend the command task immediately after boot to prevent it from running early!
  if (xCommandTaskHandle != NULL) {
    vTaskSuspend(xCommandTaskHandle);
    DEBUG_SERIAL.println("[SYSTEM] Command Task Created and Cold-Suspended safely.");
  }
  DEBUG_SERIAL.flush();

  // 3. Start UNO R4 FreeRTOS scheduler engine
  DEBUG_SERIAL.println("[SYSTEM] Starting FreeRTOS Scheduler Engine...");
  DEBUG_SERIAL.flush();
  vTaskStartScheduler();
}

void loop() {
  // Empty loop as it is a FreeRTOS environment.
}

// ---------------------------------------------------------
// [TASK 1] MQTT Initialization & Infinite URC Listening
// ---------------------------------------------------------
void vMQTTTask(void *pvParameters) {
  myME310.debugMode(true);
  myME310.powerOn(ON_OFF);

  // Preventive cleanup to avoid session entanglement
  DEBUG_SERIAL.println("[INIT_CLEANUP] Disconnecting Leftover MQTT Sessions...");
  myME310.mqtt_disconnect(instanceNum, ME310::TOUT_3SEC);
  myME310.mqtt_enable(1, 0, ME310::TOUT_3SEC);
  myME310.context_activation(cID, 0, "", "", ME310::TOUT_3SEC);
  
  DEBUG_SERIAL.println("Telit Test AT MQTT command");
  DEBUG_SERIAL.println("ME310 ON");
  DEBUG_SERIAL.println("AT Command");

  myME310.report_mobile_equipment_error(2); 
  
  rc = myME310.read_enter_pin(); 
  char *resp = (char *)myME310.buffer_cstr(2);
  
  if (resp != NULL && strcmp(resp, "OK") == 0) {
    DEBUG_SERIAL.println("Define PDP Context");
    rc = myME310.define_pdp_context(cID, ipProt, APN); 
    
    if (rc == ME310::RETURN_VALID) {
      myME310.read_define_pdp_context(); 
      rc = myME310.read_gprs_network_registration_status(); 
      
      if (rc == ME310::RETURN_VALID) {
        resp = (char *)myME310.buffer_cstr(1);
        
        while (resp != NULL) {
          if ((strcmp(resp, "+CGREG: 0,1") != 0) && (strcmp(resp, "+CGREG: 0,5") != 0)) {
            vTaskDelay(pdMS_TO_TICKS(3000)); 
            rc = myME310.read_gprs_network_registration_status();
            if (rc != ME310::RETURN_VALID) {
              DEBUG_SERIAL.println("ERROR");
              break;
            }
            resp = (char *)myME310.buffer_cstr(1);
          } else {
            break;
          }
        }
      }

      DEBUG_SERIAL.println("Activate context");
      myME310.context_activation(cID, 1, ME310::TOUT_5SEC); 

      rc = myME310.read_mqtt_enable(); 
      resp = (char *)myME310.buffer_cstr(1);
      
      if (resp != NULL && (strcmp(resp, "#MQEN: 1,0")) == 0) {
        rc = myME310.mqtt_enable(1, 1); 
        rc = myME310.read_mqtt_enable();
      }
      vTaskDelay(pdMS_TO_TICKS(1000));

      rc = myME310.mqtt_configure(1, HOSTNAME, PORT, cID); 
      vTaskDelay(pdMS_TO_TICKS(1000));

      if (rc == ME310::RETURN_VALID) {
        rc = myME310.mqtt_connect(1, CLIENT_ID, CLIENT_USERNAME, CLIENT_PASSWORD, ME310::TOUT_1MIN); 

        if (rc == ME310::RETURN_VALID) {
          isConnect = true;

          myME310.mqtt_topic_subscribe(1, SUB_TOPIC); 
          myME310.mqtt_publish(1, PUB_TOPIC, 1, 0, "message");
          
          DEBUG_SERIAL.println("\n>>> MQTT Setup Complete. Streaming Listening Loop Running...\n");

          // Immediately wake up (Resume) the sleeping control task when the setup is completely successful!
          if (xCommandTaskHandle != NULL) {
            vTaskResume(xCommandTaskHandle);
            DEBUG_SERIAL.println("[SYSTEM] Command Task Awakened and Armed Successfully.");
          }
        }
      }
    }
  } else {
    DEBUG_SERIAL.println("[ERROR] SIM Card initialization failed.");
  }

  // Infinite URC listening loop for data reception
  while (true) {
    if (isConnect) {
      while (MDMSerial.available()) {
        mqttRxBuffer += (char)MDMSerial.read();
      }

      int ringIndex = mqttRxBuffer.indexOf("#MQRING:");
      if (ringIndex >= 0) {
        int newLineIndex = mqttRxBuffer.indexOf('\n', ringIndex);
        if (newLineIndex > ringIndex) {

          String ringLine = mqttRxBuffer.substring(ringIndex, newLineIndex);
          ringLine.trim();
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
        }
      }
    } else {
      DEBUG_SERIAL.println("[FATAL] Connection state not initialized. Exiting setup loop.");
      myME310.powerOff(ON_OFF);
      DEBUG_SERIAL.println("ME310G1 Modem Power Off");     
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(20)); 
  }
}

// ---------------------------------------------------------
// [TASK 2] Monitor Serial Input for Quit Command ('q')
// ---------------------------------------------------------
void vCommandTask(void *pvParameters) {
  while (true) {
    if (DEBUG_SERIAL.available()) {
      char inputKey = DEBUG_SERIAL.read();
      
      if (inputKey == 'q' || inputKey == 'Q') {
        DEBUG_SERIAL.println("\n==============================================");
        DEBUG_SERIAL.println("[USER CMD] 'q' detected! Initiating Shutdown...");
        DEBUG_SERIAL.println("==============================================");

        if (xMQTTTaskHandle != NULL) {
          vTaskDelete(xMQTTTaskHandle);
          xMQTTTaskHandle = NULL;
          DEBUG_SERIAL.println("[SYSTEM] MQTT Listening Task has been stopped.");
        }

        DEBUG_SERIAL.println("[CLEANUP] Disconnecting MQTT Session...");
        myME310.debugMode(true); 
        myME310.mqtt_disconnect(instanceNum, ME310::TOUT_3SEC);
        myME310.mqtt_enable(1, 0, ME310::TOUT_3SEC);
        myME310.context_activation(cID, 0, "", "", ME310::TOUT_3SEC);
        
        DEBUG_SERIAL.println("\n=== All MQTT Public Procedures Done ===");

        myME310.powerOff(ON_OFF);
        DEBUG_SERIAL.println("ME310G1 Modem Power Off");
        DEBUG_SERIAL.println("[SYSTEM] Shutdown sequence completed successfully.");
        
        vTaskDelete(NULL); 
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100)); 
  }
}