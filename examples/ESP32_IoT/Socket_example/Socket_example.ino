/*Copyright (C) 2020 Telit Communications S.p.A. Italy - All Rights Reserved.*/
/*    See LICENSE file in the project root for full license information.     */

/**
  @file
  ME310.cpp
  string.h
  stdio.h

  @brief
  Sample test of using sockets via AT commands

  @details
  In this example sketch, the use of sockets is shown through the commands
  offered by the ME310 library.\n NOTE:\n For correct operation it is necessary
  to set the correct APN.

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

#include <U8x8lib.h>
U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(/* reset=*/U8X8_PIN_NONE);
#define U8LOG_WIDTH 16
#define U8LOG_HEIGHT 8
uint8_t u8log_buffer[U8LOG_WIDTH * U8LOG_HEIGHT];
U8X8LOG u8x8log;

#define APN "simplio.apn"
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

int cID = 1;          // PDP Context Identifier
int connID = 1;       // Socket connection identifier.
char ipProt[] = "IP"; // Packet Data Protocol type

char server[] = "modules.telit.com"; // echo server
int port = 10510;
char *str;

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
	Serial.println("Telit Test AT Socket command");
	u8x8log.print("Telit Test AT Socket command\n");
	myME310->debugMode(false);
	myME310->powerOn(ON_OFF);

	Serial.println("ME310 ON");
	u8x8log.print("ME310 ON\n");
	myME310->report_mobile_equipment_error(2); // issue command AT+CMEE=2 and wait for answer or timeout
	myME310->read_enter_pin(); // issue command AT+pin? in read mode, check that
							   // the SIM is inserted and the module is not waiting
							   // for the PIN

	char *resp = (char *)myME310->buffer_cstr(2);
	if (resp != NULL) {
		if (strcmp(resp, "OK") == 0) // read response in 2 array position
		{
			Serial.println("Define PDP Context");
			u8x8log.print("Define PDP Context\n");
			rc = myME310->define_pdp_context(cID, ipProt, APN); // issue command AT+CGDCONT=cid,PDP_type,APN
			if (rc == ME310::RETURN_VALID) {
				myME310->read_define_pdp_context(); // issue command AT+CGDCONT=? (read mode)
				Serial.print("pdp context read: ");
				u8x8log.print("pdp context read: ");
				str = (char *)myME310->buffer_cstr(1);
				Serial.println(str); // print second line of modem answer
				u8x8log.print(str);
				u8x8log.print("\n");
				Serial.print("gprs network registration status: ");
				u8x8log.print("gprs network registration status: ");
				rc = myME310->read_gprs_network_registration_status(); // issue command
																	   // AT+CGREG=?
																	   // (read mode)

				if (rc == ME310::RETURN_VALID) {
					resp = (char *)myME310->buffer_cstr(1);
					Serial.println(resp);
					u8x8log.print(resp);
					u8x8log.print("\n");
					while (resp != NULL) {
						if ((strcmp(resp, "+CGREG: 0,1") != 0) &&
								(strcmp(resp, "+CGREG: 0,5") != 0)) {
							delay(3000);
							rc = myME310->read_gprs_network_registration_status();
							if (rc != ME310::RETURN_VALID) {
								Serial.println("ERROR");
								u8x8log.print("ERROR\n");
								str = (char *)myME310->return_string(rc);
								Serial.println(str);
								u8x8log.print(str);
								u8x8log.print("\n");
								break;
							}
							str = (char *)myME310->buffer_cstr(1);
							Serial.println(str);
							u8x8log.print(str);
							u8x8log.print("\n");
							resp = (char *)myME310->buffer_cstr(1);
						} else {
							break;
						}
					}
				}
				Serial.println("Activate context");
				u8x8log.print("Activate context\n");
				myME310->context_activation(cID, 1); // issue command AT#SGACT=cid,state
													 // and wait for answer or timeout
			}
		}
	}
}

void loop() {

	char data[] = "We are sending some data\nSome data OK\nAnother data";

	Serial.print("Socket configuration: ");
	u8x8log.print("Socket configuration: ");
	ME310::return_t r = myME310->socket_configuration(connID,cID); // issue command AT#SCFG=connID,cID and wait for answer or timeout
	str = (char *)myME310->return_string(r);
	Serial.println(str); // returns a string with return_t codes
	u8x8log.print(str);
	u8x8log.print("\n");
	if (r == ME310::RETURN_VALID) {
		Serial.print("Socket dial: ");
		u8x8log.print("Socket dial: ");
		r = myME310->socket_dial(connID, 0, port, server, 0, 0, 1, 0, 0, ME310::TOUT_1MIN); // issue
																							// commandAT#SD=connID,protocol,port,IPAddrServer,timeout
		str = (char *)myME310->return_string(r);
		Serial.println(str); // returns a string with return_t codes
		u8x8log.print(str);
		u8x8log.print("\n");
		if (r == ME310::RETURN_VALID) {
			delay(100);
			Serial.print("Socket Status: ");
			u8x8log.print("Socket Status: ");
			r = myME310->socket_status(connID, ME310::TOUT_10SEC); // issue command AT#SS=connID and wait for answer or timeout
			delay(100);
			str = (char *)myME310->return_string(r);
			Serial.println(str);
			u8x8log.print(str);
			u8x8log.print("\n");

			Serial.print("SEND: ");
			u8x8log.print("SEND: ");
			r = myME310->socket_send_data_command_mode_extended(connID, (int)sizeof(data), data, 1, ME310::TOUT_30SEC);
			str = (char *)myME310->return_string(r);
			Serial.println(str);
			u8x8log.print(str);
			u8x8log.print("\n");
			if (r != ME310::RETURN_VALID) {
				Serial.println("Send is failed");
				u8x8log.print("Send is failed\n");
			} else {
				Serial.print("Socket Listen: ");
				u8x8log.print("Socket Listen: ");
				r = myME310->socket_listen(connID, 0, port); // issue command AT#SL=connID,listenState(0 close socket
															 // listening),port and wait for answer or timeout
				str = (char *)myME310->return_string(r);
				Serial.println(str);
				u8x8log.print(str);
				u8x8log.print("\n");
				delay(5000);
				if (r == ME310::RETURN_VALID) {
					Serial.print("READ: ");
					u8x8log.print("READ: ");
					// issue command AT#SRECV=connID,size and wait for answer or timeout
					r = myME310->socket_receive_data_command_mode(connID, (int)sizeof(data), 0, ME310::TOUT_10SEC); 
					str = (char *)myME310->return_string(r);
					Serial.println(str);
					u8x8log.print(str);
					u8x8log.print("\n");
					if (r == ME310::RETURN_VALID) {
						Serial.print("Payload: <");
						u8x8log.print("Payload: <");
						str = (char *)myME310->buffer_cstr_raw();
						Serial.print(str); //  print modem answer in raw mode
						u8x8log.print(str);
						u8x8log.print("\n");
						Serial.println(">");
						u8x8log.print(">\n");
					} else {
						str = (char *)myME310->return_string(r);
						Serial.println(str);
						u8x8log.print(str);
						u8x8log.print("\n");
					}
					r = myME310->socket_listen(connID, 0, port);
				} else {
					str = (char *)myME310->return_string(r);
					Serial.println(str);
					u8x8log.print(str);
					u8x8log.print("\n");
				}
			}
		} else {
			str = (char *)myME310->return_string(r);
			Serial.println(str);
			u8x8log.print(str);
			u8x8log.print("\n");
		}
	}
	Serial.println("The application has ended...");
	u8x8log.print("The application has ended...\n");
	while(1);
}
