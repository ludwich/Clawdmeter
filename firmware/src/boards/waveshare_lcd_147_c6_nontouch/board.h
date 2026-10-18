#pragma once

// Waveshare ESP32-C6-LCD-1.47 -- 1.47" 172x320 IPS TFT (run in landscape,
// 320x172), ST7789 over plain 4-wire SPI. ESP32-C6FH8: 8 MB in-package flash, no PSRAM. No touch,
// no PMU, no IMU, no audio. The TF slot shares the SPI bus (CS GPIO 4) and an
// onboard WS2812 sits on GPIO 8; neither is used by the firmware.
//
// Pin map from Waveshare's ESP32-C6-LCD-1.47 demo (Display_ST7789.h), checked
// against the C6FH8 read back over USB.

#define BOARD_NAME           "Waveshare LCD 1.47 (C6)"

// ---- Display geometry ----
// The glass is 172x320 portrait (LCD_PANEL_*), but the firmware runs it in
// landscape: the panel is turned a quarter turn at init (MADCTL, USB-C on
// the left when it stands in its desk stand), so BoardCaps and LVGL see
// 320x172 and every page, the meters included, is landscape.
#define LCD_PANEL_WIDTH      172
#define LCD_PANEL_HEIGHT     320
#define LCD_ROTATION         1
#define LCD_WIDTH            320
#define LCD_HEIGHT           172

// ---- SPI display pins (ST7789) ----
#define LCD_MOSI             6
#define LCD_SCLK             7
#define LCD_MISO             5     // only the TF card reads; the panel is write-only
#define LCD_CS               14
#define LCD_DC               15
#define LCD_RST              21
#define LCD_BL               22    // backlight, LEDC PWM (HIGH = on)

// The ST7789's RAM is 240 wide but only 172 columns reach the glass: the
// visible window starts at column 34 (and ends 34 short of the far edge).
#define LCD_COL_OFFSET       34
#define LCD_ROW_OFFSET       0

// TF card chip select on the same SPI bus; parked HIGH so the card stays off
// the bus while the panel is driven.
#define SD_CS                4

// ---- Buttons ----
// BOOT (GPIO 9) is a strap pin: do not hold it through power-up.
#define BTN_PWR_GPIO         9     // BOOT - PWR role: brightness, hold-to-pair

// ---- Capability flags ----
#define BOARD_HAS_SECONDARY_BUTTON 0
#define BOARD_HAS_ROTATION         0
#define BOARD_HAS_IMU              0
#define BOARD_HAS_BATTERY          0
#define BOARD_HAS_IO_EXPANDER      0
