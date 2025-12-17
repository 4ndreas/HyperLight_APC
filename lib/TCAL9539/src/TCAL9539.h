#pragma once
#include <Arduino.h>
#include <Wire.h>

class TCAL9539 {
public:
    // enum PinMode {
    //     INPUT = 1,
    //     OUTPUT = 0
    // };

    TCAL9539(uint8_t i2cAddress, TwoWire &wire = Wire);

    bool begin();

    void pinMode(uint8_t pin, uint8_t mode);
    void digitalWrite(uint8_t pin, bool value);
    bool digitalRead(uint8_t pin);

    void writePort(uint8_t port, uint8_t value);
    uint8_t readPort(uint8_t port);

    void setPullUp(uint8_t pin, bool enable);

private:
    TwoWire *_wire;
    uint8_t _addr;

    uint8_t _dir[2];      // direction registers
    uint8_t _out[2];      // output registers
    uint8_t _pullup[2];   // pullup registers

    void writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);

    uint8_t portFromPin(uint8_t pin);
    uint8_t bitFromPin(uint8_t pin);
};
