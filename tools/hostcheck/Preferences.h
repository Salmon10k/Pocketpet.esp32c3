#pragma once
#include <Arduino.h>
class Preferences { public: bool begin(const char*,bool=false){return true;} uint8_t getUChar(const char*,uint8_t d=0){return d;} uint32_t getUInt(const char*,uint32_t d=0){return d;} size_t putUChar(const char*,uint8_t){return 1;} size_t putUInt(const char*,uint32_t){return 1;} String getString(const char*,const String& d=String()){return d;} size_t putString(const char*,const String&){return 1;} void end(){} };
