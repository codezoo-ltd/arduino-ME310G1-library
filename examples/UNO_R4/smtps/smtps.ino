// ==========================================
// [Settings] Configure for your environment
// ==========================================
#include <Arduino.h>
#include <ME310.h>

#define ON_OFF 2 /*Select the GPIO to control ON_OFF*/

using namespace me310;

ME310 myME310;
ME310::return_t rc;  // Enum of return value methods

// ==========================================
// [Settings] Configure for your environment
// ==========================================
const char* BASE64_USER_ID = "YOUR_BASE64_GMAIL_ID";      // Gmail ID (Base64 Encoded)
const char* BASE64_PASSWORD = "YOUR_BASE64_APP_PASSWORD"; // Gmail App Password (Base64 Encoded)

const char* SENDER_EMAIL = "sender@gmail.com";    // Sender Email Address
const char* RECV_EMAIL = "receiver@domain.comr";  // Receiver Email Address

const char* EMAIL_SUBJECT = "ME310G1 SMTPS Test";                                           // Email Subject
const char* EMAIL_BODY = "Hello.\nThis is an SMTPS test code using the CZ-ME310G1 modem.";  // Email Body

// ==========================================
// Function Declarations
// ==========================================
bool sendATCommand(const String& cmd, const char* expected, uint32_t timeout);
String sendATCommandRead(const String& cmd, uint32_t timeout);
bool waitForString(const char* expected, uint32_t timeout);
bool waitForSslSringAndRecv(uint32_t timeout);
void executeSmtpsSequence();

void setup() {
  // Initialize Arduino UNO R4 hardware serial
  Serial.begin(115200);   // Serial0 (For computer debug output)
  Serial1.begin(115200);  // Serial1 (For ME310G1 module connection)

  while (!Serial)
    ;
  delay(2000);

  // Set default serial timeout to prevent readStringUntil from blocking too long
  Serial1.setTimeout(50);

  myME310.debugMode(true);

  Serial.println("\n[INIT] Powering on modem...");
  myME310.powerOn(ON_OFF);

  Serial.println("==============================================");
  Serial.println("ME310G1 Gmail SMTPS Command Mode Control Start");
  Serial.println("==============================================");

  // Execute email sending sequence
  executeSmtpsSequence();

  myME310.powerOff(ON_OFF);
  Serial.println("ME310G1 Modem Power Off");
}

void loop() {
  // No loop execution (runs once)
}

// ==========================================
// Core SMTPS Master Sequence Execution Function
// ==========================================
void executeSmtpsSequence() {
  // Step 1. Initialization and Network Connection
  Serial.println("\n--- Step 1. Network & Time Verification ---");
  if (!sendATCommand("AT+CMEE=2", "OK", 3000)) return;

  // ----------------------------------------------------------------
  // [Added] Loop to pre-check LTE Cat-M1 network registration status (CEREG)
  // ----------------------------------------------------------------
  Serial.println("\n[INFO] Checking LTE network registration status (CEREG)...");
  uint32_t ceregStartTime = millis();
  bool ceregSuccess = false;

  while (millis() - ceregStartTime < 60000) {  // Wait up to 60 seconds for network attachment
    String ceregResp = sendATCommandRead("AT+CEREG?", 2000);

    // Since it's a Vodafone roaming SIM, it returns ",5" (Registered, roaming) upon successful registration
    // (Included ",1" (Home) as an exception just in case a local SIM is used)
    if (ceregResp.indexOf(",5") != -1 || ceregResp.indexOf(",1") != -1) {
      Serial.println("[SUCCESS] LTE Network Attach successful! Proceeding to data session stage.");
      ceregSuccess = true;
      break;
    }

    Serial.println("[WAIT] Still searching or registering to network... Retrying in 2 seconds.");
    delay(2000);
  }

  // If network registration fails after 60 seconds, abort sequence safely
  if (!ceregSuccess) {
    Serial.println("\n[ERROR] LTE network registration failed (Timeout). Aborting sequence.");
    return;
  }

  Serial.println("\n[INFO] Checking Network IP allocation status (PDP Context)...");
  String sgactResp = sendATCommandRead("AT#SGACT?", 3000);

  if (sgactResp.indexOf("#SGACT: 1,1") >= 0) {
    // If 1,1 is present, it's already activated
    Serial.println("\n[CHECK] Already connected to the network and IP allocated. (Skipping SGACT=1,1)");
  } else {
    // If not allocated, execute activation command
    Serial.println("\n[INFO] Network IP allocation required. Requesting activation.");
    if (!sendATCommand("AT#SGACT=1,1", "OK", 15000)) {
      Serial.println("\n[ERROR] Network activation failed.");
      return;
    }
  }

  // [Step 1-1] Bind NTP engine to Internet Session 1 (SGACT 1)
  if (!sendATCommand("AT#NTPCFG=1", "OK", 3000)) {
    Serial.println("\n[ERROR] NTP Config Fail");
    return;
  }

  // [Step 1-2] NTP Sync request
  if (!sendATCommand("AT#NTP=\"216.239.35.0\",123,1,5,36", "#NTP:", 15000)) {
    Serial.println("\n[ERROR] NTP Setup Fail");
    return;
  }

  // ----------------------------------------------------

  // Step 2. SSL Secure Socket Environment Setup
  Serial.println("\n--- Step 2. SSL Environment Setup ---");
  if (!sendATCommand("AT#SSLEN=1,1", "OK", 3000)) return;
  if (!sendATCommand("AT#SSLCFG=1,1,300,90,600,50,1", "OK", 3000)) return;
  if (!sendATCommand("AT#SSLSECCFG=1,0,1", "OK", 3000)) return;
  if (!sendATCommand("AT#SSLSECCFG2=1,3,1,1,0", "OK", 3000)) return;

  // Step 3. SMTPS Socket Connection via Command Mode
  Serial.println("\n--- Step 3. Gmail Server Connection ---");
  if (!sendATCommand("AT#SSLD=1,465,\"smtp.gmail.com\",0,1", "OK", 15000)) return;
  if (!waitForSslSringAndRecv(10000)) return;

  // Step 4. Proceed with SMTP Communication (Send & Recv Polling)
  Serial.println("\n--- Step 4. SMTP Protocol Progress ---");

  // 1) EHLO Greeting
  if (!sendATCommand("AT#SSLSENDEXT=1,16", ">", 3000)) return;
  Serial1.print("EHLO gmail.com\r\n");
  if (!waitForString("OK", 3000)) return;
  if (!waitForSslSringAndRecv(5000)) return;

  // 2) AUTH LOGIN Request
  if (!sendATCommand("AT#SSLSENDEXT=1,12", ">", 3000)) return;
  Serial1.print("AUTH LOGIN\r\n");
  if (!waitForString("OK", 3000)) return;
  if (!waitForSslSringAndRecv(5000)) return;

  // 3) BASE64 ID Transmission
  String idPayload = String(BASE64_USER_ID) + "\r\n";
  if (!sendATCommand("AT#SSLSENDEXT=1," + String(idPayload.length()), ">", 3000)) return;
  Serial1.print(idPayload);
  if (!waitForString("OK", 3000)) return;
  if (!waitForSslSringAndRecv(5000)) return;

  // 4) BASE64 PASSWORD Transmission
  String pwPayload = String(BASE64_PASSWORD) + "\r\n";
  if (!sendATCommand("AT#SSLSENDEXT=1," + String(pwPayload.length()), ">", 3000)) return;
  Serial1.print(pwPayload);
  if (!waitForString("OK", 3000)) return;
  if (!waitForSslSringAndRecv(5000)) return;

  // 5) MAIL FROM Transmission
  String mailFrom = "MAIL FROM:<" + String(SENDER_EMAIL) + ">\r\n";
  if (!sendATCommand("AT#SSLSENDEXT=1," + String(mailFrom.length()), ">", 3000)) return;
  Serial1.print(mailFrom);
  if (!waitForString("OK", 3000)) return;
  if (!waitForSslSringAndRecv(5000)) return;

  // 6) RCPT TO Transmission
  String rcptTo = "RCPT TO:<" + String(RECV_EMAIL) + ">\r\n";
  if (!sendATCommand("AT#SSLSENDEXT=1," + String(rcptTo.length()), ">", 3000)) return;
  Serial1.print(rcptTo);
  if (!waitForString("OK", 3000)) return;
  if (!waitForSslSringAndRecv(5000)) return;

  // 7) DATA Command Transmission
  if (!sendATCommand("AT#SSLSENDEXT=1,6", ">", 3000)) return;
  Serial1.print("DATA\r\n");
  if (!waitForString("OK", 3000)) return;
  if (!waitForSslSringAndRecv(5000)) return;

  // 8) Email Body Build and Transmission
  String emailPayload = "";
  emailPayload += "Subject: " + String(EMAIL_SUBJECT) + "\r\n\r\n";

  String bodyStr = String(EMAIL_BODY);
  bodyStr.replace("\r\n", "\n");
  bodyStr.replace("\n", "\r\n");
  emailPayload += bodyStr;

  if (!emailPayload.endsWith("\r\n")) {
    emailPayload += "\r\n";
  }
  emailPayload += ".\r\n";

  if (!sendATCommand("AT#SSLSENDEXT=1," + String(emailPayload.length()), ">", 3000)) return;
  Serial1.print(emailPayload);
  if (!waitForString("OK", 5000)) return;
  if (!waitForSslSringAndRecv(10000)) return;

  // Step 5. Communication Termination and Socket Release
  Serial.println("\n--- Step 5. Session Termination and Socket Release ---");
  if (!sendATCommand("AT#SSLSENDEXT=1,6", ">", 3000)) return;
  Serial1.print("QUIT\r\n");
  if (!waitForString("OK", 3000)) return;
  if (!waitForSslSringAndRecv(5000)) return;

  waitForString("CARRIER", 5000);
  sendATCommand("AT#SSLH=1", "OK", 3000);

  Serial.println("\n[SUCCESS] Email sending master sequence completed successfully!");
}

String sendATCommandRead(const String& cmd, uint32_t timeout) {
  while (Serial1.available()) Serial1.read();

  Serial.print("\n[TX] ");
  Serial.println(cmd);
  Serial1.println(cmd);

  uint32_t startTime = millis();
  String response = "";
  response.reserve(256);

  while (millis() - startTime < timeout) {
    while (Serial1.available()) {
      response += (char)Serial1.read();
    }
    // Early exit upon confirming complete OK or ERROR (Lossless detection applied)
    if (response.indexOf("\r\nOK\r\n") >= 0 || response.indexOf("\r\nERROR\r\n") >= 0) {
      break;
    }
  }
  Serial.print(response);  // Output read contents to computer monitor (Debug)
  return response;
}

bool sendATCommand(const String& cmd, const char* expected, uint32_t timeout) {
  while (Serial1.available()) Serial1.read();  // Clear remaining buffer

  Serial.print("\n[TX] ");
  Serial.println(cmd);
  Serial1.println(cmd);

  return waitForString(expected, timeout);
}

bool waitForString(const char* expected, uint32_t timeout) {
  uint32_t startTime = millis();
  String currentResponse = "";
  currentResponse.reserve(512);

  String target = String(expected);
  if (target == "OK") {
    target = "\r\nOK\r\n";
  }

  while (millis() - startTime < timeout) {
    while (Serial1.available()) {
      currentResponse += (char)Serial1.read();
    }

    if (currentResponse.indexOf(target) >= 0) {
      Serial.print(currentResponse);
      return true;
    }

    if (currentResponse.indexOf("\r\nERROR\r\n") >= 0 || currentResponse.indexOf("\r\nNO CARRIER\r\n") >= 0) {
      Serial.print(currentResponse);
      Serial.println("\n[MODEM ERROR OR DISCONNECTED]");
      return false;
    }
  }

  Serial.print(currentResponse);
  Serial.print("\n[ERROR] Timeout occurred - Expected response missing: ");
  Serial.println(expected);
  return false;
}

bool waitForSslSringAndRecv(uint32_t timeout) {
  uint32_t startTime = millis();
  String currentResponse = "";
  currentResponse.reserve(512);

  while (millis() - startTime < timeout) {
    while (Serial1.available()) {
      currentResponse += (char)Serial1.read();
    }

    int ringIndex = currentResponse.indexOf("SSLSRING: 1,");
    if (ringIndex >= 0) {
      int newLineIndex = currentResponse.indexOf('\n', ringIndex);

      if (newLineIndex > ringIndex) {
        Serial.print(currentResponse);

        String lenLine = currentResponse.substring(ringIndex + 12, newLineIndex);
        lenLine.trim();

        int bytesToRead = lenLine.toInt();

        if (bytesToRead > 0) {
          delay(50);
          Serial.print("\n[INFO DETECTED] Server Receive Data Capacity: ");
          Serial.print(bytesToRead);
          Serial.println(" bytes receive command executing");

          String recvCmd = "AT#SSLRECV=1," + String(bytesToRead);
          Serial1.println(recvCmd);

          return waitForString("OK", 5000);
        }
      }
    }
  }

  Serial.print(currentResponse);
  Serial.println("\n[ERROR] SSLSRING notification timeout failed");
  return false;
}
