#pragma once
#include <stdbool.h>
#include <lvgl.h>

struct Burndown;

// The Clawd page (pages.h): Clawd alone on a landscape panel, large, playing.
// He walks or scuttles about, waves, dances, jumps and plays with his ball or
// skateboard, all from the official animations; while Claude is working he
// sits at his laptop and types until the work stops.
//
// He's somewhere you can tell apart at a glance (scene.h): in the office
// while Claude works, outside while he plays, both lit by the local time of
// day. Work starting brings the blinds down outside and up again on him at
// his desk, laptop opening; when the work stops he finishes typing, closes
// the laptop, stands up, and the blinds take him back outside, where he
// jumps for joy. Each scene holds at least SCENE_DWELL_MS (3 s), so a flicker
// of work doesn't flap between them.
//
// Moods are drawn around him, never into the sprite (the frames are the
// official art), so they ride on whatever he's doing:
//   alert  a shaking speech bubble by his head with the alert text and sound
//          waves: he's yelling for you (a press dismisses the alert)
//   angry  an anger mark popping by his head and steam rising: usage is
//          burning fast or the session is nearly spent
//
// He is the only Clawd on screen: the page is its own LVGL screen, so the
// meters (and the corner Clawd on them) aren't drawn while it's up. The
// sprite buffer (~60 KB) is allocated on open and freed on close, so it
// costs nothing while the meters are shown; the scenery is const data in
// flash drawn by one LVGL object.

struct StageCues {
    bool        working;      // Claude is working (director_claude_working)
    bool        alert;        // an alert is up (director_alert_active)
    const char* alert_text;   // its message
    bool        angry;        // usage burning (pages.cpp decides)
    int         minute_of_day; // local wall-clock minute 0..1439, -1 unknown
    // The office wall (scene_set_live): the LED countdown and the weekly poster.
    int             session_left_mins; // until the 5-hour window resets, -1 unknown / none
    const Burndown* week;              // this week's history (burndown.h), NULL = none
    int             week_used;         // weekly % used now (`w`, 0..100), -1 unknown / none
    int             week_left_mins;    // until the weekly reset (`wr` run on), -1 unknown
};

// Build the sprite on `scr` (w x h px), already in the scene `cues` call for
// (NULL: picked on the first tick). False when the buffer won't fit.
bool stage_open(lv_obj_t* scr, int w, int h, const StageCues* cues);
void stage_close(void);
void stage_tick(const StageCues* cues);   // per loop while open
