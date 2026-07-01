// =============================================================================
// wifi_ota.cpp — WiFi Firmware Downloader for Purplx
// =============================================================================
// Flow:
//   start() -> [connected] -> LIST
//           -> [saved creds] -> connect -> LIST
//           -> [no creds / fail] -> scan nearby APs -> select -> PASS -> connect
//
// Download: HTTP GET streaming -> esp_ota_write() -> esp_restart()
// Rollback: ota_0 left PENDING_VERIFY -> physical RESET -> Purplx.
// =============================================================================

#include "wifi_ota.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_ota_ops.h>

namespace WiFiOTA {

// Firmware catalog
// Ghost ESP releases .zip files (not standalone .bin) — cannot OTA flash directly.
// Evil M5 has no public standalone Cardputer bin.
// Nemo M5Cardputer.bin uses SCLK=36 (std Cardputer); ADV uses SCLK=40 — crashes on boot.
struct FwEntry { const char* icon; const char* name; const char* desc; const char* url; };
static const FwEntry CATALOG[] = {
    // Bruce moved repos: pr3y/Bruce -> BruceDevices/firmware, static filename
    { "[B]", "Bruce",          "Multi-tool hacking fw",
      "https://github.com/BruceDevices/firmware/releases/latest/download/Bruce-m5stack-cardputer.bin" },
    // Marauder ADV: built specifically for M5Cardputer ADV hardware
    { "[MA]","Marauder ADV",   "Marauder for Cardputer ADV",
      "https://github.com/justcallmekoko/ESP32Marauder/releases/download/v1.12.3/esp32_marauder_v1_12_3_20260622_m5cardputer_adv.bin" },
    // Marauder standard Cardputer (same version, different HW target)
    { "[M]", "Marauder",       "Marauder for std Cardputer",
      "https://github.com/justcallmekoko/ESP32Marauder/releases/download/v1.12.3/esp32_marauder_v1_12_3_20260622_m5cardputer.bin" },
    { "[+]", "Custom URL",     "Type URL manually", nullptr },
};
static constexpr int CATALOG_LEN = (int)(sizeof(CATALOG) / sizeof(CATALOG[0]));

// Screen states
enum class Screen : uint8_t {
    WIFI_SCAN, PASS_INPUT, SSID_INPUT, CONNECTING,
    LIST, URL_INPUT, CONFIRM, PROGRESS, ERROR_VIEW
};
static Screen _screen   = Screen::WIFI_SCAN;
static bool   _connFail = false;

// State
static int  _fwCursor  = 0;
static int  _scanCount = 0;
static int  _scanCursor= 0;
static int  _scanScroll= 0;
static constexpr int SCAN_VISIBLE = 7;
static char _wifiSsid[64]  = {};
static char _wifiPass[64]  = {};
static char _inputBuf[160] = {};
static int  _inputLen  = 0;
static bool _inputMask = false;
static char _errMsg[64]    = {};
static char _dlUrl[160]    = {};

// NVS
static bool _loadCreds() {
    Preferences p; p.begin("ota_wifi", true);
    String s = p.getString("ssid", ""); String w = p.getString("pass", ""); p.end();
    if (s.isEmpty()) return false;
    strncpy(_wifiSsid, s.c_str(), sizeof(_wifiSsid)-1);
    strncpy(_wifiPass, w.c_str(), sizeof(_wifiPass)-1);
    return true;
}
static void _saveCreds() {
    Preferences p; p.begin("ota_wifi", false);
    p.putString("ssid", _wifiSsid); p.putString("pass", _wifiPass); p.end();
}

// Input helpers
static void _inputBegin(bool mask) { memset(_inputBuf,0,sizeof(_inputBuf)); _inputLen=0; _inputMask=mask; }
static void _inputKey(char c) {
    if ((c==8||c==127)&&_inputLen>0) { _inputBuf[--_inputLen]='\0'; }
    else if (c>=32&&c<=126&&_inputLen<(int)sizeof(_inputBuf)-1) { _inputBuf[_inputLen++]=c; _inputBuf[_inputLen]='\0'; }
}

// Draw helpers
static void _chrome(const char* title, bool warn=false) {
    const ColorTheme* t=g_theme; const int W=320,H=240;
    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0,0,W,24,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,23,W,t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(warn?t->alert:t->primary,t->title_bg);
    g_lcd_secondary.setCursor(8,8); g_lcd_secondary.print(title);
    g_lcd_secondary.fillRect(0,0,2,2,t->primary); g_lcd_secondary.fillRect(W-2,0,2,2,t->primary);
    g_lcd_secondary.fillRect(0,H-16,W,16,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,H-16,W,t->border);
}
static void _hud(const char* t1,const char* t2=nullptr,const char* t3=nullptr,bool alert=false) {
    const ColorTheme* t=g_theme;
    g_lcd_primary.fillScreen(t->hud_bg);
    g_lcd_primary.fillRect(0,0,240,4,alert?t->alert:t->primary);
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setTextColor(alert?t->alert:t->primary,t->hud_bg);
    g_lcd_primary.setCursor(4,10); g_lcd_primary.print(t1);
    if(t2){g_lcd_primary.setTextColor(t->text_dim,t->hud_bg);g_lcd_primary.setCursor(4,28);g_lcd_primary.print(t2);}
    if(t3){g_lcd_primary.setTextColor(t->secondary,t->hud_bg);g_lcd_primary.setCursor(4,46);g_lcd_primary.print(t3);}
}

static int _rssiStr(int rssi) {
    if(rssi>-55)return 4; if(rssi>-65)return 3; if(rssi>-75)return 2; if(rssi>-85)return 1; return 0;
}
static void _sigStr(int bars, char* out) {
    const char* s[]={"    ","   |","  ||"," |||","||||"}; strcpy(out,s[bars]);
}

// Scan list screen
static void _drawScanList() {
    const ColorTheme* t=g_theme; const int W=320,H=240;
    if(xSemaphoreTake(g_display_mutex,pdMS_TO_TICKS(50))!=pdTRUE)return;
    _chrome("[ WIFI OTA \xe2\x80\x94 SELECT NETWORK ]");
    int total=_scanCount+1;
    char cb[20]; snprintf(cb,sizeof(cb),"%d found",_scanCount);
    g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->secondary,t->title_bg);
    int cw=g_lcd_secondary.textWidth(cb); g_lcd_secondary.setCursor(W-cw-8,8); g_lcd_secondary.print(cb);
    for(int i=0;i<SCAN_VISIBLE;i++){
        int idx=_scanScroll+i; if(idx>=total)break;
        bool sel=(idx==_scanCursor); int y=26+i*22;
        uint16_t rowBg=sel?t->highlight_bg:(i%2==0?t->row_a:t->row_b);
        uint16_t rowFg=sel?t->highlight_text:t->text;
        g_lcd_secondary.fillRect(0,y,W,21,rowBg);
        if(sel)g_lcd_secondary.fillRect(0,y,3,21,t->primary);
        g_lcd_secondary.setTextSize(1);
        if(idx==_scanCount){
            g_lcd_secondary.setTextColor(t->text_dim,rowBg);
            g_lcd_secondary.setCursor(8,y+7); g_lcd_secondary.print("[  ] Enter SSID manually");
        } else {
            int bars=_rssiStr(WiFi.RSSI(idx)); char sig[6]; _sigStr(bars,sig);
            bool open=(WiFi.encryptionType(idx)==WIFI_AUTH_OPEN);
            uint16_t sc=(bars>=3)?t->success:(bars>=2)?t->secondary:t->alert;
            g_lcd_secondary.setTextColor(sc,rowBg);
            g_lcd_secondary.setCursor(6,y+7); g_lcd_secondary.print(sig);
            g_lcd_secondary.setTextColor(open?t->text_dim:t->alert,rowBg);
            g_lcd_secondary.setCursor(38,y+7); g_lcd_secondary.print(open?"   ":"[*]");
            char sb[31]; strncpy(sb,WiFi.SSID(idx).c_str(),30); sb[30]='\0';
            g_lcd_secondary.setTextColor(rowFg,rowBg);
            g_lcd_secondary.setCursor(60,y+7); g_lcd_secondary.print(sb);
        }
    }
    if(total>SCAN_VISIBLE){
        int gx=W-4; float top=(float)_scanScroll/total,bot=(float)(_scanScroll+SCAN_VISIBLE)/total;
        int bt=26+(int)(top*(SCAN_VISIBLE*22)),bh=max(3,(int)((bot-top)*(SCAN_VISIBLE*22)));
        g_lcd_secondary.drawFastVLine(gx,26,SCAN_VISIBLE*22,t->text_dim);
        g_lcd_secondary.fillRect(gx-1,bt,3,bh,t->primary);
    }
    g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->text_dim,t->title_bg);
    g_lcd_secondary.setCursor(6,H-11);
    g_lcd_secondary.print("[W/S] nav  [ENTER] select  [R] rescan  [ESC] back");
    if(_scanCursor<_scanCount){ char hl[24]; strncpy(hl,WiFi.SSID(_scanCursor).c_str(),23); hl[23]='\0'; _hud("WIFI OTA",hl,"ENTER to connect"); }
    else { _hud("WIFI OTA","Manual SSID entry"); }
    xSemaphoreGive(g_display_mutex);
}

static void _doScan() {
    _screen=Screen::WIFI_SCAN;
    if(xSemaphoreTake(g_display_mutex,pdMS_TO_TICKS(100))==pdTRUE){
        const int W=320,H=240; const ColorTheme* t=g_theme;
        _chrome("[ WIFI OTA \xe2\x80\x94 SCANNING ]");
        g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->text_dim,t->bg);
        g_lcd_secondary.setCursor(16,80); g_lcd_secondary.print("Scanning for nearby networks...");
        g_lcd_secondary.setTextColor(t->primary,t->bg);
        g_lcd_secondary.setCursor(16,100); g_lcd_secondary.print("Please wait (2-5 seconds)");
        g_lcd_secondary.setTextColor(t->text_dim,t->title_bg);
        g_lcd_secondary.setCursor(6,H-11); g_lcd_secondary.print("Scanning...");
        _hud("WIFI OTA","Scanning..."); xSemaphoreGive(g_display_mutex);
    }
    WiFi.mode(WIFI_STA); WiFi.scanDelete();
    int n=WiFi.scanNetworks();
    _scanCount=(n>0)?n:0; _scanCursor=0; _scanScroll=0;
    _drawScanList();
}

static void _drawCredInput(const char* prompt) {
    const ColorTheme* t=g_theme; const int W=320,H=240;
    if(xSemaphoreTake(g_display_mutex,pdMS_TO_TICKS(50))!=pdTRUE)return;
    _chrome("[ WIFI OTA \xe2\x80\x94 CREDENTIALS ]");
    g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->text,t->bg);
    g_lcd_secondary.setCursor(14,36); g_lcd_secondary.print(prompt);
    const int bx=14,by=52,bw=W-28,bh=20;
    g_lcd_secondary.drawRect(bx,by,bw,bh,t->border);
    g_lcd_secondary.fillRect(bx+1,by+1,bw-2,bh-2,t->row_a);
    char disp[51]={};
    if(_inputMask){int n=min(_inputLen,50);for(int i=0;i<n;i++)disp[i]='*';}
    else{int s=(_inputLen>50)?_inputLen-50:0;strncpy(disp,_inputBuf+s,50);}
    g_lcd_secondary.setTextColor(t->text,t->row_a);
    g_lcd_secondary.setCursor(bx+4,by+7); g_lcd_secondary.print(disp);
    int cx=bx+4+(int)g_lcd_secondary.textWidth(disp);
    g_lcd_secondary.fillRect(cx,by+4,1,bh-8,t->primary);
    if(_wifiSsid[0]){
        g_lcd_secondary.setTextColor(t->text_dim,t->bg);
        g_lcd_secondary.setCursor(14,84); g_lcd_secondary.print("Network: ");
        g_lcd_secondary.setTextColor(t->secondary,t->bg); g_lcd_secondary.print(_wifiSsid);
    }
    g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->text_dim,t->title_bg);
    g_lcd_secondary.setCursor(6,H-11);
    g_lcd_secondary.print("[ENTER] confirm  [BKSP] delete  [ESC] back");
    _hud("WIFI OTA",prompt,_wifiSsid[0]?_wifiSsid:nullptr);
    xSemaphoreGive(g_display_mutex);
}

static void _drawConnecting(int tick=0) {
    const ColorTheme* t=g_theme; const int W=320,H=240;
    if(xSemaphoreTake(g_display_mutex,pdMS_TO_TICKS(40))!=pdTRUE)return;
    _chrome("[ WIFI OTA \xe2\x80\x94 CONNECTING ]");
    g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->text_dim,t->bg);
    g_lcd_secondary.setCursor(14,52); g_lcd_secondary.print("Connecting to:");
    g_lcd_secondary.setTextSize(2); g_lcd_secondary.setTextColor(t->primary,t->bg);
    int tw=g_lcd_secondary.textWidth(_wifiSsid);
    if(tw>W-16){g_lcd_secondary.setTextSize(1);tw=g_lcd_secondary.textWidth(_wifiSsid);}
    g_lcd_secondary.setCursor((W-tw)/2,70); g_lcd_secondary.print(_wifiSsid);
    g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->text_dim,t->bg);
    char dots[8]={};int n=(tick%5)+1;for(int i=0;i<n;i++)dots[i]='.';
    g_lcd_secondary.setCursor(14,110); g_lcd_secondary.print("Please wait"); g_lcd_secondary.print(dots);
    g_lcd_secondary.setTextColor(t->text_dim,t->title_bg);
    g_lcd_secondary.setCursor(6,H-11); g_lcd_secondary.print("[ESC] cancel");
    _hud("WIFI OTA","Connecting...",_wifiSsid);
    xSemaphoreGive(g_display_mutex);
}

static void _drawList() {
    const ColorTheme* t=g_theme; const int W=320,H=240;
    if(xSemaphoreTake(g_display_mutex,pdMS_TO_TICKS(50))!=pdTRUE)return;
    _chrome("[ WIFI OTA FLASH ]");
    IPAddress ip=WiFi.localIP(); char ipbuf[20];
    snprintf(ipbuf,sizeof(ipbuf),"%d.%d.%d.%d",ip[0],ip[1],ip[2],ip[3]);
    g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->success,t->title_bg);
    int iw=g_lcd_secondary.textWidth(ipbuf); g_lcd_secondary.setCursor(W-iw-8,8); g_lcd_secondary.print(ipbuf);
    for(int i=0;i<CATALOG_LEN;i++){
        bool sel=(i==_fwCursor); int y=28+i*22;
        uint16_t rowBg=sel?t->highlight_bg:(i%2==0?t->row_a:t->row_b);
        uint16_t rowFg=sel?t->highlight_text:t->text;
        g_lcd_secondary.fillRect(0,y,W,21,rowBg);
        if(sel)g_lcd_secondary.fillRect(0,y,3,21,t->primary);
        g_lcd_secondary.setTextColor(t->secondary,rowBg); g_lcd_secondary.setTextSize(1);
        g_lcd_secondary.setCursor(6,y+7); g_lcd_secondary.print(CATALOG[i].icon);
        g_lcd_secondary.setTextColor(rowFg,rowBg);
        g_lcd_secondary.setCursor(30,y+7); g_lcd_secondary.print(CATALOG[i].name);
        g_lcd_secondary.setTextColor(t->text_dim,rowBg);
        g_lcd_secondary.setCursor(120,y+7); g_lcd_secondary.print(CATALOG[i].desc);
    }
    g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->text_dim,t->title_bg);
    g_lcd_secondary.setCursor(6,H-11);
    g_lcd_secondary.print("[W/S] select  [ENTER] flash  [R] reconnect  [ESC] back");
    _hud("WIFI OTA",CATALOG[_fwCursor].name,"ENTER to flash");
    xSemaphoreGive(g_display_mutex);
}

static void _drawURLInput() {
    const ColorTheme* t=g_theme; const int W=320,H=240;
    if(xSemaphoreTake(g_display_mutex,pdMS_TO_TICKS(50))!=pdTRUE)return;
    _chrome("[ WIFI OTA \xe2\x80\x94 CUSTOM URL ]");
    g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->text,t->bg);
    g_lcd_secondary.setCursor(14,36); g_lcd_secondary.print("Enter direct .bin URL:");
    const int bx=14,by=52,bw=W-28,bh=20;
    g_lcd_secondary.drawRect(bx,by,bw,bh,t->border);
    g_lcd_secondary.fillRect(bx+1,by+1,bw-2,bh-2,t->row_a);
    char disp[51]={};int s=(_inputLen>50)?_inputLen-50:0;strncpy(disp,_inputBuf+s,50);
    g_lcd_secondary.setTextColor(t->text,t->row_a);
    g_lcd_secondary.setCursor(bx+4,by+7); g_lcd_secondary.print(disp);
    int cx=bx+4+(int)g_lcd_secondary.textWidth(disp);
    g_lcd_secondary.fillRect(cx,by+4,1,bh-8,t->primary);
    g_lcd_secondary.setTextColor(t->text_dim,t->bg);
    g_lcd_secondary.setCursor(14,84); g_lcd_secondary.print("Start with https:// or http://");
    g_lcd_secondary.setCursor(14,100); g_lcd_secondary.print("App .bin only — not a full flash image");
    g_lcd_secondary.setTextColor(t->text_dim,t->title_bg);
    g_lcd_secondary.setCursor(6,H-11);
    g_lcd_secondary.print("[ENTER] confirm  [BKSP] delete  [ESC] back");
    _hud("WIFI OTA","Enter .bin URL");
    xSemaphoreGive(g_display_mutex);
}

static void _drawConfirm() {
    const ColorTheme* t=g_theme; const int W=320,H=240;
    if(xSemaphoreTake(g_display_mutex,pdMS_TO_TICKS(50))!=pdTRUE)return;
    _chrome("[ CONFIRM DOWNLOAD ]",true);
    const char* label; char urlL[48];
    if(_fwCursor<CATALOG_LEN&&CATALOG[_fwCursor].url!=nullptr){label=CATALOG[_fwCursor].name;}
    else{strncpy(urlL,_dlUrl,47);urlL[47]='\0';label=urlL;}
    g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->text_dim,t->bg);
    g_lcd_secondary.setCursor(16,34); g_lcd_secondary.print("Download and flash:");
    g_lcd_secondary.setTextSize(2); g_lcd_secondary.setTextColor(t->primary,t->bg);
    int tw=g_lcd_secondary.textWidth(label);
    if(tw>W-16){g_lcd_secondary.setTextSize(1);tw=g_lcd_secondary.textWidth(label);}
    g_lcd_secondary.setCursor((W-tw)/2,52); g_lcd_secondary.print(label);
    g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->text_dim,t->bg);
    char urlP[48];strncpy(urlP,_dlUrl,47);urlP[47]='\0';
    g_lcd_secondary.setCursor(14,88); g_lcd_secondary.print(urlP);
    if((int)strlen(_dlUrl)>47)g_lcd_secondary.print("...");
    g_lcd_secondary.setTextColor(t->text,t->bg);
    g_lcd_secondary.setCursor(14,116); g_lcd_secondary.print("Boots once. RESET = return to Purplx.");
    g_lcd_secondary.setTextColor(t->success,t->title_bg);
    g_lcd_secondary.setCursor(6,H-11); g_lcd_secondary.print("[ENTER] DOWNLOAD+FLASH");
    g_lcd_secondary.setTextColor(t->alert,t->title_bg);
    g_lcd_secondary.setCursor(206,H-11); g_lcd_secondary.print("[ESC] cancel");
    _hud("CONFIRM","ENTER = flash","ESC   = cancel",true);
    xSemaphoreGive(g_display_mutex);
}

static void _drawProgress(int pct,int dlKB,int totalKB,const char* phase) {
    const ColorTheme* t=g_theme; const int W=320,H=240;
    if(xSemaphoreTake(g_display_mutex,pdMS_TO_TICKS(25))!=pdTRUE)return;
    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0,0,W,24,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,23,W,t->border);
    g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->primary,t->title_bg);
    g_lcd_secondary.setCursor(8,8); g_lcd_secondary.print("[ WIFI OTA \xe2\x80\x94 FLASHING ]");
    g_lcd_secondary.setTextColor(t->secondary,t->bg);
    int sw=g_lcd_secondary.textWidth(phase);
    g_lcd_secondary.setCursor((W-sw)/2,38); g_lcd_secondary.print(phase);
    const int bx=20,by=58,bw=W-40,bh=20;
    g_lcd_secondary.drawRect(bx,by,bw,bh,t->border);
    if(pct>0){int fw=(bw-2)*pct/100;g_lcd_secondary.fillRect(bx+1,by+1,fw,bh-2,t->primary);}
    char pb[8];snprintf(pb,sizeof(pb),"%d%%",max(0,pct));
    g_lcd_secondary.setTextSize(2); g_lcd_secondary.setTextColor(t->text,t->bg);
    int pw=g_lcd_secondary.textWidth(pb);
    g_lcd_secondary.setCursor((W-pw)/2,by+26); g_lcd_secondary.print(pb);
    g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->text_dim,t->bg);
    char kbuf[40];
    if(totalKB>0)snprintf(kbuf,sizeof(kbuf),"%d / %d KB",dlKB,totalKB);
    else         snprintf(kbuf,sizeof(kbuf),"%d KB",dlKB);
    int kw=g_lcd_secondary.textWidth(kbuf);
    g_lcd_secondary.setCursor((W-kw)/2,by+52); g_lcd_secondary.print(kbuf);
    if(pct>=100){
        g_lcd_secondary.setTextColor(t->success,t->bg);
        int lw=g_lcd_secondary.textWidth("Launching firmware...");
        g_lcd_secondary.setCursor((W-lw)/2,by+72); g_lcd_secondary.print("Launching firmware...");
    }
    g_lcd_secondary.fillRect(0,H-16,W,16,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,H-16,W,t->border);
    g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->alert,t->title_bg);
    g_lcd_secondary.setCursor(6,H-11); g_lcd_secondary.print("DO NOT RESET DURING FLASH");
    char h1[24];snprintf(h1,sizeof(h1),"%s %d%%",phase,pct);
    _hud(h1,"DO NOT RESET",nullptr,true);
    xSemaphoreGive(g_display_mutex);
}

static void _drawError(const char* detail) {
    snprintf(_errMsg,sizeof(_errMsg),"%s",detail);
    _screen=Screen::ERROR_VIEW;
    const ColorTheme* t=g_theme; const int W=320,H=240;
    if(xSemaphoreTake(g_display_mutex,pdMS_TO_TICKS(50))!=pdTRUE)return;
    _chrome("[ WIFI OTA ERROR ]",true);
    g_lcd_secondary.setTextSize(2); g_lcd_secondary.setTextColor(t->alert,t->bg);
    int tw=g_lcd_secondary.textWidth("FAILED");
    g_lcd_secondary.setCursor((W-tw)/2,55); g_lcd_secondary.print("FAILED");
    g_lcd_secondary.setTextSize(1); g_lcd_secondary.setTextColor(t->text,t->bg);
    g_lcd_secondary.setCursor(12,100); g_lcd_secondary.print(_errMsg);
    g_lcd_secondary.setTextColor(t->text_dim,t->bg);
    g_lcd_secondary.setCursor(12,120); g_lcd_secondary.print("Partition unchanged. Purplx is safe.");
    g_lcd_secondary.setTextColor(t->text_dim,t->title_bg);
    g_lcd_secondary.setCursor(6,H-11); g_lcd_secondary.print("[ANY KEY] back");
    _hud("WIFI OTA ERR",_errMsg,nullptr,true);
    xSemaphoreGive(g_display_mutex);
}

static bool _connectWifi() {
    _screen=Screen::CONNECTING;
    WiFi.mode(WIFI_STA); WiFi.begin(_wifiSsid,_wifiPass);
    unsigned long t0=millis(); int tick=0; _drawConnecting(tick);
    while(WiFi.status()!=WL_CONNECTED&&(millis()-t0)<15000){delay(500);_drawConnecting(++tick);}
    return (WiFi.status()==WL_CONNECTED);
}

static void _downloadAndFlash(const char* url) {
    Serial.printf("[WiFiOTA] Download: %s\n",url);
    const esp_partition_t* guest=esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,nullptr);
    if(!guest){_drawError("ota_0 partition not found");return;}
    WiFiClientSecure wcs; wcs.setInsecure();
    HTTPClient http; http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); http.setTimeout(60000);
    if(!http.begin(wcs,url)){_drawError("HTTP begin failed");return;}
    _drawProgress(0,0,0,"Connecting...");
    int code=http.GET(); Serial.printf("[WiFiOTA] HTTP %d\n",code);
    if(code!=HTTP_CODE_OK){
        http.end(); char msg[48]; snprintf(msg,sizeof(msg),"HTTP %d (bad URL?)",code);
        _drawError(msg); return;
    }
    int contentLen=http.getSize(); int totalKB=(contentLen>0)?contentLen/1024:0;
    if(contentLen>0){
        if(contentLen<256){http.end();char msg[40];snprintf(msg,sizeof(msg),"File too small (%d B)",contentLen);_drawError(msg);return;}
        if((size_t)contentLen>guest->size){http.end();char msg[56];snprintf(msg,sizeof(msg),"File %dKB > ota_0 %uKB",contentLen/1024,(unsigned)(guest->size/1024));_drawError(msg);return;}
    }
    _drawProgress(0,0,totalKB,"Erasing OTA...");
    esp_ota_handle_t handle=0;
    esp_err_t err=esp_ota_begin(guest,contentLen>0?(size_t)contentLen:OTA_WITH_SEQUENTIAL_WRITES,&handle);
    if(err!=ESP_OK){http.end();char msg[48];snprintf(msg,sizeof(msg),"ota_begin: %s",esp_err_to_name(err));_drawError(msg);return;}
    static uint8_t buf[4096];
    WiFiClient* stream=http.getStreamPtr();
    size_t written=0; int lastPct=-1; unsigned long lastDraw=millis();
    while(http.connected()||stream->available()){
        int avail=stream->available();
        if(avail>0){
            int rd=stream->read(buf,min(avail,(int)sizeof(buf))); if(rd<=0)break;
            err=esp_ota_write(handle,buf,(size_t)rd);
            if(err!=ESP_OK){esp_ota_abort(handle);http.end();char msg[48];snprintf(msg,sizeof(msg),"ota_write: %s",esp_err_to_name(err));_drawError(msg);return;}
            written+=(size_t)rd;
            int pct=(contentLen>0)?(int)(written*100UL/(size_t)contentLen):0;
            unsigned long now=millis();
            if((pct!=lastPct||now-lastDraw>500)&&now-lastDraw>150){
                _drawProgress(pct,(int)(written/1024),totalKB,"Downloading...");lastPct=pct;lastDraw=now;}
        } else {if(contentLen>0&&written>=(size_t)contentLen)break;delay(1);}
    }
    http.end(); Serial.printf("[WiFiOTA] Wrote %u bytes\n",(unsigned)written);
    if(written<256){esp_ota_abort(handle);_drawError("Download empty");return;}
    if(contentLen>0&&written<(size_t)contentLen){
        esp_ota_abort(handle);char msg[48];snprintf(msg,sizeof(msg),"Incomplete %uKB/%uKB",(unsigned)(written/1024),(unsigned)(contentLen/1024));_drawError(msg);return;}
    _drawProgress(99,(int)(written/1024),totalKB,"Verifying...");
    err=esp_ota_end(handle);
    if(err!=ESP_OK){char msg[48];snprintf(msg,sizeof(msg),"ota_end: %s",esp_err_to_name(err));_drawError(msg);return;}
    err=esp_ota_set_boot_partition(guest);
    if(err!=ESP_OK){char msg[48];snprintf(msg,sizeof(msg),"set_boot: %s",esp_err_to_name(err));_drawError(msg);return;}
    _drawProgress(100,(int)(written/1024),totalKB,"Complete!");
    delay(700);
    if(xSemaphoreTake(g_display_mutex,pdMS_TO_TICKS(100))==pdTRUE){
        g_lcd_secondary.fillScreen(TFT_BLACK);g_lcd_primary.fillScreen(TFT_BLACK);xSemaphoreGive(g_display_mutex);}
    delay(150);
    Serial.println("[WiFiOTA] Rebooting. RESET = return to Purplx."); Serial.flush(); esp_restart();
}

// Public API
void start() {
    _fwCursor=0; _connFail=false;
    if(WiFi.status()==WL_CONNECTED){_screen=Screen::LIST;_drawList();return;}
    if(_loadCreds()){if(_connectWifi()){_screen=Screen::LIST;_drawList();return;}}
    _doScan();
}
void stop() { WiFi.scanDelete(); }
void handleKey(char key) {
    bool esc=(key==27||key=='`');
    switch(_screen){
    case Screen::WIFI_SCAN:
        if(esc){OS_SetState(AppState::HOME);}
        else if(key=='w'||key=='W'){if(_scanCursor>0){_scanCursor--;if(_scanCursor<_scanScroll)_scanScroll=_scanCursor;_drawScanList();}}
        else if(key=='s'||key=='S'){int tot=_scanCount+1;if(_scanCursor<tot-1){_scanCursor++;if(_scanCursor>=_scanScroll+SCAN_VISIBLE)_scanScroll=_scanCursor-SCAN_VISIBLE+1;_drawScanList();}}
        else if(key=='r'||key=='R'){_doScan();}
        else if(key=='\n'||key=='\r'){
            if(_scanCursor==_scanCount){_inputBegin(false);_screen=Screen::SSID_INPUT;_drawCredInput("WiFi Network Name (SSID):");}
            else{
                strncpy(_wifiSsid,WiFi.SSID(_scanCursor).c_str(),sizeof(_wifiSsid)-1);_wifiSsid[sizeof(_wifiSsid)-1]='\0';
                if(WiFi.encryptionType(_scanCursor)==WIFI_AUTH_OPEN){_wifiPass[0]='\0';if(_connectWifi()){_screen=Screen::LIST;_drawList();}else{_connFail=true;_drawError("WiFi connection failed");}}
                else{_inputBegin(true);_screen=Screen::PASS_INPUT;_drawCredInput("WiFi Password:");}
            }
        }
        break;
    case Screen::SSID_INPUT:
        if(esc){_screen=Screen::WIFI_SCAN;_drawScanList();}
        else if(key=='\n'||key=='\r'){if(_inputLen>0){strncpy(_wifiSsid,_inputBuf,sizeof(_wifiSsid)-1);_inputBegin(true);_screen=Screen::PASS_INPUT;_drawCredInput("WiFi Password:");}}
        else{_inputKey(key);_drawCredInput("WiFi Network Name (SSID):");}
        break;
    case Screen::PASS_INPUT:
        if(esc){_inputBegin(false);_screen=Screen::SSID_INPUT;_drawCredInput("WiFi Network Name (SSID):");}
        else if(key=='\n'||key=='\r'){strncpy(_wifiPass,_inputBuf,sizeof(_wifiPass)-1);_saveCreds();if(_connectWifi()){_screen=Screen::LIST;_drawList();}else{_connFail=true;_drawError("WiFi failed. Check password.");}}
        else{_inputKey(key);_drawCredInput("WiFi Password:");}
        break;
    case Screen::LIST:
        if(esc){OS_SetState(AppState::HOME);}
        else if(key=='w'||key=='W'){if(_fwCursor>0){_fwCursor--;_drawList();}}
        else if(key=='s'||key=='S'){if(_fwCursor<CATALOG_LEN-1){_fwCursor++;_drawList();}}
        else if(key=='r'||key=='R'){_doScan();}
        else if(key=='\n'||key=='\r'){
            if(CATALOG[_fwCursor].url==nullptr){_inputBegin(false);_screen=Screen::URL_INPUT;_drawURLInput();}
            else{strncpy(_dlUrl,CATALOG[_fwCursor].url,sizeof(_dlUrl)-1);_screen=Screen::CONFIRM;_drawConfirm();}
        }
        break;
    case Screen::URL_INPUT:
        if(esc){_screen=Screen::LIST;_drawList();}
        else if(key=='\n'||key=='\r'){
            bool ok=(_inputLen>8)&&(strncmp(_inputBuf,"http://",7)==0||strncmp(_inputBuf,"https://",8)==0);
            if(ok){strncpy(_dlUrl,_inputBuf,sizeof(_dlUrl)-1);_screen=Screen::CONFIRM;_drawConfirm();}
            else{_drawURLInput();}
        }
        else{_inputKey(key);_drawURLInput();}
        break;
    case Screen::CONFIRM:
        if(esc){_screen=Screen::LIST;_drawList();}
        else if(key=='\n'||key=='\r'){_downloadAndFlash(_dlUrl);}
        break;
    case Screen::ERROR_VIEW:
        if(_connFail){_connFail=false;_doScan();}
        else{_screen=Screen::LIST;_drawList();}
        break;
    default:break;
    }
}

} // namespace WiFiOTA
