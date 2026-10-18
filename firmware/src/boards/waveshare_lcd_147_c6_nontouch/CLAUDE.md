# LCD-1.47 (C6) — `waveshare_lcd_147_c6_nontouch`

Hardware notes for this board. Shared firmware rules are in `firmware/CLAUDE.md`.

Waveshare ESP32-C6-LCD-1.47 (non-touch): ST7789 172×320 IPS TFT over plain 4-wire SPI, run in **landscape (320×172)**, ESP32-C6FH8 (8 MB in-package flash, **no PSRAM**). Build env: `waveshare_lcd_147_c6_nontouch`. The touch variant (ESP32-C6-Touch-LCD-1.47: JD9853 panel, AXS5106L touch) is a different board, not ported here (open PR #162).

- SPI: MOSI 6, SCLK 7, CS 14, DC 15, RST 21; backlight LEDC PWM on GPIO 22 (no brightness command). These pins go through the GPIO matrix, so the write clock is 40 MHz.
- The ST7789 RAM is 240 wide but only 172 columns reach the glass: `LCD_COL_OFFSET` 34 on both sides. Colour inversion on (`ips = true`), matching Waveshare's INVON.
- The TF slot shares the SPI bus (MISO 5, CS 4); `board_init()` parks its CS HIGH. The WS2812 on GPIO 8 is unused.
- **No touch, no PMU, no battery gauge, no IMU, no audio.**
- **BOOT (GPIO 9) is the only input**, so it takes the PWR role (`power.cpp`): short press → next page, double press → brightness, hold ~1.5 s and release → pair. `button_count = 0` (no push to talk). With no touchscreen, a short press first dismisses an active alert (`director_button`). GPIO 9 is a strap pin: don't hold it through power-up.
- C6 env: `ARDUINO_USB_MODE=1` + `CDC_ON_BOOT=1` (HWCDC), no `BOARD_HAS_PSRAM`, snapshots off, 8 MB flash layout.
- **Landscape natively.** The glass is 172×320 (`LCD_PANEL_WIDTH/HEIGHT`) but the panel is turned a quarter turn at init (`LCD_ROTATION` 1, MADCTL; USB-C on the left, BOOT on top, as it stands in its desk stand), so `BoardCaps` reports `LCD_WIDTH` 320 × `LCD_HEIGHT` 172 and LVGL runs at 320×172. The meters take the console layout (`layout.cpp`, wide-and-short shape rule: Clawd at 4 px cells on a rail along the bottom, the rows in a column to his right); the Clawd and usage pages (`pages.h`) lay out at the same resolution. `display_hal_set_landscape()` is a no-op returning `true` (it enables the pages; nothing turns between them).
- RAM: the corner Clawd's 4 px buffer (37.5 KB) and the Clawd page's stage (~60 KB) both live in internal RAM; boot logs `meters: corner Clawd cell 4 ... B free` (about 200 KB left, ~140 KB with the stage open). If the 4 px buffer ever fails, `ui.cpp` retries at 3 px.
