#include "hal/display_hal.h"

// Weak default: boards that don't rotate their panel at runtime have the
// meters as their only page (pages.h).
__attribute__((weak)) bool display_hal_set_landscape(bool on) { (void)on; return false; }
