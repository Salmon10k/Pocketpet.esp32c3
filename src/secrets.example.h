// Copy this file to src/secrets.h and fill in your local Wi-Fi credentials.
// src/secrets.h is ignored by Git so credentials are not committed.
#pragma once
#define WIFI_SSID "your_wifi_name"
#define WIFI_PASSWORD "your_wifi_password"

// ---- Optional (Online mode). Uncomment to override the defaults. ----
// Without these, your location comes from an IP lookup (ip-api.com) and the clock uses UTC+5.
// #define LOCATION_LAT 31.52f
// #define LOCATION_LON 74.36f
// #define LOCATION_NAME "Home"
// #define LOCAL_TZ "PKT-5"            // POSIX TZ string. Note the sign: UTC+5 is written "-5".
// #define CURRENCY_HOME "PKR"         // second currency on the Currency screen
// #define GITHUB_REPO "Salmon10k/Pocketpet.esp32c3"
