#pragma once
#include <WiFi.h>
enum followRedirects_t { HTTPC_DISABLE_FOLLOW_REDIRECTS, HTTPC_STRICT_FOLLOW_REDIRECTS, HTTPC_FORCE_FOLLOW_REDIRECTS };
class HTTPClient {
 public:
  bool begin(WiFiClient &, const char *) { return true; }
  void setConnectTimeout(uint16_t) {}
  void setTimeout(uint16_t) {}
  void setFollowRedirects(followRedirects_t) {}
  void setUserAgent(const char *) {}
  int GET() { return 0; }
  String getString() { return String(); }
  void setReuse(bool) {}
  void addHeader(const String&, const String&) {}
  int POST(String) { return 0; }
  int sendRequest(const char*, String) { return 0; }
  int getSize() { return 0; }
  void end() {}
};
