#include "scene.h"
#include "field.h"
#include "theme.h"
#include "burndown.h"
#include "scene_office.h"
#include "scene_outdoor.h"

#define SCENE_MAX_GROUPS 48      // tools/scenes/scenekit.py MAX_GROUPS
#define ANIM_MS          100     // drift and frames step at most this often
#define UNKNOWN_MINUTE   (10 * 60 + 10)

// The wipe: SLATS horizontal blinds close in a cascade, hold, then open.
#define SLATS       8
#define CLOSE_MS    320
#define HOLD_MS     140
#define OPEN_MS     320
#define STAGGER_MS  22           // between neighbouring slats
#define WIPE_STEP_MS 20
#define EDGE        2            // the accent line on a slat's moving edge, px

// sin(i * 6 deg) * 64; scenekit.py HAND_SIN is the same table.
static const int8_t HAND_SIN[60] = {
    0, 7, 13, 20, 26, 32, 38, 43, 48, 52, 55, 58, 61, 63, 64, 64, 64, 63, 61, 58,
    55, 52, 48, 43, 38, 32, 26, 20, 13, 7, 0, -7, -13, -20, -26, -32, -38, -43, -48, -52,
    -55, -58, -61, -63, -64, -64, -64, -63, -61, -58, -55, -52, -48, -43, -38, -32, -26, -20, -13, -7,
};

// 7-segment digits, bits a..g = 0..6 (a top, b top-right, c bottom-right,
// d bottom, e bottom-left, f top-left, g middle); 10 is "-". scenekit.py SEG7.
static const uint8_t SEG7[11] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x40 };
#define SEG_DASH  10
#define COLON_MS  500            // the countdown's colon: on, off, each half second

// "H:MM": the three digits' x and the colon's, a stroke-wide gap between
// everything (scenekit.py digits_layout).
static void digits_layout(int x, int dw, int t, int xs[3], int* xc) {
    *xc = x + dw + t;
    xs[0] = x;
    xs[1] = *xc + 2 * t;
    xs[2] = xs[1] + dw + t;
}

static_assert(sizeof(scene_office_groups) / sizeof(scene_office_groups[0]) <= SCENE_MAX_GROUPS,
              "office: too many groups");
static_assert(sizeof(scene_outdoor_groups) / sizeof(scene_outdoor_groups[0]) <= SCENE_MAX_GROUPS,
              "outdoor: too many groups");

enum Wipe { W_NONE, W_CLOSE, W_HOLD, W_OPEN };

static lv_obj_t*       base = NULL;     // the scenery, behind everything
static lv_obj_t*       cover = NULL;    // the wipe's slats, in front of everything
static int             scr_w = SCENE_W, scr_h = SCENE_H;
static const SceneDef* cur = NULL;
static SceneId         shown = SCENE_NONE, target = SCENE_NONE;
static uint8_t         tod_bit = SCENE_TOD_DAY;
static uint8_t         tod_idx = 0;     // palette: 0 day, 1 dusk, 2 night
static int             minute = UNKNOWN_MINUTE;
static int16_t         drift_dx[SCENE_MAX_GROUPS];
static uint8_t         frame_on[SCENE_MAX_GROUPS];
static uint8_t         group_tod[SCENE_MAX_GROUPS];   // when any of its rects shows
static uint32_t        anim_at = 0;
static uint32_t        now_ms = 0;      // scene_tick's clock
static SceneCoveredCb  covered_cb = NULL;
// The live wall (scene_set_live).
static int             countdown = -1;  // minutes left in the 5-hour window, -1 unknown
static bool            colon_on = true;
static const Burndown* week = NULL;
static int             week_now = -1;
static uint16_t        week_rev = 0;

static Wipe     wipe = W_NONE;
static bool     wipe_up = false;        // cascade from the bottom (leaving the office)
static uint32_t wipe_t0 = 0, wipe_step_at = 0;
static int16_t  slat_a[SLATS], slat_b[SLATS];   // covered rows [a, b) of each slat

static const SceneDef* def_of(SceneId id) {
    if (id == SCENE_OFFICE)  return &scene_office;
    if (id == SCENE_OUTDOOR) return &scene_outdoor;
    return NULL;
}

// ─── Geometry ────────────────────────────────────────────────────────────────
// Scene px to screen px (identity on the 320x172 panel the art is made for).
static int32_t sx(int32_t x) { return scr_w == SCENE_W ? x : x * scr_w / SCENE_W; }
static int32_t sy(int32_t y) { return scr_h == SCENE_H ? y : y * scr_h / SCENE_H; }

static bool to_area(int32_t x, int32_t y, int32_t w, int32_t h, lv_area_t* a) {
    a->x1 = sx(x);
    a->y1 = sy(y);
    a->x2 = sx(x + w) - 1;
    a->y2 = sy(y + h) - 1;
    return a->x2 >= a->x1 && a->y2 >= a->y1;
}

static void invalidate(lv_obj_t* obj, int32_t x, int32_t y, int32_t w, int32_t h) {
    lv_area_t a;
    if (obj && to_area(x, y, w, h, &a)) lv_obj_invalidate_area(obj, &a);
}

static void fill(lv_layer_t* layer, const lv_area_t* clip, lv_draw_fill_dsc_t* d, const lv_area_t* a) {
    if (a->x2 < clip->x1 || a->x1 > clip->x2 || a->y2 < clip->y1 || a->y1 > clip->y2) return;
    lv_draw_fill(layer, d, a);
}

// ─── Groups ──────────────────────────────────────────────────────────────────
// Where a drifting group sits relative to where it was authored: its left
// edge travels at p[0] px per 10 s and wraps within [p[1], p[2]).
static int16_t drift_of(const SceneGroup* g, uint32_t now) {
    const int32_t x0 = g->p[1], span = (int32_t)g->p[2] - g->p[1];
    if (span <= 0) return 0;
    const int64_t travel = (int64_t)now * g->p[0] / 10000;
    int32_t pos = (int32_t)(((int64_t)(g->box[0] - x0) + travel) % span);
    if (pos < 0) pos += span;
    return (int16_t)(x0 + pos - g->box[0]);
}

static uint8_t frame_of(const SceneGroup* g, uint32_t now) {
    if (!g->n || !g->p[0]) return 0;
    return (((now + (uint32_t)g->p[1]) / (uint32_t)g->p[0]) % g->n) == g->k;
}

static bool group_shows(int i) { return (group_tod[i] & tod_bit) != 0; }

static void invalidate_box(const SceneGroup* g, int dx) {
    invalidate(base, g->box[0] + dx, g->box[1], g->box[2], g->box[3]);
}

// Redraw the live groups of a type in the scene showing (just the colon,
// for the countdown's blink).
static void invalidate_live(uint8_t type, bool colon_only) {
    for (int i = 1; cur && i < cur->group_count; i++) {
        const SceneGroup* g = &cur->groups[i];
        if (g->type != type || !group_shows(i)) continue;
        if (colon_only) {
            int xs[3], xc;
            digits_layout(g->box[0], g->p[0], g->p[2], xs, &xc);
            invalidate(base, xc, g->box[1], g->p[2], g->box[3]);
        } else {
            invalidate_box(g, 0);
        }
    }
}

// Step every animated group to `now`; invalidate what moved or blinked.
static void animate(uint32_t now, bool redraw) {
    if (!cur) return;
    for (int i = 1; i < cur->group_count; i++) {
        const SceneGroup* g = &cur->groups[i];
        if (g->type == SG_DRIFT) {
            const int16_t dx = drift_of(g, now);
            if (dx == drift_dx[i]) continue;
            if (redraw && group_shows(i)) {
                invalidate_box(g, drift_dx[i]);
                invalidate_box(g, dx);
            }
            drift_dx[i] = dx;
        } else if (g->type == SG_FRAME) {
            const uint8_t on = frame_of(g, now);
            if (on == frame_on[i]) continue;
            if (redraw && group_shows(i)) invalidate_box(g, 0);
            frame_on[i] = on;
        }
    }
}

// A new scene underneath: its groups' state from scratch, nothing drawn yet.
static void load(const SceneDef* d, uint32_t now) {
    cur = d;
    if (!d) return;
    for (int i = 0; i < SCENE_MAX_GROUPS; i++) {
        drift_dx[i] = 0;
        frame_on[i] = 0;
        group_tod[i] = 0;
    }
    for (uint16_t i = 0; i < d->rect_count; i++) group_tod[d->rects[i].g] |= d->rects[i].tod;
    for (int i = 1; i < d->group_count; i++) {
        drift_dx[i] = 0x7FFF;      // force the first step
        frame_on[i] = 0xFF;
    }
    animate(now, false);
}

// ─── Drawing ─────────────────────────────────────────────────────────────────
// The clock's hands: 2x2 blocks out from the centre, hour then minute.
static void draw_hands(lv_layer_t* layer, const lv_area_t* clip, lv_draw_fill_dsc_t* d,
                       const SceneGroup* g) {
    const int cx = g->p[0], cy = g->p[1], r = g->p[2];
    const int hh = (minute / 60) % 12, mm = minute % 60;
    const int angle[2] = { hh * 5 + mm / 12, mm };
    const int len[2] = { r * 55 / 100, r - 3 };
    for (int k = 0; k < 2; k++) {
        const int a = angle[k] % 60;
        for (int i = 0; i <= len[k]; i++) {
            const int px = cx + HAND_SIN[a] * i / 64;
            const int py = cy - HAND_SIN[(a + 15) % 60] * i / 64;
            lv_area_t ar;
            if (to_area(px - 1, py - 1, 2, 2, &ar)) fill(layer, clip, d, &ar);
        }
    }
}

static void fill_at(lv_layer_t* layer, const lv_area_t* clip, lv_draw_fill_dsc_t* d,
                    int x, int y, int w, int h) {
    lv_area_t a;
    if (to_area(x, y, w, h, &a)) fill(layer, clip, d, &a);
}

// The LED countdown: lit segments for H:MM ("-:--" when unknown) and the
// colon, which blinks while it counts.
static void draw_digits(lv_layer_t* layer, const lv_area_t* clip, lv_draw_fill_dsc_t* d,
                        const SceneGroup* g) {
    const int x = g->box[0], y = g->box[1];
    const int dw = g->p[0], dh = g->p[1], t = g->p[2], mid = (dh - t) / 2;
    int xs[3], xc;
    digits_layout(x, dw, t, xs, &xc);
    uint8_t gl[3] = { SEG_DASH, SEG_DASH, SEG_DASH };
    if (countdown >= 0) {
        gl[0] = (uint8_t)LV_MIN(9, countdown / 60);
        gl[1] = (uint8_t)(countdown % 60 / 10);
        gl[2] = (uint8_t)(countdown % 10);
    }
    for (int k = 0; k < 3; k++) {
        const uint8_t m = SEG7[gl[k]];
        const int x0 = xs[k];
        if (m & 0x01) fill_at(layer, clip, d, x0, y, dw, t);                       // a
        if (m & 0x02) fill_at(layer, clip, d, x0 + dw - t, y, t, mid + t);         // b
        if (m & 0x04) fill_at(layer, clip, d, x0 + dw - t, y + mid, t, dh - mid);  // c
        if (m & 0x08) fill_at(layer, clip, d, x0, y + dh - t, dw, t);              // d
        if (m & 0x10) fill_at(layer, clip, d, x0, y + mid, t, dh - mid);           // e
        if (m & 0x20) fill_at(layer, clip, d, x0, y, t, mid + t);                  // f
        if (m & 0x40) fill_at(layer, clip, d, x0, y + mid, dw, t);                 // g
    }
    if (countdown < 0 || colon_on) {
        fill_at(layer, clip, d, xc, y + mid - t, t, t);
        fill_at(layer, clip, d, xc, y + mid + t, t, t);
    }
}

// The weekly burndown: a dashed "now" line, then per column up to now the
// weekly usage left as a filled bar whose top cell is the line colour. A
// column without data is a dither at the last known level in the no-data
// colour (nothing before the first sample).
static void draw_chart(lv_layer_t* layer, const lv_area_t* clip, lv_draw_fill_dsc_t* d,
                       const SceneGroup* g, const uint32_t* pal, uint8_t line_c) {
    if (!week || week_now < 0) return;
    const int x = g->box[0], y = g->box[1], w = g->box[2], h = g->box[3];
    const int cell = (g->p[0] & 0xFF) ? (g->p[0] & 0xFF) : 2;
    const int ghost = ((uint16_t)g->p[0]) >> 8;    // no-data colour + 1, 0 = none
    const int cols = w / cell, rows = h / cell;
    if (cols <= 0 || rows <= 0) return;
    const int nc = LV_MIN(cols - 1, week_now * cols / BURNDOWN_SLOTS);
    d->color = lv_color_hex(pal[(uint8_t)g->p[2]]);
    for (int r = 0; r < rows; r += 2) fill_at(layer, clip, d, x + nc * cell, y + r * cell, cell, cell);
    const lv_color_t area = lv_color_hex(pal[(uint8_t)g->p[1]]);
    const lv_color_t line = lv_color_hex(pal[line_c]);
    int last_hc = 0;                          // the last known height, 0 = none yet
    for (int c = 0; c <= nc; c++) {
        const int used = burndown_column(week, c, cols, week_now);
        if (used < 0) {
            if (ghost && last_hc > 0) {       // carried forward, dithered
                d->color = lv_color_hex(pal[ghost - 1]);
                for (int r = rows - last_hc; r < rows; r += 2)
                    fill_at(layer, clip, d, x + c * cell, y + r * cell, cell, cell);
            }
            continue;
        }
        const int left = 100 - used;
        int hc = (left * rows + 50) / 100;
        if (left > 0 && hc == 0) hc = 1;      // a sliver left still shows
        last_hc = hc;
        if (hc <= 0) continue;
        const int top = y + h - hc * cell;
        d->color = line;
        fill_at(layer, clip, d, x + c * cell, top, cell, cell);
        if (hc > 1) {
            d->color = area;
            fill_at(layer, clip, d, x + c * cell, top + cell, cell, (hc - 1) * cell);
        }
    }
}

static void draw_cb(lv_event_t* e) {
    if (!cur) return;
    lv_layer_t* layer = lv_event_get_layer(e);
    const lv_area_t clip = layer->_clip_area;
    const uint32_t* pal = cur->palette[tod_idx];
    lv_draw_fill_dsc_t d;
    lv_draw_fill_dsc_init(&d);
    d.opa = LV_OPA_COVER;
    d.radius = 0;
    for (uint16_t i = 0; i < cur->rect_count; i++) {
        const SceneRect& r = cur->rects[i];
        if (!(r.tod & tod_bit)) continue;
        int dx = 0;
        if (r.g) {
            const SceneGroup* g = &cur->groups[r.g];
            if (g->type == SG_FRAME && !frame_on[r.g]) continue;
            if (g->type == SG_DRIFT) dx = drift_dx[r.g];
            if (g->type == SG_CLOCK) {
                d.color = lv_color_hex(pal[r.c]);
                draw_hands(layer, &clip, &d, g);
                continue;
            }
            if (g->type == SG_DIGITS) {
                d.color = lv_color_hex(pal[r.c]);
                draw_digits(layer, &clip, &d, g);
                continue;
            }
            if (g->type == SG_CHART) {
                draw_chart(layer, &clip, &d, g, pal, r.c);
                continue;
            }
        }
        lv_area_t a;
        if (!to_area(r.x + dx, r.y, r.w, r.h, &a)) continue;
        if (a.x2 < clip.x1 || a.x1 > clip.x2 || a.y2 < clip.y1 || a.y1 > clip.y2) continue;
        d.color = lv_color_hex(pal[r.c]);
        lv_draw_fill(layer, &d, &a);
    }
}

// ─── The wipe ────────────────────────────────────────────────────────────────
static int slat_h(void) { return (scr_h + SLATS - 1) / SLATS; }

// Ease in and out, per mille.
static int32_t ease(int32_t u) {
    if (u <= 0) return 0;
    if (u >= 1000) return 1000;
    if (u < 500) return 2 * u * u / 1000;
    const int32_t v = 1000 - u;
    return 1000 - 2 * v * v / 1000;
}

// How far slat i has moved (per mille) `t` ms into a phase of `dur` ms.
static int32_t slat_progress(int i, uint32_t t, uint32_t dur) {
    const int order = wipe_up ? SLATS - 1 - i : i;
    const int32_t each = (int32_t)dur - (SLATS - 1) * STAGGER_MS;
    return ease(((int32_t)t - order * STAGGER_MS) * 1000 / each);
}

static void cover_draw_cb(lv_event_t* e) {
    lv_layer_t* layer = lv_event_get_layer(e);
    const lv_area_t clip = layer->_clip_area;
    lv_draw_fill_dsc_t d;
    lv_draw_fill_dsc_init(&d);
    d.opa = LV_OPA_COVER;
    d.radius = 0;
    const int sh = slat_h();
    for (int i = 0; i < SLATS; i++) {
        const int top = i * sh, bottom = LV_MIN(top + sh, scr_h);
        const int a = slat_a[i], b = slat_b[i];
        if (b <= a) continue;
        lv_area_t r = { 0, a, scr_w - 1, b - 1 };
        d.color = THEME_PANEL;
        fill(layer, &clip, &d, &r);
        // The slat's lip: a lighter line along its bottom.
        r.y1 = LV_MAX(a, bottom - 3);
        r.y2 = LV_MIN(b, bottom - 1) - 1;
        if (r.y2 >= r.y1) {
            d.color = THEME_BAR_BG;
            fill(layer, &clip, &d, &r);
        }
        // The moving edge, while the slat is part-way.
        if (b - a < bottom - top) {
            if (wipe == W_CLOSE) { r.y1 = LV_MAX(a, b - EDGE); r.y2 = b - 1; }
            else                 { r.y1 = a; r.y2 = LV_MIN(b, a + EDGE) - 1; }
            d.color = THEME_ACCENT;
            fill(layer, &clip, &d, &r);
        }
    }
}

static void inv_rows(int y1, int y2) {
    if (y1 < 0) y1 = 0;
    if (y2 > scr_h - 1) y2 = scr_h - 1;
    if (!cover || y2 < y1) return;
    lv_area_t a = { 0, y1, scr_w - 1, y2 };
    lv_obj_invalidate_area(cover, &a);
}

// Move every slat to where it is at time t of the wipe; invalidate only the
// rows whose colour changed (each moving end, with its accent edge).
static void slats_to(uint32_t t) {
    const int sh = slat_h();
    for (int i = 0; i < SLATS; i++) {
        const int top = i * sh, bottom = LV_MIN(top + sh, scr_h), h = bottom - top;
        int a = top, b = bottom;
        if (wipe == W_CLOSE)     b = top + h * slat_progress(i, t, CLOSE_MS) / 1000;
        else if (wipe == W_OPEN) a = top + h * slat_progress(i, t - CLOSE_MS - HOLD_MS, OPEN_MS) / 1000;
        if (a != slat_a[i]) inv_rows(LV_MIN(a, slat_a[i]) - EDGE, LV_MAX(a, slat_a[i]) + EDGE - 1);
        if (b != slat_b[i]) inv_rows(LV_MIN(b, slat_b[i]) - EDGE, LV_MAX(b, slat_b[i]) + EDGE - 1);
        slat_a[i] = a;
        slat_b[i] = b;
    }
}

static void make_cover(void* scr) {
    cover = lv_obj_create((lv_obj_t*)scr);
    lv_obj_remove_style_all(cover);
    lv_obj_set_pos(cover, 0, 0);
    lv_obj_set_size(cover, scr_w, scr_h);
    lv_obj_clear_flag(cover, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cover, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(cover, cover_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
}

static void drop_cover(void*) {
    lv_obj_delete(cover);
    cover = NULL;
}

// The scene underneath changes now; nothing is redrawn (it's covered, or
// the caller invalidates).
static void swap(uint32_t now) {
    shown = target;
    load(def_of(shown), now);
    if (covered_cb) covered_cb(shown);
}

static void wipe_start(uint32_t now) {
    // Leaving for the office the blinds come down from the top; heading out
    // they go up from the bottom.
    wipe_up = target != SCENE_OFFICE;
    for (int i = 0; i < SLATS; i++) { slat_a[i] = i * slat_h(); slat_b[i] = slat_a[i]; }
    // A new full-screen object would redraw the whole screen; it draws
    // nothing yet, so create it with invalidation paused (field.h).
    field_quietly(base, make_cover, lv_obj_get_parent(base));
    wipe = W_CLOSE;
    wipe_t0 = now;
    wipe_step_at = 0;
}

static void wipe_tick(uint32_t now) {
    if (wipe == W_NONE || now - wipe_step_at < WIPE_STEP_MS) return;
    wipe_step_at = now;
    const uint32_t t = now - wipe_t0;
    if (wipe == W_CLOSE && t >= CLOSE_MS) {
        wipe = W_HOLD;
        slats_to(t);
        swap(now);
        return;
    }
    if (wipe == W_HOLD && t >= CLOSE_MS + HOLD_MS) {
        if (target != shown) swap(now);    // changed its mind while covered
        wipe = W_OPEN;
    }
    if (wipe == W_OPEN && t >= CLOSE_MS + HOLD_MS + OPEN_MS) {
        slats_to(CLOSE_MS + HOLD_MS + OPEN_MS);
        field_quietly(base, drop_cover, NULL);   // it draws nothing now
        wipe = W_NONE;
        if (target != shown) wipe_start(now);   // asked for another scene meanwhile
        return;
    }
    slats_to(t);
}

// ─── Public ──────────────────────────────────────────────────────────────────
static uint8_t tod_of(int m) {
    if (m < 0) return SCENE_TOD_DAY;
    m %= 1440;
    if (m < 6 * 60 || m >= 21 * 60) return SCENE_TOD_NIGHT;
    if (m < 8 * 60 || m >= 18 * 60) return SCENE_TOD_DUSK;
    return SCENE_TOD_DAY;
}

bool scene_open(lv_obj_t* scr, int w, int h) {
    scene_close();
    scr_w = w;
    scr_h = h;
    base = lv_obj_create(scr);
    if (!base) return false;
    lv_obj_remove_style_all(base);
    lv_obj_set_pos(base, 0, 0);
    lv_obj_set_size(base, w, h);
    lv_obj_clear_flag(base, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(base, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(base, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    cur = NULL;
    shown = target = SCENE_NONE;
    wipe = W_NONE;
    tod_bit = SCENE_TOD_DAY;
    tod_idx = 0;
    minute = UNKNOWN_MINUTE;
    return true;
}

void scene_close(void) {
    if (cover) { lv_obj_delete(cover); cover = NULL; }
    if (base)  { lv_obj_delete(base);  base = NULL; }
    cur = NULL;
    shown = target = SCENE_NONE;
    wipe = W_NONE;
}

void scene_on_covered(SceneCoveredCb cb) { covered_cb = cb; }

void scene_set(SceneId id, bool animate_it) {
    if (!base) return;
    if (!animate_it) {
        if (cover) { lv_obj_delete(cover); cover = NULL; }
        wipe = W_NONE;
        target = id;
        swap(now_ms);
        lv_obj_invalidate(base);
        return;
    }
    if (id == target) return;
    target = id;
    if (wipe == W_NONE) wipe_start(now_ms);
}

SceneId scene_target(void) { return target; }
bool scene_busy(void) { return wipe != W_NONE; }
bool scene_transition_covered(void) { return wipe == W_HOLD; }

void scene_tick(uint32_t now, int minute_of_day) {
    if (!base) return;
    now_ms = now;
    const uint8_t tb = tod_of(minute_of_day);
    if (tb != tod_bit) {    // dawn, dusk, nightfall: everything re-colours
        tod_bit = tb;
        tod_idx = tb == SCENE_TOD_NIGHT ? 2 : tb == SCENE_TOD_DUSK ? 1 : 0;
        lv_obj_invalidate(base);
    }
    const int m = minute_of_day >= 0 ? minute_of_day % 1440 : UNKNOWN_MINUTE;
    if (m != minute) {      // the clocks' hands move once a minute
        minute = m;
        for (int i = 1; cur && i < cur->group_count; i++)
            if (cur->groups[i].type == SG_CLOCK && group_shows(i)) invalidate_box(&cur->groups[i], 0);
    }
    const bool colon = (now / COLON_MS) % 2 == 0;
    if (colon != colon_on) {     // only the colon's two dots redraw
        colon_on = colon;
        if (countdown >= 0) invalidate_live(SG_DIGITS, true);
    }
    wipe_tick(now);
    if (now - anim_at >= ANIM_MS) {
        anim_at = now;
        animate(now, wipe != W_HOLD);
    }
}

void scene_set_live(int countdown_mins, const Burndown* wk, int now_slot) {
    if (countdown_mins < 0) countdown_mins = -1;
    if (countdown_mins != countdown) {
        countdown = countdown_mins;
        invalidate_live(SG_DIGITS, false);
    }
    if (!wk || now_slot < 0 || now_slot >= BURNDOWN_SLOTS) now_slot = -1;
    const uint16_t rev = wk ? wk->rev : 0;
    if (wk != week || now_slot != week_now || rev != week_rev) {
        week = wk;
        week_now = now_slot;
        week_rev = rev;
        invalidate_live(SG_CHART, false);
    }
}

int scene_anchor_desk_x(void) {
    const int16_t* a = scene_office.anchors[SCENE_ANCHOR_DESK];
    return a[0] < 0 ? -1 : (int)sx(a[0]);
}

bool scene_anchor(SceneAnchorId id, lv_area_t* out) {
    if (!cur || id >= SCENE_ANCHOR_COUNT) return false;
    const int16_t* a = cur->anchors[id];
    if (a[0] < 0) return false;
    return to_area(a[0], a[1], a[2] ? a[2] : 1, a[3] ? a[3] : 1, out);
}
