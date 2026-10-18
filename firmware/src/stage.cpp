#include "stage.h"
#include "scene.h"
#include "clawd_anim.h"
#include "clawd_policy.h"
#include "usage_rate.h"
#include "theme.h"
#include <Arduino.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <stdio.h>

LV_FONT_DECLARE(font_styrene_14);

// ─── What he does ────────────────────────────────────────────────────────────
// Playing on his own: picked at random (never the same twice running), with a
// still pause between that shortens as usage gets busier.
static const char* const PLAYS[] = {
    "walk", "scuttle", "waving", "dancing", "jumping happy", "pointing",
    "skateboard", "soccer", "basketball", "walk",
};
static const int PLAY_COUNT = sizeof(PLAYS) / sizeof(PLAYS[0]);
// Everything the sprite buffer must fit.
static const char* const STAGE_ANIMS[] = {
    "walking", "crab walking", "waving", "dancing", "jumping happy", "pointing",
    "laptop", "skateboard", "soccer", "basketball",
};
static const uint16_t STILL_MS_BY_RATE[4] = { 2600, 2000, 1500, 1000 };
// The official art's idle body: stage columns 15..38 (the core poses' origin
// and width). Every frame is placed so this body stays put, mirrored or not.
#define BODY_OX 15
#define BODY_W  24
#define MAX_BYTES (64 * 1024)
#define IDLE_H  18   // the idle pose's rows: where his head is
#define SCENE_DWELL_MS 3000   // least time in a scene before the next switch

enum Mode { S_STILL, S_ACT, S_WALK };

static lv_obj_t*       img = NULL;
static uint8_t*        buf = NULL;
static lv_image_dsc_t  dsc;
static int             scr_w = 0, cell = 4, feet_y = 0;
static int             x = 0;              // body's left edge, px
static int             target_x = 0;
static int             facing = +1;        // +1 right, -1 left (mirrored)
static Mode            mode = S_STILL;
static const ClawdAnim* anim = NULL;
static const ClawdAnim* idle_anim = NULL;
static const ClawdAnim* laptop = NULL;
static ClawdGait       gait = GAIT_NONE;
static uint16_t        frame = 0;
static bool            from_loop = false;
static uint32_t        frame_started = 0, still_since = 0, still_ms = 0;
static const char*     last_play = NULL;
static ClawdRng        rng;
static uint32_t        scene_since = 0;    // when the last scene switch started
static bool            wiping = false;     // a switch of ours is under way

// One frame into the planar RGB565A8 buffer, placed so the body stays at x.
static void draw(void) {
    if (!img || !anim) return;
    const ClawdAnim* a = anim;
    const bool mirror = facing < 0;
    const int w = a->w * cell, h = a->h * cell;
    uint16_t* color = (uint16_t*)buf;
    uint8_t*  alpha = buf + (size_t)w * h * 2;
    const uint8_t* src = clawd_frame(a, frame);
    for (int gy = 0; gy < a->h; gy++) {
        for (int gx = 0; gx < a->w; gx++) {
            const uint8_t code = src[gy * a->w + (mirror ? a->w - 1 - gx : gx)];
            const uint16_t col = (code && code < CLAWD_PALETTE_SIZE) ? a->palette[code] : 0;
            const uint8_t  al  = code ? 255 : 0;
            for (int dy = 0; dy < cell; dy++) {
                uint16_t* cp = &color[(gy * cell + dy) * w + gx * cell];
                uint8_t*  ap = &alpha[(gy * cell + dy) * w + gx * cell];
                for (int dx = 0; dx < cell; dx++) { cp[dx] = col; ap[dx] = al; }
            }
        }
    }
    dsc.header.w = w;
    dsc.header.h = h;
    dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc.header.stride = w * 2;
    dsc.data = buf;
    dsc.data_size = (size_t)w * h * 3;
    // Mirroring flips the art stage about the body's centre.
    const int ox = mirror ? (2 * BODY_OX + BODY_W) - (a->ox + a->w) : a->ox;
    lv_image_set_src(img, &dsc);
    lv_obj_set_pos(img, x + (ox - BODY_OX) * cell, feet_y - h);
    lv_obj_invalidate(img);
}

static void start(const ClawdAnim* a, Mode m, ClawdGait g) {
    anim = a;
    mode = m;
    gait = g;
    frame = 0;
    from_loop = false;
    frame_started = millis();
    draw();
}

static void stand_still(uint32_t now) {
    anim = idle_anim;
    frame = 0;
    mode = S_STILL;
    gait = GAIT_NONE;
    still_since = now;
    int g = usage_rate_group();
    if (g < 0 || g > 3) g = 0;
    still_ms = clawd_jitter(STILL_MS_BY_RATE[g], &rng);
    draw();
}

// A walk somewhere else on the floor, at least a body away.
static void walk(bool crab) {
    const int max_x = scr_w - BODY_W * cell;
    for (int tries = 0; tries < 4; tries++) {
        target_x = (int)(clawd_rng_next(&rng) % (uint32_t)(max_x + 1));
        if (abs(target_x - x) >= BODY_W * cell) break;
    }
    facing = target_x >= x ? +1 : -1;
    const ClawdAnim* a = clawd_anim(crab ? "crab walking" : "walking");
    start(a, S_WALK, crab ? GAIT_CRAB : GAIT_WALK);
}

static void play_next(bool working) {
    if (working && laptop) {
        facing = +1;
        start(laptop, S_ACT, GAIT_NONE);
        return;
    }
    const int pick = clawd_pick_no_repeat(PLAYS, PLAY_COUNT, last_play, &rng);
    const char* p = pick >= 0 ? PLAYS[pick] : "walk";
    last_play = p;
    if (strcmp(p, "walk") == 0)    { walk(false); return; }
    if (strcmp(p, "scuttle") == 0) { walk(true);  return; }
    const ClawdAnim* a = clawd_anim(p);
    if (!a) { walk(false); return; }
    // A prop scene wider than the floor ahead plays facing the open side.
    facing = (x + a->w * cell > scr_w) ? -1 : +1;
    start(a, S_ACT, GAIT_NONE);
}

// The blinds have the screen covered (or the page just opened): set him up
// in the scene now underneath. At the office he's at his desk, opening the
// laptop as the blinds go up; let out, he jumps for joy.
static void on_covered(SceneId shown) {
    if (!img) return;
    if (shown == SCENE_OFFICE) {
        const int dx = scene_anchor_desk_x();
        if (dx >= 0) x = dx;
        facing = +1;
        if (laptop) start(laptop, S_ACT, GAIT_NONE);
        else        stand_still(millis());
    } else if (shown == SCENE_OUTDOOR && wiping) {
        const ClawdAnim* joy = clawd_anim("jumping happy");
        if (joy) {
            facing = +1;
            last_play = "jumping happy";
            start(joy, S_ACT, GAIT_NONE);
        }
    }
    wiping = false;
}

// ─── Moods (drawn around him) ───────────────────────────────────────────────
// Pixel-style LVGL shapes in the meters' palette, placed off his head each
// step so they follow him through walks, scenes and the laptop.
#define FX_MS    70              // overlay animation step
#define PUFFS    3
#define BUBBLE_W 96

static lv_obj_t* bubble = NULL;          // the yell: bubble, its text, a stepped tail
static lv_obj_t* bubble_text = NULL;
static lv_obj_t* tail[3] = { NULL };
static lv_obj_t* waves[3] = { NULL };    // sound waves off his mouth
static lv_obj_t* vein[8] = { NULL };     // anger mark: four corner brackets
static lv_obj_t* puff[PUFFS] = { NULL }; // steam
static uint32_t  fx_at = 0;
static uint16_t  fx_step = 0;
static char      shown_text[48] = "";

static lv_obj_t* fx_rect(lv_obj_t* scr, int w, int h, lv_color_t c, int radius) {
    lv_obj_t* o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

static void fx_show(lv_obj_t* o, bool on) {
    if (!o) return;
    if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void fx_create(lv_obj_t* scr) {
    for (int i = 0; i < 3; i++) {
        waves[i] = lv_arc_create(scr);
        lv_obj_remove_style_all(waves[i]);
        const int d = 18 + i * 14;
        lv_obj_set_size(waves[i], d, d);
        lv_arc_set_bg_angles(waves[i], 0, 0);
        lv_obj_set_style_arc_width(waves[i], 3, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(waves[i], THEME_ACCENT, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(waves[i], LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(waves[i], true, LV_PART_INDICATOR);
        lv_obj_clear_flag(waves[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(waves[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 3; i++) tail[i] = fx_rect(scr, 8 - i * 2, 8 - i * 2, THEME_TEXT, 1);
    bubble = fx_rect(scr, BUBBLE_W, LV_SIZE_CONTENT, THEME_TEXT, 8);
    lv_obj_set_style_pad_all(bubble, 6, 0);
    lv_obj_set_style_border_color(bubble, THEME_ACCENT, 0);
    lv_obj_set_style_border_width(bubble, 2, 0);
    bubble_text = lv_label_create(bubble);
    lv_obj_set_width(bubble_text, BUBBLE_W - 16);
    lv_label_set_long_mode(bubble_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(bubble_text, &font_styrene_14, 0);
    lv_obj_set_style_text_color(bubble_text, THEME_BG, 0);
    for (int i = 0; i < 8; i++) vein[i] = fx_rect(scr, 4, 4, THEME_RED, 1);
    for (int i = 0; i < PUFFS; i++) puff[i] = fx_rect(scr, 8, 8, THEME_DIM, 3);
    shown_text[0] = '\0';
}

static void fx_delete(void) {
    if (bubble) { lv_obj_delete(bubble); bubble = NULL; bubble_text = NULL; }  // the text is its child
    for (lv_obj_t*& o : tail)  if (o) { lv_obj_delete(o); o = NULL; }
    for (lv_obj_t*& o : waves) if (o) { lv_obj_delete(o); o = NULL; }
    for (lv_obj_t*& o : vein)  if (o) { lv_obj_delete(o); o = NULL; }
    for (lv_obj_t*& o : puff)  if (o) { lv_obj_delete(o); o = NULL; }
}

// The anger mark: four corner brackets around (cx, cy), s px out, t px thick.
static void place_vein(int cx, int cy, int s, int t) {
    static const int8_t sx[4] = { -1, +1, -1, +1 }, sy[4] = { -1, -1, +1, +1 };
    for (int k = 0; k < 4; k++) {
        const int ex = cx + sx[k] * s, ey = cy + sy[k] * s;   // the bracket's corner
        lv_obj_t* hz = vein[k * 2];
        lv_obj_t* vt = vein[k * 2 + 1];
        lv_obj_set_size(hz, s, t);
        lv_obj_set_size(vt, t, s);
        lv_obj_set_pos(hz, sx[k] < 0 ? ex : ex - s + t, ey);
        lv_obj_set_pos(vt, ex, sy[k] < 0 ? ey : ey - s + t);
    }
}

static void fx_tick(const StageCues* c, uint32_t now) {
    if (!bubble) return;
    if (now - fx_at < FX_MS) return;
    fx_at = now;
    fx_step++;
    const int body_w = BODY_W * cell;
    const int head_y = feet_y - IDLE_H * cell;           // top of his head
    const int mouth_y = head_y + IDLE_H * cell / 3;
    const bool right = x + body_w / 2 < scr_w / 2;       // more room on his right
    const int side_x = right ? x + body_w : x;           // the edge of him facing the room

    // Yelling: the bubble shakes, the tail steps down to his mouth.
    for (lv_obj_t* o : tail) fx_show(o, c->alert);
    fx_show(bubble, c->alert);
    if (c->alert) {
        char t[48];
        snprintf(t, sizeof(t), "HEY! %s",
                 (c->alert_text && c->alert_text[0]) ? c->alert_text : "I need you");
        if (strcmp(t, shown_text) != 0) {
            strncpy(shown_text, t, sizeof(shown_text) - 1);
            lv_label_set_text(bubble_text, shown_text);
        }
        const int shake = (fx_step & 1) ? 2 : -2;
        int bx = right ? side_x + 14 : side_x - 14 - BUBBLE_W;
        if (bx < 2) bx = 2;
        if (bx > scr_w - BUBBLE_W - 2) bx = scr_w - BUBBLE_W - 2;
        int by = head_y - 34;
        if (by < 2) by = 2;
        lv_obj_set_pos(bubble, bx + shake, by);
        for (int i = 0; i < 3; i++) {
            const int tx = right ? side_x + 2 + i * 4 : side_x - 8 - i * 4;
            lv_obj_set_pos(tail[i], tx, mouth_y - 10 - i * 6);
        }
    }
    // Sound waves: rings opening away from him, one more each step.
    const int lit = c->alert ? (int)(fx_step % 4) : 0;
    for (int i = 0; i < 3; i++) {
        fx_show(waves[i], i < lit);
        if (i >= lit) continue;
        const int d = 18 + i * 14;
        lv_arc_set_angles(waves[i], right ? 300 : 120, right ? 60 : 240);
        lv_obj_set_pos(waves[i], right ? side_x - d / 2 + 4 : side_x - d / 2 - 4, mouth_y - d / 2);
    }

    // Angry: the mark throbs by his head, steam rises off it.
    for (lv_obj_t* o : vein) fx_show(o, c->angry);
    for (lv_obj_t* o : puff) fx_show(o, c->angry);
    if (c->angry) {
        const int pop = (fx_step / 4) & 1;
        const int vx = right ? x + body_w / 5 : x + body_w - body_w / 5;
        place_vein(vx, head_y - 6, pop ? 9 : 7, pop ? 4 : 3);
        for (int i = 0; i < PUFFS; i++) {
            const int ph = (fx_step + i * 6) % 18;
            const int px = x + body_w / 2 - 4 + (i - 1) * 16 + (((ph / 3) & 1) ? 2 : -2);
            lv_obj_set_pos(puff[i], px, head_y - 10 - ph * 3);
            lv_obj_set_style_bg_opa(puff[i], (lv_opa_t)(220 - ph * 12), 0);
        }
    }
}

bool stage_open(lv_obj_t* scr, int w, int h, const StageCues* cues) {
    stage_close();
    idle_anim = clawd_anim("walking");
    laptop = clawd_anim("laptop");
    if (!idle_anim) return false;
    scr_w = w;
    // Large: the tallest act (jumping happy, 30 rows) just fits above the floor.
    cell = (h - h / 16 - 6) / 30;
    if (cell < 1) cell = 1;
    size_t bytes = 0;
    for (const char* n : STAGE_ANIMS) {
        const ClawdAnim* a = clawd_anim(n);
        if (!a) continue;
        const size_t b = (size_t)(a->w * cell) * (a->h * cell) * 3;
        if (b > bytes) bytes = b;
    }
    while (bytes > MAX_BYTES && cell > 1) {    // keep the buffer within budget
        bytes = bytes * (cell - 1) * (cell - 1) / (cell * cell);
        cell--;
    }
    buf = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!buf) buf = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    Serial.printf("stage: %ux%u cell %d, %u B sprite %s, %u B free\n", w, h, cell,
                  (unsigned)bytes, buf ? "ok" : "FAILED",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    if (!buf) return false;

    feet_y = h - h / 16;
    // The scenery first, so it's behind him.
    if (!scene_open(scr, w, h)) {
        heap_caps_free(buf);
        buf = NULL;
        return false;
    }
    scene_on_covered(on_covered);
    wiping = false;

    img = lv_image_create(scr);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    fx_create(scr);
    clawd_rng_seed(&rng, millis() ^ 0x5A6E);
    x = (w - BODY_W * cell) / 2;
    facing = +1;
    last_play = NULL;
    const uint32_t now = millis();
    stand_still(now);
    // Straight into the right scene before the first frame is drawn, so
    // opening the page never shows a bare screen (on_covered sits him at his
    // desk). Without cues the first tick does it.
    if (cues) {
        scene_set_live(cues->session_left_mins, cues->week, cues->week_now_slot);
        scene_tick(now, cues->minute_of_day);
        scene_set(cues->working ? SCENE_OFFICE : SCENE_OUTDOOR, false);
        scene_since = now;
    }
    return true;
}

void stage_close(void) {
    fx_delete();
    if (img)    { lv_obj_delete(img);    img = NULL; }
    scene_close();
    if (buf)    { heap_caps_free(buf);   buf = NULL; }
    anim = NULL;
}

void stage_tick(const StageCues* cues) {
    if (!img || !anim) return;
    const bool working = cues->working;
    const uint32_t now = millis();
    scene_set_live(cues->session_left_mins, cues->week, cues->week_now_slot);
    scene_tick(now, cues->minute_of_day);
    const SceneId want = working ? SCENE_OFFICE : SCENE_OUTDOOR;
    if (scene_target() == SCENE_NONE) {
        scene_set(want, false);              // opened without cues: straight there
        scene_since = now;
    } else if (want != scene_target() && !scene_busy() && now - scene_since >= SCENE_DWELL_MS &&
               (want == SCENE_OFFICE || mode == S_STILL)) {
        // Work starts: to the office at once (on_covered sits him down).
        // Work stops: out only once he's closed the laptop and stood up.
        wiping = true;
        scene_set(want, true);
        scene_since = now;
    }
    fx_tick(cues, now);
    if (mode == S_STILL) {
        if (scene_busy()) return;            // nothing new while the blinds move
        if (scene_target() == SCENE_OFFICE) {
            // At his desk: back to the laptop the moment work resumes,
            // otherwise he waits to be let out.
            if (working) play_next(true);
            return;
        }
        if (now - still_since >= still_ms) play_next(false);
        return;
    }
    const ClawdAnim* a = anim;
    if (now - frame_started < clawd_hold(a, frame, 100)) return;
    frame_started = now;

    uint16_t next = frame + 1;
    if (mode == S_WALK && frame == a->loop_end) next = a->loop_start;      // hold the gait
    if (a == laptop && working && frame == a->loop_end) next = a->loop_start; // keep typing
    if (next >= a->frame_count) {
        stand_still(now);   // every scene ends on the idle pose
        return;
    }
    const bool was_loop = from_loop;
    frame = next;
    from_loop = frame >= a->loop_start && frame <= a->loop_end;
    if (mode == S_WALK && from_loop) {
        const int step = clawd_gait_cells(gait, frame, was_loop) * cell;
        x += facing * step;
        if ((facing > 0 && x >= target_x) || (facing < 0 && x <= target_x)) {
            x = target_x;
            stand_still(now);   // there: stop and look about
            return;
        }
    }
    draw();
}
