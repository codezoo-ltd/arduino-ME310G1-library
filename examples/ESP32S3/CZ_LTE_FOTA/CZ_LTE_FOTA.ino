#include <Arduino.h>
#include <Update.h>
#include <ME310.h>

#define ON_OFF 2  /* Select the GPIO to control ON_OFF */
#define PWR_PIN 1 /* Select the GPIO to control LDO */

using namespace me310;

// ==========================================
// User Configuration
// ==========================================
#define CHUNK_SIZE 1024 // Chunk size in bytes for data fetch

/* <server:port>
 * address and port of FTP server
 * (factory default port 21), in the format:
 * "ipv4" / "ipv4:port"
 * "ipv6" / "[ipv6]" / "[ipv6]:port"
 * "dynamic_name" /
 * "dynamic_name:port"
 *  
 *  Addr:200.201.202.203, Port:21
 *  #define FTP_ADDR_PORT "200.201.202.203:21"
 *
 *  Addr:ftp.server.com, Port:21
 *  #define FTP_ADDR_PORT "ftp.server.com:21"
 */
#define FTP_ADDR_PORT "PORT"

#define FTP_USER "CLIENTUSER"
#define FTP_PASS "PASSWORD"

const char* fw_filename = "fw.bin"; // Update Firmware file name
const char* md5_filename = "fw.bin.md5"; // MD5 checksum file name

// Hardware Serial 2 (ESP32S3)
HardwareSerial ModemSerial(2);
ME310 myME310(ModemSerial);
ME310::return_t rc;   // Enum of return value methods
int cID = 1;          // PDP Context Identifier
char ipProt[] = "IP"; // Packet Data Protocol type

#define APN "simplio.apn"

// ==========================================
// Helper Function: Hex String -> 1-Byte Binary
// ==========================================
uint8_t hexToByte(char high, char low) {
  auto hexCharVal = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
  };
  return (hexCharVal(high) << 4) | hexCharVal(low);
}

// ==========================================
// Helper Function: Send AT Command & Wait Response
// ==========================================
String sendCommand(String cmd, uint32_t timeout_ms = 1000) {
  ModemSerial.println(cmd);
  uint32_t start_time = millis();
  String response = "";

  while (millis() - start_time < timeout_ms) {
    while (ModemSerial.available()) {
      char c = ModemSerial.read();
      response += c;
    }
  }
  return response;
}

// ==========================================
// Download MD5 File from FTP Server
// ==========================================
String downloadMD5() {
  Serial.println("[MD5] Receiving MD5 file from FTP server...");

  // 1. Flush serial buffer prior to command
  while (ModemSerial.available()) ModemSerial.read();

  // 2. Request packet mode reception for MD5 file
  ModemSerial.println("AT#FTPGETPKT=\"" + String(md5_filename) + "\",0");

  // Exit immediately upon receiving "OK" or "ERROR" (Max timeout: 10s)
  uint32_t start_time = millis();
  String pktResponse = "";
  while (millis() - start_time < 10000) {
    while (ModemSerial.available()) {
      pktResponse += (char)ModemSerial.read();
    }
    if (pktResponse.indexOf("OK\r\n") != -1 || pktResponse.indexOf("ERROR\r\n") != -1) {
      break;
    }
  }

  // 3. Request fetch of 34 bytes (32-byte MD5 + 2-byte CRLF)
  ModemSerial.println("AT#FTPRECV=34");

  start_time = millis();
  String response = "";
  String md5_val = "";

  // Exit immediately once 32 hex characters are collected and OK is received
  while (millis() - start_time < 3000) {
    while (ModemSerial.available()) {
      response += (char)ModemSerial.read();
    }

    // Parse MD5 hex string
    int startIdx = response.indexOf("#FTPRECV:");
    if (startIdx != -1) {
      int dataStart = response.indexOf('\n', startIdx) + 1;
      md5_val = "";
      for (int i = dataStart; i < response.length(); i++) {
        if (isxdigit(response[i])) {
          md5_val += response[i];
          if (md5_val.length() == 32) break;
        }
      }
    }

    // Break loop when 32 chars are extracted and OK is confirmed
    if (md5_val.length() == 32 && response.indexOf("OK\r\n") != -1) {
      break;
    }
  }

  // 4. Clean up remaining serial buffer (50ms)
  uint32_t quiet_time = millis();
  while (millis() - quiet_time < 50) {
    if (ModemSerial.available()) {
      ModemSerial.read();
      quiet_time = millis();
    }
  }

  return md5_val;
}

// ==========================================
// Main FOTA Routine
// ==========================================
void performFOTA() {
  Serial.println("\n========== Starting FOTA Process ==========");
  rc = myME310.ftp_close();
  if (rc == ME310::RETURN_VALID) {
    delay(2000);
    myME310.debugMode(false);
    rc = myME310.ftp_open(FTP_ADDR_PORT, FTP_USER, FTP_PASS, 0, cID, ME310::TOUT_2MIN);
//    Serial.println(myME310.buffer_cstr(0));
//    Serial.print("FTP open status: ");
//    Serial.println(myME310.return_string(rc));

    if (rc == ME310::RETURN_VALID) {
      myME310.debugMode(true);
      Serial.print("FTP Change Working Directory: ");
      rc = myME310.ftp_change_working_directory("/", ME310::TOUT_10SEC);
      Serial.println(myME310.return_string(rc));
    } else {
      Serial.println("[Error] Failed to connect to FTP server.");
      return;
    }
  }

  sendCommand("AT#FTPTYPE=0", 500);

  String expected_md5 = downloadMD5();
  if (expected_md5.length() != 32) {
    Serial.println("[Error] Failed to retrieve valid MD5 hash from FTP.");
    myME310.ftp_close();
    return;
  }
  Serial.printf("[MD5] Expected MD5 received: %s\n", expected_md5.c_str());

  String sizeResp = sendCommand("AT#FTPFSIZE=\"" + String(fw_filename) + "\"", 2000);
  int sizeIdx = sizeResp.indexOf("#FTPFSIZE:");
  if (sizeIdx == -1) {
    Serial.println("[Error] Failed to retrieve file size from server.");
    myME310.ftp_close();
    return;
  }

  String sizeStr = "";
  for (int i = sizeIdx + 11; i < sizeResp.length(); i++) {
    if (isDigit(sizeResp[i])) sizeStr += sizeResp[i];
    else if (sizeStr.length() > 0) break;
  }
  size_t total_size = sizeStr.toInt();
  Serial.println("");
  Serial.printf("[FOTA] Total firmware size: %d Bytes\n", total_size);

  if (total_size == 0) {
    myME310.ftp_close();
    return;
  }

  if (!Update.begin(total_size)) {
    Serial.println("[Error] ESP32 Update API initialization failed.");
    Update.printError(Serial);
    myME310.ftp_close();
    return;
  }

  if (!Update.setMD5(expected_md5.c_str())) {
    Serial.println("[Error] Invalid MD5 format.");
    myME310.ftp_close();
    return;
  }

  sendCommand("AT#FTPGETPKT=\"" + String(fw_filename) + "\",1", 3000);
  Serial.println("");
  Serial.println("[FOTA] Starting streaming download to modem socket buffer...");

  size_t total_written = 0;
  uint8_t binary_buffer[CHUNK_SIZE];

  // Counter for timeout / zero-byte reception guard
  int empty_retries = 0;
  const int MAX_EMPTY_RETRIES = 25; // Timeout after 25 consecutive failures (~5 sec)

  // Loop until total_written reaches total_size
  while (total_written < total_size) {
    size_t bytes_to_request = min((size_t)CHUNK_SIZE, total_size - total_written);

    // 1. Clear residual serial buffer before request
    while (ModemSerial.available()) ModemSerial.read();

    // 2. Request data fetch
    ModemSerial.print("AT#FTPRECV=");
    ModemSerial.println(bytes_to_request);

    // 3. Header parsing (#FTPRECV: <actual_bytes>)
    uint32_t req_time = millis();
    String recvHeader = "";
    bool headerFound = false;

    while (millis() - req_time < 3000) {
      if (ModemSerial.available()) {
        char c = ModemSerial.read();
        recvHeader += c;
        if (recvHeader.indexOf("#FTPRECV:") != -1 && c == '\n') {
          headerFound = true;
          break;
        }
      }
    }

    if (!headerFound) {
      Serial.println("\n[Warning] Header wait timeout, retrying...");
      empty_retries++;
      if (empty_retries > MAX_EMPTY_RETRIES) {
        Serial.println("\n[Error] No response from modem (FOTA aborted due to timeout)");
        Update.abort();
        myME310.ftp_close();
        return;
      }
      delay(200);
      continue;
    }

    int countIdx = recvHeader.indexOf("#FTPRECV:");
    String countStr = "";
    for (int i = countIdx + 9; i < recvHeader.length(); i++) {
      if (isDigit(recvHeader[i])) countStr += recvHeader[i];
      else if (countStr.length() > 0) break;
    }
    size_t actual_bytes = countStr.toInt();

    // Guard logic for empty buffer or delayed data
    if (actual_bytes == 0) {
      empty_retries++;
      if (empty_retries > MAX_EMPTY_RETRIES) {
        Serial.println("\n[Error] Data reception stopped (Network disconnected or server closed)");
        Update.abort();
        myME310.ftp_close();
        return;
      }
      delay(200);
      continue;
    }

    // Reset retry counter on successful reception
    empty_retries = 0;

    // 4. Direct Stream Parsing: Convert 2-byte Hex to 1-byte binary directly from serial
    size_t bytes_parsed = 0;
    req_time = millis();

    while (bytes_parsed < actual_bytes && (millis() - req_time < 3000)) {
      if (ModemSerial.available() >= 2) {
        char high = ModemSerial.read();
        char low  = ModemSerial.read();

        // Skip non-hex characters (newlines, spaces, etc.)
        if (!isxdigit(high) || !isxdigit(low)) {
          continue;
        }

        binary_buffer[bytes_parsed++] = hexToByte(high, low);
      }
    }

    if (bytes_parsed < actual_bytes) {
      Serial.println("\n[Error] Data reception timeout or packet loss occurred.");
      Update.abort();
      myME310.ftp_close();
      return;
    }

    // 5. Write to ESP32 Flash partition
    if (Update.write(binary_buffer, actual_bytes) != actual_bytes) {
      Serial.println("\n[Error] ESP32 flash memory write error!");
      Update.printError(Serial);
      Update.abort();
      myME310.ftp_close();
      return;
    }

    total_written += actual_bytes;

    // Single-line progress update using '\r'
    int percent = (total_written * 100) / total_size;
    Serial.printf("\r[FOTA Progress] %7d / %7d Bytes (%3d%%)", total_written, total_size, percent);

    // 6. Consume trailing \r\nOK\r\n
    req_time = millis();
    while (millis() - req_time < 100) {
      if (ModemSerial.available()) ModemSerial.read();
    }
  }

  // Print newline after progress bar completes
  Serial.println();
  Serial.println("[FOTA] Successfully received 100% of specified firmware size!");

  // 7. Close modem FTP session
  myME310.ftp_close();
  delay(500);

  // 8. Final verification of MD5 & size, then switch boot partition
  if (Update.end(true)) {
    Serial.println("[Success] MD5 checksum verified! Rebooting in 5 seconds...");
    delay(5000);
    ESP.restart();
  } else {
    Serial.println("[Error] MD5 checksum mismatch or firmware verification failed!");
    Update.printError(Serial);
  }
}

void setup() {
  Serial.begin(115200);
  ModemSerial.begin(115200, SERIAL_8N1, 37, 38); // RX2: 37, TX2: 38

  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, HIGH);
  delay(100);
  myME310.debugMode(true);

  Serial.println("TURN ON ME310");
  myME310.powerOn(ON_OFF);

  Serial.println("System Initialized.");
  myME310.report_mobile_equipment_error(2);

  rc = myME310.read_enter_pin();
  if (rc == ME310::RETURN_VALID) {
    char *resp = (char *)myME310.buffer_cstr(2);
    if (resp != NULL) {
      if (strcmp(resp, "OK") == 0) {
        Serial.println("Define PDP Context");

        rc = myME310.define_pdp_context(cID, ipProt, APN);
        if (rc == ME310::RETURN_VALID) {
          myME310.read_define_pdp_context();
          Serial.print("pdp context read :");
          Serial.println(myME310.buffer_cstr(1));

          Serial.print("gprs network registration status :");

          rc = myME310.read_gprs_network_registration_status();
          Serial.println(myME310.buffer_cstr(1));
          if (rc == ME310::RETURN_VALID) {
            resp = (char *)myME310.buffer_cstr(1);
            while (resp != NULL) {
              if ((strcmp(resp, "+CGREG: 0,1") != 0) &&
                  (strcmp(resp, "+CGREG: 0,5") != 0)) {
                delay(3000);
                rc = myME310.read_gprs_network_registration_status();
                if (rc != ME310::RETURN_VALID) {
                  Serial.println("ERROR");
                  Serial.println(myME310.return_string(rc));
                  break;
                }
                Serial.println(myME310.buffer_cstr(1));
                resp = (char *)myME310.buffer_cstr(1);
              } else {
                break;
              }
            }
          }
          Serial.println("Activate context");
          myME310.context_activation(cID, 1);
        }
      } else {
        Serial.println((String) "Error: " + rc +
                       " Error string: " + myME310.buffer_cstr(2));
      }
    }
  }

  // Execute FOTA
  performFOTA();
}

void loop() {
}
