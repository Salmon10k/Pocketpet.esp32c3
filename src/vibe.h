#pragma once
#include <stdint.h>

// "What is this music like?" for the pet. Pure C++ (no Arduino), so it is tested on the PC (tools/vibetest).
//
// Spotify no longer shares tempo / audio analysis with new apps, so the pet cannot know the real BPM. It knows:
//  - the artist's genres (Spotify's GET /artists/{id}), when Spotify still sends them,
//  - otherwise a guess from the title words, the lyric script (Urdu / Arabic ...) and how fast the lyrics are sung.
// The "beat" the pet dances to is a feel-tempo for the vibe (bpmFor), locked to the song position: steady, but not
// the song's true beat.
namespace vibe {

enum Vibe : uint8_t { V_NONE, V_CHILL, V_POP, V_HYPE, V_ROCK, V_EDM, V_DESI, V_DREAMY, V_COUNT };

const char *name(Vibe v);                                   // short label for the screen: "chill", "hype", ...

// genres: Spotify genre names joined with ", " (any case). Every genre votes once, the most votes wins, a tie goes to
// the first genre. V_NONE when nothing matches.
Vibe fromGenres(const char *genres);

// No genre data: title words ("lofi", "remix", ...), lyric script (desiScript), lyric speed in syllables per second x10
// (0 = unknown). V_NONE when there is nothing to go on.
Vibe guess(const char *title, const char *artist, bool desiScript, int pace10);

// Feel-tempo (beats per minute) the pet bobs to: a base per vibe, nudged by the lyric speed. 60..170.
int bpmFor(Vibe v, int pace10);

// 0..100: how lively the reactions are (lyric speed; 50 when unknown).
int energyFor(int pace10);

// What the pet needs to know about the music right now (filled by spotify::getMusicView).
struct Music {
  bool active;          // a track is playing right now
  Vibe vibe;
  bool guessed;         // the vibe is a guess (no genre data), the screen shows "?"
  uint8_t energy;       // 0..100
  uint16_t beatMs;      // period of one beat
  uint32_t positionMs;  // song position: the dance is locked to it
  bool singing;         // a lyric line is being sung right now
  int16_t word;         // index of the word being sung (the mouth flaps when it changes), -1 = none
  uint32_t trackSeq;    // changes with every new track (the pet reacts to the change)
};

}  // namespace vibe
