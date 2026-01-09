
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
 *  Notas: El Serial.print no funciona en esta placa. Por
 *         eso esta el telnet que actua de equivalente.
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

/************************************************************
 *                VARIABLES GLOBALES
 ************************************************************/
bool otaRunning = false;

byte data[] = {0xAA, 0x55, 0x01, 0x10, 0xFF, 0x12, 0x34, 0x56};  // Generic CAN data to send
bool flag = true;

MCP_CAN CAN0(CS_PIN);                               // Set CS to pin 10

WiFiServer telnetServer(23);
WiFiClient telnetClient;

/************************************************************
 *                       SETUP
 ************************************************************/
void setup() {

  otaSetup();

  telnetServer.begin();

  // Initialize MCP2515 running at 8MHz with a baudrate of 50kb/s and the masks and filters disabled.
  if(CAN0.begin(MCP_ANY, CAN_50KBPS, MCP_8MHZ) == CAN_OK)
    flag = true;
  else
    flag = false;
  
  // Since we do not set NORMAL mode, we are in loopback mode by default.
  CAN0.setMode(MCP_NORMAL);                           // Change to normal mode to allow messages to be transmitted

  pinMode(INT_PIN, INPUT);                            // Configuring pin for /INT input

  pinMode(LED_PIN,OUTPUT);
  digitalWrite(LED_PIN,LOW);
}

/************************************************************
 *                        LOOP
 ************************************************************/
void loop() {
  ArduinoOTA.handle();

  // send data:  ID = 0x100, Standard CAN Frame, Data length = 8 bytes, 'data' = array of data bytes to send
  byte sndStat = CAN0.sendMsgBuf(0x100, 0, 8, data);
  if(sndStat == CAN_OK){
    flag = true;
  } else {
    flag = false;
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
      
      //telnetClient.println(msgString1);
      //telnetClient.println(msgString2);
      telnetClient.println(sndStat);
      telnetClient.println(flag ? "TX OK" : "TX FAIL");       //Hay que conectarse por telnet (preferentemente usando el programa Putty en windows)
      //telnetClient.println("--------------------");         //con la ip 192.168.4.1 y el puerto 23
      
      //telnetClient.print("digital read: ");
      //telnetClient.println(digitalRead(INT_PIN));
      //telnetClient.println("--------------------");
      //newCanMsg = false;
    }
  }
  
  digitalWrite(LED_PIN,HIGH);
  delay(100);

  digitalWrite(LED_PIN,LOW);
  delay(100);

  yield();

}