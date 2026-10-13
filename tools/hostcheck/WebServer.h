#pragma once
// Minimal WebServer mock for tools/hostcheck only. Not a runtime implementation.
#include <Arduino.h>
enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT, HTTP_PATCH, HTTP_DELETE, HTTP_OPTIONS };
class WebServer {
public:
  explicit WebServer(int) {}
  template <typename Handler> void on(const char *, HTTPMethod, Handler) {}
  template <typename Handler> void onNotFound(Handler) {}
  void begin() {}
  void stop() {}
  void handleClient() {}
  String arg(const char *) { return String(); }
  void send(int, const char *, const String &) {}
  void send(int, const char *, const char *) {}
  void send_P(int, const char *, const char *) {}
};
