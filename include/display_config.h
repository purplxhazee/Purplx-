#pragma once
// =============================================================================
// display_config.h — LovyanGFX Concrete Display Class Definitions
// =============================================================================
// CONFIRMED from skizzophrenic/M5PORKCHOP_DualScreen/src/ui/ext_display.h
//
// CRITICAL ARCHITECTURE (not intuitive):
//   BOTH displays + microSD all share SPI2_HOST (FSPI).
//   They have DIFFERENT MOSI pins (35 vs 14) but SAME SCK (40).
//   LovyanGFX handles this via bus_shared=true + use_lock=true:
//   it reconfigures the MOSI mux per-transaction and serialises access
//   with an internal mutex. This is the only approach proven to work
//   on the Cardputer ADV hardware.
//
//   Do NOT put the ILI9341 on SPI3 — the expansion header's SPI3 pins
//   are not confirmed to be accessible without hardware modification.
//
// LGFX_Primary   — ST7789  internal (240×135), SPI2, MOSI=35
// LGFX_Secondary — ILI9341 external (320×240), SPI2, MOSI=14, bus_shared
// =============================================================================

#include <M5GFX.h>
#include "pin_config.h"
#include <lgfx/v1/panel/Panel_ST7789.hpp>
#include <lgfx/v1/panel/Panel_LCD.hpp>

// Custom ILI9341 panel driver (M5GFX ships none).
struct Panel_ILI9341 : public lgfx::v1::Panel_LCD {
    Panel_ILI9341(void) {
        _cfg.memory_width  = _cfg.panel_width  = 240;
        _cfg.memory_height = _cfg.panel_height = 320;
    }
protected:
    const uint8_t* getInitCommands(uint8_t listno) const override {
        static constexpr uint8_t list0[] = {
            0xEF,3,0x03,0x80,0x02, 0xCF,3,0x00,0xC1,0x30,
            0xED,4,0x64,0x03,0x12,0x81, 0xE8,3,0x85,0x00,0x78,
            0xCB,5,0x39,0x2C,0x00,0x34,0x02, 0xF7,1,0x20, 0xEA,2,0x00,0x00,
            0xC0,1,0x23, 0xC1,1,0x10, 0xC5,2,0x3E,0x28, 0xC7,1,0x86,
            0xB1,2,0x00,0x18, 0xB6,3,0x08,0x82,0x27, 0xF2,1,0x00, 0x26,1,0x01,
            0xE0,15,0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,0x37,0x07,0x10,0x03,0x0E,0x09,0x00,
            0xE1,15,0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F,
            lgfx::v1::Panel_LCD::CMD_SLPOUT, lgfx::v1::Panel_LCD::CMD_INIT_DELAY, 120,
            lgfx::v1::Panel_LCD::CMD_DISPON, lgfx::v1::Panel_LCD::CMD_INIT_DELAY, 120,
            0xFF,0xFF
        };
        return (listno == 0) ? list0 : nullptr;
    }
};
namespace lgfx { using namespace lgfx::v1; }

// =============================================================================
// PRIMARY: ST7789 — Internal Cardputer Screen (240×135)
// SPI2 / FSPI, MOSI=35, shared bus with SD card and ILI9341
// =============================================================================
class LGFX_Primary : public lgfx::LGFX_Device {
    lgfx::v1::Panel_ST7789  _panel;
    lgfx::v1::Bus_SPI       _bus;
    lgfx::v1::Light_PWM     _light;

public:
    LGFX_Primary() {
        {
            auto cfg        = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 40000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = true;        // Write-only
            cfg.use_lock    = true;        // Required: bus shared with SD + ILI9341
            cfg.dma_channel = 0;
            cfg.pin_sclk    = PIN_LCD_SCLK;   // 40
            cfg.pin_mosi    = PIN_LCD_MOSI;   // 35
            cfg.pin_miso    = PIN_LCD_MISO;   // -1
            cfg.pin_dc      = PIN_LCD_DC;     // 33
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg            = _panel.config();
            cfg.pin_cs          = PIN_LCD_CS;     // 37
            cfg.pin_rst         = PIN_LCD_RST;    // 34
            cfg.pin_busy        = -1;
            cfg.memory_width    = 240;
            cfg.memory_height   = 320;
            cfg.panel_width     = LCD_PRIMARY_W;  // 240
            cfg.panel_height    = LCD_PRIMARY_H;  // 135
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 1;       // Landscape
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable        = false;
            cfg.invert          = true;    // ST7789 on Cardputer: invert=true
            cfg.rgb_order       = false;
            cfg.dlen_16bit      = false;
            cfg.bus_shared      = true;    // Shares SPI2 with SD + ILI9341
            _panel.config(cfg);
        }
        {
            auto cfg        = _light.config();
            cfg.pin_bl      = PIN_LCD_BL;  // 38
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};

// =============================================================================
// SECONDARY: ILI9341 — External 2.8" Screen (320×240 landscape)
// SPI2 / FSPI — SAME HOST as primary, bus_shared=true
//
// Key differences from primary:
//   - MOSI = GPIO14 (not 35)
//   - spi_3wire = true (MISO unwired)
//   - dma_channel = 0 (not AUTO — proven stable on shared bus from PorkChop)
//   - freq_write = 27MHz (conservative for DuPont cable runs)
//   - No backlight PWM (LED pin tied to 3.3V)
//   - setRotation(7) called post-init, NOT offset_rotation in panel config
//
// Panel memory is 240×320 (portrait). setRotation(7) yields 320×240 landscape.
// =============================================================================
class LGFX_Secondary : public lgfx::LGFX_Device {
    Panel_ILI9341 _panel;
    lgfx::v1::Bus_SPI       _bus;

public:
    LGFX_Secondary() {
        {
            auto cfg        = _bus.config();
            cfg.spi_host    = SPI2_HOST;   // SAME host as primary — bus_shared handles arbitration
            cfg.spi_mode    = 0;
            cfg.freq_write  = 27000000;    // 27MHz — proven stable over DuPont (from PorkChop source)
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = true;        // MISO unwired
            cfg.use_lock    = true;        // Required for shared bus
            cfg.dma_channel = 0;           // dma_channel=0 per PorkChop: stable on shared SPI2
            cfg.pin_sclk    = PIN_EXT_SCLK;   // 40 — shared clock line
            cfg.pin_mosi    = PIN_EXT_MOSI;   // 14 — ILI9341-only MOSI
            cfg.pin_miso    = PIN_EXT_MISO;   // -1
            cfg.pin_dc      = PIN_EXT_DC;     // 6
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg            = _panel.config();
            cfg.pin_cs          = PIN_EXT_CS;     // 5
            cfg.pin_rst         = PIN_EXT_RST;    // 3
            cfg.pin_busy        = -1;
            // Physical panel + memory: 240×320 (portrait native)
            // setRotation(7) post-init gives us 320×240 landscape
            cfg.memory_width    = 240;
            cfg.memory_height   = 320;
            cfg.panel_width     = 240;
            cfg.panel_height    = 320;
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;       // Rotation applied via setRotation(7) in Display::init()
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable        = false;   // spi_3wire — no readback
            cfg.invert          = false;   // ILI9341: no invert
            cfg.rgb_order       = false;   // Confirmed rgb_order=false from PorkChop
            cfg.dlen_16bit      = false;
            cfg.bus_shared      = true;    // Shares SPI2 with SD + ST7789
            _panel.config(cfg);
        }
        // No Light/backlight object — LED pin tied to 3.3V (always on)
        setPanel(&_panel);
    }
};
