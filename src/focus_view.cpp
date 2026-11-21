#include "focus_view.h"
#include "gfx.h"
#include <stdio.h>
#include <math.h>

namespace focus {

petface::Target mood(const Clock &c, uint32_t now) {
  using namespace petface;
  Target t;
  (void)now;
  if (c.alert) { t.style = EY_ARC; return t; }
  if (c.kind == K_POMO && c.onBreak) { t.lower = 0.25f; return t; }                  // relaxed
  if (c.running && c.kind != K_WATCH) {                                              // concentrating
    t.slant = 0.35f; t.lid = 0.1f; t.open = 0.92f;
    if (shownMs(c) < 10000) { t.slant = 0; t.lid = 0; t.open = 1.15f; }              // the last seconds: wide awake
  }
  return t;
}

void draw(U8G2 &d, const Clock &c, const petface::EyeAnim &eyes, uint32_t now) {
  char buf[24];
  d.setDrawColor(1);
  uint32_t ms = shownMs(c);
  if (c.kind != K_WATCH && ms % 1000) ms += 1000 - ms % 1000;                         // count down on whole seconds
  uint32_t secs = ms / 1000;
  bool flashOn = c.alert && ((now / 350) & 1);
  if (flashOn) { d.drawFrame(0, 0, 128, 64); d.drawFrame(1, 1, 126, 62); d.drawFrame(2, 2, 124, 60); }   // blinking border

  // Label row
  d.setFont(u8g2_font_5x7_tf);
  const char *label = c.kind == K_TIMER ? "TIMER" : (c.kind == K_WATCH ? "STOPWATCH" : (c.onBreak ? (c.round >= 4 ? "LONG BREAK" : "BREAK") : "FOCUS"));
  d.drawStr(2, 8, label);
  if (c.kind == K_POMO) {                                                              // 4 round dots
    for (int i = 0; i < 4; ++i) {
      int x = 60 + i * 8;
      if (i < c.round || (i == c.round && c.running && !c.onBreak && (now / 500) % 2)) d.drawDisc(x, 5, 2);
      else d.drawCircle(x, 5, 2);
    }
  }
  if (!c.running && !c.alert && !atStart(c)) d.drawStr(100, 8, "II");

  // Time
  if (secs >= 3600) { snprintf(buf, sizeof(buf), "%u:%02u", (unsigned)(secs / 3600), (unsigned)((secs / 60) % 60)); }
  else snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
  d.setFont(u8g2_font_logisoso24_tn);
  d.drawStr(2, 40, buf);
  if (c.kind == K_WATCH) {                                                             // tenths
    d.setFont(u8g2_font_5x7_tf);
    snprintf(buf, sizeof(buf), ".%u", (unsigned)((c.doneMs / 100) % 10));
    d.drawStr(70, 40, buf);
  }

  // The pet watches along
  float k = 0.5f;
  eyes.drawAt(d, 106, c.alert ? 22 - (int)(fabsf(sinf(now / 130.0f)) * 4) : 24, k, now);
  if (c.alert) {
    d.setFont(u8g2_font_6x10_tf);
    const char *msg = c.kind == K_TIMER ? "TIME!" : (c.onBreak ? "BREAK!" : "FOCUS!");
    d.drawStr(106 - d.getStrWidth(msg) / 2, 43, msg);
  }

  // Progress bar
  d.drawFrame(2, 46, 124, 6);
  if (c.kind == K_WATCH) {
    d.drawBox(3, 47, (int)((c.doneMs % 60000) * 122 / 60000), 4);                       // one sweep per minute
  } else {
    d.drawBox(3, 47, (int)(fraction(c) * 122), 4);
  }

  // Hint / lap
  d.setFont(u8g2_font_5x7_tf);
  const char *hint;
  if (c.alert) hint = "any button";
  else if (c.kind == K_WATCH) hint = "A reset  C lap  B go";
  else if (atStart(c)) hint = c.kind == K_TIMER ? "A -  C +   B start" : "A/C preset  B start";
  else if (c.kind == K_TIMER) hint = "A reset C +1m B pause";
  else hint = "A reset C skip B pause";
  if (c.kind == K_WATCH && c.lapMs) {
    uint32_t s = c.lapSplit / 1000;
    snprintf(buf, sizeof(buf), "lap %02u:%02u.%u  B go", (unsigned)(s / 60), (unsigned)(s % 60), (unsigned)((c.lapSplit / 100) % 10));
    hint = buf;
  }
  d.drawStr(2, 62, hint);
  if (c.kind == K_POMO && atStart(c)) {
    Clock tmp = c; tmp.round = 0; tmp.onBreak = false;
    snprintf(buf, sizeof(buf), "%d/%d", focusMin(c), breakMin(tmp));
    d.drawStr(128 - d.getStrWidth(buf) - 2, 8, buf);
  }
  d.setDrawColor(1);
}

}  // namespace focus
