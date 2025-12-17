#include "TCAL9539.h"

// Register map (relevant subset)
#define REG_INPUT_PORT_0      0x00
#define REG_INPUT_PORT_1      0x01
#define REG_OUTPUT_PORT_0     0x02
#define REG_OUTPUT_PORT_1     0x03
#define REG_CONFIG_PORT_0     0x06
#define REG_CONFIG_PORT_1     0x07
#define REG_PULLUP_PORT_0     0x0C
#define REG_PULLUP_PORT_1     0x0D

TCAL9539::TCAL9539(uint8_t i2cAddress, TwoWire &wire)
    : _wire(&wire), _addr(i2cAddress) {
    _dir[0] = 0xFF;
    _dir[1] = 0xFF;
    _out[0] = 0x00;
    _out[1] = 0x00;
    _pullup[0] = 0x00;
    _pullup[1] = 0x00;
}

bool TCAL9539::begin() {
    _wire->begin();

    writeRegister(REG_CONFIG_PORT_0, _dir[0]);
    writeRegister(REG_CONFIG_PORT_1, _dir[1]);
    writeRegister(REG_OUTPUT_PORT_0, _out[0]);
    writeRegister(REG_OUTPUT_PORT_1, _out[1]);
    writeRegister(REG_PULLUP_PORT_0, _pullup[0]);
    writeRegister(REG_PULLUP_PORT_1, _pullup[1]);

    return true;
}

void TCAL9539::pinMode(uint8_t pin, uint8_t mode) {
    uint8_t port = portFromPin(pin);
    uint8_t bit = bitFromPin(pin);

    if (mode == INPUT)
        _dir[port] |= (1 << bit);
    else
        _dir[port] &= ~(1 << bit);

    writeRegister(REG_CONFIG_PORT_0 + port, _dir[port]);
}

void TCAL9539::digitalWrite(uint8_t pin, bool value) {
    uint8_t port = portFromPin(pin);
    uint8_t bit = bitFromPin(pin);

    if (value)
        _out[port] |= (1 << bit);
    else
        _out[port] &= ~(1 << bit);

    writeRegister(REG_OUTPUT_PORT_0 + port, _out[port]);
}

bool TCAL9539::digitalRead(uint8_t pin) {
    uint8_t port = portFromPin(pin);
    uint8_t bit = bitFromPin(pin);

    uint8_t val = readRegister(REG_INPUT_PORT_0 + port);
    return (val >> bit) & 0x01;
}

void TCAL9539::writePort(uint8_t port, uint8_t value) {
    _out[port] = value;
    writeRegister(REG_OUTPUT_PORT_0 + port, value);
}

uint8_t TCAL9539::readPort(uint8_t port) {
    return readRegister(REG_INPUT_PORT_0 + port);
}

void TCAL9539::setPullUp(uint8_t pin, bool enable) {
    uint8_t port = portFromPin(pin);
    uint8_t bit = bitFromPin(pin);

    if (enable)
        _pullup[port] |= (1 << bit);
    else
        _pullup[port] &= ~(1 << bit);

    writeRegister(REG_PULLUP_PORT_0 + port, _pullup[port]);
}

// ---------- Low-level ----------

void TCAL9539::writeRegister(uint8_t reg, uint8_t value) {
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    _wire->write(value);
    _wire->endTransmission();
}

uint8_t TCAL9539::readRegister(uint8_t reg) {
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    _wire->endTransmission(false);

    _wire->requestFrom(_addr, (uint8_t)1);
    return _wire->read();
}

uint8_t TCAL9539::portFromPin(uint8_t pin) {
    return (pin < 8) ? 0 : 1;
}

uint8_t TCAL9539::bitFromPin(uint8_t pin) {
    return pin % 8;
}
