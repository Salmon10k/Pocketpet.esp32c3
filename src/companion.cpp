#include "companion.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

namespace companion {
namespace {
WebServer server(80);
Preferences prefs;
bool running = false;
bool loaded = false;
uint8_t page = 0;
String petName = "Pocket";
float hunger = 78.0f;
float happiness = 72.0f;
float energy = 82.0f;
float hygiene = 88.0f;
uint32_t xp = 0;
uint32_t coins = 0;
uint32_t petCount = 0;
uint32_t gamesPlayed = 0;
uint32_t ageMinutes = 0;
uint32_t lastTick = 0;
uint32_t lastMinute = 0;
uint32_t lastSave = 0;
uint32_t achievements = 0;

const char PAGE_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#10151b"><title>Pocketpet Companion</title>
<style>
:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#10151b;color:#edf2f7;font:16px system-ui,sans-serif}
main{max-width:620px;margin:auto;padding:22px}.hero{padding:22px;border:1px solid #303b48;border-radius:22px;background:linear-gradient(140deg,#202c38,#151b22)}
h1{margin:0 0 6px;font-size:26px}.muted{color:#aab7c4}.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:14px 0}
.stat,.panel{padding:14px;background:#1a222b;border:1px solid #303b48;border-radius:15px}.label{font-size:12px;color:#aab7c4;text-transform:uppercase;letter-spacing:.08em}
.value{font-size:23px;font-weight:750;margin:4px 0}.bar{height:7px;border-radius:8px;background:#35414d;overflow:hidden}.bar span{display:block;height:100%;background:#8bd5ca}
.actions{display:grid;grid-template-columns:1fr 1fr;gap:9px}.actions button{border:0;border-radius:12px;padding:14px;font-weight:700;background:#c3f0df;color:#12201c;font-size:15px}
.actions button:active{transform:scale(.98)}.foot{font-size:12px;color:#91a0ae;margin-top:16px}
</style></head><body><main>
<section class="hero"><div class="muted">YOUR TINY DIGITAL ROOMMATE</div><h1 id="name">Pocket</h1><div id="stage">Loading pet status…</div><p class="muted" id="level"></p></section>
<div class="grid" id="stats"></div><section class="panel"><div class="label">Care actions</div><p class="muted">Keep your companion thriving. No death, no guilt trips.</p>
<div class="actions"><button onclick="act('feed')">🍎 Feed</button><button onclick="act('play')">🎮 Play</button><button onclick="act('clean')">🫧 Clean</button><button onclick="act('sleep')">🌙 Sleep / rest</button></div></section>
<section class="panel" style="margin-top:12px"><div class="label">Achievements</div><div id="achievements" style="margin-top:8px">Loading…</div></section>
<div class="foot">Connected directly to your Pocketpet over its local Wi-Fi. No cloud, no account.</div></main>
<script>
const names={hunger:'Fullness',happiness:'Happiness',energy:'Energy',hygiene:'Cleanliness'};
async function refresh(){try{const d=await(await fetch('/api')).json();document.querySelector('#name').textContent=d.name;document.querySelector('#stage').textContent=d.stage+' · '+d.mood;document.querySelector('#level').textContent='Level '+d.level+' · '+d.xp+' XP · '+d.coins+' coins · '+d.age+' min old';
document.querySelector('#stats').innerHTML=Object.keys(names).map(k=>'<div class="stat"><div class="label">'+names[k]+'</div><div class="value">'+Math.round(d[k])+'%</div><div class="bar"><span style="width:'+d[k]+'%"></span></div></div>').join('');
document.querySelector('#achievements').textContent=d.achievements.join(' · ')||'Your first achievement is waiting.';
}catch(e){document.querySelector('#stage').textContent='Reconnecting…'}}
async function act(a){await fetch('/'+a,{method:'POST'});await refresh()}refresh();setInterval(refresh,5000);
</script></body></html>
)HTML";

uint8_t level() { return (uint8_t)min((uint32_t)99, 1 + xp / 100); }
const char *stage() {
  if (ageMinutes < 30) return "Baby";
  if (ageMinutes < 240) return "Youngling";
  if (ageMinutes < 1440) return "Teen";
  return level() >= 10 ? "Grown-up" : "Adult";
}
const char *mood() {
  if (hunger < 20 || hygiene < 20) return "Needs a little care";
  if (energy < 20) return "Sleepy";
  if (happiness > 75) return "Delighted";
  if (happiness < 30) return "A bit lonely";
  return "Chilling";
}
void award(uint8_t bit, const char *label) {
  if (!(achievements & (1UL << bit))) {
    achievements |= 1UL << bit;
    coins += 5;
    (void)label;
  }
}
void checkAchievements() {
  if (petCount >= 1) award(0, "First cuddle");
  if (petCount >= 10) award(1, "Regular cuddler");
  if (level() >= 5) award(2, "Growing up");
  if (gamesPlayed >= 1) award(3, "Player one");
  if (coins >= 100) award(4, "Pocket change");
  if (hunger >= 70 && happiness >= 70 && energy >= 70 && hygiene >= 70) award(5, "All loved up");
}
void save() {
  if (!loaded) return;
  prefs.putString("name", petName);
  prefs.putUChar("hunger", (uint8_t)hunger);
  prefs.putUChar("happy", (uint8_t)happiness);
  prefs.putUChar("energy", (uint8_t)energy);
  prefs.putUChar("hygiene", (uint8_t)hygiene);
  prefs.putUInt("xp", xp);
  prefs.putUInt("coins", coins);
  prefs.putUInt("pets", petCount);
  prefs.putUInt("games", gamesPlayed);
  prefs.putUInt("age", ageMinutes);
  prefs.putUInt("ach", achievements);
  lastSave = millis();
}
void feed() { hunger = min(100.0f, hunger + 24); happiness = min(100.0f, happiness + 3); xp += 5; coins += 1; checkAchievements(); }
void play() { happiness = min(100.0f, happiness + 15); energy = max(0.0f, energy - 8); hunger = max(0.0f, hunger - 5); xp += 10; coins += 3; checkAchievements(); }
void clean() { hygiene = min(100.0f, hygiene + 35); happiness = min(100.0f, happiness + 2); xp += 4; coins += 1; checkAchievements(); }
void rest() { energy = min(100.0f, energy + 25); happiness = min(100.0f, happiness + 2); xp += 3; checkAchievements(); }

String achievementsText() {
  String out = "[";
  const char *names[] = {"First cuddle","Regular cuddler","Growing up","Player one","Pocket change","All loved up"};
  bool first = true;
  for (uint8_t i=0;i<6;i++) if (achievements & (1UL<<i)) {
    if (!first) out += ",";
    out += "\""; out += names[i]; out += "\""; first = false;
  }
  out += "]";
  return out;
}
void api() {
  String json = "{\"name\":\""+petName+"\",";
  json += "\"hunger\":"+String(hunger,0)+",\"happiness\":"+String(happiness,0)+",";
  json += "\"energy\":"+String(energy,0)+",\"hygiene\":"+String(hygiene,0)+",";
  json += "\"xp\":"+String(xp)+",\"level\":"+String(level())+",\"coins\":"+String(coins)+",";
  json += "\"age\":"+String(ageMinutes)+",\"stage\":\""+String(stage())+"\",\"mood\":\""+String(mood())+"\",";
  json += "\"achievements\":"+achievementsText()+"}";
  server.send(200, "application/json", json);
}
void actionFeed() { feed(); server.send(200,"text/plain","fed"); }
void actionPlay() { play(); server.send(200,"text/plain","played"); }
void actionClean() { clean(); server.send(200,"text/plain","clean"); }
void actionSleep() { rest(); server.send(200,"text/plain","rested"); }
} // namespace

void init() {
  if (loaded) return;
  prefs.begin("petplus", false);
  petName = prefs.getString("name", "Pocket");
  hunger = prefs.getUChar("hunger", 78);
  happiness = prefs.getUChar("happy", 72);
  energy = prefs.getUChar("energy", 82);
  hygiene = prefs.getUChar("hygiene", 88);
  xp = prefs.getUInt("xp", 0);
  coins = prefs.getUInt("coins", 0);
  petCount = prefs.getUInt("pets", 0);
  gamesPlayed = prefs.getUInt("games", 0);
  ageMinutes = prefs.getUInt("age", 0);
  achievements = prefs.getUInt("ach", 0);
  loaded = true;
}
void begin() {
  if (running) return;
  init();
  page = 0;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Pocketpet-Companion", "petfriend");
  server.on("/", HTTP_GET, [](){ server.send_P(200, "text/html", PAGE_HTML); });
  server.on("/api", HTTP_GET, api);
  server.on("/feed", HTTP_POST, actionFeed);
  server.on("/play", HTTP_POST, actionPlay);
  server.on("/clean", HTTP_POST, actionClean);
  server.on("/sleep", HTTP_POST, actionSleep);
  server.onNotFound([](){ server.send(404,"text/plain","Pocketpet companion: open /"); });
  server.begin();
  running = true;
  lastTick = lastMinute = millis();
}
void end() {
  if (!running) return;
  save();
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  running = false;
}
bool active() { return running; }
void tick(uint32_t now) {
  if (!running) return;
  server.handleClient();
  if (now - lastTick >= 60000) {
    lastTick = now;
    hunger = max(0.0f, hunger - 1.0f);
    happiness = max(0.0f, happiness - 0.4f);
    hygiene = max(0.0f, hygiene - 0.5f);
    energy = max(0.0f, energy - 0.3f);
  }
  if (now - lastMinute >= 60000) { lastMinute = now; ageMinutes++; }
  checkAchievements();
  if (now - lastSave >= 30000) save();
}
void input(bool a, bool b, bool c) {
  if (a) page = (page + 2) % 3;
  if (c) page = (page + 1) % 3;
}
void draw(U8G2 &d, uint32_t now) {
  d.setFont(u8g2_font_7x13B_tf);
  if (page == 0) {
    d.drawStr(0,12,"COMPANION+");
    d.drawHLine(0,15,128);
    d.setFont(u8g2_font_6x10_tf);
    d.drawStr(0,29,petName.c_str());
    d.drawStr(0,42,stage());
    char line[32]; snprintf(line,sizeof(line),"Lv %u  XP %lu  $%lu",level(),(unsigned long)xp,(unsigned long)coins);
    d.drawStr(0,55,line);
    d.drawStr(0,64,"A/C pages  B exit");
  } else if (page == 1) {
    d.drawStr(0,12,"CARE STATUS"); d.drawHLine(0,15,128);
    d.setFont(u8g2_font_6x10_tf);
    char line[32];
    snprintf(line,sizeof(line),"Fullness  %d%%",(int)hunger); d.drawStr(0,28,line);
    snprintf(line,sizeof(line),"Happy     %d%%",(int)happiness); d.drawStr(0,39,line);
    snprintf(line,sizeof(line),"Energy    %d%%",(int)energy); d.drawStr(0,50,line);
    snprintf(line,sizeof(line),"Clean     %d%%",(int)hygiene); d.drawStr(0,61,line);
  } else {
    d.drawStr(0,12,"ACHIEVEMENTS"); d.drawHLine(0,15,128);
    d.setFont(u8g2_font_5x7_tf);
    const char *names[] = {"First cuddle","10 cuddles","Level five","First game","100 coins","All stats 70+"};
    for (uint8_t i=0;i<4;i++) {
      uint8_t idx = i + ((now / 5000) % 2) * 2;
      if (idx < 6) { d.drawStr(0,28+i*9,(achievements & (1UL<<idx)) ? "*" : "-"); d.drawStr(10,28+i*9,names[idx]); }
    }
    d.drawStr(0,63,"* unlocked");
  }
}
void pet() {
  init();
  petCount++; happiness = min(100.0f,happiness+4); xp += 2;
  if (petCount >= 10) coins += 1;
  checkAchievements();
}
void gamePlayed(uint32_t score) {
  init();
  gamesPlayed++; happiness = min(100.0f,happiness+min(12.0f,3.0f+score));
  xp += 8 + min((uint32_t)20, score); coins += 5 + min((uint32_t)10, score);
  checkAchievements();
}
} // namespace companion
