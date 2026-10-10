#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <algorithm>
#include <stdarg.h>
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define OUTPUT_OPEN_DRAIN 3
#define PROGMEM
#define PSTR(x) x
#define pgm_read_byte(a) (*(const uint8_t*)(a))
#define pgm_read_word(a) (*(const uint16_t*)(a))
#define U8X8_PROGMEM
typedef bool boolean;
typedef uint8_t byte;
using std::min; using std::max;
#ifndef constrain
#define constrain(x,a,b) ((x)<(a)?(a):((x)>(b)?(b):(x)))
#endif
unsigned long millis(); unsigned long micros();
void pinMode(uint8_t,uint8_t); int digitalRead(uint8_t); void digitalWrite(uint8_t,uint8_t);
void delay(unsigned long); void delayMicroseconds(unsigned int);
long random(long); long random(long,long); void randomSeed(unsigned long);
class String { public: String(){} String(unsigned long){} String(const char*){} String& operator+=(const String&){return *this;} String& operator+=(const char*){return *this;} };
class Print { public: virtual size_t write(uint8_t)=0; virtual size_t write(const uint8_t*b,size_t n){for(size_t i=0;i<n;i++)write(b[i]);return n;} size_t print(const String&){return 0;} size_t print(const char*){return 0;} size_t print(int){return 0;} size_t print(char){return 0;} size_t println(const char*){return 0;} int printf(const char*,...){return 0;} virtual ~Print(){} };
class Stream : public Print {};
struct SerialC : public Print { void begin(unsigned long){} operator bool() const {return true;} size_t write(uint8_t){return 1;} using Print::print; using Print::write; } ;
extern SerialC Serial;
