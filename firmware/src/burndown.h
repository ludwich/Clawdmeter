#pragma once
#include <stdint.h>

// The weekly poster in the office (scene.h): this week's usage so far, how
// it compares with an even pace, and where the average rate is heading.
// Pure: no Arduino, no LVGL, so it runs in the native unit tests
// (test/test_burndown); pages.cpp feeds the history and keeps it in NVS,
// scene.cpp draws from it.
//
// The history is how much of the 7-day limit was used at each hour of the
// current weekly window, recorded on the device from the payloads it sees.
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

// ─── Pace, verdict and projection ────────────────────────────────────────────
// `used` = the weekly % used now (`w`, rounded, 0..100), `elapsed` = minutes
// into the window (BURNDOWN_WEEK_MINS - `wr`, clamped 0..WEEK_MINS). Even
// pace is the elapsed share of the week; the average rate is used / elapsed,
// and at that rate the window reaches 100 % at elapsed * 100 / used minutes.
#define BURNDOWN_GRACE_PCT      1            // within a point of pace is on pace (pace.h)
#define BURNDOWN_RED_PCT        90           // the number goes red from here
#define BURNDOWN_OUT_MARGIN     (24 * 60)    // red: the rate runs out a day or more before the reset
#define BURNDOWN_PROJECT_MINS   (12 * 60)    // no projection in the window's first 12 hours

enum WeekLevel {
    WEEK_UNKNOWN,   // no `w` / `wr`, or spend-capped: "--%"
    WEEK_ON,        // at or behind pace: green
    WEEK_AHEAD,     // ahead of pace: amber
    WEEK_OUT,       // >= 90 %, at the limit, or out a day or more early: red
};

struct WeekPace {
    uint8_t level;      // WeekLevel
    int16_t used;       // % used 0..100, -1 unknown
    int16_t elapsed;    // minutes into the window 0..WEEK_MINS, -1 unknown
    int16_t delta;      // used minus even pace, rounded points (+ = ahead)
    int16_t out_in;     // minutes until the average rate reaches 100 %: 0 = at
                        // the limit, -1 = not before the reset, or not projected
};

// Minutes into the window for `wr` minutes left, -1 when unknown.
int  burndown_elapsed(int weekly_reset_mins);
// used_pct < 0 or weekly_reset_mins < 0: unknown.
void burndown_pace(WeekPace* p, int used_pct, int weekly_reset_mins);
// The verdict beside the number, two short lines (each up to 7 chars + NUL;
// line 1 is drawn in the level's colour): "+6"/"OVER", "-4"/"UNDER",
// "ON"/"PACE" against the even pace; "EMPTY"/"IN 2D" (or "IN 9H", "IN <1H")
// when red and the average rate runs out before the reset; "EMPTY"/"NOW" at
// the limit; both empty when unknown.
void burndown_verdict(const WeekPace* p, char line1[8], char line2[8]);
// The % used `t` minutes into the window, for 0 <= t <= p->elapsed: straight
// lines between real points only (0 % at the window's start, each recorded
// hour slot before the current one at its half hour, and `used` now). `b`
// may be NULL (no history: one straight line). -1 when unknown or t is
// outside 0..elapsed.
int  burndown_used_at(const Burndown* b, const WeekPace* p, int t);
// The average rate's % used at `t` minutes (t >= elapsed), clipped at 100;
// -1 when there is no projection (unknown, or the window's first hours).
int  burndown_projected_at(const WeekPace* p, int t);
