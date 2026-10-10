#pragma once
// Mock WiFi.h + time helpers for the host syntax check only.
#include <Arduino.h>
#include <time.h>
enum { WIFI_STA = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 };
struct WiFiC {
  void mode(int) {}
  void begin(const char *, const char *) {}
  int status() { return WL_DISCONNECTED; }
  void disconnect(bool = false, bool = false) {}
};
extern WiFiC WiFi;
inline void configTime(long, int, const char *, const char * = nullptr, const char * = nullptr) {}
