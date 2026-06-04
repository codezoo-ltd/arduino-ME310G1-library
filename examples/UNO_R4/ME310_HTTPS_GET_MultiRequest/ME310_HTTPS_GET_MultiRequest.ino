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
  DEBUG_SERIAL.println(" ME310G1 HTTPS Multi-Request Test     ");
  DEBUG_SERIAL.println("======================================");

  // ---------------------------------------------------------
  // [STEP 1] Check Network Cell Registration and IP Allocation
  // ---------------------------------------------------------
  if (!connectNetwork()) {
    DEBUG_SERIAL.println("\n[FATAL] Network Connection Failed. Halting system.");
    while (true); 
  }

  // ---------------------------------------------------------
  // [STEP 2] Configure HTTPS Server and SSL Parameters
  // ---------------------------------------------------------
  DEBUG_SERIAL.println("\n[HTTPS] 1. Configuring SSL and HTTP Parameters...");
  
  // 1. Disable standalone SSL configuration for SSID 1 to prevent conflict with the internal HTTP SSL engine
  myME310.ssl_enable(1, 0, ME310::TOUT_5SEC);
  
  // 2. Configure HTTP Server Parameters (Profile: 0, Host: httpbin.org, Port: 443, SSL Enabled: 1)
  rc = myME310.configure_http_parameters(0, "httpbin.org", 443, 0, 1, 120, 1, ME310::TOUT_5SEC);
  
  if (rc == ME310::RETURN_VALID) {
    DEBUG_SERIAL.println("-> HTTP Server Configuration Setup OK.");
  } else {
    DEBUG_SERIAL.println("-> [ERROR] HTTP Configuration Failed. Halting.");
    while (true);
  }

  // 3. Configure SSL Security Parameters (SSID: 1, CipherSuite: 0, AuthMode: 1)
  myME310.ssl_configure_security_param(1, 0, 1, ME310::TOUT_5SEC);

  // 4. Configure Additional SSL Parameters (SSID: 1, Version: 4, SNI: 1, PreloadedCA: 1, CustomCA: 1)
  myME310.ssl_additional_parameters(1, 4, 1, 1, 1, ME310::TOUT_5SEC);
  
  DEBUG_SERIAL.println("-> All SSL/TLS Parameters Applied.");

  // ---------------------------------------------------------
  // [STEP 3] 1st HTTPS Request: Basic GET
  // ---------------------------------------------------------
  DEBUG_SERIAL.println("\n[HTTPS] 2. Sending 1st HTTPS GET Request (Basic)...");
  
  rc = myME310.send_http_query(0, 0, "/get", ME310::TOUT_5SEC);
  
  if (rc == ME310::RETURN_VALID) {
    DEBUG_SERIAL.println("-> 1st QRY Command Accepted. Waiting for Secure Server...");
    catchHttpRingAndReceive(0, 15000); 
  } else {
    DEBUG_SERIAL.println("-> [ERROR] 1st HTTPS QRY Command Failed.");
  }

  delay(2000); // Short break for the server and modem before the next secure request

  // ---------------------------------------------------------
  // [STEP 4] 2nd HTTPS Request: GET with Custom Headers
  // ---------------------------------------------------------
  DEBUG_SERIAL.println("\n[HTTPS] 3. Sending 2nd HTTPS GET Request (With Custom Headers)...");
  
  // Bundle of custom headers (Separated by >> symbol)
  const char* extraHeaders = "Content-Type: application/json>>Authorization: Bearer Token123";
  
  rc = myME310.send_http_query(0, 0, "/get", extraHeaders, ME310::TOUT_5SEC);
  
  if (rc == ME310::RETURN_VALID) {
    DEBUG_SERIAL.println("-> 2nd QRY Command Accepted. Waiting for Secure Server...");
    catchHttpRingAndReceive(0, 15000); 
  } else {
    DEBUG_SERIAL.println("-> [ERROR] 2nd HTTPS QRY Command Failed.");
  }

  DEBUG_SERIAL.println("\n=== All HTTPS Procedures Done ===");

  //Reset and initialize the HTTP profile's SSL configuration to prevent conflicts in other tests
  rc = myME310.configure_http_parameters(0, "httpbin.org", 443, 0, 0, 120, 1, ME310::TOUT_5SEC);
  
  if (rc == ME310::RETURN_VALID) {
    DEBUG_SERIAL.println("-> HTTP Server Configuration Reset OK.");
  } else {
    DEBUG_SERIAL.println("-> [ERROR] HTTP Configuration Reset Failed. Halting.");
    while (true);
  }   

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

  //NTP Server setup (require of ssl sequence)
  rc = myME310.configure_ntp_parameters(1,0,0,"",ME310::TOUT_5SEC);
  if (rc == ME310::RETURN_VALID) {
    DEBUG_SERIAL.println("-> NTP Configuration OK.");
  } else {
    DEBUG_SERIAL.println("-> [ERROR] NTP Configuration Failed. Halting.");
    while (true);
  } 

  //+36 ( +9(hour) x 4(15min) )
  rc = myME310.ntp("216.239.35.0",123,1,5,36,ME310::TOUT_10SEC);
  if (rc == ME310::RETURN_VALID) {
    DEBUG_SERIAL.println("-> NTP Configuration OK.");
  } else {
    DEBUG_SERIAL.println("-> [ERROR] NTP Configuration Failed. Halting.");
    while (true);
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
// Asynchronous URC (#HTTPRING) Capture & Library Raw Buffer Output
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
        
        String ringLine = currentResponse.substring(ringIndex, newLineIndex);
        ringLine.trim();

        DEBUG_SERIAL.println("\n-------------------------------------");
        DEBUG_SERIAL.println("[URC DETECTED] " + ringLine);

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

        if (dataSize > 0) {
          DEBUG_SERIAL.print("\n[INFO] Fetching ");
          DEBUG_SERIAL.print(dataSize);
          DEBUG_SERIAL.println(" Bytes Payload via Library...\n");
          delay(50); 
          
          myME310.debugMode(false); 
          ME310::return_t recv_rc = myME310.receive_http_data(prof_id, dataSize, ME310::TOUT_9SEC);

          if (recv_rc == ME310::RETURN_VALID) {
            DEBUG_SERIAL.println("[SUCCESS] HTTP Data fully received!");
            DEBUG_SERIAL.println("================ HTTP PAYLOAD ================");
            
            const char* rawData = myME310.buffer_cstr_raw();
            
            if (rawData != NULL) {
              DEBUG_SERIAL.print("Payload: <\n");
              DEBUG_SERIAL.print(rawData); 
              DEBUG_SERIAL.println("\n>");
            } else {
              DEBUG_SERIAL.println("[WARNING] buffer_cstr_raw() returned NULL.");
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