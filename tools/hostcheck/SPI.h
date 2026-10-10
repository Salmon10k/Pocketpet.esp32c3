#pragma once
#include <Arduino.h>
class SPIClass { public: void begin(){} void beginTransaction(int){} void endTransaction(){} uint8_t transfer(uint8_t){return 0;} };
extern SPIClass SPI;
