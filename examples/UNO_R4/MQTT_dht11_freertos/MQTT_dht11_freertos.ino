/*
 * ====================================================================
 * Project Name: UNO R4 + ME310G1 LTE MQTT + Sensor Task Architecture
 * * Design Philosophy: 
 * 1. Cooperative Task Termination: Tasks handle their own lifecycle for safe shutdown.
 * 2. Zero-Fragmentation Memory Strategy: Uses fixed-size C-style buffers to avoid heap fragmentation.
 * 3. Producer-Consumer Pattern: Inter-task communication via FreeRTOS Queues.
 * 4. Loose Coupling: Synchronization via Binary Semaphores for minimal task dependency.
 * ====================================================================
 */

#include <Arduino.h>
#include <ME310.h>
#include <string.h>
#include <Arduino_FreeRTOS.h>
#include <DHT.h>

// --- Configuration ---
#define APN "simplio.apn"
#define HOSTNAME "broker.emqx.io"
#define PORT 1883
#define CLIENT_ID "czme310g1001"
#define CLIENT_USERNAME ""
#define CLIENT_PASSWORD ""
#define ON_OFF 2
#define DHTPIN 8
#define DHTTYPE DHT11

#define MDMSerial Serial1
#define DEBUG_SERIAL Serial

#define SUB_TOPIC "czme310/topic/get"
#define PUB_TOPIC "czme310/topic/put"

using namespace me310;
ME310 myME310;
ME310::return_t rc;
DHT dht(DHTPIN, DHTTYPE);

int cID = 1;
char ipProt[] = "IP"; 
int instanceNum = 1;

// Global objects
volatile bool bShutdownRequested = false;
SemaphoreHandle_t xMQTTConnectSemaphore = NULL;
QueueHandle_t xSensorQueue = NULL;

char mqttRxBuffer[256];
size_t mqttRxIndex = 0;

TaskHandle_t xMQTTTaskHandle = NULL;
TaskHandle_t xCommandTaskHandle = NULL;

// --- Task Prototypes ---
void vMQTTTask(void *pvParameters);
void vSensorTask(void *pvParameters);
void vCommandTask(void *pvParameters);

void setup() {
  DEBUG_SERIAL.begin(115200);
  MDMSerial.begin(115200);
  dht.begin();

  delay(1000);
  memset(mqttRxBuffer, 0, sizeof(mqttRxBuffer));

  xMQTTConnectSemaphore = xSemaphoreCreateBinary();
  xSensorQueue = xQueueCreate(1, sizeof(float[2]));  // [0]:Temp, [1]:Hum

  xTaskCreate(vMQTTTask, "MQTT_Task", 1100, NULL, 2, &xMQTTTaskHandle);
  xTaskCreate(vSensorTask, "Sensor_Task", 200, NULL, 1, NULL);  
  xTaskCreate(vCommandTask, "Cmd_Task", 180, NULL, 1, &xCommandTaskHandle);

  vTaskStartScheduler();
}

void loop() {}

/* [TASK 1] Sensor Acquisition: EMA Filtering & Queue Update */
void vSensorTask(void *pvParameters) {
  float emaT = 0.0, emaH = 0.0;
  bool first = true;
  float alpha = 0.064;
  float buffer[2];

  for (;;) {
    float rawT = dht.readTemperature();
    float rawH = dht.readHumidity();

    if (!isnan(rawT) && !isnan(rawH)) {
      if (first) {
        emaT = rawT;
        emaH = rawH;
        first = false;
      } else {
        emaT = (alpha * rawT) + ((1.0 - alpha) * emaT);
        emaH = (alpha * rawH) + ((1.0 - alpha) * emaH);
      }
      buffer[0] = emaT;
      buffer[1] = emaH;     
      xQueueOverwrite(xSensorQueue, buffer);
      
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

/* [TASK 2] MQTT Task: Logic for 'status?' and Data Publishing */
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
      break;  // Exit loop to move to the safe shutdown sequence below
    }

    // Receive serial stream based on a fixed array (Overflow Prevention)
    while (MDMSerial.available()) {
      char c = (char)MDMSerial.read();
      if (mqttRxIndex < sizeof(mqttRxBuffer) - 1) {
        mqttRxBuffer[mqttRxIndex++] = c;
        mqttRxBuffer[mqttRxIndex] = '\0';  // Always null-terminate string
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
        *newLinePtr = '\0';  // Temporary newline conversion to separate a single line

        DEBUG_SERIAL.println("\n-------------------------------------");
        DEBUG_SERIAL.print("[URC DETECTED] ");
        DEBUG_SERIAL.println(ringPtr);

        int msgId = 1;
        // Parse integer data safely using sscanf (Completely replaces String functions)
        sscanf(ringPtr, "#MQRING: %d,%d", &instanceNum, &msgId);

        DEBUG_SERIAL.print("[INFO] MQTT Instance : ");
        DEBUG_SERIAL.println(instanceNum);
        DEBUG_SERIAL.print("[INFO] Message ID    : ");
        DEBUG_SERIAL.println(msgId);
        DEBUG_SERIAL.println("-------------------------------------");

        myME310.debugMode(false);
        myME310.mqtt_read(instanceNum, msgId, ME310::TOUT_5SEC);
        DEBUG_SERIAL.println(myME310.buffer_cstr_raw());

        float data[2];
        if (xQueuePeek(xSensorQueue, data, 0) == pdTRUE) {
            char jsonBuf[64];
            // Since MQTT AT commands do not support double quotes, 
            // single quotes are used as a fallback for JSON structure.
            sprintf(jsonBuf, "{\"temp\":%.1f,\"hum\":%.1f}", data[0], data[1]);
            myME310.mqtt_publish(1, PUB_TOPIC, 1, 0, jsonBuf);
            DEBUG_SERIAL.println("[MQTT] Status Report Sent.");
        }        
        size_t freeHeap = xPortGetFreeHeapSize();
        Serial.print("Current Free Heap: ");
        Serial.println(freeHeap);

        // Shift out consumed data from the buffer to maintain a contiguous data stream.
        // This memory-shift operation keeps the buffer aligned and prevents fragmentation.
        size_t consumedLen = (newLinePtr + 1) - mqttRxBuffer;
        memmove(mqttRxBuffer, newLinePtr + 1, mqttRxIndex - consumedLen);
        mqttRxIndex -= consumedLen;
        mqttRxBuffer[mqttRxIndex] = '\0';
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
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
  vTaskDelete(NULL);  // Self-terminate task gracefully
}

/* ---------------------------------------------------------
 * [TASK 3] Monitor Serial Input for Quit Command ('q')
 * --------------------------------------------------------- */
void vCommandTask(void *pvParameters) {
  // Wait in kernel blocking state immediately upon start (0% CPU usage)
  // Wait infinitely until the MQTT task successfully connects and gives the semaphore.
  xSemaphoreTake(xMQTTConnectSemaphore, portMAX_DELAY);
  DEBUG_SERIAL.println("[SYSTEM] Command Task Unlocked via Semaphore.");
  size_t freeHeap = xPortGetFreeHeapSize();
  Serial.print("Current Free Heap: ");
  Serial.println(freeHeap);

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
        while (xMQTTTaskHandle != NULL) {
          vTaskDelay(pdMS_TO_TICKS(50));
        }

        DEBUG_SERIAL.println("\n=== All MQTT Public Procedures Cleaned up ===");
        DEBUG_SERIAL.println("[SYSTEM] Shutdown sequence completed successfully.");

        vTaskDelete(NULL);  // Terminate own task
      }
      if (inputKey == 'p' || inputKey == 'P') {
        DEBUG_SERIAL.println("\n==============================================");
        DEBUG_SERIAL.println("[USER CMD] 'p' detected! Hello World...");
        DEBUG_SERIAL.println("==============================================");

        char jsonBuf[64];
        sprintf(jsonBuf, "{\"message\": \"Hello World!\"}");
        myME310.mqtt_publish(1, PUB_TOPIC, 1, 0, jsonBuf);
        DEBUG_SERIAL.println("[MQTT] Publish Message.");
      }        
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
