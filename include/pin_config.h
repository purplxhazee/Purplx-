#pragma once
// =============================================================================
// pin_config.h — MegaFirmware Hardware Pin Assignments
// Target: M5Stack Cardputer ADV
// =============================================================================
// ALL EXTERNAL ILI9341 PINS CONFIRMED from skizzophrenic/M5PORKCHOP_DualScreen
// source code (ext_display.h), which runs on the exact same hardware.
// Do not guess or change these unless you rewire your DuPont cables.
// =============================================================================

// ─── Internal ST7789 Display (SPI2 / FSPI) ───────────────────────────────────
// Confirmed from M5Stack Cardputer schematic and Bruce/PorkChop source
#define PIN_LCD_SCLK        40
#define PIN_LCD_MOSI        35
#define PIN_LCD_MISO        -1    // Write-only
#define PIN_LCD_DC          33
#define PIN_LCD_CS          37
#define PIN_LCD_RST         34
#define PIN_LCD_BL          38

// ─── MicroSD Card (shares SPI2 bus) ──────────────────────────────────────────
#define PIN_SD_SCLK         40    // Shared with LCD
#define PIN_SD_MOSI         35    // Shared with LCD
#define PIN_SD_MISO         39
#define PIN_SD_CS           12

// ─── External ILI9341 Display (also SPI2 / FSPI — bus_shared=true) ───────────
// Source: skizzophrenic/M5PORKCHOP_DualScreen/src/ui/ext_display.h
// "Shares the SD card's SPI bus -> FSPI/SPI2_HOST, bus_shared=true"
//
// ⚠ SCK is SHARED with the internal display (GPIO40) — this is correct.
// ⚠ MOSI is UNIQUE to the ILI9341 (GPIO14) — NOT shared with LCD/SD.
//    LovyanGFX reconfigures the MOSI pin per-transaction when bus_shared=true.
//
// Physical wiring (EXT header on Cardputer ADV expansion connector):
//   ILI9341 pin → Cardputer ADV GPIO
//   CLK/SCK     → GPIO40   (shared bus clock)
//   SDI/MOSI    → GPIO14
//   DC/RS       → GPIO6
//   CS          → GPIO5
//   RST         → GPIO3
//   LED/BL      → 3.3V direct (always on, not PWM controlled)
//   MISO/SDO    → UNWIRED (spi_3wire=true)
//
// ⚠ GPIO CONFLICT WARNING:
//   GPIO3 (RST), GPIO5 (CS), GPIO6 (DC) conflict with some Cardputer
//   peripherals when their init code drives those pins.
//   Per PorkChop notes: "Turn GPS OFF — it would drive GPIO 3/5/6"
//   If using Grove GPS (GPIO1/GPIO2): safe — no conflict.
//   If using CapLoRa GPS (GPIO15/GPIO13): safe — no conflict.
//
#define PIN_EXT_SCLK        40    // Shared with internal LCD (bus_shared handles this)
#define PIN_EXT_MOSI        14    // ILI9341-only MOSI line
#define PIN_EXT_MISO        -1    // Unwired — spi_3wire mode
#define PIN_EXT_DC           6
#define PIN_EXT_CS           5
#define PIN_EXT_RST          3
#define PIN_EXT_BL          -1    // Tied to 3.3V, no software PWM control

// ─── CapLoRa SX1262 — shares SPI2 bus ────────────────────────────────────────
// "CapLoRa868: SX1262 LoRa shares SD SPI" — PorkChop README
// ⚠ Set CS/RST/DIO1/BUSY to your actual CapLoRa module wiring
#define PIN_LORA_SCLK       PIN_SD_SCLK
#define PIN_LORA_MOSI       PIN_SD_MOSI
#define PIN_LORA_MISO       PIN_SD_MISO
#define PIN_LORA_CS         -1    // ⚠ Set to CapLoRa CS pin
#define PIN_LORA_RST        -1    // ⚠ Set to CapLoRa RST pin
#define PIN_LORA_DIO1       -1    // ⚠ SX1262 DIO1 IRQ
#define PIN_LORA_BUSY       -1    // ⚠ SX1262 BUSY

// ─── GPS UART ────────────────────────────────────────────────────────────────
// PorkChop confirmed: "GPS module: AT6668/ATGM336H (Grove G1/G2 or CapLoRa G15/G13)"
// ⚠ Use CapLoRa GPS (GPIO15/GPIO13) when ILI9341 is wired — Grove port is safe
//   too (GPIO1/GPIO2) but note the GPIO3 RST pin is adjacent on the header.
#define GPS_SOURCE_CAPLORA   1    // 1=CapLoRa (recommended), 0=Grove

#if GPS_SOURCE_CAPLORA
    #define PIN_GPS_RX      15   // CapLoRa G15
    #define PIN_GPS_TX      13   // CapLoRa G13
#else
    #define PIN_GPS_RX       1   // Grove G1
    #define PIN_GPS_TX       2   // Grove G2
#endif

#define GPS_BAUD        115200   // AT6668 default — confirmed by PorkChop
#define GPS_SERIAL_NUM       1   // Hardware Serial1

// ─── NeoPixel LED ─────────────────────────────────────────────────────────────
#ifdef PIN_NEOPIXEL
#undef PIN_NEOPIXEL
#endif
#define PIN_NEOPIXEL        21   // Confirmed from PorkChop: "NeoPixel LED (GPIO 21)"
#define NEOPIXEL_COUNT       1

// ─── Battery ADC ──────────────────────────────────────────────────────────────
#define PIN_BATTERY_ADC     -1   // ⚠ Set if you wire a voltage divider

// ─── Display Dimensions ───────────────────────────────────────────────────────
#define LCD_PRIMARY_W      240   // ST7789 landscape
#define LCD_PRIMARY_H      135
#define LCD_EXT_W          320   // ILI9341 landscape (after setRotation(7))
#define LCD_EXT_H          240
