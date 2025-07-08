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

#include <U8x8lib.h>
U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(/* reset=*/U8X8_PIN_NONE);
#define U8LOG_WIDTH 16
#define U8LOG_HEIGHT 8
uint8_t u8log_buffer[U8LOG_WIDTH * U8LOG_HEIGHT];
U8X8LOG u8x8log;

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
ME310 *myME310;
ME310::return_t rc; // Enum of return value  methods

void setup() {
	u8x8.begin();
	u8x8.setFont(u8x8_font_chroma48medium8_r);

	u8x8log.begin(u8x8, U8LOG_WIDTH, U8LOG_HEIGHT, u8log_buffer);
	u8x8log.setRedrawMode(1); // 0: Update screen with newline, 1: Update screen for every char

	Serial.begin(115200);
	MDMSerial.begin(115200,SERIAL_8N1,16,17); //RXD2:16, TXD2:17
	pinMode(PWR_PIN,OUTPUT);
	digitalWrite(PWR_PIN,HIGH);
	delay(100);
	myME310 = new ME310(MDMSerial);
	myME310->debugMode(false);

	Serial.println("TURN ON ME310");
	u8x8log.print("TURN ON ME310\n");  
	myME310->powerOn(ON_OFF);
	Serial.println("ME310 TURNED ON");
	Serial.println("Bridge Communication Enabled");
	u8x8log.print("ME310 TURNED ON\n");
	u8x8log.print("Bridge Communication Enabled\n");
}

void loop() {
	while (MDMSerial.available()) {
		char c = MDMSerial.read();
		Serial.write(c);
		u8x8log.print(c);
	}
	while (Serial.available()) {
		MDMSerial.write(Serial.read());
	}
}
