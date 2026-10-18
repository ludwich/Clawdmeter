#include "board.h"
#include <Arduino.h>

// No I2C devices, no IO expander. Park the TF card's chip select HIGH before
// the display claims the shared SPI bus, so an inserted card never answers
// panel traffic. The backlight pin is configured in display_hal_begin().
extern "C" void board_init(void) {
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
}
