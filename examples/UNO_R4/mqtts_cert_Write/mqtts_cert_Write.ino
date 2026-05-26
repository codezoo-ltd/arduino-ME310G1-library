#include <Arduino.h>
#include <ME310.h>

#define ON_OFF 2 /*Select the GPIO to control ON_OFF*/

using namespace me310;

/*
 * If a Telit-Board Charlie is not in use, the ME310 class needs the Uart Serial
 * instance in the constructor, that will be used to communicate with the
 * modem.\n Please refer to your board configuration in variant.h file. Example:
 * Uart Serial1(&sercom4, PIN_MODULE_RX, PIN_MODULE_TX, PAD_MODULE_RX,
 * PAD_MODULE_TX, PIN_MODULE_RTS, PIN_MODULE_CTS); ME310 myME310 (Serial1);
 */
ME310 myME310;
ME310::return_t rc; // Enum of return value methods

// --- Hardware Serial Setup ---
#define MODEM_SERIAL Serial1 
#define DEBUG_SERIAL Serial

// --- SSL Configuration Macro Definitions ---
#define SSL_DATA_CLIENT_CERT 0  // Client Certificate (Device Cert)
#define SSL_DATA_ROOT_CA     1  // Root CA (For server certificate verification)
#define SSL_DATA_CLIENT_KEY  2  // Client RSA Private Key

#define SSL_ACTION_DELETE    0  // Delete
#define SSL_ACTION_SAVE      1  // Save
#define SSL_ACTION_READ      2  // Read (Verify)

// --- Root CA Certificate (Amazon Root CA 1) ---
const char* rootCA = R"EOF(-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----)EOF";

// --- Device Certificate ---
const char* deviceCert = R"EOF(-----BEGIN CERTIFICATE-----
-----END CERTIFICATE-----)EOF";

// --- Private Key ---
const char* privateKey = R"EOF(-----BEGIN RSA PRIVATE KEY-----
-----END RSA PRIVATE KEY-----)EOF";

// --- Utility Function Declarations ---
bool waitResponse(const char* expected, unsigned long timeout = 3000);
void sendCommand(const char* cmd);
void deleteCertificate(int slot, int type);
void saveCertificate(int slot, int type, int size);
void readCertificate(int slot, int type);
bool uploadCertificateData(const char* certData); // Function for chunk-based upload

void setup() {
  DEBUG_SERIAL.begin(115200);
  MODEM_SERIAL.begin(115200);

  delay(100);
  myME310.debugMode(true); 
  
  DEBUG_SERIAL.println("\n[INIT] Powering on modem...");
  myME310.powerOn(ON_OFF); 
  
  DEBUG_SERIAL.println("\n=== ME310G1 SSL Provisioning Start ===");

  DEBUG_SERIAL.println("\n[STEP 0] Disable Modem Echo...");
  sendCommand("ATE0"); 
  waitResponse("OK");

  int targetSlot = 1;

  // 1. Enable SSL feature
  DEBUG_SERIAL.println("\n[STEP 1] Enabling SSL...");
  sendCommand("AT#SSLEN=1,1");
  waitResponse("OK");

  // ==========================================
  // [STEP 2] 1. Process Root CA
  // ==========================================
  DEBUG_SERIAL.println("\n--- Processing Root CA ---");
  deleteCertificate(targetSlot, SSL_DATA_ROOT_CA);
  waitResponse("OK", 2000); 

  saveCertificate(targetSlot, SSL_DATA_ROOT_CA, strlen(rootCA));
  if (uploadCertificateData(rootCA)) {
     DEBUG_SERIAL.println("\n[VERIFY] Root CA:");
     readCertificate(targetSlot, SSL_DATA_ROOT_CA);
     waitResponse("OK", 5000);
  }

  // ==========================================
  // [STEP 3] 2. Process Device Certificate
  // ==========================================
  DEBUG_SERIAL.println("\n--- Processing Device Certificate ---");
  deleteCertificate(targetSlot, SSL_DATA_CLIENT_CERT);
  waitResponse("OK", 2000); 

  saveCertificate(targetSlot, SSL_DATA_CLIENT_CERT, strlen(deviceCert));
  if (uploadCertificateData(deviceCert)) {
     DEBUG_SERIAL.println("\n[VERIFY] Device Certificate:");
     readCertificate(targetSlot, SSL_DATA_CLIENT_CERT);
     waitResponse("OK", 5000);
  }

  // ==========================================
  // [STEP 4] 3. Process Private Key
  // ==========================================
  DEBUG_SERIAL.println("\n--- Processing Private Key ---");
  deleteCertificate(targetSlot, SSL_DATA_CLIENT_KEY);
  waitResponse("OK", 2000); 

  saveCertificate(targetSlot, SSL_DATA_CLIENT_KEY, strlen(privateKey));
  if (uploadCertificateData(privateKey)) {
     DEBUG_SERIAL.println("\n[VERIFY] Private Key:");
     readCertificate(targetSlot, SSL_DATA_CLIENT_KEY);
     waitResponse("OK", 5000);
  }

  DEBUG_SERIAL.println("\n=== All Provisioning Done ===");
  myME310.powerOff(ON_OFF);
  DEBUG_SERIAL.println("ME310G1 Modem Power Off");  
}

void loop() {
  // No loop execution (runs once)
}

// --- Utility Function Implementations ---
void sendCommand(const char* cmd) {
  while (MODEM_SERIAL.available()) {
    MODEM_SERIAL.read();
  }
  DEBUG_SERIAL.print("\n[SEND] ");
  DEBUG_SERIAL.println(cmd);
  MODEM_SERIAL.println(cmd);
}

void deleteCertificate(int slot, int type) {
  String cmd = "AT#SSLSECDATA=" + String(slot) + "," + String(SSL_ACTION_DELETE) + "," + String(type);
  sendCommand(cmd.c_str());
}

void saveCertificate(int slot, int type, int size) {
  String cmd = "AT#SSLSECDATA=" + String(slot) + "," + String(SSL_ACTION_SAVE) + "," + String(type) + "," + String(size);
  sendCommand(cmd.c_str());
}

void readCertificate(int slot, int type) {
  String cmd = "AT#SSLSECDATA=" + String(slot) + "," + String(SSL_ACTION_READ) + "," + String(type);
  sendCommand(cmd.c_str());
}

bool uploadCertificateData(const char* certData) {
  if (waitResponse(">", 5000)) {
    DEBUG_SERIAL.println("\n[INFO] Prompt '>' received. Uploading data...");
    
    int certLen = strlen(certData);
    for (int i = 0; i < certLen; i += 64) {
      int chunkSize = (certLen - i) < 64 ? (certLen - i) : 64;
      MODEM_SERIAL.write(certData + i, chunkSize);
      MODEM_SERIAL.flush(); 
      delay(10); 
    }
    
    MODEM_SERIAL.write(0x1A); // Ctrl+Z
    DEBUG_SERIAL.println("\n[INFO] Ctrl+Z (0x1A) sent. Waiting for save confirmation...");
    
    if (waitResponse("OK", 10000)) {
      DEBUG_SERIAL.println("\n[SUCCESS] Data saved successfully!");
      return true;
    } else {
      DEBUG_SERIAL.println("\n[ERROR] Timeout! Failed to save data.");
      return false;
    }
  } else {
    DEBUG_SERIAL.println("\n[ERROR] Didn't receive '>' prompt.");
    return false;
  }
}

bool waitResponse(const char* expected, unsigned long timeout) {
  unsigned long start = millis();
  String response = "";
  response.reserve(2048); 

  while (millis() - start < timeout) {
    while (MODEM_SERIAL.available()) {
      response += (char)MODEM_SERIAL.read();
    }
    
    if (response.indexOf(expected) >= 0) {
      DEBUG_SERIAL.print(response); 
      return true;
    }
    
    if (String(expected) != "ERROR" && response.indexOf("ERROR") >= 0) {
      DEBUG_SERIAL.print(response);
      DEBUG_SERIAL.println("\n[MODEM ERROR DETECTED]");
      return false;
    }
  }
  
  DEBUG_SERIAL.print(response);
  DEBUG_SERIAL.println("\n[TIMEOUT]");
  return false;
}
