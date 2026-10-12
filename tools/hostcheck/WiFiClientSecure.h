#pragma once
#include <WiFi.h>
class WiFiClientSecure : public WiFiClient { public: void setInsecure() {} int lastError(char *, size_t) { return 0; } };
