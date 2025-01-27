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
    In this example sketch, it is shown how to use a basic AT commands via ME310 library.\n

  @version
    1.0.0

  @note

  @author


  @date
    20/10/2020

  @ported by rooney.jang (rooney.jang@codezoo.co.kr)
 */

#include <ME310.h>

#define ON_OFF 2 /*Select the right GPIO to control ON_OFF*/
#define MDMSerial Serial1
#define MDMLed	LED_BUILTIN

using namespace me310;
/*
 * If a Telit-Board Charlie is not in use, the ME310 class needs the Uart Serial instance in the constructor, that will be used to communicate with the modem.\n
 * Please refer to your board configuration in variant.h file.
 * Example:
 * Uart Serial1(&sercom4, PIN_MODULE_RX, PIN_MODULE_TX, PAD_MODULE_RX, PAD_MODULE_TX, PIN_MODULE_RTS, PIN_MODULE_CTS);
 * ME310 myME310 (Serial1);
 */
ME310 myME310;
bool ready = false;

void setup() {
  pinMode(MDMLed, OUTPUT);
  MDMSerial.begin(115200);
  myME310.debugMode(false);

  delay(1000);
  myME310.powerOn(ON_OFF);

  if(myME310.attention() == ME310::RETURN_VALID)
  {
	 ready = true;
  }
}

void loop() {
	if(ready)
	{
		digitalWrite(MDMLed, HIGH);
		delay(500);
		digitalWrite(MDMLed, LOW);
		delay(500);
	}
	else
	{
		exit(0);
	}
}
