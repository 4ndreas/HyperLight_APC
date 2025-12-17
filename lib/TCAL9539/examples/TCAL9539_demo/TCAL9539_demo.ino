#include <Wire.h>
#include <TCAL9539.h>

TCAL9539 io(0x74);

void setup() {
    Wire.begin();
    io.begin();

    io.pinMode(0, TCAL9539::OUTPUT);
    io.pinMode(8, TCAL9539::INPUT);
    io.setPullUp(8, true);
}

void loop() {
    io.digitalWrite(0, true);
    delay(500);
    io.digitalWrite(0, false);
    delay(500);

    bool button = io.digitalRead(8);
}
