#include "burndown.h"
#include <stdio.h>
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

// a / b rounded half away from zero (b > 0).
static int32_t div_round(int32_t a, int32_t b) {
    return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
}

int burndown_elapsed(int wr) {
    if (wr < 0) return -1;
    const int e = BURNDOWN_WEEK_MINS - wr;
    return e < 0 ? 0 : e;                         // skew past the window: its start
}

void burndown_pace(WeekPace* p, int used, int wr) {
    p->level = WEEK_UNKNOWN;
    p->used = -1;
    p->elapsed = -1;
    p->delta = 0;
    p->out_in = -1;
    if (used < 0 || wr < 0) return;
    if (used > 100) used = 100;
    const int32_t e = burndown_elapsed(wr);
    p->used = (int16_t)used;
    p->elapsed = (int16_t)e;
    // used - 100 * e / WEEK, in 1/WEEK_MINS of a point.
    const int32_t ahead = (int32_t)used * BURNDOWN_WEEK_MINS - 100 * e;
    p->delta = (int16_t)div_round(ahead, BURNDOWN_WEEK_MINS);
    if (used >= 100) {
        p->level = WEEK_OUT;
        p->out_in = 0;
        return;
    }
    if (ahead <= (int32_t)BURNDOWN_GRACE_PCT * BURNDOWN_WEEK_MINS) {
        p->level = used >= BURNDOWN_RED_PCT ? WEEK_OUT : WEEK_ON;
        return;
    }
    p->level = WEEK_AHEAD;
    // Ahead means the average rate reaches 100 % before the reset; it's
    // projected once the window has run long enough to say.
    if (e >= BURNDOWN_PROJECT_MINS) {
        const int32_t t_out = e * 100 / used;     // used > 0 here
        if (t_out < BURNDOWN_WEEK_MINS) {
            p->out_in = (int16_t)(t_out - e);
            if (BURNDOWN_WEEK_MINS - t_out >= BURNDOWN_OUT_MARGIN) p->level = WEEK_OUT;
        }
    }
    if (used >= BURNDOWN_RED_PCT) p->level = WEEK_OUT;
}

void burndown_verdict(const WeekPace* p, char l1[8], char l2[8]) {
    l1[0] = l2[0] = 0;
    if (!p || p->level == WEEK_UNKNOWN) return;
    if (p->out_in == 0) {
        snprintf(l1, 8, "OUT");
        snprintf(l2, 8, "NOW");
    } else if (p->level == WEEK_OUT && p->out_in > 0) {
        snprintf(l1, 8, "OUT");
        const int m = p->out_in;
        if (m >= 24 * 60)  snprintf(l2, 8, "%dD", m / (24 * 60));
        else if (m >= 60)  snprintf(l2, 8, "%dH", m / 60);
        else               snprintf(l2, 8, "<1H");
    } else if (p->level == WEEK_AHEAD || p->delta > BURNDOWN_GRACE_PCT) {
        snprintf(l1, 8, "+%d", p->delta);
        snprintf(l2, 8, "AHEAD");
    } else if (p->delta < -BURNDOWN_GRACE_PCT) {
        snprintf(l1, 8, "%d", p->delta);
        snprintf(l2, 8, "BEHIND");
    } else {
        snprintf(l1, 8, "ON");
        snprintf(l2, 8, "PACE");
    }
}

int burndown_used_at(const Burndown* b, const WeekPace* p, int t) {
    if (!p || p->level == WEEK_UNKNOWN || t < 0 || t > p->elapsed) return -1;
    const int e = p->elapsed;
    if (t == e) return p->used;
    // Recorded slots strictly before the current hour count, each at its
    // half hour (when in the hour it was seen isn't kept).
    int lim = e / 60;
    if (lim > BURNDOWN_SLOTS) lim = BURNDOWN_SLOTS;
    int t0 = 0, v0 = 0, t1 = e, v1 = p->used;
    int k = t / 60;
    if (k >= lim) k = lim - 1;
    if (b) {
        int j = k;
        for (; j >= 0; j--) {
            if (b->used[j] == BURNDOWN_EMPTY || j * 60 + 30 > t) continue;
            t0 = j * 60 + 30;
            v0 = b->used[j];
            break;
        }
        for (int n = (j < 0 ? 0 : j + 1); n < lim; n++) {
            if (b->used[n] == BURNDOWN_EMPTY || n * 60 + 30 <= t) continue;
            t1 = n * 60 + 30;
            v1 = b->used[n];
            break;
        }
    }
    if (t1 <= t0) return v1;
    return v0 + (int)div_round((int32_t)(v1 - v0) * (t - t0), t1 - t0);
}

int burndown_projected_at(const WeekPace* p, int t) {
    if (!p || p->level == WEEK_UNKNOWN || p->elapsed < BURNDOWN_PROJECT_MINS || t < p->elapsed) return -1;
    const int32_t v = div_round((int32_t)p->used * t, p->elapsed);
    return v > 100 ? 100 : (int)v;
}
