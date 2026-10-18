#include "burndown.h"
#include <string.h>

void burndown_clear(Burndown* b) {
    memset(b->used, BURNDOWN_EMPTY, sizeof(b->used));
    b->reset_min = 0;
    b->rev++;
}

int burndown_slot_of(int wr) {
    if (wr < 0) return -1;
    int elapsed = BURNDOWN_WEEK_MINS - wr;
    if (elapsed < 0) elapsed = 0;               // skew past the window: its first hour
    const int s = elapsed / 60;
    return s >= BURNDOWN_SLOTS ? BURNDOWN_SLOTS - 1 : s;
}

// The most recent value recorded in the window, EMPTY when none.
static uint8_t latest(const Burndown* b) {
    for (int s = BURNDOWN_SLOTS - 1; s >= 0; s--)
        if (b->used[s] != BURNDOWN_EMPTY) return b->used[s];
    return BURNDOWN_EMPTY;
}

int burndown_sample(Burndown* b, int32_t now_min, int wr, float pct) {
    if (!b || now_min <= 0 || wr < 0 || pct != pct) return 0;   // no clock, unknown, NaN
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    const uint8_t u = (uint8_t)(pct + 0.5f);
    const int slot = burndown_slot_of(wr);
    const int32_t reset = now_min + wr;
    int flags = 0;

    int32_t dr = reset - b->reset_min;
    if (dr < 0) dr = -dr;
    bool fresh = b->reset_min == 0 || dr > BURNDOWN_SAME_MINS;
    if (!fresh) {
        const uint8_t last = latest(b);
        fresh = last != BURNDOWN_EMPTY && (int)u + BURNDOWN_DROP_PCT <= (int)last;
    }
    if (fresh) {
        burndown_clear(b);
        flags |= BURNDOWN_CHANGED | BURNDOWN_SAVE;
    }
    b->reset_min = reset;                        // follows the app's minute-level jitter
    if (b->used[slot] != u) {
        b->used[slot] = u;
        flags |= BURNDOWN_CHANGED;
    }
    if (slot != b->saved_slot) flags |= BURNDOWN_SAVE;   // first sample this hour
    if (flags & BURNDOWN_SAVE) b->saved_slot = (int16_t)slot;
    if (flags & BURNDOWN_CHANGED) b->rev++;
    return flags;
}

void burndown_sanitize(Burndown* b) {
    b->saved_slot = -1;
    if (b->reset_min <= 0) {
        burndown_clear(b);
        return;
    }
    for (int s = 0; s < BURNDOWN_SLOTS; s++)
        if (b->used[s] > 100 && b->used[s] != BURNDOWN_EMPTY) b->used[s] = BURNDOWN_EMPTY;
}

int burndown_column(const Burndown* b, int col, int cols, int now_slot) {
    if (!b || cols <= 0 || col < 0 || col >= cols || now_slot < 0) return -1;
    const int s0 = col * BURNDOWN_SLOTS / cols;
    int s1 = (col + 1) * BURNDOWN_SLOTS / cols;  // exclusive
    if (s1 > now_slot + 1) s1 = now_slot + 1;
    for (int s = s1 - 1; s >= s0; s--)
        if (b->used[s] != BURNDOWN_EMPTY) return b->used[s];
    return -1;
}
