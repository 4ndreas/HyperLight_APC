#include <Arduino.h>
#include <ETH.h>
#include "SPI.h"
#include <SD.h>
#include "Wire.h"

#include <TCAL9539.h>

#include "config.h"
#include "usbMidiHost.h"
// #include "midi_interface.h"
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


void setCSHigh(int idx) { io.digitalWrite(idx, HIGH); }
void setCSLOW(int idx) { io.digitalWrite(idx, LOW); }
void setRST(int val) { io.digitalWrite(ioRST_PIN, val); }
void setDC(int val) { io.digitalWrite(ioDC_PIN, val); }

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

  Wire.begin(displaySDA, displaySCL, 1000000);

  io.begin();
  for (int i = 0; i < 16; i++)
  {
    io.pinMode(i, OUTPUT);
    io.digitalWrite(i, HIGH);
  }

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, -1);

  for ( int i = 0; i< 9; i++)
  {
    Serial.print("init Display");
    Serial.println(i);
    
    displays[i]->onSetHigh(setCSHigh);
    displays[i]->onSetLow(setCSLOW);
    displays[i]->onSetDC(setDC);
    displays[i]->onSetRst(setRST);
    displays[i]->setCShigh();

    displays[i]->begin(i, SPI, 10000000);
  }

  oled.startup();     // reset all displays

  for ( int i = 0; i< 9; i++)
  {
    displays[i]->defaultConfigure();
    displays[i]->setCShigh();

    displays[i]->clearDisplay();
    displays[i]->fillDisplay(0x00FF );

    displays[i]->setCursor(0,0);
    displays[i]->println("Hello");
    displays[i]->setCursor(20,20);
    displays[i]->print("idx: ");
    displays[i]->print(i);
    displays[i]->setCShigh();
  }


  Serial.println("Setup SD Card");
  // delay( 500 );
  // if (!SD.begin(SD_CS)) Serial.println("SD begin failed");
  // while(!SD.begin()){
  //   Serial.println("SD begin failed");
  //   // delay(500);
  // }
  // Serial.printf("SD Card Size: %d, sectors %d, totalBytes: %d, usedBytes: %d \r\n", SD.cardSize(),SD.numSectors(), SD.totalBytes(), SD.usedBytes());
  // // delay( 500 );



  // UsbMidi_Setup();

  // // Register onInit() function
  // Midi.attachOnInit(onInit);  

  // Serial.println("Setup end");
}


long currentTime = 0;
long lastTime = 0;
int tick = 0;
int ticker = 0;

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

    // UsbMidi_Loop();

    // Usb.Task();
    // if ( Midi ) {
    //   MIDI_poll();
    // }
    // delay(1);
}



// Poll USB MIDI Controler and send to serial MIDI
void MIDI_poll()
{
  char buf[16];
  uint8_t bufMidi[MIDI_EVENT_PACKET_SIZE];
  uint16_t  rcvd;

  if (Midi.RecvData( &rcvd,  bufMidi) == 0 ) {
    uint32_t time = (uint32_t)millis();
    sprintf(buf, "%04X%04X:%3d:", (uint16_t)(time >> 16), (uint16_t)(time & 0xFFFF), rcvd); // Split variable to prevent warnings on the ESP8266 platform
    Serial.print(buf);

    for (int i = 0; i < MIDI_EVENT_PACKET_SIZE; i++) {
      sprintf(buf, " %02X", bufMidi[i]);
      Serial.print(buf);
    }
    Serial.println("");
  }
}




