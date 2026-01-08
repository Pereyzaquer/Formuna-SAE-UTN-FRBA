
/************************************************************
 *  Proyecto : RPM_Sensor
 *  Archivo  : main.cpp
 *  Equipo   : UTN BA Motorsport Formula student team 
 *  Fecha    : 8/1/2026
 *
 *  Descripción:
 *  --------------------------------------------------------
 *  Modulo encargado de la lectura y transmision de los
 *  datos de los sensores de RPM de las ruedas
 *
 *  Hardware:
 *  --------------------------------------------------------
 *  - MCU: ESP32 mini.
 *  - Sensores: Sensor de proximidad inductivo
 *
 *  Notas:
 *  --------------------------------------------------------
 *
 ************************************************************/

/************************************************************
 *                     INCLUDES
 ************************************************************/
#include "../include/RPM.h"

/************************************************************
 *               CONSTANTES DEL SISTEMA
 ************************************************************/
bool otaRunning = false;

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/

/***************************************************************************************************** */

// CAN TX Variables
unsigned long prevTX = 0;                                        // Variable to store last execution time
const unsigned int invlTX = 1000;                                // One second interval constant
byte data[] = {0xAA, 0x55, 0x01, 0x10, 0xFF, 0x12, 0x34, 0x56};  // Generic CAN data to send

// CAN RX Variables
long unsigned int rxId;
unsigned char len;
unsigned char rxBuf[8];
char tmp[8];

// Serial Output String Buffer
char msgString1[64];
char msgString2[64];
bool flag = true, newCanMsg = true;

// CAN0 INT and CS
MCP_CAN CAN0(CS_PIN);                               // Set CS to pin 10

/***************************************************************************************************** */

WiFiServer telnetServer(23);
WiFiClient telnetClient;

/************************************************************
 *                       SETUP
 ************************************************************/
void setup() {

  //Serial.begin(115200);
  delay(1000);

  otaSetup();

  telnetServer.begin();

  /***************************************************************************************************** */

  // Initialize MCP2515 running at 8MHz with a baudrate of 500kb/s and the masks and filters disabled.
  if(CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) == CAN_OK)
    //Serial.println("MCP2515 Initialized Successfully!");
    flag = true;
  else
    //Serial.println("Error Initializing MCP2515...");
    flag = false;
  
  // Since we do not set NORMAL mode, we are in loopback mode by default.
  //CAN0.setMode(MCP_NORMAL);

  pinMode(INT_PIN, INPUT);                           // Configuring pin for /INT input
  
  //Serial.println("MCP2515 Library Loopback Example...");

  /***************************************************************************************************** */

  pinMode(LED_PIN,OUTPUT);
  digitalWrite(LED_PIN,LOW);
}

/************************************************************
 *                        LOOP
 ************************************************************/
void loop() {
  ArduinoOTA.handle();

  /***************************************************************************************************** */

  if(!digitalRead(INT_PIN))                          // If CAN0_INT pin is low, read receive buffer
  {
    CAN0.readMsgBuf(&rxId, &len, rxBuf);              // Read data: len = data length, buf = data byte(s)
    
    if((rxId & 0x80000000) == 0x80000000)             // Determine if ID is standard (11 bits) or extended (29 bits)
      sprintf(msgString1, "Extended ID: 0x%.8lX  DLC: %1d  Data:", (rxId & 0x1FFFFFFF), len);
    else
      sprintf(msgString1, "Standard ID: 0x%.3lX       DLC: %1d  Data:", rxId, len);
  
    if((rxId & 0x40000000) == 0x40000000){            // Determine if message is a remote request frame.
      sprintf(msgString2, " REMOTE REQUEST FRAME");
    } else {

      msgString2[0] = '\0';  // limpiar buffer

      for(byte i = 0; i<len; i++){
        sprintf(tmp, " 0x%.2X", rxBuf[i]);
        strcat(msgString2, tmp);
      }
    }
        
    newCanMsg = true;
  }
  
  if(millis() - prevTX >= invlTX){                    // Send this at a one second interval. 
    prevTX = millis();
    byte sndStat = CAN0.sendMsgBuf(0x100, 8, data);
    
    if(sndStat == CAN_OK)
    {
      flag = true;
      digitalWrite(LED_PIN,HIGH);
    }
    else
    {
      flag = false;
      digitalWrite(LED_PIN,LOW);
    }
  }

  /***************************************************************************************************** */

  if (!otaRunning) {

    if (telnetServer.hasClient()) {
      if (!telnetClient || !telnetClient.connected()) {
        telnetClient = telnetServer.available();
        telnetClient.println("ESP32 RPM Sensor - Telnet conectado");
      } else {
        telnetServer.available().stop(); // solo 1 cliente
      }
    }

    if (telnetClient && telnetClient.connected()) {
      //telnetClient.println("--------------------");
      if(newCanMsg)
      {
        telnetClient.println(msgString1);
        telnetClient.println(msgString2);
        telnetClient.println(flag ? "TX OK" : "TX FAIL");     //Hay que conectarse por telnet (preferentemente usando el programa Putty en windows)
        telnetClient.println("--------------------");         //con la ip 192.168.4.1 y el puerto 23
      }
      //telnetClient.print("digital read: ");
      //telnetClient.println(digitalRead(INT_PIN));
      //telnetClient.println("--------------------");
      newCanMsg = false;
    }
  }
  
  digitalWrite(LED_PIN,HIGH);
  delay(100);

  digitalWrite(LED_PIN,LOW);
  delay(100);

  yield();

}