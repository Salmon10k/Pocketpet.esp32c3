#pragma once
// Spotify mode: now-playing, synced lyrics (LRCLIB) and playback buttons (needs Premium).
// Login is done once on a PC with tools/spotify_auth.py, which puts SPOTIFY_CLIENT_ID and
// SPOTIFY_REFRESH_TOKEN into src/secrets.h. All state lives in spotify.cpp.
#include <Arduino.h>
#include <U8g2lib.h>

namespace spotify {

void enter();                                      // entering Spotify mode: asks Online for the radio
void leave();                                      // leaving: radio released, state dropped
void tick(uint32_t now);                           // call every loop(); at most one request per call
void input(bool a, bool b, bool c, uint32_t now);  // A previous, B play/pause, C next
void draw(U8G2 &d, uint32_t now);
bool active();                                     // true while the mode is open (keeps the screen on)

// Read-only lyric data for independent visualizers. This does not call or alter draw().
struct LyricsView {
  bool haveTrack;
  bool lyricsReady;
  bool playing;
  uint32_t positionMs;
  uint32_t durationMs;
  uint32_t lineStartMs;
  uint32_t lineEndMs;
  char status[40];       // short text about what Spotify mode is doing ("connecting wifi", "finding lyrics", errors); empty = nothing to say
  bool statusBusy;       // true while it is still working (show animated dots)
  int focusWord;         // index of the word being sung in `line` (real Enhanced-LRC times, else estimated); -1 = none
  char line[128];       // Urdu / Arabic lines start with the byte 0x1B and hold raw UTF-8
  char nextLine[128];
};
void setLyricOffset(int ms);                       // Settings > Lyrics sync: + shows lyrics earlier, - later
bool getLyricsView(LyricsView &out, uint32_t now);

}  // namespace spotify
