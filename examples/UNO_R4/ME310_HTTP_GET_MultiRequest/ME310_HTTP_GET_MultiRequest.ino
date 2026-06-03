#include <Arduino.h>
#include <ME310.h>

#define ON_OFF 2 /* Select the GPIO to control ON_OFF */

using namespace me310;

ME310 myME310;
ME310::return_t rc;

#define MODEM_SERIAL Serial1 
#define DEBUG_SERIAL Serial

// Function Declarations
bool sendATCommand(const String& cmd, const char* expected, uint32_t timeout);
String sendATCommandRead(const String& cmd, uint32_t timeout);
bool waitForString(const char* expected, uint32_t timeout);
bool catchHttpRingAndReceive(int prof_id, uint32_t timeout);
bool connectNetwork();

void setup() {
  DEBUG_SERIAL.begin(115200);
  MODEM_SERIAL.begin(115200);
  delay(100);

  myME310.debugMode(true); 

  DEBUG_SERIAL.println("\n[INIT] Powering on modem...");
  myME310.powerOn(ON_OFF); 

  DEBUG_SERIAL.println("======================================");
  DEBUG_SERIAL.println(" ME310G1 HTTP Multi-Request Test      ");
  DEBUG_SERIAL.println("======================================");

  // ---------------------------------------------------------
  // [STEP 1] Check Network Cell Registration and IP Allocation
  // ---------------------------------------------------------
  if (!connectNetwork()) {
    DEBUG_SERIAL.println("\n[FATAL] Network Connection Failed. Halting system.");
    while (true); 
  }

  // ---------------------------------------------------------
  // [STEP 2] Configure HTTP Server Parameters
  // ---------------------------------------------------------
  DEBUG_SERIAL.println("\n[HTTP] 1. Configuring HTTP Parameters...");
  
  rc = myME310.configure_http_parameters(0, "postman-echo.com", 80, 0, 0, 120, 1, ME310::TOUT_5SEC);
  
  if (rc == ME310::RETURN_VALID) {
    DEBUG_SERIAL.println("-> HTTP Configuration Setup OK.");
  } else {
    DEBUG_SERIAL.println("-> [ERROR] HTTP Configuration Failed. Halting.");
    while (true);
  }

  // ---------------------------------------------------------
  // [STEP 3] 1st Request: Basic GET
  // ---------------------------------------------------------
  DEBUG_SERIAL.println("\n[HTTP] 2. Sending 1st GET Request (Basic)...");
  
  rc = myME310.send_http_query(0, 0, "/get", ME310::TOUT_5SEC);
  
  if (rc == ME310::RETURN_VALID) {
    DEBUG_SERIAL.println("-> 1st QRY Command Accepted. Waiting for Server...");
    catchHttpRingAndReceive(0, 15000); 
  } else {
    DEBUG_SERIAL.println("-> [ERROR] 1st HTTP QRY Command Failed.");
  }

  delay(2000); // Short break for the server and modem before the next request

  // ---------------------------------------------------------
  // [STEP 4] 2nd Request: GET with Custom Headers (Using Overloaded Function)
  // ---------------------------------------------------------
  DEBUG_SERIAL.println("\n[HTTP] 3. Sending 2nd GET Request (With Custom Headers)...");
  
  // Bundle of custom headers (Separated by >> symbol)
  const char* extraHeaders = "Content-Type: application/json>>Authorization: Bearer Token123";
  
  // Profile: 0, Command: 0 (GET), Resource: "/get", Apply extra headers
  rc = myME310.send_http_query(0, 0, "/get", extraHeaders, ME310::TOUT_5SEC);
  
  if (rc == ME310::RETURN_VALID) {
    DEBUG_SERIAL.println("-> 2nd QRY Command Accepted. Waiting for Server...");
    catchHttpRingAndReceive(0, 15000); 
  } else {
    DEBUG_SERIAL.println("-> [ERROR] 2nd HTTP QRY Command Failed.");
  }

  DEBUG_SERIAL.println("\n=== All HTTP Procedures Done ===");
  myME310.powerOff(ON_OFF);
  Serial.println("ME310G1 Modem Power Off");
}

void loop() {
  // No loop execution (runs once)
}

// ==========================================
// Network Connection and IP Allocation Sequence Function
// ==========================================
bool connectNetwork() {
  DEBUG_SERIAL.println("\n--- Network & Time Verification ---");
  if (!sendATCommand("AT+CMEE=2", "OK", 3000)) return false;

  DEBUG_SERIAL.println("\n[INFO] Checking LTE network registration status (CEREG)...");
  uint32_t ceregStartTime = millis();
  bool ceregSuccess = false;

  while (millis() - ceregStartTime < 60000) { 
    String ceregResp = sendATCommandRead("AT+CEREG?", 2000);

    if (ceregResp.indexOf(",5") != -1 || ceregResp.indexOf(",1") != -1) {
      DEBUG_SERIAL.println("[SUCCESS] LTE Network Attach successful! Proceeding to data session stage.");
      ceregSuccess = true;
      break;
    }

    DEBUG_SERIAL.println("[WAIT] Still searching or registering to network... Retrying in 2 seconds.");
    delay(2000);
  }

  if (!ceregSuccess) {
    DEBUG_SERIAL.println("\n[ERROR] LTE network registration failed (Timeout). Aborting sequence.");
    return false;
  }

  DEBUG_SERIAL.println("\n[INFO] Checking Network IP allocation status (PDP Context)...");
  String sgactResp = sendATCommandRead("AT#SGACT?", 3000);

  if (sgactResp.indexOf("#SGACT: 1,1") >= 0) {
    DEBUG_SERIAL.println("\n[CHECK] Already connected to the network and IP allocated. (Skipping SGACT=1,1)");
  } else {
    DEBUG_SERIAL.println("\n[INFO] Network IP allocation required. Requesting activation.");
    if (!sendATCommand("AT#SGACT=1,1", "OK", 15000)) {
      DEBUG_SERIAL.println("\n[ERROR] Network activation failed.");
      return false;
    }
  }

  return true;
}

// ==========================================
// AT Command Send Function returning a String
// ==========================================
String sendATCommandRead(const String& cmd, uint32_t timeout) {
  while(MODEM_SERIAL.available()) MODEM_SERIAL.read(); 
  
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
    
    if (currentResponse.indexOf("\r\nOK\r\n") >= 0 || 
        currentResponse.indexOf("\r\nERROR\r\n") >= 0) {
      DEBUG_SERIAL.print(currentResponse);
      return currentResponse;
    }
  }
  
  DEBUG_SERIAL.print(currentResponse);
  DEBUG_SERIAL.println("\n[TIMEOUT]");
  return currentResponse;
}

bool sendATCommand(const String& cmd, const char* expected, uint32_t timeout) {
  while(MODEM_SERIAL.available()) MODEM_SERIAL.read();
  DEBUG_SERIAL.print("\n[TX] ");
  DEBUG_SERIAL.println(cmd);
  MODEM_SERIAL.println(cmd); 
  return waitForString(expected, timeout);
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
      DEBUG_SERIAL.println("\n[MODEM ERROR OR DISCONNECTED]");
      return false;
    }
  }
  DEBUG_SERIAL.print(currentResponse);
  DEBUG_SERIAL.println("\n[TIMEOUT]");
  return false;
}

// ==========================================
// Asynchronous URC (#HTTPRING) Capture & Library Raw Buffer Output (Final Version)
// ==========================================
bool catchHttpRingAndReceive(int prof_id, uint32_t timeout) {
  uint32_t startTime = millis();
  String currentResponse = "";
  currentResponse.reserve(256);

  String expectedRing = "#HTTPRING: " + String(prof_id) + ",";

  while (millis() - startTime < timeout) {
    while (MODEM_SERIAL.available()) {
      currentResponse += (char)MODEM_SERIAL.read();
    }

    int ringIndex = currentResponse.indexOf(expectedRing);
    if (ringIndex >= 0) {
      int newLineIndex = currentResponse.indexOf('\n', ringIndex);
      if (newLineIndex > ringIndex) {
        
        // Extract the RING notification line
        String ringLine = currentResponse.substring(ringIndex, newLineIndex);
        ringLine.trim();

        DEBUG_SERIAL.println("\n-------------------------------------");
        DEBUG_SERIAL.println("[URC DETECTED] " + ringLine);

        // Extract status code and data size
        int firstComma = ringLine.indexOf(',');
        int secondComma = ringLine.indexOf(',', firstComma + 1);
        int lastComma = ringLine.lastIndexOf(',');

        int statusCode = 0;
        int dataSize = 0;

        if (firstComma > 0 && secondComma > firstComma) {
          statusCode = ringLine.substring(firstComma + 1, secondComma).toInt();
        }
        if (lastComma > 0 && lastComma != secondComma) {
          dataSize = ringLine.substring(lastComma + 1).toInt();
        }

        DEBUG_SERIAL.print("[INFO] HTTP Status Code : ");
        DEBUG_SERIAL.println(statusCode);
        DEBUG_SERIAL.print("[INFO] Payload Data Size: ");
        DEBUG_SERIAL.print(dataSize);
        DEBUG_SERIAL.println(" Bytes");
        DEBUG_SERIAL.println("-------------------------------------");

        // Proceed with library reception only if data exists
        if (dataSize > 0) {
          DEBUG_SERIAL.print("\n[INFO] Fetching ");
          DEBUG_SERIAL.print(dataSize);
          DEBUG_SERIAL.println(" Bytes Payload via Library...\n");
          delay(50); 
          
          // 1. Delegate communication and buffering 100% to the library (3100-byte built-in buffer is plenty!)
          myME310.debugMode(false); 
          ME310::return_t recv_rc = myME310.receive_http_data(prof_id, dataSize, ME310::TOUT_9SEC);

          // 2. Extract data directly from the library's built-in _payloadData raw buffer after reception!
          if (recv_rc == ME310::RETURN_VALID) {
            DEBUG_SERIAL.println("[SUCCESS] HTTP Data fully received!");
            
            DEBUG_SERIAL.println("================ HTTP PAYLOAD ================");
            
            // Use buffer_cstr_raw() as intended to print only the pure payload!
            const char* rawData = myME310.buffer_cstr_raw();
            
            if (rawData != NULL) {
              DEBUG_SERIAL.print("Payload: <\n");
              DEBUG_SERIAL.print(rawData); 
              DEBUG_SERIAL.println("\n>");
            } else {
              DEBUG_SERIAL.println("[WARNING] buffer_cstr_raw() returned NULL.");
              DEBUG_SERIAL.println("-> Check if the _payloadData pointer is mapped as intended!");
            }
            
            DEBUG_SERIAL.println("==============================================");
            return true;
          } else {
            DEBUG_SERIAL.println("\n[ERROR] Library failed to receive HTTP data.");
            return false;
          }
        } else {
          DEBUG_SERIAL.println("\n[WARNING] Payload is 0 bytes. No data to receive.");
          return false; 
        }
      }
    }

    if (currentResponse.indexOf("\r\nERROR\r\n") >= 0 || currentResponse.indexOf("\r\nNO CARRIER\r\n") >= 0) {
      DEBUG_SERIAL.print(currentResponse);
      DEBUG_SERIAL.println("\n[MODEM ERROR DETECTED]");
      return false;
    }
  }

  DEBUG_SERIAL.print(currentResponse);
  DEBUG_SERIAL.println("\n[TIMEOUT] Waiting for #HTTPRING.");
  return false;
}