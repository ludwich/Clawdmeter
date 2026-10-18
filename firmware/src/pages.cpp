#include "pages.h"
#include "stage.h"
#include "ui.h"
#include "pace.h"
#include "theme.h"
#include "clawd_director.h"
#include "usage_rate.h"
#include "hal/board_caps.h"
#include "hal/display_hal.h"
#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>

LV_FONT_DECLARE(font_styrene_48);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_14);

static bool      enabled = false;
static int       debug_mood = 0;
static int       debug_tod = -1;       // forced minute of day, -1 = real
static int       debug_work = -1;      // forced working, -1 = real
static long      clock_epoch = 0;      // the app's local wall-clock epoch (s), 0 = none
static uint32_t  clock_ms = 0;         // millis() when it arrived
static Page      current = PAGE_METERS;
static lv_obj_t* clawd_scr = NULL;
static lv_obj_t* usage_scr = NULL;
static int       land_w = 0, land_h = 0;   // the landscape resolution

// ─── Usage page ──────────────────────────────────────────────────────────────
// Two rows, each: label and reset line on the left, the big percentage on
// the right, a full-width bar under them.
struct UsageRow { lv_obj_t *label, *reset, *pct, *bar; };
static UsageRow   rows[2];
static lv_obj_t*  usage_box = NULL;
static lv_obj_t*  waiting = NULL;
static UsageData  last;
static bool       have_last = false;

static lv_obj_t* text(lv_obj_t* parent, const lv_font_t* f, lv_color_t c) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

static void build_usage(void) {
    usage_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(usage_scr, THEME_BG, 0);
    lv_obj_set_style_bg_opa(usage_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(usage_scr, LV_OBJ_FLAG_SCROLLABLE);

    usage_box = lv_obj_create(usage_scr);
    lv_obj_remove_style_all(usage_box);
    lv_obj_set_size(usage_box, land_w, land_h);
    const int margin = 14;
    const int row_h = (land_h - 2 * 6) / 2;
    for (int i = 0; i < 2; i++) {
        const int top = 6 + i * row_h;
        UsageRow& r = rows[i];
        r.label = text(usage_box, &font_styrene_16, THEME_TEXT);
        lv_obj_set_pos(r.label, margin, top + 6);
        r.reset = text(usage_box, &font_styrene_14, THEME_DIM);
        lv_obj_set_pos(r.reset, margin, top + 28);
        r.pct = text(usage_box, &font_styrene_48, THEME_TEXT);
        lv_obj_align(r.pct, LV_ALIGN_TOP_RIGHT, -margin, top);
        r.bar = lv_bar_create(usage_box);
        lv_obj_set_size(r.bar, land_w - 2 * margin, 12);
        lv_obj_set_pos(r.bar, margin, top + row_h - 22);
        lv_bar_set_range(r.bar, 0, 100);
        lv_obj_set_style_bg_color(r.bar, THEME_BAR_BG, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(r.bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(r.bar, 6, LV_PART_MAIN);
        lv_obj_set_style_radius(r.bar, 6, LV_PART_INDICATOR);
    }
    waiting = text(usage_scr, &font_styrene_16, THEME_DIM);
    lv_label_set_text(waiting, "Waiting for data\xE2\x80\xA6");
    lv_obj_center(waiting);
}

static void set_row(UsageRow& r, const char* label, float pct, const char* reset, lv_color_t col) {
    char buf[16];
    lv_label_set_text(r.label, label);
    lv_label_set_text(r.reset, reset);
    snprintf(buf, sizeof(buf), "%d%%", (int)(pct + 0.5f));
    lv_label_set_text(r.pct, buf);
    int v = (int)(pct + 0.5f);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    lv_bar_set_value(r.bar, v, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(r.bar, col, LV_PART_INDICATOR);
}

static void draw_usage(void) {
    if (!usage_scr) return;
    const bool show = have_last && last.ok;
    if (show) lv_obj_clear_flag(usage_box, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(usage_box, LV_OBJ_FLAG_HIDDEN);
    if (show) lv_obj_add_flag(waiting, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_clear_flag(waiting, LV_OBJ_FLAG_HIDDEN);
    if (!show) return;
    char reset[40];
    if (last.enterprise) {
        set_row(rows[0], "Spending", last.session_pct, "of spending limit",
                ui_bar_color(last.session_pct, -1, 0));
        snprintf(reset, sizeof(reset), "Resets %s", last.reset_date);
        set_row(rows[1], "Period", (float)last.time_pct, reset, THEME_DIM);
    } else {
        ui_format_reset(last.session_reset_mins, reset, sizeof(reset), "Not started");
        set_row(rows[0], "Session", last.session_pct, reset,
                ui_bar_color(last.session_pct, last.session_reset_mins, PACE_SESSION_MINS));
        ui_format_reset(last.weekly_reset_mins, reset, sizeof(reset), "---");
        set_row(rows[1], "Weekly", last.weekly_pct, reset,
                ui_bar_color(last.weekly_pct, last.weekly_reset_mins, PACE_WEEK_MINS));
    }
    // Stale numbers dim, like the meters.
    lv_obj_set_style_opa(usage_box, ui_meters_live() ? LV_OPA_COVER : LV_OPA_40, 0);
}

// ─── Pages ───────────────────────────────────────────────────────────────────
void pages_init(void) {
    const BoardCaps& caps = board_caps();
    enabled = display_hal_set_landscape(false);
    if (!enabled) return;
    land_w = caps.width > caps.height ? caps.width : caps.height;
    land_h = caps.width > caps.height ? caps.height : caps.width;
    clawd_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(clawd_scr, THEME_BG, 0);
    lv_obj_set_style_bg_opa(clawd_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(clawd_scr, LV_OBJ_FLAG_SCROLLABLE);
    build_usage();
    draw_usage();
}

bool pages_enabled(void) { return enabled; }
Page pages_current(void) { return current; }

static void clawd_cues(StageCues* c);

void pages_show(Page p) {
    if (!enabled || p == current || p >= PAGE_COUNT) return;
    const BoardCaps& caps = board_caps();
    lv_display_t* disp = lv_display_get_default();
    const Page was = current;
    current = p;
    if (was == PAGE_CLAWD) stage_close();
    if (p == PAGE_METERS) {
        display_hal_set_landscape(false);
        lv_display_set_resolution(disp, caps.width, caps.height);
        lv_screen_load(ui_screen());
        director_set_offstage(false);
    } else {
        if (was == PAGE_METERS) {
            director_set_offstage(true);
            display_hal_set_landscape(true);
            lv_display_set_resolution(disp, land_w, land_h);
        }
        if (p == PAGE_CLAWD) {
            lv_screen_load(clawd_scr);
            StageCues c;
            clawd_cues(&c);
            if (!stage_open(clawd_scr, land_w, land_h, &c)) Serial.println("pages: no room for the Clawd page");
        } else {
            draw_usage();
            lv_screen_load(usage_scr);
        }
    }
    lv_obj_invalidate(lv_screen_active());
    Serial.printf("page: %d\n", (int)p);
}

void pages_next(void) {
    if (!enabled) return;
    if (current == PAGE_METERS && director_room() != ROOM_METERS) {
        director_debug("home");   // the face is up: show me the meters first
        return;
    }
    pages_show((Page)((current + 1) % PAGE_COUNT));
}

void pages_update(const UsageData* d) {
    last = *d;
    have_last = true;
    if (d->valid && d->ok) {       // like the meters' title clock (ui.cpp)
        clock_epoch = d->clock_epoch;
        clock_ms = millis();
    }
    if (current == PAGE_USAGE) draw_usage();
}

// Angry: usage burning this hour (the Heavy mood group) or the window
// nearly spent.
#define ANGRY_PCT 80.0f
static bool usage_angry(void) {
    if (usage_rate_group() >= 3) return true;
    return have_last && last.ok && last.session_pct >= ANGRY_PCT;
}

void pages_on_alert(void) {
    if (current == PAGE_USAGE) pages_show(PAGE_CLAWD);
}

void pages_debug_mood(int bits) { debug_mood = bits; }
int pages_debug_tod(int minute) { return debug_tod = minute < 0 ? -1 : minute % 1440; }
int pages_debug_work(int working) { return debug_work = working < 0 ? -1 : (working != 0); }

// Local minute of day for the Clawd page's scenery, -1 when unknown.
static int minute_of_day(void) {
    if (debug_tod >= 0) return debug_tod;
    if (clock_epoch <= 0) return -1;
    const long s = clock_epoch + (long)((millis() - clock_ms) / 1000);
    return (int)(((s % 86400) + 86400) % 86400 / 60);
}

// What the Clawd page shows him doing, and where.
static void clawd_cues(StageCues* c) {
    c->working = debug_work >= 0 ? debug_work != 0 : director_claude_working();
    c->alert = director_alert_active() || (debug_mood & 2);
    c->alert_text = director_alert_active() ? director_alert_message() : "Claude needs you";
    c->angry = usage_angry() || (debug_mood & 1);
    c->minute_of_day = minute_of_day();
}

void pages_tick(void) {
    if (!enabled) return;
    if (current == PAGE_CLAWD) {
        StageCues c;
        clawd_cues(&c);
        stage_tick(&c);
    }
    // Freshness can lapse without a payload; keep the dimming honest.
    static uint32_t last_refresh = 0;
    if (current == PAGE_USAGE && millis() - last_refresh >= 1000) {
        last_refresh = millis();
        lv_obj_set_style_opa(usage_box, ui_meters_live() ? LV_OPA_COVER : LV_OPA_40, 0);
    }
}
