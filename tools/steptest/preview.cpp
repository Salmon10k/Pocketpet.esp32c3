// PC preview of the step counter screens: the REAL steps_view.cpp + pet_face.cpp drawn into a U8g2 buffer (see build.sh).
#include <Arduino.h>
#include <U8g2lib.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../../src/steps.h"
#include "../../src/steps_view.h"
#include "../../src/pet_face.h"
#include "../../src/focus.h"
#include "../../src/focus_view.h"

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
static std::string OUTDIR = "/tmp/pocketpet-stepview";
static FILE *openOut(const char *n) { std::string p = OUTDIR + "/" + n; FILE *f = fopen(p.c_str(), "wb"); if (!f) { perror(p.c_str()); exit(1); } return f; }
static void dump(FILE *f) { fwrite(display.getBufferPtr(), 1, 1024, f); }

static void page(const steps::History &h, uint32_t goal, bool walking, int cad, bool clockOk, bool flash, uint32_t now, int streak = 0) {
  display.clearBuffer();
  display.setFont(u8g2_font_7x13B_tf);
  display.drawStr(0, 12, "STEPS");
  for (int i = 0; i < 3; ++i) { if (i == 1) display.drawDisc(108 + i * 6, 8, 2); else display.drawCircle(108 + i * 6, 8, 2); }
  display.drawHLine(0, 15, 128);
  stepsview::Info in; in.h = &h; in.goal = goal; in.walking = walking; in.cadence = cad; in.clockOk = clockOk; in.flash = flash; in.streak = streak;
  stepsview::draw(display, in, now);
}

// The pet as drawPet() draws it while strolling / celebrating (happy face, no blink).
static void pet(bool cheer, int cad, uint32_t now, uint32_t sinceStep) {
  using namespace petface;
  display.clearBuffer();
  int mdy = 0;
  if (!cheer) mdy = sinceStep < 70 ? -2 : (sinceStep < 150 ? -1 : 0);
  else mdy = -(int)(fabsf(sinf(now / 130.0f)) * 5);
  int cy = EYE_Y + mdy;
  static EyeAnim anim;
  Target t; t.style = cheer ? EY_ARC : EY_NORMAL; t.lower = cheer ? 0 : 0.22f;
  for (int i = 0; i < 20; ++i) anim.update(now - (20 - i) * 40, t, true, 0, 0, false);
  anim.blink = 1;
  anim.draw(display, 0, cy, now);
  drawMouth(display, cheer ? M_BIG : M_CAT, MOUTH_Y + mdy, now, 0);
  if (cheer) stepsview::decorCheer(display, 0, now);
  else stepsview::decorWalk(display, cy, EYE_R_X, cad, now);
}

int main(int argc, char **argv) {
  if (argc > 1) OUTDIR = argv[1];
  display.begin();
  steps::History h;
  uint32_t week[7] = {3200, 8123, 5400, 6120, 2300, 9900, 4100};
  for (int i = 0; i < 7; ++i) h.cnt[i] = week[i];
  FILE *f = openOut("page.bin");
  { steps::History z; page(z, 6000, false, 0, false, false, 1000); dump(f); }          // fresh, no clock
  page(h, 6000, false, 0, true, false, 1000); dump(f);                                 // mid-day
  page(h, 6000, true, 112, true, false, 1000); dump(f);                                // walking (text on)
  page(h, 6000, true, 112, true, false, 1100); dump(f);                                // walking, feet other way
  { steps::History g = h; g.cnt[0] = 6000; page(g, 6000, false, 0, true, true, 1000); dump(f); page(g, 6000, false, 0, true, false, 1000); dump(f); }   // goal hit, flash on/off
  page(h, 6000, false, 0, true, false, 1000, 5); dump(f);                              // with a 5-day streak
  { steps::History b = h; b.cnt[0] = 14250; b.cnt[3] = 18000; page(b, 10000, false, 0, true, false, 1000); dump(f); }  // 5-digit numbers
  fclose(f);
  f = openOut("badges.bin");
  {
    progress::Data p;
    for (int n : {0, 3, 9, 16}) {
      p.badges = n >= 16 ? 0xFFFFu : ((1u << n) - 1) | (n == 9 ? 0x8000u : 0);
      display.clearBuffer();
      display.setFont(u8g2_font_7x13B_tf); display.drawStr(0, 12, "BADGES"); display.drawHLine(0, 15, 128);
      stepsview::badges(display, p, 2200u * (n % 16)); dump(f);
    }
    display.clearBuffer();                                                            // toasts: sliding in, shown, sliding out
    for (uint32_t age : {60u, 125u, 1500u, 3300u}) { display.clearBuffer(); stepsview::toast(display, "NEW BADGE", "Dancer", age); dump(f); }
  }
  fclose(f);
  f = openOut("focus.bin");
  {
    using namespace focus;
    auto show = [&](Clock &c, uint32_t now, float lx = 0) {
      static petface::EyeAnim anim;
      petface::Target t = mood(c, now);
      for (int i = 0; i < 20; ++i) anim.update(now - (20 - i) * 40, t, true, lx, 0, false);
      anim.blink = 1; anim.wink = 1;
      display.clearBuffer(); draw(display, c, anim, now); dump(f);
    };
    Clock t; init(t, K_TIMER);
    show(t, 1000);                                           // setup 10:00
    toggle(t, 0); update(t, 150000); show(t, 150000);        // 2.5 min in
    update(t, 595000); show(t, 595000);                      // last seconds: wide eyes
    update(t, 600000); show(t, 700); show(t, 1300);          // alert, both flash phases
    Clock p; init(p, K_POMO);
    show(p, 1000);                                           // preset screen
    toggle(p, 0); update(p, 600000); show(p, 600000);        // focusing, serious
    update(p, 25 * 60000 + 1000); p.alert = false; update(p, 25 * 60000 + 61000); show(p, 25 * 60000 + 61000);   // on a break
    Clock w; init(w, K_WATCH);
    toggle(w, 0); update(w, 83400); lap(w); update(w, 95900); show(w, 95900);
  }
  fclose(f);
  f = openOut("pet.bin");
  for (int i = 0; i < 4; ++i) { pet(false, 110, 1000 + i * 70, i * 70); dump(f); }
  pet(false, 170, 3000, 200); dump(f);                                                 // jogging: sweat drop
  for (int i = 0; i < 4; ++i) { pet(true, 0, 400 + i * 450, 0); dump(f); }
  fclose(f);
  printf("wrote frames to %s\n", OUTDIR.c_str());
  return 0;
}
