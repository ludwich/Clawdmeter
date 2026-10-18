# LCD-1.47 (C6) — `waveshare_lcd_147_c6`

Hardware notes for this board. Shared firmware rules are in `firmware/CLAUDE.md`.

Waveshare ESP32-C6-LCD-1.47 (non-touch): ST7789 172×320 portrait IPS TFT over plain 4-wire SPI, ESP32-C6FH8 (8 MB in-package flash, **no PSRAM**). Build env: `waveshare_lcd_147_c6`. The touch variant (ESP32-C6-Touch-LCD-1.47, AXS5106L) would need its own board folder.

- SPI: MOSI 6, SCLK 7, CS 14, DC 15, RST 21; backlight LEDC PWM on GPIO 22 (no brightness command). These pins go through the GPIO matrix, so the write clock is 40 MHz.
- The ST7789 RAM is 240 wide but only 172 columns reach the glass: `LCD_COL_OFFSET` 34 on both sides. Colour inversion on (`ips = true`), matching Waveshare's INVON.
- The TF slot shares the SPI bus (MISO 5, CS 4); `board_init()` parks its CS HIGH. The WS2812 on GPIO 8 is unused.
- **No touch, no PMU, no battery gauge, no IMU, no audio.**
- **BOOT (GPIO 9) is the only input**, so it takes the PWR role (`power.cpp`): short press → brightness, hold ~1.5 s and release → pair. `button_count = 0` (no push to talk). With no touchscreen, a short press first dismisses an active alert (`director_button`). GPIO 9 is a strap pin: don't hold it through power-up.
- C6 env: `ARDUINO_USB_MODE=1` + `CDC_ON_BOOT=1` (HWCDC), no `BOARD_HAS_PSRAM`, snapshots off, 8 MB flash layout.
