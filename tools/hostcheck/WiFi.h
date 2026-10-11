#pragma once
// Mock WiFi.h + time helpers for the host syntax check only.
#include <Arduino.h>
#include <time.h>
#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED (-2)
enum { WIFI_POWER_8_5dBm = 34, WIFI_POWER_11dBm = 44, WIFI_OFF = 0, WIFI_STA = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 };
struct IPAddress { String toString() const { return String(); } };
struct WiFiC {
  void mode(int) {}
  void setTxPower(int) {}
  void begin(const char *, const char *) {}
  int status() { return WL_DISCONNECTED; }
  void disconnect(bool = false, bool = false) {}
  int RSSI() { return 0; }
  int RSSI(int) { return 0; }
  String SSID() { return String(); }
  String SSID(int) { return String(); }
  int scanNetworks(bool = false) { return 0; }
  int16_t scanComplete() { return 0; }
  void scanDelete() {}
  IPAddress localIP() { return IPAddress(); }
};
extern WiFiC WiFi;
class WiFiClient { public: virtual ~WiFiClient() {} void stop() {} };
inline void configTime(long, int, const char *, const char * = nullptr, const char * = nullptr) {}
