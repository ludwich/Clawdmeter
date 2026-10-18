#pragma once
#include <stdbool.h>
#include <lvgl.h>

// The Clawd page (pages.h): Clawd alone on a landscape panel, large, playing.
// He walks or scuttles about, waves, dances, jumps and plays with his ball or
// skateboard, all from the official animations; while Claude is working he
// sits at his laptop and types until the work stops.
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
// costs nothing while the meters are shown.

struct StageCues {
    bool        working;      // Claude is working (director_claude_working)
    bool        alert;        // an alert is up (director_alert_active)
    const char* alert_text;   // its message
    bool        angry;        // usage burning (pages.cpp decides)
};

// Build the sprite on `scr` (w x h px). False when the buffer won't fit.
bool stage_open(lv_obj_t* scr, int w, int h);
void stage_close(void);
void stage_tick(const StageCues* cues);   // per loop while open
