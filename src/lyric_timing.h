#pragma once
#include <stdint.h>

// Word timing inside one lyric line. LRC files normally only carry the start time of each line, so when a
// line has no real per-word timestamps (Enhanced LRC "<mm:ss.xx>word") the word starts are estimated.
namespace lyric_timing {

const int MAX_WORDS = 24;

// Estimates when each space-separated word starts, in ms from the start of the line. `text` is Latin-1, or UTF-8
// when it begins with the 0x1B marker used for Urdu / Arabic lines. `spanMs` = time until the next line.
// Returns the number of words written (at most maxWords).
//  - short line: words follow each other at a syllable-based pace, stretched by 40% of the spare time, and the LAST
//    word is held until the line ends (singers stretch the last note of a line);
//  - slow lines (the words nearly fill the line, ballads): all words are stretched to fill the line.
int estimate(const char *text, uint32_t spanMs, uint32_t *starts, int maxWords);

// Index of the word being sung `elapsedMs` after the line started (the last word whose start has passed), -1 if n == 0.
int focusIndex(const uint32_t *starts, int n, uint32_t elapsedMs);

// Syllable estimate of one Latin-1 word (min 1). Exposed for the host tests.
int syllables(const char *w, int len);

}  // namespace lyric_timing
