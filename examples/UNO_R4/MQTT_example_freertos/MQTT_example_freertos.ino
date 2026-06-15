/*
 * ====================================================================
 * Project Name: UNO R4 + ME310G1 LTE MQTT Real-time Event Handling Standard Example
 * Design Philosophy: 
 * 1. Cooperative Task Termination
 * 2. C-Style Fixed Buffer Parsing (Zero Memory Fragmentation)
 * 3. Binary Semaphore Synchronization (Loose Coupling)
 * ====================================================================
 */

#include <Arduino.h>
#include <ME310.h>
#include <string.h>
#include <Arduino_FreeRTOS.h>

#define APN "simplio.apn"
#define HOSTNAME "broker.emqx.io"
#define PORT 1883
#define CLIENT_ID "czme310g1001"
#define CLIENT_USERNAME ""
#define CLIENT_PASSWORD ""

#define ON_OFF 2 
#define MDMSerial Serial1
#define DEBUG_SERIAL Serial

#define SUB_TOPIC "czme310/topic/get"
#define PUB_TOPIC "czme310/topic/put"

using namespace me310;
ME310 myME310;
ME310::return_t rc; 

int cID = 1;      
char ipProt[] = "IP"; 
int instanceNum = 1;

// Global flags and kernel object definitions
volatile bool bShutdownRequested = false; // Flag must be declared as volatile
SemaphoreHandle_t xMQTTConnectSemaphore = NULL; // Binary semaphore for inter-task synchronization

// Fixed-size C-Style buffer completely excluding String objects
char mqttRxBuffer[256];
size_t mqttRxIndex = 0;

TaskHandle_t xMQTTTaskHandle = NULL;
TaskHandle_t xCommandTaskHandle = NULL;

void vMQTTTask(void *pvParameters);
void vCommandTask(void *pvParameters);

void setup() {
  DEBUG_SERIAL.begin(115200);
  MDMSerial.begin(115200);
  
  while(!DEBUG_SERIAL) { delay(10); }
  delay(1000);

  // Initialize buffer
  memset(mqttRxBuffer, 0, sizeof(mqttRxBuffer));

  DEBUG_SERIAL.println("======================================");
  DEBUG_SERIAL.println(" ME310 FreeRTOS Robust Architecture   ");
  DEBUG_SERIAL.println("======================================");

  // Create binary semaphore
  xMQTTConnectSemaphore = xSemaphoreCreateBinary();

  // 1. Create MQTT master task
  xTaskCreate(vMQTTTask, "MQTT_Task", 1280, NULL, 2, &xMQTTTaskHandle);

  // 2. Create user control task (No longer force-suspended at boot!)
  xTaskCreate(vCommandTask, "Cmd_Task", 192, NULL, 1, &xCommandTaskHandle);

  DEBUG_SERIAL.println("[SYSTEM] Starting FreeRTOS Scheduler Engine...");
  DEBUG_SERIAL.flush();
  vTaskStartScheduler();
}

void loop() {
  // Empty loop
}

/* ---------------------------------------------------------
 * [TASK 1] MQTT Initialization & Zero-Fragmentation Listening
 * --------------------------------------------------------- */
void vMQTTTask(void *pvParameters) {
  myME310.debugMode(true);
  myME310.powerOn(ON_OFF);

  DEBUG_SERIAL.println("[INIT] Disconnecting Leftover MQTT Sessions...");
  myME310.mqtt_disconnect(instanceNum, ME310::TOUT_3SEC);
  myME310.mqtt_enable(1, 0, ME310::TOUT_3SEC);
  myME310.context_activation(cID, 0, "", "", ME310::TOUT_3SEC);
  
  myME310.report_mobile_equipment_error(2); 
  rc = myME310.read_enter_pin(); 
  char *resp = (char *)myME310.buffer_cstr(2);
  
  if (resp != NULL && strcmp(resp, "OK") == 0) {
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
            if (rc != ME310::RETURN_VALID) break;
            resp = (char *)myME310.buffer_cstr(1);
          } else {
            break;
          }
        }
      }

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
          myME310.mqtt_topic_subscribe(1, SUB_TOPIC); 
          myME310.mqtt_publish(1, PUB_TOPIC, 1, 0, "Connected smoothly");
          
          DEBUG_SERIAL.println("\n>>> MQTT Setup Complete. Streaming Listening Loop Running...\n");

          // Give semaphore to wake up the waiting Command task (Loose Coupling)
          xSemaphoreGive(xMQTTConnectSemaphore);
        }
      }
    }
  }

  // Enter infinite loop
  for (;;) {
    // Check at the beginning of each loop if a shutdown was requested (Cooperative Termination)
    if (bShutdownRequested) {
      break; // Exit loop to move to the safe shutdown sequence below
    }

    // Receive serial stream based on a fixed array (Overflow Prevention)
    while (MDMSerial.available()) {
      char c = (char)MDMSerial.read();
      if (mqttRxIndex < sizeof(mqttRxBuffer) - 1) {
        mqttRxBuffer[mqttRxIndex++] = c;
        mqttRxBuffer[mqttRxIndex] = '\0'; // Always null-terminate string
      } else {
        // If buffer is full, reset to prevent data corruption (Defensive Coding)
        mqttRxIndex = 0;
        mqttRxBuffer[0] = '\0';
      }
    }

    // Search for URC string using the C standard function strstr()
    char *ringPtr = strstr(mqttRxBuffer, "#MQRING:");
    if (ringPtr != NULL) {
      char *newLinePtr = strchr(ringPtr, '\n');
      if (newLinePtr != NULL) {
        *newLinePtr = '\0'; // Temporary newline conversion to separate a single line

        DEBUG_SERIAL.println("\n-------------------------------------");
        DEBUG_SERIAL.print("[URC DETECTED] "); DEBUG_SERIAL.println(ringPtr);

        int msgId = 1;
        // Parse integer data safely using sscanf (Completely replaces String functions)
        sscanf(ringPtr, "#MQRING: %d,%d", &instanceNum, &msgId);

        DEBUG_SERIAL.print("[INFO] MQTT Instance : "); DEBUG_SERIAL.println(instanceNum);
        DEBUG_SERIAL.print("[INFO] Message ID    : "); DEBUG_SERIAL.println(msgId);
        DEBUG_SERIAL.println("-------------------------------------");

        myME310.debugMode(false); 
        myME310.mqtt_read(instanceNum, msgId, ME310::TOUT_5SEC);

        DEBUG_SERIAL.println("================ MQTT PAYLOAD ================");
        DEBUG_SERIAL.println(myME310.buffer_cstr_raw());
        DEBUG_SERIAL.println("==============================================");

        // Shift out consumed data from buffer (Shift operation results in 0% heap fragmentation)
        size_t consumedLen = (newLinePtr + 1) - mqttRxBuffer;
        memmove(mqttRxBuffer, newLinePtr + 1, mqttRxIndex - consumedLen);
        mqttRxIndex -= consumedLen;
        mqttRxBuffer[mqttRxIndex] = '\0';
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20)); 
  }

  // Resource cleanup executed after safely exiting the loop (Cooperative Self-Termination)
  DEBUG_SERIAL.println("[TASK_INTERNAL] Executing Safe Shutdown Sequence...");
  myME310.debugMode(true); 
  myME310.mqtt_disconnect(instanceNum, ME310::TOUT_3SEC);
  myME310.mqtt_enable(1, 0, ME310::TOUT_3SEC);
  myME310.context_activation(cID, 0, "", "", ME310::TOUT_3SEC);
  
  myME310.powerOff(ON_OFF);
  DEBUG_SERIAL.println("ME310G1 Modem Power Off");
  DEBUG_SERIAL.println("[SYSTEM] MQTT Task Terminated Itself Safely.");
  
  xMQTTTaskHandle = NULL;
  vTaskDelete(NULL); // Self-terminate task gracefully
}

/* ---------------------------------------------------------
 * [TASK 2] Monitor Serial Input for Quit Command ('q')
 * --------------------------------------------------------- */
void vCommandTask(void *pvParameters) {
  // Wait in kernel blocking state immediately upon start (0% CPU usage)
  // Wait infinitely until the MQTT task successfully connects and gives the semaphore.
  xSemaphoreTake(xMQTTConnectSemaphore, portMAX_DELAY);
  DEBUG_SERIAL.println("[SYSTEM] Command Task Unlocked via Semaphore.");

  for (;;) {
    if (DEBUG_SERIAL.available()) {
      char inputKey = DEBUG_SERIAL.read();
      
      if (inputKey == 'q' || inputKey == 'Q') {
        DEBUG_SERIAL.println("\n==============================================");
        DEBUG_SERIAL.println("[USER CMD] 'q' detected! Signalling Graceful Shutdown...");
        DEBUG_SERIAL.println("==============================================");

        // Signal termination instead of force-killing the target task!
        bShutdownRequested = true; 

        // Wait safely until the MQTT task terminates and its handle becomes NULL
        while(xMQTTTaskHandle != NULL) {
          vTaskDelay(pdMS_TO_TICKS(50));
        }

        DEBUG_SERIAL.println("\n=== All MQTT Public Procedures Cleaned up ===");
        DEBUG_SERIAL.println("[SYSTEM] Shutdown sequence completed successfully.");
        
        vTaskDelete(NULL); // Terminate own task
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100)); 
  }
}