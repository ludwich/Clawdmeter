#pragma once
#include <stdbool.h>
#include "data.h"

// Pages the PWR-role button cycles through on boards whose panel can turn
// into landscape at runtime (display_hal_set_landscape):
//
//   meters  the native-orientation dashboard with the corner Clawd (ui.h)
//   clawd   landscape: Clawd alone, playing outside; in the office at his
//           laptop while Claude works, lit by the local time (stage.h, scene.h)
//   usage   landscape: just the two usage bars, large
//
// A board whose native orientation already is landscape (BoardCaps wider
// than tall, display_hal_set_landscape a no-op returning true) keeps one
// orientation throughout: nothing turns between pages.
//
// Elsewhere the meters are the only page and nothing here changes anything.
// While another page is up the director is offstage (clawd_director.h); an
// alert shows on the Clawd page (pages_on_alert).

enum Page { PAGE_METERS, PAGE_CLAWD, PAGE_USAGE, PAGE_COUNT };

void pages_init(void);          // after ui_init() and director_init()
bool pages_enabled(void);
Page pages_current(void);
void pages_show(Page p);
// The button's next page. From the meters with Clawd's face up (or anything
// but the meters room) it's "show me the meters" instead.
void pages_next(void);
void pages_update(const UsageData* d);   // every parsed payload, after ui_update()
// An alert arrived: Clawd yells for you from his page; from the usage page he
// takes the panel to do it; on the meters the director's attention plays.
void pages_on_alert(void);
// QA (`mood <n>` serial command): force Clawd's moods on his page, bit 1 =
// angry, bit 2 = yelling; 0 = back to the real cues.
void pages_debug_mood(int bits);
// QA (`tod <minute>`): the Clawd page's local time, 0..1439; -1 = the real
// clock (the app's `t`). Returns the minute that took effect.
int pages_debug_tod(int minute);
// QA (`work <0|1|-1>`): force Claude working (1) or not (0) on the Clawd
// page; -1 = the real cue. Returns the value that took effect.
int pages_debug_work(int working);
void pages_tick(void);
