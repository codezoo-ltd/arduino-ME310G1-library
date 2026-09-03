#include <Arduino.h>
#include <ME310.h>

// ==========================================
// 1. Pin and Hardware Configuration
// ==========================================
#define ON_OFF 2            // ME310 Modem Power GPIO
#define ModemSerial Serial1 // ME310 Modem Serial Port

// Software SPI Flash Pin Configuration (W25Q16)
#define FLASH_CS 4
#define FLASH_SCK 5
#define FLASH_MISO 6
#define FLASH_MOSI 7

// Firmware storage start address (0x000000 reserved for header)
#define FW_FLASH_START_ADDR 0x001000

#define CHUNK_SIZE 1024  // FTP receive and processing chunk size (1 KB)

// Firmware header structure for Sector 0 storage (64 Bytes)
#pragma pack(push, 1)
typedef struct {
  uint32_t magic;          // Magic Number for bootloader recognition (0x55AA1234)
  uint8_t status;          // Status flag (0x01: UPDATE PENDING)
  uint8_t reserved[3];     // Reserved padding space for memory alignment
  uint32_t firmware_size;  // Firmware size in bytes
  uint32_t firmware_crc;   // Firmware CRC32 checksum
  uint8_t padding[48];     // Padding to align total size to 64 bytes
} FirmwareHeader_t;
#pragma pack(pop)

#include <stdarg.h>

// Serial.printf replacement function for UNO R4
void serialPrintf(const char *format, ...) {
  char buffer[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  Serial.print(buffer);
}

using namespace me310;
ME310 myME310;
ME310::return_t rc;

// FTP Configuration
#define FTP_ADDR_PORT "ADDRESS:PORT"
#define FTP_USER      "CLIENTUSER"
#define FTP_PASS      "PASSWORD"
#define APN "simplio.apn"

const char *fw_filename = "fw.bin";
const char *crc_filename = "fw.bin.crc32";

int cID = 1;
char ipProt[] = "IP";

// ==========================================
// 2. Software CRC-32 Algorithm Implementation
// ==========================================
void initSoftwareCRC32(uint32_t *crc) {
  *crc = 0xFFFFFFFF;
}

void updateSoftwareCRC32(uint32_t *crc, const uint8_t *data, size_t length) {
  uint32_t current_crc = *crc;
  for (size_t i = 0; i < length; i++) {
    current_crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (current_crc & 1) {
        current_crc = (current_crc >> 1) ^ 0xEDB88320;
      } else {
        current_crc >>= 1;
      }
    }
  }
  *crc = current_crc;
}

uint32_t getSoftwareCRC32Result(uint32_t crc) {
  return (crc ^ 0xFFFFFFFF);
}

// ==========================================
// 3. Software SPI Flash Control Routines (Bit-Banging)
// ==========================================
uint8_t spiTransfer(uint8_t data) {
  uint8_t result = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(FLASH_MOSI, (data & (1 << i)) ? HIGH : LOW);
    digitalWrite(FLASH_SCK, HIGH);
    if (digitalRead(FLASH_MISO)) result |= (1 << i);
    digitalWrite(FLASH_SCK, LOW);
  }
  return result;
}

void flashWaitBusy() {
  digitalWrite(FLASH_CS, LOW);
  spiTransfer(0x05);  // Read Status Register-1
  while (spiTransfer(0x00) & 0x01) { delayMicroseconds(100); }
  digitalWrite(FLASH_CS, HIGH);
}

void flashWriteEnable() {
  digitalWrite(FLASH_CS, LOW);
  spiTransfer(0x06);  // Write Enable
  digitalWrite(FLASH_CS, HIGH);
}

void flashEraseSector4K(uint32_t addr) {
  flashWriteEnable();
  digitalWrite(FLASH_CS, LOW);
  spiTransfer(0x20);  // Sector Erase (4KB)
  spiTransfer((addr >> 16) & 0xFF);
  spiTransfer((addr >> 8) & 0xFF);
  spiTransfer(addr & 0xFF);
  digitalWrite(FLASH_CS, HIGH);
  flashWaitBusy();
}

void flashWritePage(uint32_t addr, const uint8_t *data, size_t len) {
  flashWriteEnable();
  digitalWrite(FLASH_CS, LOW);
  spiTransfer(0x02);  // Page Program (Up to 256B)
  spiTransfer((addr >> 16) & 0xFF);
  spiTransfer((addr >> 8) & 0xFF);
  spiTransfer(addr & 0xFF);
  for (size_t i = 0; i < len; i++) spiTransfer(data[i]);
  digitalWrite(FLASH_CS, HIGH);
  flashWaitBusy();
}

void flashReadData(uint32_t addr, uint8_t *data, size_t len) {
  digitalWrite(FLASH_CS, LOW);
  spiTransfer(0x03);  // Read Data
  spiTransfer((addr >> 16) & 0xFF);
  spiTransfer((addr >> 8) & 0xFF);
  spiTransfer(addr & 0xFF);
  for (size_t i = 0; i < len; i++) data[i] = spiTransfer(0x00);
  digitalWrite(FLASH_CS, HIGH);
}

// ==========================================
// 4. Utility and Modem Utility Functions
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

String sendCommand(String cmd, uint32_t timeout_ms = 1000) {
  ModemSerial.println(cmd);
  uint32_t start_time = millis();
  String response = "";
  while (millis() - start_time < timeout_ms) {
    while (ModemSerial.available()) {
      response += (char)ModemSerial.read();
    }
  }
  return response;
}

// ==========================================
// 5. Download and Parse fw.bin.crc32 from FTP
// ==========================================
bool downloadCRCandSize(uint32_t &expected_crc, size_t &expected_size) {
  Serial.println("[FTP] Receiving fw.bin.crc32 file...");

  while (ModemSerial.available()) ModemSerial.read();
  ModemSerial.println("AT#FTPGETPKT=\"" + String(crc_filename) + "\",0");

  uint32_t start_time = millis();
  String pktResponse = "";
  while (millis() - start_time < 10000) {
    while (ModemSerial.available()) pktResponse += (char)ModemSerial.read();
    if (pktResponse.indexOf("OK\r\n") != -1 || pktResponse.indexOf("ERROR\r\n") != -1) break;
  }

  ModemSerial.println("AT#FTPRECV=100");  // Read metadata

  start_time = millis();
  String response = "";
  while (millis() - start_time < 3000) {
    while (ModemSerial.available()) response += (char)ModemSerial.read();
  }

  int startIdx = response.indexOf("#FTPRECV:");
  if (startIdx == -1) return false;

  int dataStart = response.indexOf('\n', startIdx) + 1;
  String textData = response.substring(dataStart);

  // Line 1: CRC32 Hex, Line 2: Size
  int lineBreak = textData.indexOf('\n');
  if (lineBreak == -1) return false;

  String crcStr = textData.substring(0, lineBreak);
  crcStr.trim();
  if (crcStr.startsWith("0x") || crcStr.startsWith("0X")) crcStr = crcStr.substring(2);

  String sizeStr = textData.substring(lineBreak + 1);
  sizeStr.trim();

  expected_crc = strtoul(crcStr.c_str(), NULL, 16);
  expected_size = sizeStr.toInt();

  return (expected_crc != 0 && expected_size != 0);
}

// ==========================================
// 6. Main FOTA & 2-Stage CRC32 Verification Routine
// ==========================================
void performFOTA() {
  Serial.println("\n========== Starting FOTA Process ==========");
  rc = myME310.ftp_close();
  if (rc == ME310::RETURN_VALID) {
    delay(2000);
    myME310.debugMode(false);
    rc = myME310.ftp_open(FTP_ADDR_PORT, FTP_USER, FTP_PASS, 0, cID, ME310::TOUT_2MIN);

    if (rc == ME310::RETURN_VALID) {
      myME310.debugMode(true);
      rc = myME310.ftp_change_working_directory("/", ME310::TOUT_10SEC);
    } else {
      Serial.println("[Error] Failed to connect to FTP server.");
      return;
    }
  }

  sendCommand("AT#FTPTYPE=0", 500);

  uint32_t expected_crc = 0;
  size_t total_size = 0;

  if (!downloadCRCandSize(expected_crc, total_size)) {
    Serial.println("[Error] Failed to parse fw.bin.crc32 file!");
    myME310.ftp_close();
    return;
  }

  serialPrintf("[META] Target CRC32: 0x%08X, Total Size: %d Bytes\n", expected_crc, total_size);

  sendCommand("AT#FTPGETPKT=\"" + String(fw_filename) + "\",1", 3000);
  Serial.println("[FOTA] Downloading & Writing to Serial Flash...");

  size_t total_written = 0;
  uint8_t binary_buffer[CHUNK_SIZE];

  // Initialize RAM CRC calculation
  uint32_t ram_crc_accum;
  initSoftwareCRC32(&ram_crc_accum);

  int empty_retries = 0;
  const int MAX_EMPTY_RETRIES = 25;

  while (total_written < total_size) {
    size_t bytes_to_request = min((size_t)CHUNK_SIZE, total_size - total_written);

    while (ModemSerial.available()) ModemSerial.read();
    ModemSerial.print("AT#FTPRECV=");
    ModemSerial.println(bytes_to_request);

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
      empty_retries++;
      if (empty_retries > MAX_EMPTY_RETRIES) {
        Serial.println("\n[Error] Modem response timeout.");
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

    if (actual_bytes == 0) {
      empty_retries++;
      if (empty_retries > MAX_EMPTY_RETRIES) {
        Serial.println("\n[Error] Network disconnected or zero bytes.");
        myME310.ftp_close();
        return;
      }
      delay(200);
      continue;
    }

    empty_retries = 0;

    // Convert Hex String -> Binary
    size_t bytes_parsed = 0;
    req_time = millis();

    while (bytes_parsed < actual_bytes && (millis() - req_time < 3000)) {
      if (ModemSerial.available() >= 2) {
        char high = ModemSerial.read();
        char low = ModemSerial.read();
        if (!isxdigit(high) || !isxdigit(low)) continue;
        binary_buffer[bytes_parsed++] = hexToByte(high, low);
      }
    }

    if (bytes_parsed < actual_bytes) {
      Serial.println("\n[Error] Stream parsing timeout!");
      myME310.ftp_close();
      return;
    }

    // 1) Accumulate S/W CRC32 for received buffer
    updateSoftwareCRC32(&ram_crc_accum, binary_buffer, actual_bytes);

    // 2) Write to SPI Flash (Erase at 4KB sector boundaries, then write in 256B pages)
    uint32_t current_flash_addr = FW_FLASH_START_ADDR + total_written;
    for (size_t offset = 0; offset < actual_bytes; offset += 256) {
      uint32_t target_addr = current_flash_addr + offset;

      // Check 4KB sector boundary and erase
      if (target_addr % 4096 == 0) {
        flashEraseSector4K(target_addr);
      }

      size_t write_len = min((size_t)256, actual_bytes - offset);
      flashWritePage(target_addr, &binary_buffer[offset], write_len);
    }

    total_written += actual_bytes;
    int percent = (total_written * 100) / total_size;
    serialPrintf("\r[Downloading & Writing] %7d / %7d Bytes (%3d%%)\r\n", total_written, total_size, percent);

    req_time = millis();
    while (millis() - req_time < 100) {
      if (ModemSerial.available()) ModemSerial.read();
    }
  }

  Serial.println("\n[FTP] Download and Flash Write Complete!");
  myME310.ftp_close();

  // ==========================================
  // 7. 2-Stage Integrity Verification
  // ==========================================
  Serial.println("\n--- [Integrity Verification Started] ---");

  // Stage 1: Validate CRC32 of RAM received data
  uint32_t final_ram_crc = getSoftwareCRC32Result(ram_crc_accum);
  serialPrintf("Stage 1 RAM RX CRC32 Check   : 0x%08X (Target: 0x%08X) -> ", final_ram_crc, expected_crc);

  if (final_ram_crc != expected_crc) {
    Serial.println("❌ FAIL (Data corrupted during transmission)");
    return;
  }
  Serial.println("✅ PASS");

  // Stage 2: Read-back and validate CRC32 from SPI Flash
  Serial.println("Calculating CRC32 by reading back SPI Flash data for Stage 2...");
  uint32_t flash_crc_accum;
  initSoftwareCRC32(&flash_crc_accum);

  uint8_t read_buf[256];
  size_t bytes_left = total_size;
  uint32_t read_addr = FW_FLASH_START_ADDR;

  while (bytes_left > 0) {
    size_t read_len = min((size_t)256, bytes_left);
    flashReadData(read_addr, read_buf, read_len);
    updateSoftwareCRC32(&flash_crc_accum, read_buf, read_len);

    read_addr += read_len;
    bytes_left -= read_len;
  }

  uint32_t final_flash_crc = getSoftwareCRC32Result(flash_crc_accum);
  serialPrintf("Stage 2 Flash Read-back CRC32: 0x%08X (Target: 0x%08X) -> ", final_flash_crc, expected_crc);

  if (final_flash_crc != expected_crc) {
    Serial.println("❌ FAIL (Flash write error)");
    return;
  }
  Serial.println("✅ PASS");

  // Output final success result
  Serial.println("\n=======================================================");
  Serial.println("🎉 [SUCCESS] Download & Serial Flash Write Successfully Completed!");
  Serial.println("=======================================================");

  // Execute header marking logic right after Stage 2 PASS
  Serial.println("Marking bootloader update header on Sector 0...");

  // Erase Sector 0
  flashEraseSector4K(0x000000);

  // Create firmware header structure
  FirmwareHeader_t header;
  header.magic = 0x55AA1234;           // Magic Number for bootloader recognition
  header.status = 0x01;                // UPDATE PENDING
  header.firmware_size = total_size;   // Firmware size in bytes
  header.firmware_crc = expected_crc;  // CRC32 checksum

  // Write header (64 Bytes) at address 0x000000
  flashWritePage(0x000000, (uint8_t *)&header, sizeof(header));

  // Read-back verification for Sector 0 header
  Serial.println("\nReading back Sector 0 header for verification...");
  FirmwareHeader_t read_header;
  flashReadData(0x000000, (uint8_t *)&read_header, sizeof(read_header));

  serialPrintf(" - Magic Number : 0x%08X (Target: 0x55AA1234)\n", read_header.magic);
  serialPrintf(" - Status       : 0x%02X (Target: 0x01)\n", read_header.status);
  serialPrintf(" - Size         : %d Bytes (Target: %d Bytes)\n", read_header.firmware_size, total_size);
  serialPrintf(" - CRC32        : 0x%08X (Target: 0x%08X)\n", read_header.firmware_crc, expected_crc);

  if (read_header.magic == 0x55AA1234 && read_header.status == 0x01 && read_header.firmware_crc == expected_crc) {
    Serial.println("✅ [SUCCESS] Header marking verified successfully!");
  } else {
    Serial.println("❌ [FAIL] Header marking data mismatch!");
    return;
  }

  Serial.println("\nRebooting system via NVIC_SystemReset...");
  NVIC_SystemReset(); // Software reset to trigger bootloader entry
}

// ==========================================
// 8. Setup & Main Loop
// ==========================================
void setup() {
  Serial.begin(115200);
  ModemSerial.begin(115200);
  while (!Serial)
    ;

  // Configure Software SPI Pin Modes
  pinMode(FLASH_CS, OUTPUT);
  pinMode(FLASH_SCK, OUTPUT);
  pinMode(FLASH_MOSI, OUTPUT);
  pinMode(FLASH_MISO, INPUT);
  digitalWrite(FLASH_CS, HIGH);
  digitalWrite(FLASH_SCK, LOW);

  Serial.println("TURN ON ME310");
  myME310.debugMode(true);
  myME310.powerOn(ON_OFF);
  myME310.report_mobile_equipment_error(2);

  rc = myME310.read_enter_pin();
  if (rc == ME310::RETURN_VALID) {
    char *resp = (char *)myME310.buffer_cstr(2);
    if (resp != NULL && strcmp(resp, "OK") == 0) {
      rc = myME310.define_pdp_context(cID, ipProt, APN);
      if (rc == ME310::RETURN_VALID) {
        myME310.context_activation(cID, 1);
      }
    }
  }

  // Execute FOTA test
  performFOTA();
}

void loop() {
  // Standby loop after test completion
}