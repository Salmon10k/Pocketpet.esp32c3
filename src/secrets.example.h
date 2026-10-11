// Copy this file to src/secrets.h and fill in your local Wi-Fi credentials.
// src/secrets.h is ignored by Git so credentials are not committed.
#pragma once
#define WIFI_SSID "your_wifi_name"
#define WIFI_PASSWORD "your_wifi_password"

// Want it to work at home, at uni and on your phone hotspot? List several networks instead
// (this replaces WIFI_SSID / WIFI_PASSWORD, delete those two lines when you use it).
// On entering Online it scans and tries the strongest known network first, then the others.
// 2.4 GHz networks only (the ESP32-C3 has no 5 GHz). Phone hotspot: set it to 2.4 GHz.
// #define WIFI_NETWORKS { {"HomeWifi", "home_pass"}, {"UniWifi", "uni_pass"}, {"MyPhone", "hotspot_pass"} }

// ---- Optional (Online mode). Uncomment to override the defaults. ----
// Without these, your location comes from an IP lookup (ip-api.com) and the clock uses UTC+5.
// #define LOCATION_LAT 31.52f
// #define LOCATION_LON 74.36f
// #define LOCATION_NAME "Home"
// #define LOCAL_TZ "PKT-5"            // POSIX TZ string. Note the sign: UTC+5 is written "-5".
// #define CURRENCY_HOME "PKR"         // second currency on the Currency screen
// #define GITHUB_REPO "Salmon10k/Pocketpet.esp32c3"
// #define WIFI_TX_POWER WIFI_POWER_11dBm   // default is 8.5 dBm (fixes flaky connects on many C3 Super Minis)
