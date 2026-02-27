#include <Arduino.h>
#include <ETH.h>
#include <SPI.h>
#include <SD.h>
#include "Wire.h"

#include <TCAL9539.h>

#include "config.h"
#include "usbMidiHost.h"
#include "midi_interface.h"
#include "networkMidiBridge.h"
#include "webDisplayServer.h"
#include "SparkFun_RGB_OLED_64x64.h"




void MIDI_poll();

void onInit()
{
  char buf[20];
  uint16_t vid = Midi.idVendor();
  uint16_t pid = Midi.idProduct();
  sprintf(buf, "VID:%04X, PID:%04X", vid, pid);
  Serial.println(buf); 
}

TCAL9539 io(0x74);

const int ioDC_PIN = 14;
const int ioRST_PIN = 15;

// Instantiate SparkFun RGB OLED object
RGB_OLED_64x64 oled;
RGB_OLED_64x64 oled1;
RGB_OLED_64x64 oled2;
RGB_OLED_64x64 oled3;
RGB_OLED_64x64 oled4;
RGB_OLED_64x64 oled5;
RGB_OLED_64x64 oled6;
RGB_OLED_64x64 oled7;
RGB_OLED_64x64 oled8;

RGB_OLED_64x64 * displays[] = { &oled, &oled1, &oled2, &oled3, &oled4, &oled5, &oled6, &oled7, &oled8};

// // Pin definitions
const uint8_t PIN_SDIN   = SD_MOSI;  // MOSI
const uint8_t PIN_SCLK   = SD_SCK;  // SCK
const uint8_t PIN_DC     = D_C_PIN;  // D/C#
const uint8_t PIN_RESET  = RES_PIN;   // RES#

SPIClass firstSPI(HSPI);
SPIClass secondSPI(VSPI);

void setCSHigh(int idx) { io.digitalWrite(idx, HIGH); }
void setCSLOW(int idx) { io.digitalWrite(idx, LOW); }
void setRST(int val) { io.digitalWrite(ioRST_PIN, val); }
void setDC(int val) { io.digitalWrite(ioDC_PIN, val); }

void initDisplays()
{
  // Cold boot is sensitive here: keep I2C conservative for the IO expander.
  Wire.begin(displaySDA, displaySCL, 400000);
  delay(10);

  io.begin();
  delay(10);

  for (int i = 0; i < 16; i++)
  {
    io.pinMode(i, OUTPUT);
    io.digitalWrite(i, HIGH);
  }

  secondSPI.begin(CS9_PIN, -1, D_C_PIN, -1);

  for (int i = 0; i < 9; i++)
  {
    Serial.print("init Display");
    Serial.println(i);

    displays[i]->onSetHigh(setCSHigh);
    displays[i]->onSetLow(setCSLOW);
    displays[i]->onSetDC(setDC);
    displays[i]->onSetRst(setRST);
    displays[i]->setCShigh();
    displays[i]->begin(i, secondSPI, 10000000);
  }

  // Shared hardware reset for all OLEDs.
  setRST(LOW);
  delay(20);
  setRST(HIGH);
  delay(200);

  // First pass configure.
  for (int i = 0; i < 9; i++)
  {
    displays[i]->defaultConfigure();
    displays[i]->clearDisplay();
    displays[i]->setCShigh();
  }

  // Second pass helps with occasional power-on garbage on some panels.
  for (int i = 0; i < 9; i++)
  {
    displays[i]->defaultConfigure();
    displays[i]->clearDisplay();
    displays[i]->setCShigh();
  }

  for (int i = 0; i < 9; i++)
  {
    displays[i]->fillDisplay(0x00FF);
    displays[i]->setCursor(0, 0);
    displays[i]->println("Hello");
    displays[i]->setCursor(20, 20);
    displays[i]->print("idx: ");
    displays[i]->print(i);
    displays[i]->setCShigh();
  }
}

void setup()
{
  delay(100);

  Serial.begin(115200);
  Serial.println();

  // set all the CS pins! 
  pinMode(SD_CS, OUTPUT);  
  pinMode(USB_CS, OUTPUT);  

  digitalWrite(SD_CS, HIGH);  
  digitalWrite(USB_CS, HIGH);  	

  initDisplays();

  SPI = firstSPI;
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, -1);

  Serial.println("Setup SD Card");
  // delay( 500 );
  if (!SD.begin(SD_CS)) Serial.println("SD begin failed");
  while(!SD.begin()){
    Serial.println("SD begin failed");
    // delay(500);
  }
  Serial.printf("SD Card Size: %d, sectors %d, totalBytes: %d, usedBytes: %d \r\n", SD.cardSize(),SD.numSectors(), SD.totalBytes(), SD.usedBytes());
  // delay( 500 );


  UsbMidi_Setup();

  // Register onInit() function
  Midi.attachOnInit(onInit);  

  Serial.println("Setup end");

  NetworkMidi_Setup();
  WebDisplayServer_Setup(displays, sizeof(displays) / sizeof(displays[0]));

}


long currentTime = 0;
long lastTime = 0;
int tick = 0;
int ticker = 0;

void updateDisplay0Status()
{
  static uint32_t lastUpdate = 0;
  uint32_t now = millis();
  if ((now - lastUpdate) < 1000) {
    return;
  }
  lastUpdate = now;

  RGB_OLED_64x64* d = displays[0];
  d->clearDisplay();
  d->setCursor(0, 0);
  d->println("NET STATUS");

  d->setCursor(0, 10);
  d->print("LINK:");
  d->println(NetworkMidi_IsLinkUp() ? "UP" : "DOWN");

  NetworkIpConfig cfg = NetworkMidi_GetConfig();
  d->setCursor(0, 20);
  d->print("MODE:");
  d->println(cfg.dhcp ? "DHCP" : "STATIC");

  d->setCursor(0, 30);
  d->print("IP:");
  d->println(NetworkMidi_GetLocalIP().toString());

  d->setCursor(0, 50);
  d->print("USB:");
  d->println(Midi ? "OK" : "WAIT");
  d->setCShigh();
}

void loop()
{
    currentTime = millis();

    if ((currentTime - lastTime) > 5000)
    {
        lastTime = currentTime;


        if (tick == 0){

          tick = 1; 	
          Serial.println("Tick");
        }
        else{

          tick = 0;
          Serial.println("Tock");
        }	
      
    }

    UsbMidi_Loop();
    NetworkMidi_Loop();
    WebDisplayServer_Loop();
    updateDisplay0Status();

    // Usb.Task();
    if ( Midi ) {
      MIDI_poll();
    }
    // delay(1);
}



// Poll USB MIDI Controler and send to serial MIDI
void MIDI_poll()
{
  char buf[16];
  uint8_t msg[3];
  uint8_t size = 0;

  do {
    size = Midi.RecvData(msg);
    if (size > 0) {
      uint32_t time = (uint32_t)millis();
      sprintf(buf, "%04X%04X:%d:", (uint16_t)(time >> 16), (uint16_t)(time & 0xFFFF), size);
      Serial.print(buf);

      for (int i = 0; i < size; i++) {
        sprintf(buf, " %02X", msg[i]);
        Serial.print(buf);
      }
      Serial.println("");
      NetworkMidi_SendFromUsb(msg, size);
    }
  }
  while (size > 0);
}




