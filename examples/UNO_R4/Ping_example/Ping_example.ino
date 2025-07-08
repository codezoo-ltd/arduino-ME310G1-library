/*Copyright (C) 2020 Telit Communications S.p.A. Italy - All Rights Reserved.*/
/*    See LICENSE file in the project root for full license information.     */

/**
  @file
    ME310.cpp
    string.h
    stdio.h

  @brief
    Sample test of the use of AT#PING command via ME310 library

  @details
    In this example sketch, the use of methods offered by the ME310 library for
  using AT commands is shown.\n The PDP context is defined, the registration
  status read, the context activated and finally the ping command is sent and
  the response printed.\n NOTE:\n For correct operation it is necessary to set
  the correct APN.


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
#include <string.h>

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

char server[] = "8.8.8.8";

void setup() {

  int cID = 1;          // PDP Context Identifier
  char ipProt[] = "IP"; // Packet Data Protocol type
  ME310::return_t rc;   // return value

  Serial.begin(115200);
  MDMSerial.begin(115200);
  delay(100);
  myME310.debugMode(false); // DebugMode On(true)/Off(false)

  Serial.println("Telit Test AT command Ping");
  myME310.powerOn(ON_OFF);
  Serial.println("ME310 is ON");

  myME310.report_mobile_equipment_error(
      2); // issue command AT+CMEE=2 and wait for answer or timeout
  Serial.println("Define PDP Context");
  rc = myME310.define_pdp_context(
      cID, ipProt, APN); // issue command AT+CGDCONT=cid,PDP_type,APN
  if (rc == ME310::RETURN_VALID) {
    myME310.read_define_pdp_context(); // issue command AT+CGDCONT=? (read mode)
    Serial.print("pdp context read :");
    Serial.println(myME310.buffer_cstr(1)); // print second line of modem answer

    Serial.print("gprs network registration status :");
    rc = myME310
             .read_gprs_network_registration_status(); // issue command
                                                       // AT+CGREG=? (read mode)

    if (rc == ME310::RETURN_VALID) {
      Serial.println(myME310.buffer_cstr(1));
      char *resp = (char *)myME310.buffer_cstr(1);
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
    myME310.context_activation(
        cID,
        1); // issue command AT#SGACT=cid,state and wait for answer or timeout
  }
}

void loop() {
  ME310::return_t rc;
  rc = myME310.ping(
      server,
      ME310::TOUT_20SEC); // issue command AT#PING=server and wait for answer or
                          // timeout (ME310::TOUT_30SEC is 30 seconds)
  Serial.println((String) "Ping to server: " + server);
  if (rc == ME310::RETURN_VALID) {
    Serial.println("Ping value:<");
    int i = 0;
    while (myME310.buffer_cstr(i) != NULL) {
      delay(1000);
      Serial.print(myME310.buffer_cstr(i));
      i++;
    }
    Serial.println(">");
    Serial.println((String) "Ping value raw mode: <" +
                   myME310.buffer_cstr_raw() + ">");
  } else {
    Serial.println((String) "Error: " + myME310.return_string(rc));
  }
  Serial.println("The application has ended...");
  exit(0);
}
