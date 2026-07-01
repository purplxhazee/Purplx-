// =============================================================================
// learn.cpp — Ethical Hacking Learning Library
// =============================================================================
#include "learn.h"
#include "../../../include/main.h"
#include "../../../include/themes.h"

namespace Learn {

// ─── Lesson content ───────────────────────────────────────────────────────────
// Each lesson is an array of lines. Kept short and plain. ~38 chars/line fits
// the ILI9341 at text size 1.
struct Lesson {
    const char* title;
    const char* subtitle;
    const char* const* lines;
    int         line_count;
};

// ── Lesson 1: What is ethical hacking ────────────────────────────────────────
static const char* L1[] = {
    "Ethical hacking means finding security",
    "weaknesses BEFORE the bad guys do — with",
    "permission — so they can be fixed.",
    "",
    "The skills are identical to malicious",
    "hacking. The ONLY difference is consent.",
    "",
    "THE GOLDEN RULE:",
    "Only test systems you own, or that you",
    "have WRITTEN permission to test.",
    "",
    "Same action, two outcomes:",
    "  - Your own router  = learning",
    "  - A stranger's     = a crime",
    "",
    "Professionals call this 'scope'. Before",
    "any test, you agree IN WRITING exactly",
    "what you're allowed to touch. Stay in",
    "scope and you're a researcher. Step",
    "outside it and you're a criminal.",
    "",
    "This device is a learning tool. Treat it",
    "like a lockpick set: legal to own and",
    "practice with on your own locks, illegal",
    "to use on someone else's door.",
};

// ── Lesson 2: How WiFi works ─────────────────────────────────────────────────
static const char* L2[] = {
    "To understand WiFi attacks, first know",
    "how WiFi itself works.",
    "",
    "WiFi devices talk in 'frames' — small",
    "packets of radio data. There are 3 kinds:",
    "",
    "1. MANAGEMENT frames",
    "   Setup + control. 'Here I am' beacons,",
    "   join requests, disconnect notices.",
    "",
    "2. CONTROL frames",
    "   Traffic cops. 'Ready to receive?' etc.",
    "",
    "3. DATA frames",
    "   The actual content — your web pages,",
    "   messages, video.",
    "",
    "KEY WEAKNESS: in older WiFi, management",
    "frames are NOT authenticated. Anyone can",
    "send a fake 'disconnect' frame pretending",
    "to be the router. That's the basis of the",
    "deauth attack (next lesson).",
    "",
    "Channels: WiFi splits 2.4GHz into 13",
    "channels. Devices hop between them. A",
    "scanner listens channel by channel.",
};

// ── Lesson 3: Deauthentication attacks ───────────────────────────────────────
static const char* L3[] = {
    "A 'deauth' attack kicks devices off WiFi.",
    "",
    "HOW IT WORKS:",
    "Management frames aren't authenticated in",
    "older WiFi (WPA2 and earlier). The attack",
    "sends a forged 'deauthentication' frame",
    "that claims to be from the router, telling",
    "a device to disconnect.",
    "",
    "The device believes it and drops off. Spam",
    "these frames and it can't reconnect.",
    "",
    "WHY ATTACKERS DO IT:",
    "  - Force a device to reconnect, so they",
    "    can capture the 'handshake' (the login",
    "    exchange) to try cracking the password",
    "  - Just to disrupt (jamming)",
    "",
    "THE DEFENSE:",
    "WPA3 and '802.11w' (Protected Management",
    "Frames / PMF) authenticate these frames,",
    "so forged deauths are ignored. This is why",
    "modern networks resist the attack.",
    "",
    "LEGAL: Jamming someone's WiFi is illegal",
    "in nearly every country. Practice only on",
    "your OWN network.",
};

// ── Lesson 4: Captive portals & evil twins ───────────────────────────────────
static const char* L4[] = {
    "A 'captive portal' is the login page you",
    "see on hotel/airport WiFi. Attackers abuse",
    "this with an 'evil twin'.",
    "",
    "HOW THE ATTACK WORKS:",
    "1. Attacker makes a fake WiFi network with",
    "   the SAME name as a real one (e.g.",
    "   'Starbucks WiFi').",
    "2. Optionally deauths you off the real one.",
    "3. Your device connects to the stronger",
    "   fake one automatically.",
    "4. A fake login page asks for a password,",
    "   email, or credit card.",
    "5. Whatever you type goes to the attacker.",
    "",
    "WHY IT WORKS:",
    "Devices trust networks by NAME, not",
    "identity. Two networks can share a name.",
    "And people are used to 'log in to WiFi'",
    "pages, so a fake one doesn't look odd.",
    "",
    "THE DEFENSE:",
    "  - Use a VPN on public WiFi",
    "  - Never enter passwords on WiFi login",
    "    pages you didn't expect",
    "  - 'Forget' open networks so you don't",
    "    auto-join look-alikes",
    "",
    "LEGAL: Capturing others' credentials is",
    "wire fraud / unauthorized access. Felony.",
    "Build portals only to test your own logins.",
};

// ── Lesson 5: WiFi CSI sensing ───────────────────────────────────────────────
static const char* L5[] = {
    "CSI (Channel State Information) lets you",
    "DETECT PEOPLE using only WiFi — no camera.",
    "",
    "HOW IT WORKS:",
    "WiFi radio waves bounce around a room. When",
    "a person moves, their body changes how the",
    "waves bounce — tiny shifts in timing and",
    "strength across the signal's sub-carriers.",
    "",
    "CSI is the detailed 'fingerprint' of the",
    "channel. By watching how that fingerprint",
    "wobbles over time, you can tell:",
    "  - someone is moving (big wobble)",
    "  - someone is still but present (breathing",
    "    causes small, regular wobble)",
    "  - empty room (steady fingerprint)",
    "",
    "This is the tech behind 'WiFi sensing' —",
    "used for presence detection, fall detection",
    "for elderly care, and gesture recognition.",
    "",
    "Your Purplx CSI Radar uses this: it watches",
    "amplitude AND phase variance, and lights up",
    "when motion disturbs the room's WiFi.",
    "",
    "LEGAL: passive sensing of your own space is",
    "fine. Tracking people without consent is a",
    "privacy violation — keep it to your space.",
};

// ── Lesson 6: Bluetooth basics & BLE spam ────────────────────────────────────
static const char* L6[] = {
    "Bluetooth Low Energy (BLE) is how phones,",
    "earbuds, watches, and tags talk nearby.",
    "",
    "ADVERTISING:",
    "BLE devices constantly broadcast tiny",
    "'advertising' packets — 'I'm AirPods, pair",
    "with me!'. Phones listen and show popups.",
    "",
    "BLE SPAM:",
    "Attackers flood the air with fake",
    "advertising packets. Nearby phones show a",
    "storm of fake pairing popups (fake AirPods,",
    "fake Android devices, etc.). It's annoying",
    "and can drain batteries, but doesn't steal",
    "data by itself.",
    "",
    "WHY IT WORKS:",
    "Phones auto-react to advertising packets to",
    "make pairing easy. That convenience is the",
    "weakness.",
    "",
    "THE DEFENSE:",
    "Phone makers now filter spammy adverts.",
    "Updating your phone helps.",
    "",
    "LEGAL: spamming people in public is",
    "harassment and may break radio laws. Test",
    "only on your own devices.",
};

// ── Lesson 7: Staying legal & a learning path ────────────────────────────────
static const char* L7[] = {
    "HOW TO LEARN THIS THE RIGHT WAY",
    "",
    "1. BUILD A LAB",
    "   Use YOUR OWN old router, a spare phone,",
    "   a Raspberry Pi. Attack those freely.",
    "",
    "2. USE LEGAL PRACTICE SITES",
    "   TryHackMe, Hack The Box, PortSwigger",
    "   Web Security Academy (free), OverTheWire.",
    "   These are built for you to hack legally.",
    "",
    "3. GET CERTIFIED (optional, powerful)",
    "   CompTIA Security+, then PNPT or OSCP for",
    "   hands-on pentesting. Employers value",
    "   these and they keep you on the legal",
    "   side.",
    "",
    "4. KNOW THE LAW",
    "   US: CFAA. UK: Computer Misuse Act.",
    "   Most countries: unauthorized access is a",
    "   crime even if nothing is damaged.",
    "",
    "5. THE MINDSET",
    "   Real hackers are curious, patient, and",
    "   ethical. The goal is to understand",
    "   systems deeply and make them safer.",
    "",
    "You own the tools. Own the responsibility",
    "too. Hack what's yours. Help others stay",
    "safe. That's what makes you the good guy.",
};

static const Lesson LESSONS[] = {
    { "Ethical Hacking 101", "consent is everything", L1, sizeof(L1)/sizeof(L1[0]) },
    { "How WiFi Works",      "frames & channels",     L2, sizeof(L2)/sizeof(L2[0]) },
    { "Deauth Attacks",      "kicking devices off",   L3, sizeof(L3)/sizeof(L3[0]) },
    { "Captive Portals",     "evil twin networks",    L4, sizeof(L4)/sizeof(L4[0]) },
    { "WiFi CSI Sensing",    "see people w/o a cam",  L5, sizeof(L5)/sizeof(L5[0]) },
    { "Bluetooth & BLE",     "spam & advertising",    L6, sizeof(L6)/sizeof(L6[0]) },
    { "Learn It Right",      "your legal path in",    L7, sizeof(L7)/sizeof(L7[0]) },
};
static const int LESSON_COUNT = sizeof(LESSONS)/sizeof(LESSONS[0]);

// ─── State ────────────────────────────────────────────────────────────────────
static bool _running = false;
static bool _reading = false;   // false = index, true = reading a lesson
static int  _sel     = 0;
static int  _scroll  = 0;       // line offset while reading
static const int VISIBLE_LINES = 13;

// =============================================================================
static void drawIndex() {
    const ColorTheme* t = g_theme;
    g_lcd_secondary.fillScreen(t->bg);
    g_lcd_secondary.fillRect(0,0,320,22,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,21,320,t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary,t->title_bg);
    g_lcd_secondary.setCursor(8,7);
    g_lcd_secondary.print("LEARN  //  Ethical Hacking");

    for (int i=0;i<LESSON_COUNT;i++) {
        const int y = 28 + i*26;
        const bool sel = (i==_sel);
        if (sel) {
            g_lcd_secondary.fillRect(0,y,320,24,t->highlight_bg);
            g_lcd_secondary.setTextColor(t->highlight_text,t->highlight_bg);
        } else {
            g_lcd_secondary.fillRect(0,y,320,24, (i%2)?t->row_b:t->row_a);
            g_lcd_secondary.setTextColor(t->text,(i%2)?t->row_b:t->row_a);
        }
        g_lcd_secondary.setCursor(8,y+3);
        g_lcd_secondary.printf("%d. %s", i+1, LESSONS[i].title);
        g_lcd_secondary.setTextColor(sel?t->highlight_text:t->text_dim,
                                     sel?t->highlight_bg:((i%2)?t->row_b:t->row_a));
        g_lcd_secondary.setCursor(20,y+13);
        g_lcd_secondary.print(LESSONS[i].subtitle);
    }
    g_lcd_secondary.fillRect(0,222,320,18,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,222,320,t->border);
    g_lcd_secondary.setTextColor(t->text_dim,t->title_bg);
    g_lcd_secondary.setCursor(6,227);
    g_lcd_secondary.print("[W/S] pick  [ENTER] read  [ESC] back");
}

static void drawReader() {
    const ColorTheme* t = g_theme;
    const Lesson& L = LESSONS[_sel];
    g_lcd_secondary.fillScreen(t->bg);
    // Title
    g_lcd_secondary.fillRect(0,0,320,22,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,21,320,t->border);
    g_lcd_secondary.setTextSize(1);
    g_lcd_secondary.setTextColor(t->primary,t->title_bg);
    g_lcd_secondary.setCursor(8,7);
    g_lcd_secondary.print(L.title);
    // scroll indicator
    char pos[16];
    snprintf(pos,sizeof(pos),"%d/%d", _scroll+1, L.line_count);
    g_lcd_secondary.setTextColor(t->secondary,t->title_bg);
    g_lcd_secondary.setCursor(320-g_lcd_secondary.textWidth(pos)-6,7);
    g_lcd_secondary.print(pos);

    // Body
    int y = 28;
    for (int i=0;i<VISIBLE_LINES;i++) {
        int li = _scroll + i;
        if (li >= L.line_count) break;
        const char* line = L.lines[li];
        // Headers (ALL CAPS ending in ':') get accent color
        int len = strlen(line);
        bool header = (len>2 && line[len-1]==':' && line[0]>='A' && line[0]<='Z');
        g_lcd_secondary.setTextColor(header?t->secondary:t->text, t->bg);
        g_lcd_secondary.setCursor(8,y);
        g_lcd_secondary.print(line);
        y += 14;
    }

    // Footer
    g_lcd_secondary.fillRect(0,222,320,18,t->title_bg);
    g_lcd_secondary.drawFastHLine(0,222,320,t->border);
    g_lcd_secondary.setTextColor(t->text_dim,t->title_bg);
    g_lcd_secondary.setCursor(6,227);
    g_lcd_secondary.print("[W/S] scroll  [ESC] back to list");
}

static void drawHUD() {
    const ColorTheme* t = g_theme;
    g_lcd_primary.fillScreen(t->hud_bg);
    g_lcd_primary.setTextColor(t->primary,t->hud_bg);
    g_lcd_primary.setTextSize(2);
    g_lcd_primary.setCursor(4,6);
    g_lcd_primary.print("LEARN");
    g_lcd_primary.setTextSize(1);
    g_lcd_primary.setTextColor(t->text,t->hud_bg);
    g_lcd_primary.setCursor(4,32);
    if (_reading) {
        char nm[28]; strncpy(nm,LESSONS[_sel].title,27); nm[27]=0;
        g_lcd_primary.print(nm);
        g_lcd_primary.setCursor(4,48);
        g_lcd_primary.setTextColor(t->text_dim,t->hud_bg);
        g_lcd_primary.print("Scroll with W/S");
    } else {
        g_lcd_primary.printf("%d lessons", LESSON_COUNT);
        g_lcd_primary.setCursor(4,48);
        g_lcd_primary.setTextColor(t->secondary,t->hud_bg);
        g_lcd_primary.print("Start with #1");
    }
    g_lcd_primary.setCursor(4,80);
    g_lcd_primary.setTextColor(t->text_dim,t->hud_bg);
    g_lcd_primary.print("Knowledge = power");
    g_lcd_primary.setCursor(4,94);
    g_lcd_primary.print("Use it ethically");
}

// =============================================================================
void start() {
    _running = true; _reading = false; _sel = 0; _scroll = 0;
    drawIndex();
    drawHUD();
}

void handleKey(char key) {
    if (!_reading) {
        // INDEX navigation
        if (key=='w'||key=='W') { if(_sel>0)_sel--; drawIndex(); }
        else if (key=='s'||key=='S') { if(_sel<LESSON_COUNT-1)_sel++; drawIndex(); }
        else if (key=='\n'||key=='\r') {
            _reading = true; _scroll = 0; drawReader(); drawHUD();
        }
        // ESC handled by main (exits Learn)
    } else {
        // READER navigation
        const Lesson& L = LESSONS[_sel];
        const int maxScroll = (L.line_count > VISIBLE_LINES)
                            ? (L.line_count - VISIBLE_LINES) : 0;
        if (key=='s'||key=='S') { if(_scroll<maxScroll)_scroll++; drawReader(); }
        else if (key=='w'||key=='W') { if(_scroll>0)_scroll--; drawReader(); }
        else if (key==27||key=='`') {
            // back to index (consume ESC here, don't exit Learn)
            _reading = false; drawIndex(); drawHUD();
        }
    }
}

bool isRunning() { return _running; }
// Returns true if ESC should exit Learn entirely (only from index view)
bool atIndex() { return !_reading; }
void stop() { _running=false; _reading=false; }

} // namespace Learn
