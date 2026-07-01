// =============================================================================
// themes.cpp — Theme persistence
// =============================================================================
#include "../include/themes.h"
#include <Preferences.h>

const ColorTheme* g_theme    = &THEME_PURPLE;
ThemeID           g_theme_id = ThemeID::PURPLE;

namespace Themes {

void begin() {
    Preferences prefs;
    prefs.begin("megafw", true);
    uint8_t id = prefs.getUChar("theme", 0);
    prefs.end();
    if (id >= (uint8_t)ThemeID::COUNT) id = 0;
    g_theme_id = (ThemeID)id;
    g_theme    = THEMES[id];
}

void set(ThemeID id) {
    if ((uint8_t)id >= (uint8_t)ThemeID::COUNT) id = ThemeID::PURPLE;
    g_theme_id = id;
    g_theme    = THEMES[(uint8_t)id];
    Preferences prefs;
    prefs.begin("megafw", false);
    prefs.putUChar("theme", (uint8_t)id);
    prefs.end();
}

void next() {
    uint8_t n = ((uint8_t)g_theme_id + 1) % (uint8_t)ThemeID::COUNT;
    set((ThemeID)n);
}

void prev() {
    uint8_t n = ((uint8_t)g_theme_id + (uint8_t)ThemeID::COUNT - 1)
                % (uint8_t)ThemeID::COUNT;
    set((ThemeID)n);
}

ThemeID    current()     { return g_theme_id; }
const char* currentName(){ return g_theme->name; }

} // namespace Themes
