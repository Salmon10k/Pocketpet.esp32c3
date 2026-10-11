#include "lyrics_visual.h"
#include "online.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <math.h>
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

namespace lyrics_visual {
namespace {
const uint32_t POLL_MS=2500, HTTP_TIMEOUT_MS=8000, MIN_HEAP=50000;
const int MAX_LINES=160, POOL_BYTES=7000, MAX_BODY=24000;

struct Lyric { uint32_t ms; uint16_t off; };
Lyric lines[MAX_LINES];
char pool[POOL_BYTES];
int lineCount=0, poolUsed=0;

SemaphoreHandle_t mutex=nullptr;
TaskHandle_t task=nullptr;
volatile bool busy=false;
uint8_t work=0;
uint32_t epoch=0, workEpoch=0;
bool open=false, releaseRadio=false;

char token[512]="", refreshTok[256]="";
uint32_t tokenUntil=0, nextPoll=0;
bool haveToken=false, playing=false, haveTrack=false;
char trackId[40]="", title[96]="", artist[64]="", album[64]="";
uint32_t durMs=0, progMs=0, progAt=0;
bool lyricsWanted=false, lyricsHave=false;
int curLine=-1;
uint32_t lineShownAt=0;

void lock(){if(mutex)xSemaphoreTakeRecursive(mutex,portMAX_DELAY);}
void unlock(){if(mutex)xSemaphoreGiveRecursive(mutex);}
struct G{G(){lock();}~G(){unlock();}};
bool live(uint32_t e){return open&&e==epoch;}

void latin1(char*s){
  char*w=s;
  for(const unsigned char*r=(const unsigned char*)s;*r;){
    uint32_t c;
    if(*r<0x80)c=*r++;
    else if((*r&0xE0)==0xC0&&(r[1]&0xC0)==0x80){c=((r[0]&31)<<6)|(r[1]&63);r+=2;}
    else if((*r&0xF0)==0xE0&&(r[1]&0xC0)==0x80&&(r[2]&0xC0)==0x80){c=((r[0]&15)<<12)|((r[1]&63)<<6)|(r[2]&63);r+=3;}
    else{c='?';r++;}
    if(c==0x2018||c==0x2019)c='\\'';
    else if(c==0x201C||c==0x201D)c='"';
    else if(c==0x2013||c==0x2014)c='-';
    else if(c==0x2026){*w++='.';*w++='.';c='.';}
    else if(c==0xA0)c=' ';
    if(c>255)c='?';
    *w++=(char)c;
  }
  *w=0;
}
bool encode(const char*s,char*out,size_t cap){
  static const char h[]="0123456789ABCDEF";size_t n=0;
  for(const unsigned char*p=(const unsigned char*)s;*p;p++){
    bool safe=(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_'||*p=='.'||*p=='~';
    if(n+(safe?1:3)+1>cap){out[0]=0;return false;}
    if(safe)out[n++]=(char)*p;else{out[n++]='%';out[n++]=h[*p>>4];out[n++]=h[*p&15];}
  }out[n]=0;return true;
}
void cleanTitle(const char*s,char*d,size_t cap){
  size_t n=0;int depth=0;
  for(const char*p=s;*p&&n+1<cap;p++){
    if(p[0]==' '&&p[1]=='-'&&p[2]==' ')break;
    if(*p=='('||*p=='['){depth++;continue;}
    if((*p==')'||*p==']')&&depth){depth--;continue;}
    if(!depth)d[n++]=*p;
  }
  while(n&&d[n-1]==' ')n--;d[n]=0;if(!n){strncpy(d,s,cap-1);d[cap-1]=0;}
}
int parseLrc(const char*lrc){
  lineCount=poolUsed=0;const char*p=lrc;
  while(*p&&lineCount<MAX_LINES){
    const char*e=p;while(*e&&*e!='\\n')e++;
    uint32_t ts[4];int nt=0;const char*q=p;
    while(*q=='['&&nt<4){
      const char*t=q+1;if(*t<'0'||*t>'9')break;
      uint32_t mm=0,ss=0,fr=0;
      while(*t>='0'&&*t<='9')mm=mm*10+(*t++-'0');if(*t++!=':')break;
      while(*t>='0'&&*t<='9')ss=ss*10+(*t++-'0');
      if(*t=='.'||*t==':'){t++;int digs=0;while(*t>='0'&&*t<='9'){if(digs<3)fr=fr*10+(*t-'0');digs++;t++;}while(digs++<3)fr*=10;}
      if(*t!=']')break;ts[nt++]=mm*60000UL+ss*1000UL+fr;q=t+1;
    }
    if(nt){
      while(q<e&&*q==' ')q++;const char*z=e;while(z>q&&(z[-1]==' '||z[-1]=='\\r'))z--;
      int len=(int)(z-q);if(len>90)len=90;if(poolUsed+len+1>POOL_BYTES)break;
      char*dst=pool+poolUsed;memcpy(dst,q,len);dst[len]=0;latin1(dst);
      uint16_t off=(uint16_t)poolUsed;poolUsed+=strlen(dst)+1;
      for(int i=0;i<nt&&lineCount<MAX_LINES;i++)lines[lineCount++]={ts[i],off};
    }
    p=*e?e+1:e;
  }
  for(int i=1;i<lineCount;i++){Lyric k=lines[i];int j=i-1;while(j>=0&&lines[j].ms>k.ms){lines[j+1]=lines[j];j--;}lines[j+1]=k;}
  return lineCount;
}

struct Reply{int code;String body;};
WiFiClientSecure sec;
Reply req(const char*host,const char*method,const char*path,const char*bearer,const char*form,bool body,uint32_t e){
  Reply r{-1,String()};{G g;if(!live(e))return r;}
  if(ESP.getFreeHeap()<MIN_HEAP){r.code=-2;return r;}
  char url[620];snprintf(url,sizeof(url),"https://%s%s",host,path);HTTPClient http;sec.setInsecure();
  if(!http.begin(sec,url))return r;http.setConnectTimeout(HTTP_TIMEOUT_MS);http.setTimeout(HTTP_TIMEOUT_MS);
  http.setUserAgent("Pocketpet-LyricsVisual/1.0");
  if(bearer){char h[560];snprintf(h,sizeof(h),"Bearer %s",bearer);http.addHeader("Authorization",h);}
  if(form){http.addHeader("Content-Type","application/x-www-form-urlencoded");r.code=http.sendRequest(method,String(form));}
  else r.code=!strcmp(method,"GET")?http.GET():http.sendRequest(method,String(""));
  if(r.code==200&&body){int sz=http.getSize();if(sz>MAX_BODY)r.code=-3;else r.body=http.getString();}
  http.end();if(r.code!=200)sec.stop();return r;
}
void loadRefresh(){
  strncpy(refreshTok,SPOTIFY_REFRESH_TOKEN,sizeof(refreshTok)-1);refreshTok[sizeof(refreshTok)-1]=0;
  Preferences p;if(!p.begin("spotify",true))return;String base=p.getString("base",""),rt=p.getString("rt","");p.end();
  size_t n=strlen(SPOTIFY_REFRESH_TOKEN);const char*tail=SPOTIFY_REFRESH_TOKEN+(n>12?n-12:0);
  if(rt.length()>20&&rt.length()<sizeof(refreshTok)&&base==tail){strncpy(refreshTok,rt.c_str(),sizeof(refreshTok)-1);refreshTok[sizeof(refreshTok)-1]=0;}
}
void saveRefresh(const char*rt){
  Preferences p;if(!p.begin("spotify",false))return;size_t n=strlen(SPOTIFY_REFRESH_TOKEN);
  p.putString("base",SPOTIFY_REFRESH_TOKEN+(n>12?n-12:0));p.putString("rt",rt);p.end();
}
bool login(uint32_t now,uint32_t e){
  char form[520];{G g;if(!live(e))return false;snprintf(form,sizeof(form),"grant_type=refresh_token&refresh_token=%s&client_id=%s",refreshTok,SPOTIFY_CLIENT_ID);}
  Reply r=req("accounts.spotify.com","POST","/api/token",nullptr,form,true,e);if(r.code!=200)return false;
  JsonDocument doc,f;f["access_token"]=true;f["expires_in"]=true;f["refresh_token"]=true;
  if(deserializeJson(doc,r.body.c_str(),DeserializationOption::Filter(f)))return false;
  const char*t=doc["access_token"]|"",*rt=doc["refresh_token"]|"";
  if(!t[0]||strlen(t)>=sizeof(token))return false;uint32_t life=doc["expires_in"]|3600;
  G g;if(!live(e))return false;strcpy(token,t);tokenUntil=now+(life>120?life-90:life/2)*1000UL;haveToken=true;
  if(rt[0]&&strcmp(rt,refreshTok)&&strlen(rt)<sizeof(refreshTok)){strcpy(refreshTok,rt);saveRefresh(rt);}return true;
}
void setTrack(const char*id,const char*t,const char*ar,const char*al,uint32_t dur,uint32_t now,uint32_t e){
  G g;if(!live(e))return;bool changed=strcmp(id,trackId);
  strncpy(trackId,id,sizeof(trackId)-1);trackId[sizeof(trackId)-1]=0;strncpy(title,t,sizeof(title)-1);title[sizeof(title)-1]=0;
  strncpy(artist,ar,sizeof(artist)-1);artist[sizeof(artist)-1]=0;strncpy(album,al,sizeof(album)-1);album[sizeof(album)-1]=0;
  durMs=dur;progAt=now;if(changed){lineCount=poolUsed=0;lyricsHave=false;lyricsWanted=true;curLine=-1;}
}
void poll(uint32_t now,uint32_t e){
  char access[sizeof(token)];{G g;if(!live(e))return;strncpy(access,token,sizeof(access)-1);access[sizeof(access)-1]=0;}
  Reply r=req("api.spotify.com","GET","/v1/me/player/currently-playing",access,nullptr,true,e);G g;if(!live(e))return;
  if(r.code==401){haveToken=false;nextPoll=now+1000;return;}if(r.code!=200){haveTrack=false;playing=false;nextPoll=now+POLL_MS;return;}
  JsonDocument doc,f;f["is_playing"]=true;f["progress_ms"]=true;f["item"]=true;
  if(deserializeJson(doc,r.body.c_str(),DeserializationOption::Filter(f))){nextPoll=now+POLL_MS;return;}
  JsonObjectConst it=doc["item"];if(it.isNull()){haveTrack=false;playing=false;nextPoll=now+POLL_MS;return;}
  const char*id=it["id"]|"",*nm=it["name"]|"",*ar=it["artists"][0]["name"]|"",*al=it["album"]["name"]|"";
  uint32_t dur=it["duration_ms"]|0;playing=doc["is_playing"]|false;progMs=doc["progress_ms"]|0;progAt=now;haveTrack=true;
  if(strcmp(id,trackId))setTrack(id,nm,ar,al,dur,now,e);nextPoll=now+POLL_MS;
}
bool lyricsReq(const char*t,const char*ar,const char*al,uint32_t dur,bool albumArg,int&e,uint32_t epochNow){
  char et[200],ea[140],el[140],path[620];if(!encode(t,et,sizeof(et))||!encode(ar,ea,sizeof(ea))||!encode(al,el,sizeof(el))){e=-4;return false;}
  int secDur=(int)((dur+500)/1000);
  if(albumArg)snprintf(path,sizeof(path),"/api/get?artist_name=%s&track_name=%s&album_name=%s&duration=%d",ea,et,el,secDur);
  else snprintf(path,sizeof(path),"/api/get?artist_name=%s&track_name=%s&duration=%d",ea,et,secDur);
  Reply r=req("lrclib.net","GET",path,nullptr,nullptr,true,epochNow);e=r.code;if(r.code!=200)return false;
  JsonDocument doc,f;f["syncedLyrics"]=true;if(deserializeJson(doc,r.body.c_str(),DeserializationOption::Filter(f))){e=-5;return false;}
  const char*l=doc["syncedLyrics"]|"";if(!l[0]){e=404;return false;}return parseLrc(l)>0;
}
void fetchLyrics(uint32_t e){
  char t[96],ar[64],al[64];uint32_t dur;{G g;if(!live(e)||!lyricsWanted)return;strncpy(t,title,95);t[95]=0;strncpy(ar,artist,63);ar[63]=0;strncpy(al,album,63);al[63]=0;dur=durMs;}
  int code=0;bool ok=lyricsReq(t,ar,al,dur,true,code,e);
  if(!ok){char clean[96];cleanTitle(t,clean,sizeof(clean));if(strcmp(clean,t))ok=lyricsReq(clean,ar,al,dur,false,code,e);}
  G g;if(!live(e))return;lyricsWanted=false;lyricsHave=ok&&lineCount>0;curLine=-1;
}
void netTask(void*){
  for(;;){
    uint8_t w=0;uint32_t e=0;{G g;w=work;if(w){work=0;busy=true;e=workEpoch;}}
    if(!w){vTaskDelay(pdMS_TO_TICKS(25));continue;}
    uint32_t now=millis();if(w==1)login(now,e);else if(w==2)poll(now,e);else fetchLyrics(e);
    {G g;busy=false;}
  }
}
void ensureTask(){if(task)return;if(!mutex)mutex=xSemaphoreCreateRecursiveMutex();if(mutex)xTaskCreate(netTask,"lyricsVisNet",16*1024,nullptr,1,&task);}

int lineAt(uint32_t p){int n=-1;for(int i=0;i<lineCount;i++){if(lines[i].ms<=p)n=i;else break;}return n;}
int words(const char*s){int n=0;bool in=false;for(;*s;s++){if(*s!=' '&&!in){n++;in=true;}else if(*s==' ')in=false;}return n;}
bool wordAt(const char*s,int want,char*out,size_t cap){
  int n=0;while(*s){while(*s==' ')s++;if(!*s)break;const char*e=s;while(*e&&*e!=' ')e++;
    if(n++==want){size_t z=e-s;if(z>=cap)z=cap-1;memcpy(out,s,z);out[z]=0;return true;}s=e;}return false;
}

void art(U8G2&d,int style,uint32_t now,const char*word){
  int cx=64,cy=32;float a=now/240.0f;int w=d.getStrWidth(word);
  int fh=(strlen(word)<=9)?21:(strlen(word)<=13?15:12),bw=min(112,w+14),bh=fh+7,bx=(128-bw)/2,by=cy-bh/2;
  int wob=(int)lroundf(sinf(a)*3);
  if(style==0){
    d.drawLine(bx-10,cy,bx+3,by-7+wob);d.drawLine(bx+3,by-7+wob,bx+bw+7,cy);
    d.drawLine(bx+bw+7,cy,bx+bw-3,by+bh+7);d.drawLine(bx+bw-3,by+bh+7,bx-10,cy);
    for(int i=0;i<7;i++)d.drawLine(36+i*9,60-(i%3)*2,42+i*9,60-(i%3)*2);
  }else if(style==1){
    d.drawCircle(cx,cy,bw/2+8);d.drawCircle(cx,cy,bw/2+13);
    d.drawLine(6,cy,25,cy);d.drawLine(103,cy,122,cy);
    d.drawLine(16,18,30,by);d.drawLine(112,18,98,by);
  }else if(style==2){
    for(int i=0;i<5;i++){int y=by+bh+5+i*4;d.drawLine(8+i*7,y,120-i*7,y);}
    d.drawLine(cx,by+bh,cx-38,63);d.drawLine(cx,by+bh,cx+38,63);
    d.drawLine(bx,by,bx-20,55);d.drawLine(bx+bw,by,bx+bw+20,55);
  }else{
    d.drawLine(bx-8,by-7,bx+bw+8,by+bh+7);d.drawLine(bx+bw+8,by-7,bx-8,by+bh+7);
    d.drawLine(bx-13,cy,bx-3,by-7);d.drawLine(bx+bw+13,cy,bx+bw+3,by+bh+7);
    d.drawCircle(cx,cy,bw/2+11);
  }
  d.drawRFrame(bx,by,bw,bh,3);d.setFont(strlen(word)<=9?u8g2_font_10x20_tf:(strlen(word)<=13?u8g2_font_7x13B_tf:u8g2_font_6x10_tf));
  int tw=d.getStrWidth(word);d.drawStr(cx-tw/2,by+bh-(strlen(word)<=9?3:2),word);
  d.drawPixel(10,10+(now/90)%6);d.drawPixel(117,52-(now/110)%6);
}
}

void enter(){
  ensureTask();loadRefresh();{G g;open=true;releaseRadio=false;++epoch;workEpoch=epoch;haveToken=false;haveTrack=false;playing=false;trackId[0]=0;
    lineCount=poolUsed=0;lyricsHave=false;lyricsWanted=false;curLine=-1;nextPoll=millis();work=0;}
  online::wifiAcquire(millis());
}
void leave(){G g;open=false;++epoch;work=0;releaseRadio=true;}
bool active(){G g;return open;}
void input(bool a,bool b,bool c,uint32_t now){(void)a;(void)c;(void)now;if(b)leave();}
void tick(uint32_t now){
  bool rel=false;{G g;
    if(!open)rel=releaseRadio&&!busy;
    if(open&&online::wifiUp()&&!busy&&!work){
      if(!haveToken||(int32_t)(tokenUntil-now)<=0){work=1;workEpoch=epoch;}
      else if(lyricsWanted){work=3;workEpoch=epoch;}
      else if((int32_t)(now-nextPoll)>=0){work=2;workEpoch=epoch;}
    }
  }
  if(rel){online::wifiRelease();G g;releaseRadio=false;}
}
void draw(U8G2&d,uint32_t now){
  G g;if(!open)return;
  if(!online::wifiUp()){d.setFont(u8g2_font_7x13B_tf);d.drawStr(16,29,"LYRICS VISUAL");d.setFont(u8g2_font_6x10_tf);d.drawStr(29,44,"Connecting...");return;}
  if(!haveTrack){d.setFont(u8g2_font_7x13B_tf);d.drawStr(20,29,"PLAY A SONG");d.setFont(u8g2_font_5x7_tf);d.drawStr(43,43,"Spotify");art(d,(now/700)%4,now,"");return;}
  uint32_t p=progMs;if(playing&&now>=progAt)p+=now-progAt;if(durMs&&p>durMs)p=durMs;
  int li=lineAt(p);if(li<0||!lyricsHave){art(d,(now/700)%4,now,"");return;}
  const char*line=pool+lines[li].off;uint32_t a=lines[li].ms,b=(li+1<lineCount)?lines[li+1].ms:durMs;if(b<=a+120)b=a+600;
  uint32_t local=p>a?p-a:0,span=b-a;int wc=words(line),wi=wc?(int)((uint64_t)local*wc/span):0;if(wi>=wc)wi=wc-1;
  char word[32]="";if(!wordAt(line,wi,word,sizeof(word)))strcpy(word,"...");
  if(li!=curLine){curLine=li;lineShownAt=now;}int style=(li+wi)&3;art(d,style,now-lineShownAt,word);
}
}
