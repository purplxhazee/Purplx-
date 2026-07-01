// =============================================================================
// cyberfx.cpp — Animated Cyberdeck Background Engine
// =============================================================================
#include "cyberfx.h"
#include "../../include/themes.h"
#include <math.h>

namespace CyberFX {

static Effect _fx = Effect::MATRIX;
static int    _w  = 320;
static int    _h  = 240;

// ─── MATRIX rain state ────────────────────────────────────────────────────────
static const int MAX_COLS = 40;
static int16_t _col_y[MAX_COLS];
static uint8_t _col_speed[MAX_COLS];
static uint8_t _col_len[MAX_COLS];
static int     _num_cols = 0;
static const int CELL = 8;   // glyph cell size in px

// ─── STARS state ──────────────────────────────────────────────────────────────
static const int MAX_STARS = 60;
static float _star_x[MAX_STARS];
static float _star_y[MAX_STARS];
static float _star_z[MAX_STARS];

// ─── GRID state ───────────────────────────────────────────────────────────────
static float _grid_phase = 0.0f;

// ─── PULSE state ──────────────────────────────────────────────────────────────
static uint32_t _pulse_start = 0;

// ─── Helpers ──────────────────────────────────────────────────────────────────
static inline uint16_t dimColor(uint16_t c, float f) {
    if (f >= 1.0f) return c;
    if (f <= 0.0f) return 0;
    uint8_t r = ((c >> 11) & 0x1F) * f;
    uint8_t g = ((c >> 5)  & 0x3F) * f;
    uint8_t b = ( c        & 0x1F) * f;
    return (r << 11) | (g << 5) | b;
}

// =============================================================================
// begin()
// =============================================================================
void begin(Effect fx, int w, int h) {
    _fx = fx; _w = w; _h = h;

    switch (fx) {
        case Effect::MATRIX: {
            _num_cols = w / CELL;
            if (_num_cols > MAX_COLS) _num_cols = MAX_COLS;
            for (int i = 0; i < _num_cols; i++) {
                _col_y[i]     = -(int16_t)random(0, h);
                _col_speed[i] = 1 + random(0, 3);
                _col_len[i]   = 4 + random(0, 12);
            }
            break;
        }
        case Effect::STARS: {
            for (int i = 0; i < MAX_STARS; i++) {
                _star_x[i] = random(-w, w);
                _star_y[i] = random(-h, h);
                _star_z[i] = random(1, w);
            }
            break;
        }
        case Effect::GRID:
            _grid_phase = 0.0f;
            break;
        case Effect::PULSE:
            _pulse_start = millis();
            break;
        default: break;
    }
}

// =============================================================================
// MATRIX rain
// =============================================================================
static void drawMatrix(LGFX_Sprite& spr, uint32_t now, float dim) {
    static const char GLYPHS[] =
        "0123456789ABCDEFアカサタナハ@#$%&*<>=+";
    static const int GN = sizeof(GLYPHS) - 1;

    const uint16_t head = dimColor(g_theme->text, dim);          // bright head
    const uint16_t body = dimColor(g_theme->primary, dim * 0.7f); // trail

    spr.setTextSize(1);
    spr.setTextFont(1);

    for (int c = 0; c < _num_cols; c++) {
        const int x = c * CELL;
        // advance
        if ((now / (30 + _col_speed[c] * 10)) % 1 == 0) {
            // movement handled by speed below
        }
        _col_y[c] += _col_speed[c];
        if (_col_y[c] - _col_len[c] * CELL > _h) {
            _col_y[c]     = -(int16_t)random(0, _h / 2);
            _col_speed[c] = 1 + random(0, 3);
            _col_len[c]   = 4 + random(0, 12);
        }
        // draw trail
        for (int k = 0; k < _col_len[c]; k++) {
            const int y = _col_y[c] - k * CELL;
            if (y < 0 || y >= _h) continue;
            char g[2] = { GLYPHS[(c * 7 + k * 13 + (now/120)) % GN], 0 };
            if (k == 0) {
                spr.setTextColor(head);
            } else {
                float f = (float)(_col_len[c] - k) / _col_len[c];
                spr.setTextColor(dimColor(body, f));
            }
            spr.drawString(g, x, y);
        }
    }
}

// =============================================================================
// STARS warp field
// =============================================================================
static void drawStars(LGFX_Sprite& spr, uint32_t now, float dim) {
    const int cx = _w / 2, cy = _h / 2;
    const uint16_t col = dimColor(g_theme->primary, dim);
    const uint16_t hot = dimColor(g_theme->text, dim);

    for (int i = 0; i < MAX_STARS; i++) {
        _star_z[i] -= 4.0f;
        if (_star_z[i] <= 1.0f) {
            _star_x[i] = random(-_w, _w);
            _star_y[i] = random(-_h, _h);
            _star_z[i] = _w;
        }
        const float k  = 128.0f / _star_z[i];
        const int   sx = cx + (int)(_star_x[i] * k);
        const int   sy = cy + (int)(_star_y[i] * k);
        if (sx < 0 || sx >= _w || sy < 0 || sy >= _h) continue;
        // closer stars are bigger + hotter
        const float depth = 1.0f - (_star_z[i] / _w);
        if (depth > 0.7f) {
            spr.fillRect(sx, sy, 2, 2, hot);
        } else {
            spr.drawPixel(sx, sy, col);
        }
    }
}

// =============================================================================
// GRID perspective scroll
// =============================================================================
static void drawGrid(LGFX_Sprite& spr, uint32_t now, float dim) {
    const uint16_t col = dimColor(g_theme->primary, dim * 0.6f);
    const int horizon  = _h / 3;
    const int cx       = _w / 2;

    _grid_phase += 0.8f;
    if (_grid_phase > 24.0f) _grid_phase -= 24.0f;

    // Horizontal lines receding to horizon (perspective)
    for (int i = 0; i < 14; i++) {
        float t = (i + (_grid_phase / 24.0f)) / 14.0f;
        int y = horizon + (int)((_h - horizon) * t * t);
        if (y < horizon || y >= _h) continue;
        spr.drawFastHLine(0, y, _w, col);
    }
    // Vertical lines fanning out from center
    for (int i = -7; i <= 7; i++) {
        int x_bottom = cx + i * (_w / 7);
        spr.drawLine(cx + i * 6, horizon, x_bottom, _h, col);
    }
    // Horizon glow line
    spr.drawFastHLine(0, horizon, _w, dimColor(g_theme->secondary, dim));
}

// =============================================================================
// PULSE concentric radar
// =============================================================================
static void drawPulse(LGFX_Sprite& spr, uint32_t now, float dim) {
    const int cx = _w / 2, cy = _h / 2;
    const uint32_t elapsed = now - _pulse_start;
    const int maxR = (int)sqrtf(cx * cx + cy * cy);

    // 3 staggered rings
    for (int ring = 0; ring < 3; ring++) {
        const uint32_t period = 2400;
        const uint32_t offset = ring * (period / 3);
        const float prog = (float)((elapsed + offset) % period) / period;
        const int r = (int)(prog * maxR);
        const float fade = (1.0f - prog) * dim;
        if (fade <= 0.02f) continue;
        spr.drawCircle(cx, cy, r,     dimColor(g_theme->primary, fade));
        spr.drawCircle(cx, cy, r + 1, dimColor(g_theme->primary, fade * 0.5f));
    }
    // center dot
    spr.fillCircle(cx, cy, 2, dimColor(g_theme->text, dim));
}

// =============================================================================
// draw() dispatch
// =============================================================================
void draw(LGFX_Sprite& spr, uint32_t now, float dim) {
    switch (_fx) {
        case Effect::MATRIX: drawMatrix(spr, now, dim); break;
        case Effect::STARS:  drawStars(spr, now, dim);  break;
        case Effect::GRID:   drawGrid(spr, now, dim);   break;
        case Effect::PULSE:  drawPulse(spr, now, dim);  break;
        default: break;
    }
}

void next(int w, int h) {
    uint8_t n = ((uint8_t)_fx + 1) % (uint8_t)Effect::COUNT;
    begin((Effect)n, w, h);
}

const char* name() {
    switch (_fx) {
        case Effect::MATRIX: return "MATRIX";
        case Effect::STARS:  return "STARFIELD";
        case Effect::GRID:   return "GRID";
        case Effect::PULSE:  return "PULSE";
        default:             return "?";
    }
}

Effect current() { return _fx; }

} // namespace CyberFX
