/*Copyright (C) 2020 Telit Communications S.p.A. Italy - All Rights Reserved.*/
/*    See LICENSE file in the project root for full license information.     */

/**
  @file
  ME310.cpp
  string.h
  stdio.h

  @brief
  Driver Library for ME310 Telit Modem

  @details
  The library contains a single class that implements a C++ interface to all
  ME310 AT Commands.\n It makes it easy to build Arduino applications that use
  the full power of ME310 module.\n

  @version
  1.0.0

  @note

  @author
  Cristina Desogus

  @date
  28/10/2020

  @ported by rooney.jang (rooney.jang@codezoo.co.kr)
 */

#include <ME310.h>

#define ON_OFF 18 /*Select the GPIO to control ON_OFF*/
#define PWR_PIN 5 /*Select the GPIO to control LDO*/
HardwareSerial MDMSerial(2); //use ESP32 UART2

using namespace me310;
/*
 * If a Telit-Board Charlie is not in use, the ME310 class needs the Uart Serial
 * instance in the constructor, that will be used to communicate with the
 * modem.\n Please refer to your board configuration in variant.h file. Example:
 * Uart Serial1(&sercom4, PIN_MODULE_RX, PIN_MODULE_TX, PAD_MODULE_RX,
 * PAD_MODULE_TX, PIN_MODULE_RTS, PIN_MODULE_CTS); ME310 myME310 (Serial1);
 */
ME310 myME310(MDMSerial);
ME310::return_t rc; // Enum of return value  methods

void setup() {
	Serial.begin(115200);
	MDMSerial.begin(115200,SERIAL_8N1,16,17); //RXD2:16, TXD2:17

	pinMode(PWR_PIN,OUTPUT);
	digitalWrite(PWR_PIN,HIGH);
	delay(100);

	myME310.debugMode(false);

	Serial.println("TURN ON ME310");
	myME310.powerOn(ON_OFF);
	Serial.println("ME310 TURNED ON");
	Serial.println("Bridge Communication Enabled");
}

void loop() {
  if (MDMSerial.available()) {
    char buffer[256];
    int len = 0;

    delay(10);

    while (MDMSerial.available() && len < sizeof(buffer)) {
      buffer[len++] = MDMSerial.read();
    }

    Serial.write((uint8_t*)buffer, len);
  }

  if (Serial.available()) {
    char buffer[256];
    int len = 0;

    delay(10);

    while (Serial.available() && len < sizeof(buffer)) {
      buffer[len++] = Serial.read();
    }

    MDMSerial.write((uint8_t*)buffer, len);
  }
}
