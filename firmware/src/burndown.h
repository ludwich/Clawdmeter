#pragma once
#include <stdint.h>

// The weekly burndown on the office poster (scene.h): how much of the 7-day
// usage limit was left at each hour of the current weekly window, recorded on
// the device from the payloads it sees. Pure: no Arduino, no LVGL, so the
// slotting runs in the native unit tests (test/test_burndown); pages.cpp
// feeds it and keeps it in NVS.
//
// The window is 168 hour slots, slot 0 starting 7 days before the reset
// (slot = (10080 - wr) / 60). Each slot holds the weekly % used last seen in
// that hour, or BURNDOWN_EMPTY when the device saw nothing then (it was off,
// or the app wasn't sending): no data is never drawn as a value.
//
// A sample's window is told apart by when it resets, on the app's wall clock
// (`t` / 60 + `wr`). A reset more than BURNDOWN_SAME_MINS away from the one
// recorded is a new window (the weekly reset moved on) and clears the
// history; so does weekly use dropping by BURNDOWN_DROP_PCT or more.

#define BURNDOWN_WEEK_MINS (7 * 24 * 60)
#define BURNDOWN_SLOTS     168
#define BURNDOWN_EMPTY     0xFF
#define BURNDOWN_SAME_MINS (6 * 60)   // reset jitter / DST shifts stay one window
#define BURNDOWN_DROP_PCT  10

struct Burndown {
    uint8_t  used[BURNDOWN_SLOTS];   // weekly % used (0..100) per hour slot, or EMPTY
    int32_t  reset_min;              // the window's reset, app wall-clock minutes; 0 = none
    int16_t  saved_slot;             // the slot when it was last saved (RAM only), -1 none
    uint16_t rev;                    // bumps on every change worth a redraw (RAM only)
};

enum { BURNDOWN_CHANGED = 1, BURNDOWN_SAVE = 2 };

void burndown_clear(Burndown* b);
// The hour slot `wr` minutes before the reset falls in, 0..167; -1 when
// unknown (wr < 0).
int  burndown_slot_of(int weekly_reset_mins);
// One real sample (ok && !replay, Pro/Max): `now_min` = the app's wall clock
// in minutes (0 = no clock: nothing is recorded, a window can't be told
// apart without it). Returns BURNDOWN_* flags: CHANGED (redraw), SAVE (write
// to NVS now: a new window, or the first sample in a new hour slot, so at
// most once an hour).
int  burndown_sample(Burndown* b, int32_t now_min, int weekly_reset_mins, float weekly_pct);
// After loading from NVS: a blob that doesn't make sense is cleared.
void burndown_sanitize(Burndown* b);
// For drawing: the weekly % used shown in column `col` of `cols` (each
// column covers SLOTS / cols slots): the latest recorded slot in the column
// at or before `now_slot`. -1 when there is none (a gap, or the future).
int  burndown_column(const Burndown* b, int col, int cols, int now_slot);
