/*Copyright (C) 2020 Telit Communications S.p.A. Italy - All Rights Reserved.*/
/*    See LICENSE file in the project root for full license information.     */

/**
  @file
  ME310.cpp
  string.h
  stdio.h

  @brief
  Sample test of using GNSS to sockets via AT commands

  @details
  In this example sketch, the use of sockets is shown through the commands
offered by the ME310 library.\n NOTE:\n For correct operation it is necessary to
set the correct APN.

@version
1.0.0

@note

@author
Cristina Desogus

@date
26/02/2021

@ported by rooney.jang (rooney.jang@codezoo.co.kr)
 */
#include <ME310.h>

#define APN "simplio.apn"
#define ON_OFF 2 /*Select the GPIO to control ON_OFF*/
#define MDMSerial Serial1

using namespace me310;
/*
 * If a Telit-Board Charlie is not in use, the ME310 class needs the Uart Serial
 * instance in the constructor, that will be used to communicate with the
 * modem.\n Please refer to your board configuration in variant.h file. Example:
 * Uart Serial1(&sercom4, PIN_MODULE_RX, PIN_MODULE_TX, PAD_MODULE_RX,
 * PAD_MODULE_TX, PIN_MODULE_RTS, PIN_MODULE_CTS); ME310 myME310 (Serial1);
 */
ME310 myME310;
ME310::return_t rc; // Enum of return value  methods

int count = 0;      // GNSS retry count
char GNSS_buf[128]; // GNSS Data

int cID = 1;          // PDP Context Identifier
int connID = 1;       // Socket connection identifier.
char ipProt[] = "IP"; // Packet Data Protocol type

char server[] = "modules.telit.com"; // echo server
int port = 10510;

void setup() {

  Serial.begin(115200);
  MDMSerial.begin(115200);
  delay(100);
  myME310.debugMode(false);

  myME310.module_reboot(); // issue command at#reboot

  delay(10000);
  Serial.println("Telit Test GNSS To LTE command");
  myME310.powerOn(ON_OFF);
  Serial.println("ME310 ON");

  myME310.report_mobile_equipment_error(
      2); // issue command AT+CMEE=2 and wait for answer or timeout
  myME310.read_gnss_configuration(); // issue command AT$GPSCFG?
  Serial.println(myME310.buffer_cstr(1));

  /////////////////////////////////////
  // Set GNSS priority
  // AT$GPSCFG=0,0
  // 0 : priority GNSS
  // 1 : priority WWAN
  /////////////////////////////////////
  rc = myME310.gnss_configuration(0, 0);
  if (rc == ME310::RETURN_VALID) {
    Serial.println(myME310.buffer_cstr(1));
  }

  /////////////////////////////////////
  // Set constellations
  // AT$GPSCFG=2,X
  // 0 : the constellation is selected automatically based on Mobile Country
  // Code (MCC) of camped network 1 : GPS+GLO 2 : GPS+GAL 3 : GPS+BDS 4 :
  // GPS+QZSS
  /////////////////////////////////////
  rc = myME310.gnss_configuration(2, 1);
  if (rc == ME310::RETURN_VALID) {
    Serial.println(myME310.buffer_cstr(1));
  }

  /////////////////////////////////////
  // Set  priority to GNSS priority in runtime
  // AT$GPSCFG=3,0
  // 0 : priority GNSS
  // 1 : priority WWAN
  /////////////////////////////////////
  rc = myME310.gnss_configuration(3, 0);
  if (rc == ME310::RETURN_VALID) {
    Serial.println(myME310.buffer_cstr(1));
  }

  /////////////////////////////////////
  // Set on/off GNSS controller
  // AT$GPSP=<status>
  // 0 : GNSS controller is powered down
  // 1 : GNSS controller is powered up
  /////////////////////////////////////
  Serial.println("GNSS controller is powered up.");
  rc = myME310.gnss_controller_power_management(1);

  /////////////////////////////////////
  //  Get Acquired Position
  //  AT$GPSACP
  /////////////////////////////////////
  do {
    rc = myME310.gps_get_acquired_position();

    if (rc == ME310::RETURN_VALID) {
      Serial.println(myME310.buffer_cstr(1));
      char *buff = (char *)myME310.buffer_cstr(1);
      std::string tmp_pos;
      if (buff != NULL) {
        tmp_pos = buff;
        std::size_t len_pos = tmp_pos.find(":");
        if (len_pos != std::string::npos) {
          std::size_t len_pos2 = tmp_pos.find(",");
          char valid_pos[64];
          if (len_pos2 != std::string::npos) {
            int len = tmp_pos.copy(valid_pos, ((len_pos2 - 1) - len_pos),
                                   len_pos + 1);
            if (len > 2) {
              /*When the position is fixed, Copy GNSS Data*/
              memset(GNSS_buf, 0x0, sizeof(GNSS_buf));
              strcpy(GNSS_buf, buff);
              break;
            }
          }
        }
      }
    }
    delay(5000);
    count++;
  } while (count < 20);

  /////////////////////////////////////
  // GNSS controller is powered down
  // AT$GPSP=0
  /////////////////////////////////////
  Serial.println("Stop GNSS Session");
  Serial.println("GNSS controller is powered down.");
  myME310.gnss_controller_power_management(0);

  if (count == 20) {
    Serial.println("GNSS data was not obtained.");
    delay(5000);
    exit(0);
  }

  myME310.read_enter_pin(); // issue command AT+pin? in read mode, check that
                            // the SIM is inserted and the module is not waiting
                            // for the PIN
  char *resp = (char *)myME310.buffer_cstr(2);

  if (resp != NULL) {
    if (strcmp(resp, "OK") == 0) // read response in 2 array position
    {
      Serial.println("Define PDP Context");
      rc = myME310.define_pdp_context(
          cID, ipProt, APN); // issue command AT+CGDCONT=cid,PDP_type,APN
      if (rc == ME310::RETURN_VALID) {
        myME310.read_define_pdp_context(); // issue command AT+CGDCONT=? (read
                                           // mode)
        Serial.print("pdp context read: ");
        Serial.println(
            myME310.buffer_cstr(1)); // print second line of modem answer

        Serial.print("gprs network registration status: ");
        rc = myME310.read_gprs_network_registration_status(); // issue command
                                                              // AT+CGREG=?
                                                              // (read mode)

        if (rc == ME310::RETURN_VALID) {
          resp = (char *)myME310.buffer_cstr(1);
          Serial.println(resp);
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
        myME310.context_activation(cID, 1); // issue command AT#SGACT=cid,state
                                            // and wait for answer or timeout
      }
    }
  }
}

void loop() {

  Serial.print("Socket configuration: ");
  ME310::return_t r = myME310.socket_configuration(
      connID,
      cID); // issue command AT#SCFG=connID,cID and wait for answer or timeout
  Serial.println(
      myME310.return_string(r)); // returns a string with return_t codes
  if (r == ME310::RETURN_VALID) {
    Serial.print("Socket dial: ");
    r = myME310.socket_dial(
        connID, 0, port, server, 0, 0, 1, 0, 0,
        ME310::
            TOUT_1MIN); // issue
                        // commandAT#SD=connID,protocol,port,IPAddrServer,timeout
    Serial.println(
        myME310.return_string(r)); // returns a string with return_t codes
    if (r == ME310::RETURN_VALID) {
      delay(100);
      Serial.print("Socket Status: ");
      r = myME310.socket_status(
          connID, ME310::TOUT_10SEC); // issue command AT#SS=connID and wait for
                                      // answer or timeout
      delay(100);
      Serial.println(myME310.return_string(r));

      Serial.print("SEND: ");
      r = myME310.socket_send_data_command_mode_extended(
          connID, (int)strlen(GNSS_buf) + 1, GNSS_buf, 1,
          ME310::TOUT_30SEC); // include the NULL character
      Serial.println(myME310.return_string(r));
      if (r != ME310::RETURN_VALID) {
        Serial.println("Send is failed");
      } else {
        Serial.print("Socket Listen: ");
        r = myME310.socket_listen(
            connID, 0,
            port); // issue command AT#SL=connID,listenState(0 close socket
                   // listening),port and wait for answer or timeout
        Serial.println(myME310.return_string(r));
        delay(5000);
        if (r == ME310::RETURN_VALID) {
          Serial.print("READ: ");
          r = myME310.socket_receive_data_command_mode(
              connID, (int)strlen(GNSS_buf) + 1, 0,
              ME310::TOUT_10SEC); // issue command AT#SRECV=connID,size and wait
                                  // for answer or timeout
          Serial.println(myME310.return_string(r));
          if (r == ME310::RETURN_VALID) {
            Serial.print("Payload: <");
            Serial.print(
                (String)myME310
                    .buffer_cstr_raw()); // print modem answer in raw mode
            Serial.println(">");
          } else {
            Serial.println(myME310.return_string(r));
          }
          r = myME310.socket_listen(connID, 0, port);
        } else {
          Serial.println(myME310.return_string(r));
        }
      }
    } else {
      Serial.println(myME310.return_string(r));
    }
  }
  Serial.println("The application has ended...");
  exit(0);
}
