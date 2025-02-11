/*Copyright (C) 2020 Telit Communications S.p.A. Italy - All Rights Reserved.*/
/*    See LICENSE file in the project root for full license information.     */

/**
  @file
  ME310.cpp
  string.h
  stdio.h

  @brief
  Sample test of the use of AT commands via ME310 library

  @details
  In this example sketch, it is shown how to get the board to sleep mode using
  the AT + CFUN = 5 command via the ME310 library.\n

  @version
  1.0.0

  @note

  @author
  Cristina Desogus

  @date
  04/20/2022

  @ported by rooney.jang (rooney.jang@codezoo.co.kr)
 */

#include <ME310.h>
#include <string.h>

/*When NMEA_DEBUG is 0 Unsolicited NMEA is disable*/
#define NMEA_DEBUG 0

#define ON_OFF 2   /*Select the GPIO to control ON_OFF*/
#define DTR_GPIO 3 /*Select the GPIO to control Module_DTR*/
#define MDMSerial Serial1

using namespace me310;

/*
 * If a Telit-Board Charlie is not in use, the ME310 class needs the Uart Serial
 * instance in the constructor, that will be used to communicate with the
 * modem.\n Please refer to your board configuration in variant.h file. Example:
 * Uart Serial1(&sercom4, PIN_MODULE_RX, PIN_MODULE_TX, PAD_MODULE_RX,
 * PAD_MODULE_TX, PIN_MODULE_RTS, PIN_MODULE_CTS); ME310 myME310 (Serial1);
 */

ME310 myME310(Serial1);
ME310::return_t rc;

int val;

void setup() {
  Serial.begin(115200);
  MDMSerial.begin(115200);
  delay(100);
  myME310.debugMode(false);
  myME310.powerOn(ON_OFF); /*Deivce Initialization AT --> OK */

  Serial.println("ME310 is ON");
  Serial.println("Application start");

  Serial.print("Set DTR PIN: ");
  // this command sets GPIO5 as DTR and save the configuration
  // default GPIO Setup(GPIO5, INPUT, internal PullDown, Save) --> rc =
  // myME310.gpio_control(5,4,0,1);
  rc = myME310.gpio_control(5, 0, 9, 1);
  Serial.println(myME310.return_string(rc));
  /*After the AT#GPIO=5,0,9,1 command*/
  delay(1000);

  pinMode(DTR_GPIO, OUTPUT);   // this command sets the DTR PIN in OUTPUT mode
  digitalWrite(DTR_GPIO, LOW); // this command sets the DTR PIN to LOW level
  delay(5000);
}

void loop() {

  Serial.print("Value of DTR is: ");
  val = digitalRead(
      DTR_GPIO); // this command reads the DTR PIN value, it is only for control
  Serial.println(val);

  Serial.print("CFUN=5 result: ");
  rc = myME310.set_phone_functionality(
      5); // issues AT#CFUN=5 command and wait for answer or timeout
  Serial.println(myME310.return_string(rc));

  myME310.attention(); // issue AT command, for control
  Serial.print("Control AT command result: ");
  if (myME310.buffer_cstr(1) != NULL) {
    Serial.println(myME310.buffer_cstr(1));
  }

  Serial.println("Move DTR to OFF");
  digitalWrite(DTR_GPIO, HIGH); // this command sets the DTR PIN to HIGH level
  delay(5000);

  Serial.print("Value of DTR: ");
  val = digitalRead(DTR_GPIO);
  Serial.println(val);

  myME310.attention();
  if (myME310.buffer_cstr(1) == NULL) {
    Serial.println("Module is in sleep mode");
  } else {
    Serial.println("ERROR module is not in sleep mode");
  }
  delay(1000);

  Serial.println("Move DTR to ON");
  digitalWrite(DTR_GPIO, LOW); // this command sets the DTR PIN to LOW level, it
                               // moves the DTR PIN to wake-up the module
  delay(5000);
  Serial.print("Value of DTR: ");
  val = digitalRead(DTR_GPIO);
  Serial.println(val);
  delay(3000);

  myME310.attention();
  if (myME310.buffer_cstr(1) != NULL) {
    Serial.println("Module is awakened");
  } else {
    Serial.println("ERROR Module is not awakened");
  }
  exit(0);
}
