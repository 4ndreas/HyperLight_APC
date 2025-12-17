#ifndef CONFIG_H_
#define CONFIG_H_

/* this will force using const velocity for all notes, remove this to get dynamic velocity */
#define MIDI_USE_CONST_VELOCITY

// Input Only
#define IN2_PIN     GPIO_NUM_35
#define IN3_PIN     GPIO_NUM_36
#define IN4_PIN     GPIO_NUM_39

// SPI PINS
#define SD_MOSI     GPIO_NUM_32
#define SD_MISO     GPIO_NUM_33
#define SD_SCK      GPIO_NUM_5

// SD-CARD
#define SD_CS       GPIO_NUM_2

// MAX3421E
// SPI Pins are hardcoded in avrpins.h and usbhost.h
#define USB_CS      GPIO_NUM_4
#define USB_INT     GPIO_NUM_35    //IN2

// Display CS
// #define CS9_PIN     GPIO_NUM_12
// #define SR_EN_PIN   GPIO_NUM_14

// Display
#define D_C_PIN     GPIO_NUM_17
#define RES_PIN     GPIO_NUM_12

#define displaySDA  GPIO_NUM_14
#define displaySCL  GPIO_NUM_15
#define dispARD     0x20


#endif /* CONFIG_H_ */