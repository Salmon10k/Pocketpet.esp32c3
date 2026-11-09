#pragma once
#include <Arduino.h>
class TwoWire : public Stream { public: bool begin(){return true;} bool begin(int,int,uint32_t=0){return true;} void setClock(uint32_t){} void setTimeOut(uint16_t){} void beginTransmission(uint8_t){} uint8_t endTransmission(bool=true){return 0;} size_t write(uint8_t){return 1;} size_t write(const uint8_t*,size_t){return 1;} size_t requestFrom(uint8_t,uint8_t,bool=true){return 0;} int read(){return 0;} };
extern TwoWire Wire;
