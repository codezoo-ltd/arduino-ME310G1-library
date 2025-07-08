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
  In this example sketch, it is shown how to use GNSS management using ME310
library.\n GNSS configuration, GNSS controller power management, GNSS nmea
configuration functions are shown.\n GPS positions are acquired and response is
printed. NOTE:\n For the sketch to work correctly, GNSS should be tested in open
sky conditions to allow a fix. The fix may take a few minutes.


@version
1.0.0

@note

@author
Cristina Desogus

@date
02/07/2021

@ported by rooney.jang (rooney.jang@codezoo.co.kr)
 */

#include <ME310.h>
#include <string>

/*When NMEA_DEBUG is 0 Unsolicited NMEA is disable*/
#define NMEA_DEBUG 0

#define ON_OFF 2 /*Select the GPIO to control ON_OFF*/
#define MDMSerial Serial1
#define MDMLed LED_BUILTIN

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

int count = 0;

void setup() {
  pinMode(MDMLed, OUTPUT);

  Serial.begin(115200);
  MDMSerial.begin(115200);
  delay(100);
  myME310.debugMode(false);
  myME310.powerOn(ON_OFF);
  Serial.println("Telit Test AT GNSS command");
  Serial.println("ME310 ON");
  Serial.println("AT Command");
  ME310::return_t rc =
      myME310.attention(); // issue command and wait for answer or timeout
  Serial.println(myME310.buffer_cstr(0)); // print first line of modem answer
  Serial.print(ME310::return_string(rc)); // print return value

  ////////////////////////////////////
  // Report Mobile Equipment Error CMEE
  // (0 -> disable, 1-> enable numeric
  // values, 2 -> enable verbose mode)
  ///////////////////////////////////
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
#if NMEA_DEBUG
  if (rc == ME310::RETURN_VALID) {
    /////////////////////////////////////
    // Set GNGSA, GLGSV and GNRMC as available sentence in the unsolicited NMEA
    // sentences. AT$GPSNMUNEX=0,1,1,0,0,0,0,0,0,0,0,1,0
    /////////////////////////////////////
    rc = myME310.gnss_nmea_extended_data_configuration(0, 1, 1, 0, 0, 0, 0, 0,
                                                       0, 0, 0, 1, 0);
    if (rc == ME310::RETURN_VALID) {
      /////////////////////////////////////
      //  Activate unsolicited NMEA sentences flow in the AT port and
      //  GPGGA,GPRMC, GPGSA and GPGSV sentences. AT$GPSNMUN=2,1,0,1,1,1,0
      /////////////////////////////////////
      rc = myME310.gnss_nmea_data_configuration(2, 1, 0, 1, 1, 1, 0);
      int i = 0;
      while (strcmp(myME310.buffer_cstr(i), "OK") != 0) {
        Serial.println(myME310.buffer_cstr(i));
        i++;
      }
    }
  }
#endif
}

void loop() {
  delay(1000);
  /////////////////////////////////////
  //  Get Acquired Position
  //  AT$GPSACP
  /////////////////////////////////////
  rc = myME310.gps_get_acquired_position();

  /*When the position is fixed, the led blinks*/
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
          int len =
              tmp_pos.copy(valid_pos, ((len_pos2 - 1) - len_pos), len_pos + 1);
          if (len > 2) {
            digitalWrite(MDMLed, HIGH);
            delay(6000);
            digitalWrite(MDMLed, LOW);
            delay(1000);
          }
        }
      }
    }
  }
  delay(5000);
  if (count > 20) {
#if NMEA_DEBUG
    //////////////////////////////////
    // Stop NMEA Flow and Stop GNSS Session
    //////////////////////////////////
    Serial.println("Stop NMEA Flow and GNSS Session");
    /////////////////////////////////////
    // De-activate unsolicited NMEA sentences flow.
    // AT$GPSNMUN=0
    /////////////////////////////////////
    rc = myME310.gnss_nmea_extended_data_configuration(0);
    if (rc == ME310::RETURN_VALID) {
      Serial.println("De-activate unsolicited NMEA sentences flow");
    }
#endif
    /////////////////////////////////////
    // GNSS controller is powered down
    // AT$GPSP=0
    /////////////////////////////////////
    Serial.println("Stop GNSS Session");
    Serial.println("GNSS controller is powered down.");
    myME310.gnss_controller_power_management(0);
    delay(5000);
    exit(0);
  }
  count++;
}
