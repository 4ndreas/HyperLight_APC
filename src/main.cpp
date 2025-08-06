#include <Arduino.h>
#include <ETH.h>
#include "SPI.h"
#include <SD.h>

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

// Instantiate SparkFun RGB OLED object
// 8 Displays CS Pins are connected to a 74HC595 shift register the 9 display CS pins is connected directly to GPIO_NUM_12
RGB_OLED_64x64 oled;
RGB_OLED_64x64 oled1;
RGB_OLED_64x64 oled2;
RGB_OLED_64x64 oled3;
RGB_OLED_64x64 oled4;
RGB_OLED_64x64 oled5;
RGB_OLED_64x64 oled6;
RGB_OLED_64x64 oled7;
RGB_OLED_64x64 oled8;

// // Pin definitions
const uint8_t PIN_SDIN   = SD_MOSI;  // MOSI
const uint8_t PIN_SCLK   = SD_SCK;  // SCK
const uint8_t PIN_DC     = D_C_PIN;  // D/C#
const uint8_t PIN_RESET  = RES_PIN;   // RES#
const uint8_t PIN_LATCH  = SR_EN_PIN;   // 74HC595 RCLK (EN)
const uint8_t PIN_CS9    = CS9_PIN;  // Direct CS for display 9

void setup()
{
  delay(500);

  Serial.begin(115200);

  Serial.println();

  pinMode(PIN_CS9, OUTPUT);
	pinMode(PIN_RESET, OUTPUT);
	pinMode(PIN_DC, OUTPUT);

	// Set pins to default positions
	digitalWrite(PIN_CS9, HIGH);
	digitalWrite(PIN_RESET, HIGH);
	digitalWrite(PIN_DC, HIGH);

  // set all the CS pins! 
  pinMode(CS9_PIN, OUTPUT);
  pinMode(SR_EN_PIN, OUTPUT);
  pinMode(SD_CS, OUTPUT);  
  pinMode(USB_CS, OUTPUT);  

  digitalWrite(CS9_PIN, HIGH);
  digitalWrite(SR_EN_PIN, LOW);
  digitalWrite(SD_CS, HIGH);  
  digitalWrite(USB_CS, HIGH);  	

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, -1);

  oled.begin(PIN_DC, PIN_RESET, 9, SPI, 10000000);
  oled7.begin(PIN_DC, PIN_RESET, 7, SPI, 10000000);
  oled8.begin(PIN_DC, PIN_RESET, 8, SPI, 10000000);

  // Initialize OLED library
  oled.setCShigh();   // set all CS to high to disable the displays
  oled.startup();     // reset all displays

  // this works fine
  oled.defaultConfigure();
  oled.clearDisplay();
  oled.fillDisplay(0x00FF);
  delay(1000);
  oled.setCursor(0,0);
  oled.println("Hello");
  oled.setCursor(20,20);
  oled.print(9);
  oled.setCShigh();

  delay(1000);

  // only noise on the screen 
  oled8.defaultConfigure();
  oled8.clearDisplay();
  oled8.fillDisplay(0xFF00);
  oled8.setCursor(0,0);
  delay(1000);
  oled8.println("World");
  oled8.setCursor(20,20);
  oled8.println(8);
  oled8.setCShigh();
  
  delay(1000);
  // this works
  oled.setCursor(0,40);
  oled.println("again");
  oled.setCShigh();

  delay(1000);
  // this not
  oled8.setCursor(0,40);
  oled8.println("goes around");
  oled8.setCShigh();

  // for( int i = 1; i < 9; i++)
  // {
  //   oled.setDisplay(i);
  //   oled.defaultConfigure();

  //   oled.clearDisplay();            // Fills the screen with black
  //   oled.setCursor(0,0);            // Sets the cursor relative to the display. (0,0) is the upper left corner and (63,63) is the lower right
  //   oled.println("world!");   // Prints using the default font at the cursor location
  //   oled.setCursor(20,20);
  //   oled.println(i);

  //   oled.setDisplay(-1);
  // }

  Serial.println("Setup SD Card");
  // delay( 500 );
  // if (!SD.begin(SD_CS)) Serial.println("SD begin failed");
  // while(!SD.begin()){
  //   Serial.println("SD begin failed");
  //   // delay(500);
  // }
  // Serial.printf("SD Card Size: %d, sectors %d, totalBytes: %d, usedBytes: %d \r\n", SD.cardSize(),SD.numSectors(), SD.totalBytes(), SD.usedBytes());
  // // delay( 500 );



  UsbMidi_Setup();

  // Register onInit() function
  Midi.attachOnInit(onInit);  

  Serial.println("Setup end");
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
        // oled.setCShigh();
        // oled.setDisplay(ticker);
        // oled.setCSlow();
        // ticker++;
        // if(ticker > 9)
        // {
        //   ticker = 0;
        // }

        if (tick == 0){
          // digitalWrite(PIN_LATCH, LOW);
          // // shiftOut(PIN_SDIN, PIN_SCLK, MSBFIRST, 0xFF);
	        // SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
	        // SPI.write(0xFF);
          // SPI.endTransaction();
          // digitalWrite(PIN_LATCH, HIGH);	
          tick = 1; 	
          Serial.println("Tick");
        }
        else{
          // digitalWrite(PIN_LATCH, LOW);
          // // shiftOut(PIN_SDIN, PIN_SCLK, MSBFIRST, 0xFF);
	        // SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
	        // SPI.write(0x00);
          // SPI.endTransaction();
          // digitalWrite(PIN_LATCH, HIGH);	
          tick = 0;
          Serial.println("Tock");
        }	
      
    }

    UsbMidi_Loop();

    Usb.Task();
    if ( Midi ) {
      MIDI_poll();
    }
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




