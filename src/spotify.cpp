#include "spotify.h"
#include "online.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef SPOTIFY_CLIENT_ID
#define SPOTIFY_CLIENT_ID ""
#endif
#ifndef SPOTIFY_REFRESH_TOKEN
#define SPOTIFY_REFRESH_TOKEN ""
#endif

namespace spotify {
namespace {

// ---------------------------------------------------------------- tuning
const uint32_t POLL_PLAYING_MS = 4000;     // how often to ask Spotify what is playing
const uint32_t POLL_PAUSED_MS  = 8000;
const uint32_t HTTP_TIMEOUT_MS = 8000;
const int      LYRIC_LEAD_MS   = 300;      // show a line slightly early (network + draw delay)
const int      MAX_LINES       = 160;
const int      POOL_BYTES      = 7000;
const int      MAX_BODY        = 24000;    // refuse lyric answers bigger than this
const uint32_t MIN_HEAP        = 55000;    // a TLS handshake needs roughly this much free heap

const bool configured = SPOTIFY_CLIENT_ID[0] && SPOTIFY_REFRESH_TOKEN[0];

// ---------------------------------------------------------------- pure helpers (host-tested)
// ---- helpers begin
// U8g2 *_tf fonts want one byte per character (Latin-1), not UTF-8. Convert in place.
// Curly quotes and dashes become ASCII, anything outside Latin-1 becomes one '?'.
void toLatin1(char *s) {
  char *w = s;
  bool lastQ = false;
  for (const unsigned char *r = (const unsigned char *)s; *r;) {
    uint32_t cp;
    if (*r < 0x80) { cp = *r++; }
    else if ((*r & 0xE0) == 0xC0 && (r[1] & 0xC0) == 0x80) { cp = ((r[0] & 0x1F) << 6) | (r[1] & 0x3F); r += 2; }
    else if ((*r & 0xF0) == 0xE0 && (r[1] & 0xC0) == 0x80 && (r[2] & 0xC0) == 0x80) {
      cp = ((r[0] & 0x0F) << 12) | ((r[1] & 0x3F) << 6) | (r[2] & 0x3F); r += 3;
    } else if ((*r & 0xF8) == 0xF0 && (r[1] & 0xC0) == 0x80 && (r[2] & 0xC0) == 0x80 && (r[3] & 0xC0) == 0x80) {
      cp = 0x10000; r += 4;
    } else { cp = '?'; r++; }
    if (cp == 0x2018 || cp == 0x2019) cp = '\'';
    else if (cp == 0x201C || cp == 0x201D) cp = '"';
    else if (cp == 0x2013 || cp == 0x2014) cp = '-';
    else if (cp == 0x2026) { *w++ = '.'; *w++ = '.'; cp = '.'; }
    else if (cp == 0x00A0) cp = ' ';
    if (cp > 0xFF) { if (lastQ) continue; cp = '?'; }
    lastQ = (cp == '?');
    *w++ = (char)cp;
  }
  *w = 0;
}

// Percent-encode UTF-8 for a query string. Returns false when out is too small.
bool urlEncode(const char *s, char *out, size_t cap) {
  static const char HEXD[] = "0123456789ABCDEF";
  size_t n = 0;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    bool safe = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                *p == '-' || *p == '_' || *p == '.' || *p == '~';
    if (n + (safe ? 1 : 3) + 1 > cap) { if (cap) out[0] = 0; return false; }
    if (safe) out[n++] = (char)*p;
    else { out[n++] = '%'; out[n++] = HEXD[*p >> 4]; out[n++] = HEXD[*p & 15]; }
  }
  out[n] = 0;
  return true;
}

// "Song - 2011 Remastered (feat. X) [Live]" -> "Song". Used for the second lyrics lookup.
void cleanTitle(const char *src, char *dst, size_t cap) {
  size_t n = 0;
  int depth = 0;
  for (const char *p = src; *p && n + 1 < cap; p++) {
    if (p[0] == ' ' && p[1] == '-' && p[2] == ' ') break;
    if (*p == '(' || *p == '[') { depth++; continue; }
    if ((*p == ')' || *p == ']') && depth > 0) { depth--; continue; }
    if (depth == 0) dst[n++] = *p;
  }
  while (n > 0 && dst[n - 1] == ' ') n--;
  dst[n] = 0;
  if (n == 0) { strncpy(dst, src, cap - 1); dst[cap - 1] = 0; }
}

struct Lyric { uint32_t ms; uint16_t off; };
Lyric lines[MAX_LINES];
int lineCount = 0;
char pool[POOL_BYTES];
int poolUsed = 0;

// Parse "[mm:ss.xx]text" lines (several leading timestamps allowed). Lines are sorted by time.
// Metadata tags like [ar:Artist] are skipped. Returns the number of lines kept.
int parseLrc(const char *lrc) {
  lineCount = 0;
  poolUsed = 0;
  const char *p = lrc;
  while (*p && lineCount < MAX_LINES) {
    const char *eol = p;
    while (*eol && *eol != '\n') eol++;
    uint32_t times[4];
    int nt = 0;
    const char *q = p;
    while (*q == '[' && nt < 4) {
      const char *t = q + 1;
      if (!(*t >= '0' && *t <= '9')) break;
      uint32_t mm = 0;
      while (*t >= '0' && *t <= '9') mm = mm * 10 + (uint32_t)(*t++ - '0');
      if (*t != ':') break;
      t++;
      if (!(*t >= '0' && *t <= '9')) break;
      uint32_t ss = 0;
      while (*t >= '0' && *t <= '9') ss = ss * 10 + (uint32_t)(*t++ - '0');
      uint32_t frac = 0;
      if (*t == '.' || *t == ':') {
        t++;
        int digits = 0;
        while (*t >= '0' && *t <= '9') {
          if (digits < 3) { frac = frac * 10 + (uint32_t)(*t - '0'); digits++; }
          t++;
        }
        while (digits < 3) { frac *= 10; digits++; }
      }
      if (*t != ']') break;
      times[nt++] = mm * 60000UL + ss * 1000UL + frac;
      q = t + 1;
    }
    if (nt > 0) {
      while (q < eol && *q == ' ') q++;
      const char *e = eol;
      while (e > q && (e[-1] == ' ' || e[-1] == '\r')) e--;
      int len = (int)(e - q);
      if (len > 90) len = 90;
      if (poolUsed + len + 1 > POOL_BYTES) break;
      char *dst = pool + poolUsed;
      memcpy(dst, q, (size_t)len);
      dst[len] = 0;
      toLatin1(dst);
      uint16_t off = (uint16_t)poolUsed;
      poolUsed += (int)strlen(dst) + 1;
      for (int i = 0; i < nt && lineCount < MAX_LINES; i++) lines[lineCount++] = {times[i], off};
    }
    p = *eol ? eol + 1 : eol;
  }
  for (int i = 1; i < lineCount; i++) {        // insertion sort (already sorted in nearly every file)
    Lyric k = lines[i];
    int j = i - 1;
    while (j >= 0 && lines[j].ms > k.ms) { lines[j + 1] = lines[j]; j--; }
    lines[j + 1] = k;
  }
  return lineCount;
}

// Word-wrap `s` into at most `maxLines` rows of `cols` characters. Returns rows written.
int wrapText(const char *s, int cols, int maxLines, char out[][24]) {
  int rows = 0;
  const char *p = s;
  while (*p == ' ') p++;
  while (*p && rows < maxLines) {
    int len = (int)strlen(p);
    int take = len <= cols ? len : cols;
    if (len > cols) {
      int sp = -1;
      for (int i = cols; i > 0; i--) if (p[i] == ' ') { sp = i; break; }
      if (sp > 0) take = sp;
    }
    if (rows == maxLines - 1 && len > take) {         // last row: cut with a dot
      take = cols;
      memcpy(out[rows], p, (size_t)(take - 1));
      out[rows][take - 1] = '~';
      out[rows][take] = 0;
      return rows + 1;
    }
    memcpy(out[rows], p, (size_t)take);
    out[rows][take] = 0;
    rows++;
    p += take;
    while (*p == ' ') p++;
  }
  return rows;
}
// ---- helpers end

// ---------------------------------------------------------------- state
enum Phase : uint8_t { P_WIFI, P_LOGIN, P_RUN, P_ERROR };
enum Lyr : uint8_t { L_NONE, L_WANT, L_HAVE, L_MISSING };
enum Cmd : uint8_t { C_NONE, C_PLAY, C_PAUSE, C_NEXT, C_PREV };

bool isOpen = false;
Phase phase = P_WIFI;
char errMsg[40] = "";
uint32_t drawnAt = 0;
bool drawnSince = true;
uint32_t gateUntil = 0;

WiFiClientSecure sec;
const char *secHost = "";

char token[512] = "";
uint32_t tokenUntil = 0;       // millis() when the access token stops being valid
bool haveToken = false;
char refreshTok[256] = "";
uint32_t blockedUntil = 0;     // rate limit / failure backoff for every request
uint8_t loginFails = 0;

bool playing = false, haveTrack = false, nothing = false;
char trackId[40] = "";
char title[96] = "", artist[64] = "", album[64] = "", artists[80] = "";   // raw UTF-8 for lookups
char dTitle[96] = "", dArtists[80] = "";                                  // Latin-1 for the screen
uint32_t durMs = 0, progMs = 0, progAt = 0;
uint32_t nextPoll = 0;
Lyr lyr = L_NONE;
uint8_t lyrTries = 0;
uint32_t lyrAt = 0;
int curLine = -1;
Cmd cmd = C_NONE;
uint32_t cmdAt = 0;
char note[32] = "";
uint32_t noteUntil = 0;

void setNote(const char *m, uint32_t now) {
  strncpy(note, m, sizeof(note) - 1);
  note[sizeof(note) - 1] = 0;
  noteUntil = now + 3000;
}

void setError(const char *m) {
  strncpy(errMsg, m, sizeof(errMsg) - 1);
  errMsg[sizeof(errMsg) - 1] = 0;
  phase = P_ERROR;
}

// ---------------------------------------------------------------- refresh token (SD-free: NVS)
// A refresh token that Spotify rotates is kept in NVS. If you log in again on the PC (a new token in
// secrets.h), the stored one is ignored: "base" remembers the tail of the secrets token it came from.
void loadRefreshToken() {
  strncpy(refreshTok, SPOTIFY_REFRESH_TOKEN, sizeof(refreshTok) - 1);
  refreshTok[sizeof(refreshTok) - 1] = 0;
  Preferences p;
  if (!p.begin("spotify", true)) return;
  String base = p.getString("base", "");
  String rt = p.getString("rt", "");
  p.end();
  size_t L = strlen(SPOTIFY_REFRESH_TOKEN);
  const char *tail = SPOTIFY_REFRESH_TOKEN + (L > 12 ? L - 12 : 0);
  if (rt.length() > 20 && rt.length() < sizeof(refreshTok) && base == tail) {
    strncpy(refreshTok, rt.c_str(), sizeof(refreshTok) - 1);
    refreshTok[sizeof(refreshTok) - 1] = 0;
  }
}

void saveRefreshToken(const char *rt) {
  Preferences p;
  if (!p.begin("spotify", false)) return;
  size_t L = strlen(SPOTIFY_REFRESH_TOKEN);
  p.putString("base", SPOTIFY_REFRESH_TOKEN + (L > 12 ? L - 12 : 0));
  p.putString("rt", rt);
  p.end();
}

// ---------------------------------------------------------------- HTTP
struct Reply { int code; String body; };

// One TLS client is reused. A request to another host closes it first. Anything but 200 closes the
// connection too, so a half-read 204 can never poison the next request.
Reply request(const char *host, const char *method, const char *path, const char *bearer,
              const char *form, bool wantBody) {
  Reply r{-1, String()};
  if (ESP.getFreeHeap() < MIN_HEAP) { r.code = -2; return r; }
  if (strcmp(secHost, host) != 0) { sec.stop(); secHost = host; }
  sec.setInsecure();
  char url[360];
  snprintf(url, sizeof(url), "https://%s%s", host, path);
  HTTPClient http;
  http.setReuse(true);
  if (!http.begin(sec, url)) return r;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setUserAgent("Pocketpet/1.0 (esp32-c3 keychain)");
  if (bearer) {
    char h[560];
    snprintf(h, sizeof(h), "Bearer %s", bearer);
    http.addHeader("Authorization", h);
  }
  if (form) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    r.code = http.sendRequest(method, String(form));
  } else if (strcmp(method, "GET") == 0) {
    r.code = http.GET();
  } else {
    http.addHeader("Content-Length", "0");     // Spotify answers 411 to a PUT/POST without it
    r.code = http.sendRequest(method, String(""));
  }
  if (r.code == 200 && wantBody) {
    int sz = http.getSize();
    if (sz > MAX_BODY) r.code = -3;
    else r.body = http.getString();
  }
  http.end();
  if (r.code != 200) sec.stop();
  return r;
}

// ---------------------------------------------------------------- Spotify calls
bool login(uint32_t now) {
  char form[480];
  snprintf(form, sizeof(form), "grant_type=refresh_token&refresh_token=%s&client_id=%s", refreshTok, SPOTIFY_CLIENT_ID);
  Reply r = request("accounts.spotify.com", "POST", "/api/token", nullptr, form, true);
  if (r.code == 400 || r.code == 401) {            // refresh token revoked or expired
    setError("Login expired. Run tools/spotify_auth.py");
    return false;
  }
  if (r.code != 200) {
    if (r.code == -2) setNote("low memory", now);
    blockedUntil = now + (r.code == 429 ? 15000 : 4000);
    return false;
  }
  JsonDocument doc, f;
  f["access_token"] = true; f["expires_in"] = true; f["refresh_token"] = true;
  if (deserializeJson(doc, r.body.c_str(), DeserializationOption::Filter(f))) { blockedUntil = now + 4000; return false; }
  const char *t = doc["access_token"] | "";
  if (!t[0] || strlen(t) >= sizeof(token)) { blockedUntil = now + 4000; return false; }
  strcpy(token, t);
  uint32_t life = doc["expires_in"] | 3600;
  tokenUntil = now + (life > 120 ? life - 90 : life / 2) * 1000UL;
  haveToken = true;
  const char *rt = doc["refresh_token"] | "";
  if (rt[0] && strcmp(rt, refreshTok) != 0 && strlen(rt) < sizeof(refreshTok)) {
    strcpy(refreshTok, rt);
    saveRefreshToken(rt);
  }
  return true;
}

bool tokenOk(uint32_t now) { return haveToken && (int32_t)(tokenUntil - now) > 0; }

void applyTrack(const char *id, const char *t, const char *al, const char *ar, const char *all, uint32_t dur) {
  strncpy(trackId, id, sizeof(trackId) - 1);
  trackId[sizeof(trackId) - 1] = 0;
  strncpy(title, t, sizeof(title) - 1);   title[sizeof(title) - 1] = 0;
  strncpy(album, al, sizeof(album) - 1);  album[sizeof(album) - 1] = 0;
  strncpy(artist, ar, sizeof(artist) - 1); artist[sizeof(artist) - 1] = 0;
  strncpy(artists, all, sizeof(artists) - 1); artists[sizeof(artists) - 1] = 0;
  strcpy(dTitle, title);   toLatin1(dTitle);
  strcpy(dArtists, artists); toLatin1(dArtists);
  durMs = dur;
  lineCount = 0;
  curLine = -1;
  lyr = L_WANT;
  lyrTries = 0;
  lyrAt = 0;
}

void poll(uint32_t now) {
  Reply r = request("api.spotify.com", "GET", "/v1/me/player/currently-playing", token, nullptr, true);
  uint32_t t1 = millis();
  if (r.code == 401) { haveToken = false; return; }                       // token expired early: log in again
  if (r.code == 429) { blockedUntil = t1 + 15000; nextPoll = t1 + 15000; return; }
  if (r.code == 204) {                                                    // nothing playing anywhere
    nothing = true; haveTrack = false; playing = false;
    trackId[0] = 0; lineCount = 0; lyr = L_NONE;
    nextPoll = t1 + POLL_PAUSED_MS;
    return;
  }
  if (r.code != 200) {
    nextPoll = t1 + 5000;
    if (r.code == -2) setNote("low memory", t1);
    else if (r.code <= 0 || r.code >= 500) setNote("spotify unreachable", t1);
    return;
  }
  JsonDocument doc, f;
  f["is_playing"] = true;
  f["progress_ms"] = true;
  f["currently_playing_type"] = true;
  JsonObject fi = f["item"].to<JsonObject>();
  fi["id"] = true; fi["name"] = true; fi["duration_ms"] = true;
  fi["artists"][0]["name"] = true;
  fi["album"]["name"] = true;
  fi["show"]["name"] = true;
  if (deserializeJson(doc, r.body.c_str(), DeserializationOption::Filter(f))) { nextPoll = t1 + 4000; return; }
  nothing = false;
  playing = doc["is_playing"] | false;
  progMs = doc["progress_ms"] | 0;
  progAt = t1;
  JsonObjectConst it = doc["item"];
  if (it.isNull()) { haveTrack = false; nothing = true; nextPoll = t1 + POLL_PAUSED_MS; return; }
  const char *id = it["id"] | "";
  const char *name = it["name"] | "";
  const char *al = it["album"]["name"] | (it["show"]["name"] | "");
  const char *ar = it["artists"][0]["name"] | (it["show"]["name"] | "");
  char all[80] = "";
  size_t n = 0;
  for (JsonObjectConst a : it["artists"].as<JsonArrayConst>()) {
    const char *nm = a["name"] | "";
    size_t l = strlen(nm);
    if (n + l + 3 >= sizeof(all)) break;
    if (n) { all[n++] = ','; all[n++] = ' '; }
    memcpy(all + n, nm, l);
    n += l;
    all[n] = 0;
  }
  if (!all[0]) strncpy(all, ar, sizeof(all) - 1);
  uint32_t dur = it["duration_ms"] | 0;
  bool podcast = strcmp(doc["currently_playing_type"] | "track", "track") != 0;
  haveTrack = true;
  if (strcmp(id, trackId) != 0 || (!id[0] && strcmp(name, title) != 0)) {
    applyTrack(id, name, al, ar, all, dur);
    if (podcast) lyr = L_MISSING;
  }
  nextPoll = t1 + (playing ? POLL_PLAYING_MS : POLL_PAUSED_MS);
}

void sendCommand(uint32_t now) {
  const char *method = "PUT", *path = "/v1/me/player/play";
  if (cmd == C_PAUSE) path = "/v1/me/player/pause";
  else if (cmd == C_NEXT) { method = "POST"; path = "/v1/me/player/next"; }
  else if (cmd == C_PREV) { method = "POST"; path = "/v1/me/player/previous"; }
  Cmd was = cmd;
  cmd = C_NONE;
  Reply r = request("api.spotify.com", method, path, token, nullptr, false);
  uint32_t t1 = millis();
  if (r.code == 401) { haveToken = false; cmd = was; cmdAt = t1; return; }   // log in again, then resend
  if (r.code == 204 || r.code == 200 || r.code == 202) {
    nextPoll = t1 + (was == C_NEXT || was == C_PREV ? 900 : 600);         // Spotify needs a moment before it reports the change
    return;
  }
  if (r.code == 403) setNote("needs Premium", t1);
  else if (r.code == 404) setNote("open Spotify on a device", t1);
  else if (r.code == 429) { blockedUntil = t1 + 8000; setNote("slow down", t1); }
  else setNote("command failed", t1);
  if (was == C_PLAY) playing = false;                                      // undo the optimistic flip
  else if (was == C_PAUSE) playing = true;
  nextPoll = t1 + 300;
}

// ---------------------------------------------------------------- lyrics (LRCLIB)
bool lyricsRequest(const char *titleForLookup, bool withAlbum, int &code) {
  char t[200], a[140], al[140], path[560];
  if (!urlEncode(titleForLookup, t, sizeof(t)) || !urlEncode(artist, a, sizeof(a)) || !urlEncode(album, al, sizeof(al))) { code = -4; return false; }
  int dur = (int)((durMs + 500) / 1000);
  if (withAlbum) snprintf(path, sizeof(path), "/api/get?artist_name=%s&track_name=%s&album_name=%s&duration=%d", a, t, al, dur);
  else snprintf(path, sizeof(path), "/api/get?artist_name=%s&track_name=%s&duration=%d", a, t, dur);
  Reply r = request("lrclib.net", "GET", path, nullptr, nullptr, true);
  sec.stop();                                       // free the TLS buffers before parsing a big body
  secHost = "";
  code = r.code;
  if (r.code != 200) return false;
  JsonDocument doc, f;
  f["syncedLyrics"] = true;
  if (deserializeJson(doc, r.body.c_str(), DeserializationOption::Filter(f))) { code = -5; return false; }
  r.body = String();                                // drop the raw body before the parsed copy is used
  const char *lrc = doc["syncedLyrics"] | "";
  if (!lrc[0]) { code = 404; return false; }
  return parseLrc(lrc) >= 2;
}

void fetchLyrics(uint32_t now) {
  int code = 0;
  bool ok = lyricsRequest(title, true, code);
  if (!ok && code == 404) {
    char clean[96];
    cleanTitle(title, clean, sizeof(clean));
    ok = lyricsRequest(clean, false, code);
  }
  curLine = -1;
  if (ok) { lyr = L_HAVE; return; }
  lineCount = 0;
  if (code == 404) { lyr = L_MISSING; return; }                 // no synced lyrics for this song
  if (++lyrTries >= 3) { lyr = L_MISSING; return; }              // network trouble: three goes, then give up
  lyrAt = now + 5000;
}

// ---------------------------------------------------------------- time helpers
uint32_t position(uint32_t now) {
  uint32_t p = progMs;
  if (playing && haveTrack) p += now - progAt;
  if (durMs && p > durMs) p = durMs;
  return p;
}

void fmtTime(uint32_t ms, char *out) {
  uint32_t s = ms / 1000;
  snprintf(out, 8, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

}  // namespace

// ================================================================ public API
bool active() { return isOpen; }

void enter() {
  isOpen = true;
  phase = P_WIFI;
  errMsg[0] = 0;
  haveToken = false;
  haveTrack = false;
  nothing = false;
  playing = false;
  trackId[0] = 0;
  lineCount = 0;
  lyr = L_NONE;
  cmd = C_NONE;
  note[0] = 0;
  blockedUntil = 0;
  nextPoll = 0;
  loginFails = 0;
  secHost = "";
  if (!configured) { setError("Add Spotify keys to secrets.h"); return; }
  if (!online::wifiConfigured()) { setError("No Wi-Fi in secrets.h"); return; }
  loadRefreshToken();
  online::wifiAcquire(millis());
}

void leave() {
  isOpen = false;
  sec.stop();
  secHost = "";
  token[0] = 0;
  haveToken = false;
  lineCount = 0;
  online::wifiRelease();
}

void tick(uint32_t now) {
  if (!isOpen || phase == P_ERROR) return;
  if (!online::wifiUp()) { phase = P_WIFI; return; }
  if (phase == P_WIFI) { phase = P_LOGIN; blockedUntil = now + 700; }   // let the link settle first
  if (!drawnSince && (int32_t)(now - gateUntil) < 0) return;           // let one frame reach the screen before a request blocks
  if ((int32_t)(now - blockedUntil) < 0) return;

  if (!tokenOk(now)) {
    if (phase == P_RUN) phase = P_LOGIN;                                // token aged out: quiet re-login
    if (login(now)) { phase = P_RUN; loginFails = 0; nextPoll = now; }
    else if (phase != P_ERROR && ++loginFails >= 6) setError("Cannot reach Spotify");
    drawnSince = false; gateUntil = now + 250;
    return;
  }
  phase = P_RUN;

  if (cmd != C_NONE && (int32_t)(now - cmdAt) >= 0) {
    sendCommand(now);
    drawnSince = false; gateUntil = millis() + 250;
    return;
  }
  if (lyr == L_WANT && (int32_t)(now - lyrAt) >= 0 && haveTrack && durMs) {
    fetchLyrics(now);
    drawnSince = false; gateUntil = millis() + 250;
    return;
  }
  if ((int32_t)(now - nextPoll) >= 0) {
    poll(now);
    drawnSince = false; gateUntil = millis() + 250;
  }
}

void input(bool a, bool b, bool c, uint32_t now) {
  if (!isOpen) return;
  if (phase == P_ERROR) {                       // B retries when the problem was only Wi-Fi / network
    if (b && configured && online::wifiConfigured() && strstr(errMsg, "Cannot")) {
      errMsg[0] = 0; phase = P_WIFI; loginFails = 0; online::wifiAcquire(now);
    }
    return;
  }
  if (phase != P_RUN) { if (b) online::wifiAcquire(now); return; }
  if (nothing && !haveTrack) {                  // nothing playing: B asks Spotify to resume on the last device
    if (b) { cmd = C_PLAY; cmdAt = now; setNote("resuming...", now); }
    return;
  }
  if (b) {
    cmd = playing ? C_PAUSE : C_PLAY;
    cmdAt = now;
    progMs = position(now); progAt = now;       // freeze/unfreeze the clock right away
    playing = !playing;
  } else if (c) {
    cmd = C_NEXT; cmdAt = now; setNote("next >>", now);
  } else if (a) {
    cmd = C_PREV; cmdAt = now; setNote("<< previous", now);
  }
}

// ---------------------------------------------------------------- drawing
namespace {

void centered(U8G2 &d, int y, const char *s) {
  int w = d.getStrWidth(s);
  d.drawStr(w >= 128 ? 0 : (128 - w) / 2, y, s);
}

void drawStatus(U8G2 &d, const char *l1, const char *l2, const char *l3) {
  d.setFont(u8g2_font_7x13B_tf);
  centered(d, 14, "SPOTIFY");
  d.drawHLine(0, 17, 128);
  d.setFont(u8g2_font_6x10_tf);
  if (l1) centered(d, 33, l1);
  if (l2) centered(d, 45, l2);
  d.setFont(u8g2_font_5x7_tf);
  if (l3) centered(d, 62, l3);
}

void drawMarquee(U8G2 &d, const char *s, int baseline, uint32_t now) {
  int w = d.getStrWidth(s);
  if (w <= 128) { d.drawStr(0, baseline, s); return; }
  int span = w + 24;
  int off = (int)((now / 45) % (uint32_t)span);
  d.drawStr(-off, baseline, s);
  d.drawStr(span - off, baseline, s);
}

void trimTo(U8G2 &d, const char *s, char *out, size_t cap, int maxW) {   // cut with "~" so it fits maxW pixels
  strncpy(out, s, cap - 1);
  out[cap - 1] = 0;
  size_t n = strlen(out);
  if (d.getStrWidth(out) <= maxW) return;
  while (n > 1 && d.getStrWidth(out) + d.getStrWidth("~") > maxW) out[--n] = 0;
  if (n < cap - 1) { out[n] = '~'; out[n + 1] = 0; }
}

}  // namespace

void draw(U8G2 &d, uint32_t now) {
  drawnSince = true;
  d.setFontPosBaseline();
  if (phase == P_ERROR) {
    char row[3][24];
    int n = wrapText(errMsg, 21, 3, row);
    d.setFont(u8g2_font_7x13B_tf);
    centered(d, 14, "SPOTIFY");
    d.drawHLine(0, 17, 128);
    d.setFont(u8g2_font_6x10_tf);
    for (int i = 0; i < n; i++) centered(d, 32 + i * 11, row[i]);
    if (strstr(errMsg, "Cannot")) { d.setFont(u8g2_font_5x7_tf); centered(d, 62, "B retry  Touch=Home"); }
    else { d.setFont(u8g2_font_5x7_tf); centered(d, 62, "Touch = Home"); }
    return;
  }
  if (phase == P_WIFI) { drawStatus(d, online::wifiStatusText(), "...", "B retry  Touch=Home"); return; }
  if (phase == P_LOGIN) { drawStatus(d, "logging in", "to Spotify...", "Touch = Home"); return; }
  if (!haveTrack) {
    drawStatus(d, nothing ? "Nothing playing" : "Asking Spotify...", nothing ? "B = resume" : "", "open Spotify on a device");
    if (note[0] && (int32_t)(noteUntil - now) > 0) { d.setFont(u8g2_font_5x7_tf); d.setDrawColor(0); d.drawBox(0, 54, 128, 10); d.setDrawColor(1); centered(d, 62, note); }
    return;
  }

  // header: title - artists, scrolling when it is too long
  char head[180];
  snprintf(head, sizeof(head), "%s - %s", dTitle, dArtists);
  d.setFont(u8g2_font_5x7_tf);
  drawMarquee(d, head, 7, now);

  uint32_t pos = position(now);
  if (lyr == L_HAVE && lineCount > 0) {
    int target = -1;                                  // last line whose time has come
    int p = (int)pos + LYRIC_LEAD_MS;
    int i = curLine >= 0 && curLine < lineCount && (int)lines[curLine].ms <= p ? curLine : 0;
    for (; i < lineCount && (int)lines[i].ms <= p; i++) target = i;
    curLine = target;
    char buf[40];
    d.setFont(u8g2_font_5x7_tf);
    if (target > 0 && pool[lines[target - 1].off]) { trimTo(d, pool + lines[target - 1].off, buf, sizeof(buf), 128); centered(d, 18, buf); }
    d.setFont(u8g2_font_6x10_tf);
    if (target < 0) {
      centered(d, 36, "~ ~ ~");
    } else if (!pool[lines[target].off]) {
      centered(d, 36, "~ ~ ~");                       // instrumental gap
    } else {
      char row[2][24];
      int n = wrapText(pool + lines[target].off, 21, 2, row);
      int y0 = n == 1 ? 36 : 30;
      for (int k = 0; k < n; k++) centered(d, y0 + k * 11, row[k]);
    }
    d.setFont(u8g2_font_5x7_tf);
    if (target + 1 < lineCount && pool[lines[target + 1].off]) { trimTo(d, pool + lines[target + 1].off, buf, sizeof(buf), 128); centered(d, 52, buf); }
  } else {
    char row[2][24];
    d.setFont(u8g2_font_6x10_tf);
    int n = wrapText(dTitle, 21, 2, row);
    for (int k = 0; k < n; k++) centered(d, 24 + k * 11, row[k]);
    d.setFont(u8g2_font_5x7_tf);
    char ar[40];
    trimTo(d, dArtists, ar, sizeof(ar), 128);
    centered(d, 24 + n * 11 + 2, ar);
    centered(d, 52, lyr == L_WANT ? "finding lyrics..." : (lyr == L_MISSING ? "no synced lyrics" : ""));
  }

  // progress bar and the bottom row
  if (durMs) d.drawBox(0, 55, (int)((uint64_t)pos * 128 / durMs), 2);
  d.drawPixel(0, 56); d.drawPixel(127, 56);
  d.setFont(u8g2_font_5x7_tf);
  char t1[8], t2[8];
  fmtTime(pos, t1);
  fmtTime(durMs, t2);
  d.drawStr(0, 63, t1);
  d.drawStr(128 - d.getStrWidth(t2), 63, t2);
  if (note[0] && (int32_t)(noteUntil - now) > 0) centered(d, 63, note);
  else centered(d, 63, playing ? "A<<  B pause  C>>" : "A<<  B play   C>>");
}

}  // namespace spotify
