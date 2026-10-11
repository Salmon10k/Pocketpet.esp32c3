#pragma once
// Mock WiFi.h + time helpers for the host syntax check only.
#include <Arduino.h>
#include <time.h>
enum { WIFI_OFF = 0, WIFI_STA = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 };
struct IPAddress { String toString() const { return String(); } };
struct WiFiC {
  void mode(int) {}
  void begin(const char *, const char *) {}
  int status() { return WL_DISCONNECTED; }
  void disconnect(bool = false, bool = false) {}
  int RSSI() { return 0; }
  IPAddress localIP() { return IPAddress(); }
};
extern WiFiC WiFi;
class WiFiClient { public: virtual ~WiFiClient() {} };
inline void configTime(long, int, const char *, const char * = nullptr, const char * = nullptr) {}
