#pragma once
// The meters' pieces the extra landscape pages share (pages.cpp only; kept
// out of ui.h so the host tests that include ui.h need no lv_color_t).
#include <lvgl.h>
#include <stddef.h>

// The meters' LVGL screen (the extra pages load their own, pages.h).
lv_obj_t* ui_screen(void);
// The meters' bar colour for a window (usage %, pace marker setting) and
// its "Resets in ..." line, shared with the usage page.
lv_color_t ui_bar_color(float used_pct, int reset_mins, int window_mins);
void ui_format_reset(int mins, char* buf, size_t len, const char* none);
