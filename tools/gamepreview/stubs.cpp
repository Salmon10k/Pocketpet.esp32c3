// Minimal Arduino runtime for the PC preview (the hostcheck stubs also want the Wi-Fi mock, which is not needed here).
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
SerialC Serial; TwoWire Wire; SPIClass SPI;
unsigned long millis() { return 0; } unsigned long micros() { return 0; }
void pinMode(uint8_t, uint8_t) {} int digitalRead(uint8_t) { return 0; } void digitalWrite(uint8_t, uint8_t) {}
void delay(unsigned long) {} void delayMicroseconds(unsigned int) {}
long random(long m) { return m ? rand() % m : 0; } long random(long a, long b) { return a + random(b - a); } void randomSeed(unsigned long s) { srand((unsigned)s); }
ESPClass ESP;

// U8g2's Arduino hardware callbacks: nothing is sent anywhere, the frame buffer is all we look at.
#include "u8x8.h"
extern "C" uint8_t u8x8_gpio_and_delay_arduino(u8x8_t *, uint8_t, uint8_t, void *) { return 1; }
extern "C" uint8_t u8x8_byte_arduino_hw_i2c(u8x8_t *, uint8_t, uint8_t, void *) { return 1; }
