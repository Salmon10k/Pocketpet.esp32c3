#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
SerialC Serial; TwoWire Wire; SPIClass SPI;
unsigned long millis(){return 0;} unsigned long micros(){return 0;}
void pinMode(uint8_t,uint8_t){} int digitalRead(uint8_t){return 0;} void digitalWrite(uint8_t,uint8_t){}
void delay(unsigned long){} void delayMicroseconds(unsigned int){}
long random(long m){return m?rand()%m:0;} long random(long a,long b){return a+random(b-a);} void randomSeed(unsigned long){}
WiFiC WiFi;
