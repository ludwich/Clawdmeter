#include "../../hal/touch_hal.h"

// The C6-LCD-1.47 has no touch controller. Report "never pressed" so the
// shared input path stays inert. (The Touch-LCD-1.47 variant carries an
// AXS5106L on I2C and would need its own board folder.)

void touch_hal_init(void) {}

void touch_hal_read(uint16_t* x, uint16_t* y, bool* pressed) {
    *x = 0;
    *y = 0;
    *pressed = false;
}
