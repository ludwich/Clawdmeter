#include "../../hal/display_hal.h"
#include "board.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// ST7789 over plain 4-wire SPI, the same driver path as the LCD-1.54. The
// panel needs colour inversion (Waveshare's init ends with INVON) and has no
// brightness command, so brightness is a LEDC PWM duty on the backlight GPIO
// and the idle fade works unchanged.

static Arduino_DataBus* bus = nullptr;
static Arduino_ST7789*  gfx = nullptr;

void display_hal_init(void) {
    bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI,
                               GFX_NOT_DEFINED /* panel is write-only */);
    // 172-column window centred in the 240-column GRAM: offset 34 on both
    // sides, so rotations 0 and 2 land on the glass alike.
    gfx = new Arduino_ST7789(bus, LCD_RST, 0 /* rotation */, true /* ips */,
                             LCD_WIDTH, LCD_HEIGHT,
                             LCD_COL_OFFSET, LCD_ROW_OFFSET,
                             LCD_COL_OFFSET, LCD_ROW_OFFSET);
}

void display_hal_begin(void) {
    // The SPI pins go through the GPIO matrix (not the C6's IOMUX FSPI pins),
    // which caps the reliable write clock at 40 MHz.
    gfx->begin(40000000);
    gfx->fillScreen(0x0000);
    ledcAttach(LCD_BL, 5000 /* Hz */, 8 /* bits */);
    ledcWrite(LCD_BL, 200);
}

void display_hal_set_brightness(uint8_t level) {
    ledcWrite(LCD_BL, level);
}

void display_hal_draw_bitmap(int32_t x, int32_t y, int32_t w, int32_t h,
                             const uint16_t* pixels) {
    if (gfx) gfx->draw16bitRGBBitmap(x, y, (uint16_t*)pixels, w, h);
}

// Landscape for the extra pages: a quarter turn in the controller (MADCTL),
// USB-C on the right. Arduino_GFX applies the matching column/row offsets.
bool display_hal_set_landscape(bool on) {
    if (!gfx) return false;
    gfx->setRotation(on ? 1 : 0);
    return true;
}

void display_hal_tick(void) {
    // No rotation on this board.
}

// ST7789 over SPI has no flush-region alignment requirement.
void display_hal_round_area(int32_t* x1, int32_t* y1, int32_t* x2, int32_t* y2) {
    (void)x1; (void)y1; (void)x2; (void)y2;
}
