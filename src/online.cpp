#include "online.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// ---- Optional overrides (put them in the gitignored src/secrets.h) ----
#ifndef LOCAL_TZ
#define LOCAL_TZ "PKT-5"                    // POSIX TZ string for the clock (PKT = UTC+5, note the sign)
#endif
#ifndef CURRENCY_HOME
#define CURRENCY_HOME "PKR"                 // shown as USD > CURRENCY_HOME
#endif
#ifndef GITHUB_REPO
#define GITHUB_REPO "Salmon10k/Pocketpet.esp32c3"
#endif
// LOCATION_LAT / LOCATION_LON / LOCATION_NAME: if defined, used instead of the IP-based lookup.

namespace online {
namespace {

// ---------------------------------------------------------------- tuning
const uint32_t CONNECT_TIMEOUT_MS = 10000;     // per attempt
const int      CONNECT_TRIES      = 3;         // random "can't connect" is common on C3 boards: retry by itself
const uint32_t BACKOFF_MS         = 1000;      // pause between connect attempts
const uint32_t SETTLE_MS          = 600;       // let DHCP/DNS settle before the first request
const uint32_t HTTP_TIMEOUT_MS    = 7000;
const int      JOB_TRIES          = 3;         // a request that fails for a network reason is retried
const uint32_t NTP_RESYNC_MS      = 6UL * 3600UL * 1000UL;
const uint32_t NEVER              = 0xFFFFFFFFUL;

// ---------------------------------------------------------------- items (screens) and jobs (requests)
enum Item : uint8_t {
  IT_CLOCK, IT_WEATHER, IT_FORECAST, IT_AQI, IT_CURRENCY, IT_WORLD,
  IT_FACT, IT_ADVICE, IT_MOON, IT_NET, IT_ISS, IT_GITHUB, IT_COUNT
};
const char *const ITEM_NAMES[IT_COUNT] = {
  "Clock", "Weather", "Forecast", "Air quality", "Currency", "World clocks",
  "Random fact", "Advice", "Moon phase", "Net status", "ISS tracker", "GitHub"
};
const int LIST_COUNT = IT_COUNT + 2;           // + "Sync now" + "Back"

enum Job : uint8_t { J_GEO, J_WEATHER, J_AQI, J_CUR, J_FACT, J_ADVICE, J_NET, J_ISS, J_CREW, J_GITHUB, J_COUNT };
inline uint16_t jbit(int j) { return (uint16_t)(1u << j); }

// How old cached data may get before opening the screen refetches it.
const uint32_t JOB_EVERY[J_COUNT] = {
  NEVER,                       // geo: once per boot
  30UL * 60000UL,              // weather (also feeds Forecast and the pet)
  30UL * 60000UL,              // air quality
  6UL * 3600000UL,             // currency
  NEVER,                       // fact: only when you press A
  NEVER,                       // advice: only when you press A
  20000UL,                     // net status
  60000UL,                     // ISS position
  6UL * 3600000UL,             // people in space
  10UL * 60000UL               // GitHub
};

struct JobState {
  uint32_t at;     // millis() of last success
  uint32_t failAt; // millis() of last final failure (background refresh waits a minute before trying again)
  bool has;
  uint8_t err;     // 0 ok, 1 no Wi-Fi, 2 HTTP error, 3 bad reply, 4 no location
  int16_t http;
};
JobState jobs[J_COUNT];

enum WifiState : uint8_t { W_OFF, W_SCAN, W_CONNECTING, W_BACKOFF, W_UP, W_FAIL };

// ---------------------------------------------------------------- state
bool viewing = false;          // false = list, true = a screen
int listIdx = 0;
int cur = IT_CLOCK;
uint32_t openedAt = 0;
uint32_t hintUntil = 0;

// One Wi-Fi session per visit to Online mode: connect once on entering, fetch everything, stay
// connected while you are in Online mode, switch the radio off when you leave.
bool session = false;
bool wifiHold = false;          // another mode (Spotify) is using the radio
WifiState wst = W_OFF;
int attempt = 0, attemptTotal = CONNECT_TRIES;   // 1-based attempt counter shown as "wifi 2/3"
uint8_t order[8];              // known networks to try, strongest signal first
int orderLen = 0;
uint32_t scanStart = 0;
uint32_t connectStart = 0, backoffUntil = 0, settleUntil = 0, lastAuto = 0;
uint16_t pending = 0;
uint8_t tries[J_COUNT];        // attempts used by the request currently pending
int syncTotal = 0;             // jobs queued by the current "sync everything" (0 = none running)
bool syncGotData = false;
bool needTime = false;         // a clock-type screen is waiting for NTP
bool ntpStarted = false, ntpForce = false;
uint32_t ntpStartedAt = 0;
bool drawnSince = true;
uint32_t gate = 0;
int treats = 0;

// Known Wi-Fi networks. Either the single WIFI_SSID / WIFI_PASSWORD, or WIFI_NETWORKS in secrets.h:
//   #define WIFI_NETWORKS { {"Home", "pass1"}, {"Uni", "pass2"}, {"MyPhone", "pass3"} }
struct Net { const char *ssid; const char *pass; };
#ifdef WIFI_NETWORKS
const Net NETS[] = WIFI_NETWORKS;
#else
const Net NETS[] = {{WIFI_SSID, WIFI_PASSWORD}};
#endif
const int NETN = (int)(sizeof(NETS) / sizeof(NETS[0])) < 8 ? (int)(sizeof(NETS) / sizeof(NETS[0])) : 8;
bool anyNet() {
  for (int i = 0; i < NETN; i++)
    if (NETS[i].ssid[0]) return true;
  return false;
}
const bool netConfigured = anyNet();

// ---------------------------------------------------------------- cached data
char locCity[24] = "";
float locLat = 0, locLon = 0;
bool haveLoc = false;

struct Wx {
  float temp, feels, wind;
  uint8_t hum, code;
  bool day;
  int8_t hi[5], lo[5];
  uint8_t pop[5], dow[5];
} wx;

struct Aq { int16_t aqi; float pm25; } aq;
struct Fx { float home, eur, gbp; } fx;
struct Iss { float lat, lon; int16_t crew; } iss = {0, 0, -1};
struct Gh { int stars, forks, issues; int64_t pushed; } gh;
bool netOk = false;
int pingMs = 0;
uint32_t ntpOkAt = 0;

struct Wrapped {
  char text[200];
  uint8_t start[14], len[14], n;
};
Wrapped fact, advice;

// ---------------------------------------------------------------- date math (no timegm needed)
int64_t dfc(int y, int m, int d) {             // days since 1970-01-01
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;
  int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
int dowOf(int64_t days) { return (int)(((days % 7) + 11) % 7); }   // 0 = Sunday
int daysIn(int y, int m) {
  static const uint8_t dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return dm[m - 1];
}
int nthSun(int y, int m, int n) {               // n = 1..4, or 5 for the last Sunday
  int first = 1 + (7 - dowOf(dfc(y, m, 1))) % 7;
  if (n < 5) return first + 7 * (n - 1);
  int d = first, dim = daysIn(y, m);
  while (d + 7 <= dim) d += 7;
  return d;
}

enum Rule : uint8_t { Z_NONE, Z_EU, Z_US, Z_AU };
struct Zone { const char *name; Rule rule; int16_t baseMin; };
const Zone ZONES[4] = {{"LON", Z_EU, 0}, {"NYC", Z_US, -300}, {"TYO", Z_NONE, 540}, {"SYD", Z_AU, 600}};

int zoneOffsetSec(const Zone &z, int64_t t) {
  time_t tt = (time_t)t;
  struct tm u;
  gmtime_r(&tt, &u);
  int y = u.tm_year + 1900;
  bool dst = false;
  if (z.rule == Z_EU) {
    int64_t s = dfc(y, 3, nthSun(y, 3, 5)) * 86400 + 3600;
    int64_t e = dfc(y, 10, nthSun(y, 10, 5)) * 86400 + 3600;
    dst = t >= s && t < e;
  } else if (z.rule == Z_US) {
    int64_t s = dfc(y, 3, nthSun(y, 3, 2)) * 86400 + 7 * 3600;
    int64_t e = dfc(y, 11, nthSun(y, 11, 1)) * 86400 + 6 * 3600;
    dst = t >= s && t < e;
  } else if (z.rule == Z_AU) {
    int64_t s = dfc(y, 10, nthSun(y, 10, 1)) * 86400 - 8 * 3600;
    int64_t e = dfc(y, 4, nthSun(y, 4, 1)) * 86400 - 8 * 3600;
    dst = t >= s || t < e;
  }
  return z.baseMin * 60 + (dst ? 3600 : 0);
}

bool timeValid() { return time(nullptr) > 1700000000; }

// ---------------------------------------------------------------- text helpers
void cleanText(char *dst, size_t cap, const char *src) {
  size_t n = 0;
  const unsigned char *p = (const unsigned char *)src;
  while (*p && n + 1 < cap) {
    unsigned char c = *p;
    if (c < 0x80) {
      dst[n++] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : (char)c;
      p++;
    } else {
      int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
      char rep = '?';
      if (len == 3 && p[1] == 0x80) {
        if (p[2] == 0x98 || p[2] == 0x99) rep = '\'';
        else if (p[2] == 0x9C || p[2] == 0x9D) rep = '"';
        else if (p[2] == 0x93 || p[2] == 0x94) rep = '-';
      }
      dst[n++] = rep;
      for (int i = 0; i < len && *p; i++) p++;
    }
  }
  dst[n] = 0;
}

void wrapText(Wrapped &w, int cols) {
  w.n = 0;
  int i = 0, L = (int)strlen(w.text);
  while (i < L && w.n < 14) {
    while (w.text[i] == ' ') i++;
    if (i >= L) break;
    int end = i + cols;
    if (end >= L) {
      end = L;
    } else {
      int k = end;
      while (k > i && w.text[k] != ' ') k--;
      if (k > i) end = k;
    }
    w.start[w.n] = (uint8_t)i;
    w.len[w.n] = (uint8_t)(end - i);
    w.n++;
    i = end;
  }
}

void setWrapped(Wrapped &w, const char *src) {
  cleanText(w.text, sizeof(w.text), src);
  wrapText(w, 25);
}

void copyStr(char *dst, size_t cap, const char *src) { snprintf(dst, cap, "%s", src); }

// ---------------------------------------------------------------- Wi-Fi
#ifndef WIFI_TX_POWER
// Many ESP32-C3 Super Mini boards fail to connect at full power (antenna layout). Lower power is the
// well-known fix. If your router is far away and the signal is weak, try WIFI_POWER_11dBm or WIFI_POWER_19_5dBm.
#define WIFI_TX_POWER WIFI_POWER_8_5dBm
#endif

void radioOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wst = W_OFF;
}

void startAttempt(uint32_t now) {
  attempt++;
  const Net &n = NETS[order[(attempt - 1) % orderLen]];
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_TX_POWER);
  WiFi.begin(n.ssid, n.pass);
  wst = W_CONNECTING;
  connectStart = now;
}

// Work out which known networks to try and in what order, from the scan result (n < 0 = scan failed).
void buildOrder(int n) {
  int8_t best[8];
  bool used[8];
  for (int k = 0; k < NETN; k++) { best[k] = -127; used[k] = false; }
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    for (int k = 0; k < NETN; k++)
      if (NETS[k].ssid[0] && strcmp(ssid.c_str(), NETS[k].ssid) == 0 && rssi > best[k]) best[k] = (int8_t)rssi;
  }
  orderLen = 0;
  for (;;) {                                         // strongest visible network first
    int pick = -1;
    for (int k = 0; k < NETN; k++)
      if (!used[k] && best[k] > -127 && (pick < 0 || best[k] > best[pick])) pick = k;
    if (pick < 0) break;
    used[pick] = true;
    order[orderLen++] = (uint8_t)pick;
  }
  if (orderLen == 0)                                 // nothing seen (scan failed, hidden SSID): try them all in config order
    for (int k = 0; k < NETN; k++)
      if (NETS[k].ssid[0]) order[orderLen++] = (uint8_t)k;
  if (n >= 0) WiFi.scanDelete();
  int total = orderLen * 2;                          // each network gets two goes
  attemptTotal = total < CONNECT_TRIES ? CONNECT_TRIES : (total > 8 ? 8 : total);
}

void beginSession(uint32_t now) {              // pick a network and connect (with automatic retries)
  if (!netConfigured) return;
  attempt = 0;
  int valid = 0, only = 0;
  for (int k = 0; k < NETN; k++)
    if (NETS[k].ssid[0]) { valid++; only = k; }
  if (valid == 1) {                            // one network: no scan needed
    order[0] = (uint8_t)only;
    orderLen = 1;
    attemptTotal = CONNECT_TRIES;
    startAttempt(now);
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.scanNetworks(true);                     // async, polled in tick()
  wst = W_SCAN;
  scanStart = now;
}

void queueGate(uint32_t now) {      // let one frame ("Fetching...") reach the screen before a request blocks the loop
  drawnSince = false;
  gate = now + 400;
}

void failPending(uint8_t err) {
  for (int j = 0; j < J_COUNT; j++)
    if (pending & jbit(j)) jobs[j].err = err;
  pending = 0;
  syncTotal = 0;
  needTime = false;
}

void startSntp(uint32_t now) {
  if (ntpStarted && !ntpForce && now - ntpStartedAt < NTP_RESYNC_MS) return;
  configTime(0, 0, "pool.ntp.org", "time.google.com");   // system time stays UTC
  setenv("TZ", LOCAL_TZ, 1);                             // localtime() then uses the local zone
  tzset();
  ntpStarted = true;
  ntpForce = false;
  ntpStartedAt = now;
}

// ---------------------------------------------------------------- HTTP
int httpGet(const char *url, bool tls, String *out) {
  WiFiClient plain;
  WiFiClientSecure secure;
  WiFiClient *client = &plain;
  if (tls) {
    secure.setInsecure();      // no certificate pinning on purpose: keeps flash and RAM small
    client = &secure;
  }
  HTTPClient http;
  if (!http.begin(*client, url)) return -1;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("Pocketpet/1.0");
  int code = http.GET();
  if (code == 200 && out) *out = http.getString();
  http.end();
  return code;
}

uint8_t getJson(const char *url, bool tls, JsonDocument &doc, const JsonDocument &filter, int16_t &httpOut) {
  String body;
  int code = httpGet(url, tls, &body);
  httpOut = (int16_t)code;
  if (code != 200) return 2;
  DeserializationError e = deserializeJson(doc, body.c_str(), DeserializationOption::Filter(filter));
  return e ? 3 : 0;
}

// ---------------------------------------------------------------- jobs
uint8_t jobGeo() {
#ifdef LOCATION_LAT
  locLat = LOCATION_LAT;
  locLon = LOCATION_LON;
#ifdef LOCATION_NAME
  copyStr(locCity, sizeof(locCity), LOCATION_NAME);
#else
  copyStr(locCity, sizeof(locCity), "Home");
#endif
  haveLoc = true;
  return 0;
#else
  JsonDocument doc, f;
  f["status"] = true; f["city"] = true; f["lat"] = true; f["lon"] = true;
  uint8_t e = getJson("http://ip-api.com/json/?fields=status,city,lat,lon", false, doc, f, jobs[J_GEO].http);
  if (e) return e;
  if (strcmp(doc["status"] | "", "success") != 0) return 3;
  locLat = doc["lat"] | 0.0f;
  locLon = doc["lon"] | 0.0f;
  copyStr(locCity, sizeof(locCity), doc["city"] | "Here");
  haveLoc = true;
  return 0;
#endif
}

int parseDow(const char *iso) {                 // "2026-10-12" -> 0..6, or 0 on bad input
  int y, m, d;
  if (sscanf(iso, "%d-%d-%d", &y, &m, &d) != 3) return 0;
  return dowOf(dfc(y, m, d));
}

uint8_t jobWeather() {
  char url[420];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
           "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
           "&timezone=auto&forecast_days=5",
           locLat, locLon);
  JsonDocument doc, f;
  f["current"] = true; f["daily"] = true;
  uint8_t e = getJson(url, true, doc, f, jobs[J_WEATHER].http);
  if (e) return e;
  JsonObject c = doc["current"];
  if (c.isNull()) return 3;
  wx.temp = c["temperature_2m"] | 0.0f;
  wx.feels = c["apparent_temperature"] | wx.temp;
  wx.hum = (uint8_t)(c["relative_humidity_2m"] | 0);
  wx.code = (uint8_t)(c["weather_code"] | 0);
  wx.wind = c["wind_speed_10m"] | 0.0f;
  wx.day = (c["is_day"] | 1) != 0;
  JsonArray tmax = doc["daily"]["temperature_2m_max"];
  JsonArray tmin = doc["daily"]["temperature_2m_min"];
  JsonArray pop = doc["daily"]["precipitation_probability_max"];
  JsonArray days = doc["daily"]["time"];
  for (int i = 0; i < 5; i++) {
    wx.hi[i] = (int8_t)lroundf(tmax[i] | 0.0f);
    wx.lo[i] = (int8_t)lroundf(tmin[i] | 0.0f);
    wx.pop[i] = (uint8_t)(pop[i] | 0);
    wx.dow[i] = (uint8_t)parseDow(days[i] | "");
  }
  return 0;
}

uint8_t jobAqi() {
  char url[200];
  snprintf(url, sizeof(url),
           "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.3f&longitude=%.3f&current=us_aqi,pm2_5",
           locLat, locLon);
  JsonDocument doc, f;
  f["current"] = true;
  uint8_t e = getJson(url, true, doc, f, jobs[J_AQI].http);
  if (e) return e;
  int a = doc["current"]["us_aqi"] | -1;
  if (a < 0) return 3;
  aq.aqi = (int16_t)a;
  aq.pm25 = doc["current"]["pm2_5"] | 0.0f;
  return 0;
}

uint8_t jobCurrency() {
  JsonDocument doc, f;
  f["result"] = true;
  f["rates"][CURRENCY_HOME] = true;
  f["rates"]["EUR"] = true;
  f["rates"]["GBP"] = true;
  uint8_t e = getJson("https://open.er-api.com/v6/latest/USD", true, doc, f, jobs[J_CUR].http);
  if (e) return e;
  if (strcmp(doc["result"] | "", "success") != 0) return 3;
  fx.home = doc["rates"][CURRENCY_HOME] | 0.0f;
  fx.eur = doc["rates"]["EUR"] | 0.0f;
  fx.gbp = doc["rates"]["GBP"] | 0.0f;
  return fx.home > 0 ? 0 : 3;
}

uint8_t jobFact() {
  JsonDocument doc, f;
  f["text"] = true;
  uint8_t e = getJson("https://uselessfacts.jsph.pl/api/v2/facts/random?language=en", true, doc, f, jobs[J_FACT].http);
  if (e) return e;
  const char *t = doc["text"] | "";
  if (!t[0]) return 3;
  setWrapped(fact, t);
  return 0;
}

uint8_t jobAdvice() {
  JsonDocument doc, f;
  f["slip"]["advice"] = true;
  uint8_t e = getJson("https://api.adviceslip.com/advice", true, doc, f, jobs[J_ADVICE].http);
  if (e) return e;
  const char *t = doc["slip"]["advice"] | "";
  if (!t[0]) return 3;
  setWrapped(advice, t);
  return 0;
}

uint8_t jobNet() {
  uint32_t t0 = millis();
  int code = httpGet("http://connectivitycheck.gstatic.com/generate_204", false, nullptr);
  pingMs = (int)(millis() - t0);
  jobs[J_NET].http = (int16_t)code;
  netOk = (code == 204 || code == 200);
  return 0;                       // the screen shows "Internet: NO" itself, so this is not an error
}

uint8_t jobIss() {
  JsonDocument doc, f;
  f["iss_position"] = true;
  uint8_t e = getJson("http://api.open-notify.org/iss-now.json", false, doc, f, jobs[J_ISS].http);
  if (e) return e;
  JsonObject p = doc["iss_position"];
  if (p.isNull()) return 3;
  iss.lat = (float)atof(p["latitude"] | "0");
  iss.lon = (float)atof(p["longitude"] | "0");
  return 0;
}

uint8_t jobCrew() {
  JsonDocument doc, f;
  f["number"] = true;
  uint8_t e = getJson("http://api.open-notify.org/astros.json", false, doc, f, jobs[J_CREW].http);
  if (e) return e;
  iss.crew = (int16_t)(doc["number"] | -1);
  return 0;
}

uint8_t jobGithub() {
  char url[120];
  snprintf(url, sizeof(url), "https://api.github.com/repos/%s", GITHUB_REPO);
  JsonDocument doc, f;
  f["stargazers_count"] = true; f["forks_count"] = true; f["open_issues_count"] = true; f["pushed_at"] = true;
  uint8_t e = getJson(url, true, doc, f, jobs[J_GITHUB].http);
  if (e) return e;
  gh.stars = doc["stargazers_count"] | 0;
  gh.forks = doc["forks_count"] | 0;
  gh.issues = doc["open_issues_count"] | 0;
  int y, mo, d, h, mi, s;
  gh.pushed = 0;
  if (sscanf(doc["pushed_at"] | "", "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6)
    gh.pushed = dfc(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
  return 0;
}

uint8_t runJob(int j) {
  switch (j) {
    case J_GEO: return jobGeo();
    case J_WEATHER: return jobWeather();
    case J_AQI: return jobAqi();
    case J_CUR: return jobCurrency();
    case J_FACT: return jobFact();
    case J_ADVICE: return jobAdvice();
    case J_NET: return jobNet();
    case J_ISS: return jobIss();
    case J_CREW: return jobCrew();
    default: return jobGithub();
  }
}

uint16_t itemJobs(int it) {
  switch (it) {
    case IT_WEATHER:
    case IT_FORECAST: return jbit(J_WEATHER);
    case IT_AQI: return jbit(J_AQI);
    case IT_CURRENCY: return jbit(J_CUR);
    case IT_FACT: return jbit(J_FACT);
    case IT_ADVICE: return jbit(J_ADVICE);
    case IT_NET: return jbit(J_NET);
    case IT_ISS: return jbit(J_ISS) | jbit(J_CREW);
    case IT_GITHUB: return jbit(J_GITHUB);
    default: return 0;
  }
}

bool itemNeedsTime(int it) { return it == IT_CLOCK || it == IT_WORLD || it == IT_MOON; }

// Queue whatever this screen needs. force = user pressed A.
void requestFresh(int it, bool force, uint32_t now) {
  uint16_t mask = itemJobs(it);
  bool any = false;
  for (int j = 0; j < J_COUNT; j++) {
    if (!(mask & jbit(j)) || (pending & jbit(j))) continue;
    bool want = !jobs[j].has || (JOB_EVERY[j] != NEVER && now - jobs[j].at >= JOB_EVERY[j]);
    if (force) want = !(jobs[j].has && now - jobs[j].at < 1500);        // A refreshes, but ignore button mashing
    if (want) { pending |= jbit(j); jobs[j].err = 0; tries[j] = 0; any = true; }
  }
  if (itemNeedsTime(it) && (!timeValid() || force)) {
    needTime = true;
    if (force) ntpForce = true;
    any = true;
  }
  if (any) {
    queueGate(now);
    if (!netConfigured) {
      pending = 0;
      needTime = false;
    } else if (wst == W_OFF || wst == W_FAIL) {
      beginSession(now);                         // the session was lost: reconnect (with retries)
    } else if (wst == W_UP && ntpForce) {
      startSntp(now);
    }
  }
}

// Everything every screen needs, in one go, over the one connection.
// force = "Sync now": refetch everything (random fact / advice are only fetched when empty).
void queueSync(uint32_t now, bool force) {
  pending = 0;
  for (int j = 0; j < J_COUNT; j++) {
    if (j == J_GEO) continue;                     // pulled in automatically before weather / air quality
    bool want = force || !jobs[j].has || j == J_NET || (JOB_EVERY[j] != NEVER && now - jobs[j].at >= JOB_EVERY[j]);
    if (j == J_FACT || j == J_ADVICE) want = !jobs[j].has;
    if (want) { pending |= jbit(j); jobs[j].err = 0; tries[j] = 0; }
  }
  if (force) ntpForce = true;
  needTime = !timeValid() || force;
  syncTotal = __builtin_popcount(pending);
  syncGotData = false;
  queueGate(now);
}

// Order the requests run in: what you most likely look at first, the slow "extras" last.
const uint8_t RUN_ORDER[J_COUNT] = {J_GEO, J_WEATHER, J_AQI, J_CUR, J_NET, J_GITHUB, J_ISS, J_CREW, J_FACT, J_ADVICE};

// Background refresh while the session is up. Screens with live data (Net, ISS) only refresh while you look at them.
void autoRefresh(uint32_t now) {
  for (int j = 0; j < J_COUNT; j++) {
    if (JOB_EVERY[j] == NEVER || !jobs[j].has || (pending & jbit(j))) continue;
    if (j == J_NET && !(viewing && cur == IT_NET)) continue;
    if (j == J_ISS && !(viewing && cur == IT_ISS)) continue;
    if (jobs[j].err && now - jobs[j].failAt < 60000UL) continue;
    if (now - jobs[j].at >= JOB_EVERY[j]) { pending |= jbit(j); tries[j] = 0; }
  }
}

void sessionTag(char *b, size_t n) {
  if (!netConfigured) snprintf(b, n, "no setup");
  else if (wst == W_SCAN) snprintf(b, n, "scanning");
  else if (wst == W_CONNECTING || wst == W_BACKOFF) snprintf(b, n, "wifi %d/%d", attempt, attemptTotal);
  else if (wst == W_FAIL) snprintf(b, n, "offline");
  else if (wst == W_UP && pending && syncTotal > 0) snprintf(b, n, "sync %d/%d", syncTotal - __builtin_popcount(pending), syncTotal);
  else if (wst == W_UP) snprintf(b, n, "online");
  else snprintf(b, n, "off");
}

void openItem(int it, uint32_t now) {
  cur = it;
  viewing = true;
  openedAt = now;
  hintUntil = now + 2500;
  requestFresh(it, false, now);
}

// ---------------------------------------------------------------- pet hint (cached data only)
bool fresh(int j, uint32_t maxAgeMs) {
  return jobs[j].has && millis() - jobs[j].at < maxAgeMs;
}

bool rainCode(int c) { return (c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95; }

// ---------------------------------------------------------------- drawing helpers
void ageStr(char *b, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s < 60) snprintf(b, n, "now");
  else if (s < 3600) snprintf(b, n, "%lum", (unsigned long)(s / 60));
  else snprintf(b, n, "%luh", (unsigned long)(s / 3600));
}

void header(U8G2 &d, const char *title, const char *tag) {
  d.setFont(u8g2_font_7x13B_tf);
  d.drawStr(0, 12, title);
  d.setFont(u8g2_font_5x7_tf);
  if (tag && tag[0]) d.drawStr(128 - d.getStrWidth(tag), 11, tag);
  d.drawHLine(0, 15, 128);
}

int primaryJob(int it) {
  switch (it) {
    case IT_WEATHER:
    case IT_FORECAST: return J_WEATHER;
    case IT_AQI: return J_AQI;
    case IT_CURRENCY: return J_CUR;
    case IT_FACT: return J_FACT;
    case IT_ADVICE: return J_ADVICE;
    case IT_NET: return J_NET;
    case IT_ISS: return J_ISS;
    case IT_GITHUB: return J_GITHUB;
    default: return -1;
  }
}

bool busyFor(int it) {
  if (pending & itemJobs(it)) return true;
  return itemNeedsTime(it) && needTime && !timeValid() && (wst == W_SCAN || wst == W_CONNECTING || wst == W_BACKOFF || wst == W_UP);
}

bool ready(int it) {
  if (itemNeedsTime(it)) return timeValid();
  int j = primaryJob(it);
  return j >= 0 && jobs[j].has;
}

void tagFor(int it, uint32_t now, char *b, size_t n) {
  b[0] = 0;
  if (busyFor(it)) { snprintf(b, n, "..."); return; }
  int j = primaryJob(it);
  if (j >= 0) {
    if (jobs[j].err) snprintf(b, n, "!");
    else if (jobs[j].has && it != IT_NET) ageStr(b, n, now - jobs[j].at);
    return;
  }
  if (it == IT_CLOCK && ntpOkAt) ageStr(b, n, now - ntpOkAt);
}

const char *errText(uint8_t e) {
  switch (e) {
    case 1: return "No Wi-Fi";
    case 2: return "Server error";
    case 3: return "Bad reply";
    case 4: return "No location";
    default: return "Not loaded";
  }
}

void drawStatus(U8G2 &d, int it, uint32_t now) {
  d.setFont(u8g2_font_6x10_tf);
  if (!netConfigured) {
    d.drawStr(0, 32, "Wi-Fi not set up");
    d.setFont(u8g2_font_5x7_tf);
    d.drawStr(0, 44, "Copy secrets.example.h");
    d.drawStr(0, 52, "to secrets.h and fill in");
    d.drawStr(0, 60, "your SSID + password");
    return;
  }
  if (busyFor(it)) {
    char line[28];
    char what[20];
    if (wst == W_SCAN) snprintf(what, sizeof(what), "Scanning Wi-Fi");
    else if (wst == W_CONNECTING || wst == W_BACKOFF) snprintf(what, sizeof(what), "Connecting %d/%d", attempt, attemptTotal);
    else snprintf(what, sizeof(what), "%s", itemNeedsTime(it) ? "Syncing time" : "Fetching");
    snprintf(line, sizeof(line), "%s%.*s", what, (int)((now / 350) % 4), "...");
    d.drawStr(0, 36, line);
    return;
  }
  int j = primaryJob(it);
  uint8_t e = j >= 0 ? jobs[j].err : (wst == W_FAIL ? 1 : 0);
  if (itemNeedsTime(it) && wst == W_FAIL) e = 1;
  d.drawStr(0, 32, "Couldn't load");
  d.drawStr(0, 44, errText(e));
  if (e == 2 && j >= 0 && jobs[j].http != 0) {
    char c[16];
    snprintf(c, sizeof(c), "code %d", (int)jobs[j].http);
    d.drawStr(0, 56, c);
  } else {
    d.setFont(u8g2_font_5x7_tf);
    d.drawStr(0, 60, "A = try again");
  }
}

const char *wxText(int c) {
  if (c == 0) return "Clear";
  if (c <= 2) return "P. cloudy";
  if (c == 3) return "Overcast";
  if (c == 45 || c == 48) return "Fog";
  if (c >= 51 && c <= 57) return "Drizzle";
  if (c >= 61 && c <= 67) return "Rain";
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return "Snow";
  if (c >= 80 && c <= 82) return "Showers";
  if (c >= 95) return "Storm";
  return "Cloudy";
}

const char *aqiLabel(int a) {
  if (a <= 50) return "Good";
  if (a <= 100) return "Moderate";
  if (a <= 150) return "Sensitive";
  if (a <= 200) return "Unhealthy";
  if (a <= 300) return "Very bad";
  return "Hazardous";
}

const char *const DOW3[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// ---------------------------------------------------------------- screens
void drawClock(U8G2 &d, uint32_t now) {
  char tag[8];
  tagFor(IT_CLOCK, now, tag, sizeof(tag));
  header(d, "CLOCK", tag);
  time_t t = time(nullptr);
  struct tm tmNow;
  localtime_r(&t, &tmNow);
  char clk[16], date[24];
  strftime(clk, sizeof(clk), "%H:%M:%S", &tmNow);
  strftime(date, sizeof(date), "%a %d %b %Y", &tmNow);
  d.setFont(u8g2_font_logisoso24_tn);
  d.drawStr(10, 46, clk);
  d.setFont(u8g2_font_6x10_tf);
  d.drawStr((128 - d.getStrWidth(date)) / 2, 60, date);
}

void drawWeather(U8G2 &d, uint32_t now) {
  char tag[8], b[24];
  tagFor(IT_WEATHER, now, tag, sizeof(tag));
  char title[12];
  copyStr(title, sizeof(title), locCity[0] ? locCity : "WEATHER");
  header(d, title, tag);
  snprintf(b, sizeof(b), "%d", (int)lroundf(wx.temp));
  d.setFont(u8g2_font_logisoso24_tn);
  d.drawStr(0, 46, b);
  int w = d.getStrWidth(b);
  d.drawCircle(w + 4, 28, 2);
  d.setFont(u8g2_font_6x10_tf);
  d.drawStr(w + 9, 46, "C");
  d.drawStr(64, 28, wxText(wx.code));
  snprintf(b, sizeof(b), "Feels %d", (int)lroundf(wx.feels));
  d.drawStr(64, 39, b);
  snprintf(b, sizeof(b), "Hum %d%%", wx.hum);
  d.drawStr(64, 50, b);
  d.setFont(u8g2_font_5x7_tf);
  snprintf(b, sizeof(b), "Rain %d%%  Wind %d km/h", wx.pop[0], (int)lroundf(wx.wind));
  d.drawStr(0, 63, b);
}

void drawForecast(U8G2 &d, uint32_t now) {
  char tag[8];
  tagFor(IT_FORECAST, now, tag, sizeof(tag));
  header(d, "5 DAYS", tag);
  char b[12];
  for (int i = 0; i < 5; i++) {
    int x = i * 25 + 2;
    d.setFont(u8g2_font_5x7_tf);
    d.drawStr(x, 25, DOW3[wx.dow[i] % 7]);
    d.setFont(u8g2_font_6x10_tf);
    snprintf(b, sizeof(b), "%d", wx.hi[i]);
    d.drawStr(x, 38, b);
    snprintf(b, sizeof(b), "%d", wx.lo[i]);
    d.drawStr(x, 49, b);
    d.setFont(u8g2_font_5x7_tf);
    snprintf(b, sizeof(b), "%d%%", wx.pop[i]);
    d.drawStr(x, 62, b);
    if (i) d.drawVLine(i * 25 - 2, 18, 46);
  }
}

void drawAqi(U8G2 &d, uint32_t now) {
  char tag[8], b[24];
  tagFor(IT_AQI, now, tag, sizeof(tag));
  header(d, "AIR", tag);
  snprintf(b, sizeof(b), "%d", aq.aqi);
  d.setFont(u8g2_font_logisoso24_tn);
  d.drawStr(0, 46, b);
  d.setFont(u8g2_font_6x10_tf);
  d.drawStr(62, 30, aqiLabel(aq.aqi));
  snprintf(b, sizeof(b), "PM2.5 %d", (int)lroundf(aq.pm25));
  d.drawStr(62, 42, b);
  int n = aq.aqi * 10 / 300;
  if (n > 10) n = 10;
  if (aq.aqi > 0 && n == 0) n = 1;
  for (int i = 0; i < 10; i++) {
    if (i < n) d.drawBox(i * 12 + 4, 52, 10, 5);
    else d.drawFrame(i * 12 + 4, 52, 10, 5);
  }
  d.setFont(u8g2_font_5x7_tf);
  d.drawStr(0, 63, "US AQI");
}

void drawCurrency(U8G2 &d, uint32_t now) {
  char tag[8], b[24];
  tagFor(IT_CURRENCY, now, tag, sizeof(tag));
  snprintf(b, sizeof(b), "USD > %s", CURRENCY_HOME);
  header(d, b, tag);
  snprintf(b, sizeof(b), "%.2f", fx.home);
  d.setFont(u8g2_font_logisoso16_tf);
  d.drawStr(0, 40, b);
  d.setFont(u8g2_font_6x10_tf);
  snprintf(b, sizeof(b), "EUR %.2f", fx.eur);
  d.drawStr(0, 53, b);
  snprintf(b, sizeof(b), "GBP %.2f", fx.gbp);
  d.drawStr(66, 53, b);
  d.setFont(u8g2_font_5x7_tf);
  d.drawStr(0, 63, "per 1 US dollar");
}

void drawWorld(U8G2 &d, uint32_t now) {
  time_t t = time(nullptr);
  struct tm home;
  localtime_r(&t, &home);
  char tag[8], b[24];
  strftime(tag, sizeof(tag), "%H:%M", &home);
  header(d, "WORLD", tag);
  int64_t homeDay = dfc(home.tm_year + 1900, home.tm_mon + 1, home.tm_mday);
  d.setFont(u8g2_font_6x10_tf);
  for (int i = 0; i < 4; i++) {
    int64_t ct = (int64_t)t + zoneOffsetSec(ZONES[i], t);
    time_t ctt = (time_t)ct;
    struct tm u;
    gmtime_r(&ctt, &u);
    int diff = (int)(dfc(u.tm_year + 1900, u.tm_mon + 1, u.tm_mday) - homeDay);
    const char *dayTxt = diff > 0 ? "+1d" : diff < 0 ? "-1d" : "";
    snprintf(b, sizeof(b), "%s %02d:%02d %s", ZONES[i].name, u.tm_hour, u.tm_min, dayTxt);
    d.drawStr(0, 28 + i * 12, b);
  }
  (void)now;
}

void drawText(U8G2 &d, const Wrapped &w, uint32_t now) {
  d.setFont(u8g2_font_5x7_tf);
  const int rows = 5;
  int first = 0;
  if (w.n > rows) first = (int)(((now - openedAt) / 2500) % (uint32_t)(w.n - rows + 1));
  char buf[28];
  for (int r = 0; r < rows && first + r < w.n; r++) {
    int idx = first + r;
    int n = w.len[idx] < 26 ? w.len[idx] : 26;
    memcpy(buf, w.text + w.start[idx], n);
    buf[n] = 0;
    d.drawStr(1, 27 + r * 8, buf);
  }
  if (w.n > rows) {
    int y = 18 + first * 36 / (w.n - rows + 1);
    d.drawVLine(127, y, 8);
  }
}

void drawFact(U8G2 &d, uint32_t now) {
  char tag[8];
  tagFor(IT_FACT, now, tag, sizeof(tag));
  header(d, "FACT", tag);
  drawText(d, fact, now);
}

void drawAdvice(U8G2 &d, uint32_t now) {
  char tag[8];
  tagFor(IT_ADVICE, now, tag, sizeof(tag));
  header(d, "PET SAYS", tag);
  drawText(d, advice, now);
}

void drawMoon(U8G2 &d, uint32_t now) {
  (void)now;
  const double SYN = 29.530588853, TWO_PI_D = 6.283185307179586;
  double days = ((double)time(nullptr) - 947182440.0) / 86400.0;    // since the new moon of 2000-01-06
  double age = fmod(days, SYN);
  if (age < 0) age += SYN;
  double theta = TWO_PI_D * age / SYN;
  int lit = (int)lround((1.0 - cos(theta)) * 50.0);
  header(d, "MOON", "");
  const int cx = 30, cy = 39, r = 18;
  d.drawCircle(cx, cy, r);
  double ct = cos(theta);
  for (int y = -r + 1; y < r; y++) {
    int w = (int)floor(sqrt((double)(r * r - y * y)));
    int xt = (int)lround(w * ct);
    int x0, x1;
    if (theta < TWO_PI_D / 2) { x0 = xt; x1 = w; }       // waxing: lit on the right
    else { x0 = -w; x1 = -xt; }                            // waning: lit on the left
    if (x1 > x0) d.drawHLine(cx + x0, cy + y, x1 - x0 + 1);
  }
  const char *l1, *l2;
  double f = age / SYN;
  if (f < 0.03 || f > 0.97) { l1 = "NEW"; l2 = "MOON"; }
  else if (f < 0.22) { l1 = "WAXING"; l2 = "CRESCENT"; }
  else if (f < 0.28) { l1 = "FIRST"; l2 = "QUARTER"; }
  else if (f < 0.47) { l1 = "WAXING"; l2 = "GIBBOUS"; }
  else if (f < 0.53) { l1 = "FULL"; l2 = "MOON"; }
  else if (f < 0.72) { l1 = "WANING"; l2 = "GIBBOUS"; }
  else if (f < 0.78) { l1 = "LAST"; l2 = "QUARTER"; }
  else { l1 = "WANING"; l2 = "CRESCENT"; }
  char b[24];
  d.setFont(u8g2_font_6x10_tf);
  d.drawStr(60, 30, l1);
  d.drawStr(60, 41, l2);
  snprintf(b, sizeof(b), "Lit %d%%", lit);
  d.drawStr(60, 53, b);
  double toFull = age < SYN / 2 ? SYN / 2 - age : SYN * 1.5 - age;
  d.setFont(u8g2_font_5x7_tf);
  snprintf(b, sizeof(b), "Full moon in %dd", (int)lround(toFull));
  d.drawStr(0, 63, b);
}

void drawNet(U8G2 &d, uint32_t now) {
  (void)now;
  char tag[12], b[28];
  bool up = (wst == W_UP && WiFi.status() == WL_CONNECTED);
  int rssi = up ? WiFi.RSSI() : 0;
  if (up) snprintf(tag, sizeof(tag), "%d dBm", rssi);
  else snprintf(tag, sizeof(tag), "offline");
  header(d, "NET", tag);
  d.setFont(u8g2_font_6x10_tf);
  if (up) {
    snprintf(b, sizeof(b), "%.16s", WiFi.SSID().c_str());      // which of your networks it picked
    d.drawStr(0, 29, b);
    int bars = rssi > -60 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : 1;
    for (int i = 0; i < 4; i++) {
      if (i < bars) d.drawBox(104 + i * 5, 29 - 3 - i * 2, 3, 3 + i * 2);
      else d.drawFrame(104 + i * 5, 29 - 3 - i * 2, 3, 3 + i * 2);
    }
    snprintf(b, sizeof(b), "IP %s", WiFi.localIP().toString().c_str());
    d.drawStr(0, 41, b);
  } else {
    d.drawStr(0, 29, "Not connected");
    d.drawStr(0, 41, "A = try again");
  }
  if (netOk) snprintf(b, sizeof(b), "Internet OK  %d ms", pingMs);
  else snprintf(b, sizeof(b), "Internet: NO");
  d.drawStr(0, 53, b);
}

void drawIss(U8G2 &d, uint32_t now) {
  char tag[8], b[32];
  tagFor(IT_ISS, now, tag, sizeof(tag));
  header(d, "ISS", tag);
  const int mx = 2, my = 18, mw = 124, mh = 30;
  d.drawFrame(mx, my, mw, mh);
  d.drawHLine(mx, my + mh / 2, mw);                       // equator
  d.drawVLine(mx + mw / 2, my, mh);                       // prime meridian
  int px = mx + (int)lroundf((iss.lon + 180.0f) / 360.0f * (mw - 1));
  int py = my + (int)lroundf((90.0f - iss.lat) / 180.0f * (mh - 1));
  d.drawCircle(px, py, 3);
  d.drawPixel(px, py);
  d.setFont(u8g2_font_5x7_tf);
  snprintf(b, sizeof(b), "%.1f%c %.1f%c", fabsf(iss.lat), iss.lat >= 0 ? 'N' : 'S', fabsf(iss.lon), iss.lon >= 0 ? 'E' : 'W');
  d.drawStr(0, 58, b);
  if (iss.crew >= 0) {
    snprintf(b, sizeof(b), "%d in space", iss.crew);
    d.drawStr(128 - d.getStrWidth(b), 58, b);
  }
}

void drawGithub(U8G2 &d, uint32_t now) {
  char tag[8], b[32];
  tagFor(IT_GITHUB, now, tag, sizeof(tag));
  header(d, "GITHUB", tag);
  const char *slash = strchr(GITHUB_REPO, '/');
  d.setFont(u8g2_font_5x7_tf);
  d.drawStr(0, 26, slash ? slash + 1 : GITHUB_REPO);
  d.setFont(u8g2_font_6x10_tf);
  snprintf(b, sizeof(b), "Stars %d  Forks %d", gh.stars, gh.forks);
  d.drawStr(0, 40, b);
  snprintf(b, sizeof(b), "Open issues %d", gh.issues);
  d.drawStr(0, 51, b);
  d.setFont(u8g2_font_5x7_tf);
  if (gh.pushed > 0 && timeValid()) {
    long mins = (long)(((int64_t)time(nullptr) - gh.pushed) / 60);
    if (mins < 0) mins = 0;
    if (mins < 60) snprintf(b, sizeof(b), "Last push %ldm ago", mins);
    else if (mins < 2880) snprintf(b, sizeof(b), "Last push %ldh ago", mins / 60);
    else snprintf(b, sizeof(b), "Last push %ldd ago", mins / 1440);
  } else {
    snprintf(b, sizeof(b), "Last push: time unknown");
  }
  d.drawStr(0, 63, b);
}

void drawList(U8G2 &d) {
  char tag[16];
  sessionTag(tag, sizeof(tag));
  header(d, "ONLINE", tag);
  d.setFont(u8g2_font_7x13_tf);
  const int visible = 3;
  int first = listIdx - visible + 1;
  if (first < 0) first = 0;
  if (first > LIST_COUNT - visible) first = LIST_COUNT - visible;
  for (int row = 0; row < visible && first + row < LIST_COUNT; row++) {
    int i = first + row;
    int y = 29 + row * 13;
    if (i == listIdx) d.drawStr(0, y, ">");
    d.drawStr(14, y, i == IT_COUNT ? "Sync now" : i == IT_COUNT + 1 ? "Back" : ITEM_NAMES[i]);
  }
  d.setFont(u8g2_font_5x7_tf);
  d.drawStr(0, 63, "A up B ok C dn Touch=Home");
}

}  // namespace

// ================================================================ public API
void enter() {
  viewing = false;
  listIdx = 0;
  session = true;
  pending = 0;
  needTime = false;
  syncTotal = 0;
  if (!netConfigured) return;
  uint32_t now = millis();
  queueSync(now, false);
  beginSession(now);                 // the one and only connection for this visit
}

void leave() {
  session = false;
  viewing = false;
  pending = 0;
  needTime = false;
  syncTotal = 0;
  if (wst != W_OFF && !wifiHold) radioOff();
}

bool wifiConfigured() { return netConfigured; }

void wifiAcquire(uint32_t now) {            // another mode wants the radio: connect (retries and network choice as usual)
  wifiHold = true;
  if (netConfigured && (wst == W_OFF || wst == W_FAIL)) beginSession(now);
}

void wifiRelease() {
  wifiHold = false;
  if (!session && wst != W_OFF) radioOff();
}

bool wifiUp() { return wst == W_UP && WiFi.status() == WL_CONNECTED; }

const char *wifiStatusText() {
  switch (wst) {
    case W_SCAN: return "scanning wifi";
    case W_CONNECTING: case W_BACKOFF: return "connecting wifi";
    case W_FAIL: return "no wifi";
    case W_UP: return "online";
    default: return netConfigured ? "wifi off" : "no wifi set";
  }
}

bool input(bool a, bool b, bool c, uint32_t now) {
  if (!viewing) {
    if (a) listIdx = (listIdx + LIST_COUNT - 1) % LIST_COUNT;
    if (c) listIdx = (listIdx + 1) % LIST_COUNT;
    if (b) {
      if (listIdx == IT_COUNT + 1) return true;
      if (listIdx == IT_COUNT) {
        if (netConfigured) {
          queueSync(now, true);
          if (wst == W_OFF || wst == W_FAIL) beginSession(now);
        }
      } else {
        openItem(listIdx, now);
      }
    }
    return false;
  }
  if (b) {                     // back to the list (a running sync keeps going)
    viewing = false;
    return false;
  }
  if (c) openItem((cur + 1) % IT_COUNT, now);
  else if (a) requestFresh(cur, true, now);
  return false;
}

void tick(uint32_t now) {
  if (!session && !wifiHold) return;
  if (wst == W_SCAN) {
    int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING && now - scanStart < 8000) return;
    buildOrder(n < 0 ? -1 : n);
    startAttempt(now);
    return;
  }
  if (wst == W_BACKOFF) {
    if ((int32_t)(now - backoffUntil) >= 0) startAttempt(now);
    return;
  }
  if (wst == W_CONNECTING) {
    int s = WiFi.status();
    if (s == WL_CONNECTED) {
      wst = W_UP;
      settleUntil = now + SETTLE_MS;
      startSntp(now);
    } else if (s == WL_CONNECT_FAILED || now - connectStart > CONNECT_TIMEOUT_MS) {
      WiFi.disconnect();
      if (attempt < attemptTotal) {                     // random connect failures: just try again (next network if several)
        wst = W_BACKOFF;
        backoffUntil = now + BACKOFF_MS;
      } else {
        failPending(1);
        radioOff();
        wst = W_FAIL;
      }
    }
    return;
  }
  if (wst != W_UP) return;

  if (WiFi.status() != WL_CONNECTED) {                  // link dropped: reconnect by itself, queued work stays queued
    beginSession(now);
    return;
  }
  if (!session) return;                                 // radio is only held for Spotify: no jobs
  if (needTime && timeValid() && now - ntpStartedAt > 1500) {
    needTime = false;
    ntpOkAt = now;
  }
  if ((int32_t)(now - settleUntil) < 0) return;

  if (pending) {
    if (!drawnSince && (int32_t)(now - gate) < 0) return;      // wait for the "Fetching..." frame
    int pick = -1;
    for (int i = 0; i < J_COUNT && pick < 0; i++)
      if (pending & jbit(RUN_ORDER[i])) pick = RUN_ORDER[i];
    int run = pick;
    if ((pick == J_WEATHER || pick == J_AQI) && !haveLoc) run = J_GEO;
    uint8_t e = runJob(run);
    bool transient = e == 3 || (e == 2 && (jobs[run].http <= 0 || jobs[run].http >= 500));
    if (e && transient && tries[run] + 1 < JOB_TRIES) {         // random network hiccup: retry shortly
      tries[run]++;
      drawnSince = false;
      gate = millis() + 800;
      return;
    }
    tries[run] = 0;
    jobs[run].err = e;
    if (!e) {
      jobs[run].has = true;
      jobs[run].at = millis();
      if (run != J_GEO) syncGotData = true;
    } else {
      jobs[run].failAt = millis();
    }
    if (run == J_NET) jobs[J_NET].has = true;
    if (run == pick) pending &= ~jbit(pick);
    else if (e) {                           // location lookup failed: the jobs waiting for it fail too
      jobs[pick].err = 4;
      jobs[pick].failAt = millis();
      pending &= ~jbit(pick);
    }
    queueGate(millis());
    return;
  }

  if (syncTotal > 0) {                      // sync finished
    if (syncGotData) treats++;              // one treat per sync
    if (Serial) Serial.printf("online: sync done, heap free %lu KB (min %lu KB)\n",
                              (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)(ESP.getMinFreeHeap() / 1024));
    syncTotal = 0;
    syncGotData = false;
  }
  if (now - lastAuto >= 3000) {
    lastAuto = now;
    autoRefresh(now);
    if (pending) queueGate(now);
  }
}

void draw(U8G2 &d, uint32_t now) {
  drawnSince = true;
  if (!viewing) {
    drawList(d);
    return;
  }
  if (!ready(cur)) {
    char tag[8];
    tagFor(cur, now, tag, sizeof(tag));
    header(d, ITEM_NAMES[cur], tag);
    drawStatus(d, cur, now);
  } else {
    switch (cur) {
      case IT_CLOCK: drawClock(d, now); break;
      case IT_WEATHER: drawWeather(d, now); break;
      case IT_FORECAST: drawForecast(d, now); break;
      case IT_AQI: drawAqi(d, now); break;
      case IT_CURRENCY: drawCurrency(d, now); break;
      case IT_WORLD: drawWorld(d, now); break;
      case IT_FACT: drawFact(d, now); break;
      case IT_ADVICE: drawAdvice(d, now); break;
      case IT_MOON: drawMoon(d, now); break;
      case IT_NET: drawNet(d, now); break;
      case IT_ISS: drawIss(d, now); break;
      default: drawGithub(d, now); break;
    }
  }
  if (now < hintUntil) {
    d.setDrawColor(0);
    d.drawBox(0, 56, 128, 8);
    d.setDrawColor(1);
    d.setFont(u8g2_font_5x7_tf);
    d.drawStr(0, 63, "A refresh  B list  C next");
  }
}

uint8_t petHint() {
  uint8_t h = 0;
  if (fresh(J_WEATHER, 3UL * 3600000UL)) {
    if (wx.temp >= 35.0f) h |= HINT_HOT;
    if (wx.pop[0] >= 60 || rainCode(wx.code)) h |= HINT_RAIN;
    if (!wx.day) h |= HINT_NIGHT;
  }
  if (fresh(J_AQI, 3UL * 3600000UL) && aq.aqi >= 150) h |= HINT_SMOG;
  return h;
}

void drawPetBadge(U8G2 &d) {
  uint8_t h = petHint();
  int slot = 0;
  for (int k = 0; k < 4 && slot < 2; k++) {
    uint8_t m = (uint8_t)(1u << k);
    if (!(h & m)) continue;
    int cx = 121 - slot * 14;
    slot++;
    if (m == HINT_HOT) {                    // sweat drop
      d.drawTriangle(cx, 0, cx - 3, 5, cx + 3, 5);
      d.drawDisc(cx, 7, 3);
    } else if (m == HINT_RAIN) {            // umbrella
      d.drawCircle(cx, 5, 5, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
      d.drawVLine(cx, 5, 5);
      d.drawPixel(cx - 1, 10);
    } else if (m == HINT_SMOG) {            // cloud
      d.drawDisc(cx - 3, 5, 2);
      d.drawDisc(cx, 4, 3);
      d.drawDisc(cx + 3, 5, 2);
      d.drawBox(cx - 5, 5, 11, 3);
    } else {                                // moon
      d.drawDisc(cx, 5, 4);
      d.setDrawColor(0);
      d.drawDisc(cx + 2, 4, 3);
      d.setDrawColor(1);
    }
  }
}

int takeTreats() {
  int t = treats;
  treats = 0;
  return t;
}

}  // namespace online
