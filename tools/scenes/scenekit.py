#!/usr/bin/env python3
"""scenekit: the pixel-art scenes behind Clawd on the Clawd page.

Scenes are authored in Python (tools/scenes/<name>.py) and the same data
both emits the firmware's generated header (firmware/src/scene_<name>.h, read
by firmware/src/scene.cpp) and renders preview PNGs, so a preview is what the
device draws. The canvas is the LCD-1.47's landscape panel, 320x172 px; the
engine scales other sizes proportionally.

    python tools/scenes/scenekit.py build
    python tools/scenes/scenekit.py preview office --tod night --out office.png
    python tools/scenes/scenekit.py preview all        # every scene x time of day

A scene is a list of flat rectangles drawn in order (later on top). Every
rectangle names a palette INDEX; the palette has a day, dusk and night
variant, so the whole scene re-colours by the real local time. Rectangles
can belong to one group: a drift (clouds sliding by and wrapping), a frame of
a cycling animation (twinkle, steam, blink), a clock (hands at the real
time), an LED countdown (time left in the 5-hour window), or the weekly
poster's live parts (a burn-up chart and the % used with a verdict). Clawd is never drawn here: the previews composite his official frames
from firmware/src/splash_animations.h exactly where stage.cpp puts them.
"""
import argparse
import importlib.util
import math
import os
import re
import sys
from contextlib import contextmanager

W, H = 320, 172
FLOOR_Y = H - H // 16                 # 162: Clawd's feet line (stage.cpp feet_y)
CELL = (H - H // 16 - 6) // 30        # 5 px per art cell (stage.cpp cell)
BODY_OX, BODY_W = 15, 24              # the idle body on the 55x37 art stage
TODS = ("day", "dusk", "night")
TOD_BIT = {"day": 1, "dusk": 2, "night": 4}
ALL = 7
G_STATIC, G_DRIFT, G_FRAME, G_CLOCK, G_DIGITS, G_CHART, G_WEEKNUM = 0, 1, 2, 3, 4, 5, 6
G_LIVE = (G_CLOCK, G_DIGITS, G_CHART, G_WEEKNUM)   # drawn by the engine in their slot; box set by hand
MAX_GROUPS = 48                       # scene.cpp SCENE_MAX_GROUPS
MIN_PERIOD_MS = 100                   # animated updates at most ~10 Hz
ANCHORS = ("DESK", "SIGN")            # scene_data.h SceneAnchorId, in order
SCENES = ("office", "outdoor")

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(REPO, "firmware", "src")
PREVIEW_DIR = os.path.join(os.path.dirname(REPO), "scene-previews")

# Clock hands: sin(i * 6 deg) * 64, the same table as scene.cpp HAND_SIN.
HAND_SIN = [round(64 * math.sin(2 * math.pi * i / 60)) for i in range(60)]

# ─── 3x5 pixel font ──────────────────────────────────────────────────────────
FONT = {
    "A": (".#.", "#.#", "###", "#.#", "#.#"), "B": ("##.", "#.#", "##.", "#.#", "##."),
    "C": (".##", "#..", "#..", "#..", ".##"), "D": ("##.", "#.#", "#.#", "#.#", "##."),
    "E": ("###", "#..", "##.", "#..", "###"), "F": ("###", "#..", "##.", "#..", "#.."),
    "G": (".##", "#..", "#.#", "#.#", ".##"), "H": ("#.#", "#.#", "###", "#.#", "#.#"),
    "I": ("###", ".#.", ".#.", ".#.", "###"), "J": ("..#", "..#", "..#", "#.#", ".#."),
    "K": ("#.#", "#.#", "##.", "#.#", "#.#"), "L": ("#..", "#..", "#..", "#..", "###"),
    "M": ("#.#", "###", "###", "#.#", "#.#"), "N": ("##.", "#.#", "#.#", "#.#", "#.#"),
    "O": (".#.", "#.#", "#.#", "#.#", ".#."), "P": ("##.", "#.#", "##.", "#..", "#.."),
    "Q": (".#.", "#.#", "#.#", "##.", ".##"), "R": ("##.", "#.#", "##.", "#.#", "#.#"),
    "S": (".##", "#..", ".#.", "..#", "##."), "T": ("###", ".#.", ".#.", ".#.", ".#."),
    "U": ("#.#", "#.#", "#.#", "#.#", "###"), "V": ("#.#", "#.#", "#.#", "#.#", ".#."),
    "W": ("#.#", "#.#", "###", "###", "#.#"), "X": ("#.#", "#.#", ".#.", "#.#", "#.#"),
    "Y": ("#.#", "#.#", ".#.", ".#.", ".#."), "Z": ("###", "..#", ".#.", "#..", "###"),
    "0": ("###", "#.#", "#.#", "#.#", "###"), "1": (".#.", "##.", ".#.", ".#.", "###"),
    "2": ("##.", "..#", ".#.", "#..", "###"), "3": ("##.", "..#", ".#.", "..#", "##."),
    "4": ("#.#", "#.#", "###", "..#", "..#"), "5": ("###", "#..", "##.", "..#", "##."),
    "6": (".##", "#..", "###", "#.#", "###"), "7": ("###", "..#", ".#.", ".#.", ".#."),
    "8": ("###", "#.#", "###", "#.#", "###"), "9": ("###", "#.#", "###", "..#", "##."),
    " ": ("...", "...", "...", "...", "..."), ".": ("...", "...", "...", "...", ".#."),
    ",": ("...", "...", "...", ".#.", "#.."), ":": ("...", ".#.", "...", ".#.", "..."),
    "!": (".#.", ".#.", ".#.", "...", ".#."), "?": ("##.", "..#", ".#.", "...", ".#."),
    "-": ("...", "...", "###", "...", "..."), "+": ("...", ".#.", "###", ".#.", "..."),
    "/": ("..#", "..#", ".#.", "#..", "#.."), "%": ("#.#", "..#", ".#.", "#..", "#.#"),
    "'": (".#.", ".#.", "...", "...", "..."), "=": ("...", "###", "...", "###", "..."),
    "<": ("..#", ".#.", "#..", ".#.", "..#"), ">": ("#..", ".#.", "..#", ".#.", "#.."),
    "(": (".#.", "#..", "#..", "#..", ".#."), ")": (".#.", "..#", "..#", "..#", ".#."),
    "*": ("#.#", ".#.", "#.#", "...", "..."), "_": ("...", "...", "...", "...", "###"),
    "#": ("#.#", "###", "#.#", "###", "#.#"), "^": ("#.#", "###", "###", ".#.", "..."),  # ^ = heart
}
FONT_W, FONT_H = 3, 5

# ─── 7-segment digits (the LED countdown; scene.cpp SEG7 is the same table) ──
# Bits a..g = 0..6: a top, b top-right, c bottom-right, d bottom, e bottom-left,
# f top-left, g middle. Index 10 is "-".
SEG7 = (0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x40)
SEG_DASH = 10
WEEK_SLOTS = 168                      # burndown.h BURNDOWN_SLOTS: hours in the weekly window
WEEK_MINS = 7 * 24 * 60               # burndown.h BURNDOWN_WEEK_MINS
# burndown.h: pace grace, red from, red when out this early, no projection before.
GRACE_PCT, RED_PCT, OUT_MARGIN, PROJECT_MINS = 1, 90, 24 * 60, 12 * 60
WEEK_UNKNOWN, WEEK_ON, WEEK_AHEAD, WEEK_OUT = 0, 1, 2, 3
# The glyphs scene.cpp GLYPHS carries for the poster's live text.
LIVE_GLYPHS = "0123456789%+-<ABCDEHINOPTUW "


def seg_rects(x, y, dw, dh, t, mask):
    """scene.cpp draw_digits(): the lit segments of one digit as (x, y, w, h)."""
    mid = (dh - t) // 2
    segs = ((0, 0, dw, t), (dw - t, 0, t, mid + t), (dw - t, mid, t, dh - mid), (0, dh - t, dw, t),
            (0, mid, t, dh - mid), (0, 0, t, mid + t), (0, mid, dw, t))
    return [(x + sx, y + sy, w, h) for i, (sx, sy, w, h) in enumerate(segs) if mask >> i & 1]


def digits_layout(x, y, dw, dh, t):
    """scene.cpp digits_layout(): "H:MM" -> the three digits' x, the colon's
    two dots and the width, with a stroke-wide gap between everything."""
    xc = x + dw + t
    xs = (x, xc + 2 * t, xc + 2 * t + dw + t)
    mid = y + (dh - t) // 2
    dots = ((xc, mid - t, t, t), (xc, mid + t, t, t))
    return xs, dots, 3 * dw + 4 * t


def countdown_glyphs(mins):
    """The three digits' SEG7 indices for `mins` left (H:MM), -:-- when unknown."""
    if mins is None or mins < 0:
        return (SEG_DASH,) * 3
    h = min(9, mins // 60)
    return (h, mins % 60 // 10, mins % 10)


def div_round(a, b):
    """burndown.cpp div_round(): a / b rounded half away from zero (b > 0)."""
    return (a + b // 2) // b if a >= 0 else -((-a + b // 2) // b)


def week_pace(used, wr):
    """burndown.cpp burndown_pace(): dict(level, used, elapsed, delta, out_in)."""
    p = dict(level=WEEK_UNKNOWN, used=-1, elapsed=-1, delta=0, out_in=-1)
    if used is None or wr is None or used < 0 or wr < 0:
        return p
    used = min(100, used)
    e = max(0, WEEK_MINS - wr)
    p.update(used=used, elapsed=e)
    ahead = used * WEEK_MINS - 100 * e
    p["delta"] = div_round(ahead, WEEK_MINS)
    if used >= 100:
        p.update(level=WEEK_OUT, out_in=0)
        return p
    if ahead <= GRACE_PCT * WEEK_MINS:
        p["level"] = WEEK_OUT if used >= RED_PCT else WEEK_ON
        return p
    p["level"] = WEEK_AHEAD
    if e >= PROJECT_MINS:
        t_out = e * 100 // used
        if t_out < WEEK_MINS:
            p["out_in"] = t_out - e
            if WEEK_MINS - t_out >= OUT_MARGIN:
                p["level"] = WEEK_OUT
    if used >= RED_PCT:
        p["level"] = WEEK_OUT
    return p


def week_verdict(p):
    """burndown.cpp burndown_verdict(): the two lines beside the number."""
    if p["level"] == WEEK_UNKNOWN:
        return "", ""
    if p["out_in"] == 0:
        return "OUT", "NOW"
    if p["level"] == WEEK_OUT and p["out_in"] > 0:
        m = p["out_in"]
        return "OUT", (f"{m // 1440}D" if m >= 1440 else f"{m // 60}H" if m >= 60 else "<1H")
    if p["level"] == WEEK_AHEAD or p["delta"] > GRACE_PCT:
        return f"+{p['delta']}", "AHEAD"
    if p["delta"] < -GRACE_PCT:
        return f"{p['delta']}", "BEHIND"
    return "ON", "PACE"


def week_used_at(week, p, t):
    """burndown.cpp burndown_used_at(): % used t minutes in, straight lines
    between the real points (0 at the start, recorded slots before this hour
    at their half hour, `used` now); None outside 0..elapsed."""
    if p["level"] == WEEK_UNKNOWN or t < 0 or t > p["elapsed"]:
        return None
    e = p["elapsed"]
    if t == e:
        return p["used"]
    lim = min(e // 60, WEEK_SLOTS)
    t0, v0, t1, v1 = 0, 0, e, p["used"]
    k = min(t // 60, lim - 1)
    if week is not None:
        j = k
        while j >= 0:
            if week[j] is not None and j * 60 + 30 <= t:
                t0, v0 = j * 60 + 30, week[j]
                break
            j -= 1
        for n in range(max(0, j + 1), lim):
            if week[n] is not None and n * 60 + 30 > t:
                t1, v1 = n * 60 + 30, week[n]
                break
    if t1 <= t0:
        return v1
    return v0 + div_round((v1 - v0) * (t - t0), t1 - t0)


def week_projected_at(p, t):
    """burndown.cpp burndown_projected_at(): the average rate at t, clipped at 100."""
    if p["level"] == WEEK_UNKNOWN or p["elapsed"] < PROJECT_MINS or t < p["elapsed"]:
        return None
    return min(100, div_round(p["used"] * t, p["elapsed"]))


def chart_row(pct, h):
    """scene.cpp row_of(): 100 % on the plot's top row, 0 % on the bottom."""
    return (h - 1) - (pct * (h - 1) + 50) // 100


def chart_col_mid(c, w):
    """scene.cpp col_mid(): the minute at the middle of plot column c of w."""
    return (2 * c + 1) * WEEK_MINS // (2 * w)


def text_width(s, scale=1):
    """Width in px of `s` drawn by Scene.text (1 px gap between glyphs, x scale)."""
    return max(0, len(s) * (FONT_W + 1) * scale - scale)


# ─── Colour helpers ──────────────────────────────────────────────────────────
def rgb(c):
    return ((c >> 16) & 255, (c >> 8) & 255, c & 255)


def hexc(r, g, b):
    return (max(0, min(255, int(round(r)))) << 16) | (max(0, min(255, int(round(g)))) << 8) | \
        max(0, min(255, int(round(b))))


def mix(a, b, t):
    """a blended toward b by t (0..1)."""
    ra, ga, ba = rgb(a)
    rb, gb, bb = rgb(b)
    return hexc(ra + (rb - ra) * t, ga + (gb - ga) * t, ba + (bb - ba) * t)


def shade(c, f):
    """c scaled by f (0.5 = half as bright, 1.2 = brighter)."""
    r, g, b = rgb(c)
    return hexc(r * f, g * f, b * f)


def dusk_of(day):
    """The default dusk variant: warmer and a little darker."""
    return shade(mix(day, 0xFF8A50, 0.22), 0.82)


def night_of(day):
    """The default night variant: blue and dark."""
    return shade(mix(day, 0x2A3A70, 0.40), 0.42)


def q565(c):
    """What the panel shows: the colour through RGB565 and back."""
    r, g, b = rgb(c)
    r5, g6, b5 = r >> 3, g >> 2, b >> 3
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))


def tod_mask(names):
    if isinstance(names, int):
        return names
    if isinstance(names, str):
        names = (names,)
    m = 0
    for n in names:
        if n not in TOD_BIT:
            raise ValueError(f"time of day {n!r}: use day, dusk or night")
        m |= TOD_BIT[n]
    return m


def tod_of_minute(m):
    """scene.cpp tod_of(): night 21:00-05:59, dusk 06:00-07:59 and 18:00-20:59."""
    if m is None or m < 0:
        return "day"
    m %= 1440
    if m < 360 or m >= 1260:
        return "night"
    if m < 480 or m >= 1080:
        return "dusk"
    return "day"


def tdiv(a, b):
    """C integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


# ─── Scene ───────────────────────────────────────────────────────────────────
class _Frames:
    """`with s.frames(period_ms=300) as f:` then `with f.frame(): ...` per frame."""

    def __init__(self, scene, period_ms, phase_ms):
        self.scene = scene
        self.period = period_ms
        self.phase = phase_ms
        self.ids = []

    @contextmanager
    def frame(self):
        s = self.scene
        if s._group:
            raise ValueError("frames can't nest inside another group")
        gid = s._new_group(dict(type=G_FRAME, period=self.period, phase=self.phase))
        self.ids.append(gid)
        s._group = gid
        try:
            yield
        finally:
            s._group = 0


class Scene:
    def __init__(self, name):
        self.name = name
        self.pal = {t: [] for t in TODS}
        self._colors = {}
        self.rects = []        # [x, y, w, h, c, tod, g]
        self.groups = [dict(type=G_STATIC)]
        self.anchors = {}
        self._tod = ALL
        self._group = 0
        self._batch = None     # rects of one shape, merged on close

    # Colours ----------------------------------------------------------------
    def color(self, day, dusk=None, night=None):
        """A palette index. Variants default to the day colour."""
        key = (day, day if dusk is None else dusk, day if night is None else night)
        if key not in self._colors:
            if len(self._colors) >= 255:
                raise ValueError("more than 255 colours")
            self._colors[key] = len(self._colors)
            for t, v in zip(TODS, key):
                self.pal[t].append(v)
        return self._colors[key]

    def ambient(self, day, dusk=None, night=None):
        """A lit-by-the-sky colour: dusk and night variants derived from day."""
        return self.color(day, dusk_of(day) if dusk is None else dusk,
                          night_of(day) if night is None else night)

    # Shapes -----------------------------------------------------------------
    def rect(self, x, y, w, h, c, tod=ALL):
        """A filled rectangle; `tod` narrows when it shows (on top of any `with s.tod`)."""
        for v in (x, y, w, h, c):
            if not isinstance(v, int):
                raise TypeError(f"rect values are ints (got {v!r})")
        if w <= 0 or h <= 0:
            return
        if not 0 <= c < len(self._colors):
            raise ValueError(f"colour index {c} isn't defined")
        m = self._tod & tod_mask(tod)
        if not m:
            return
        r = [x, y, w, h, c, m, self._group]
        if self._batch is not None:
            self._batch.append(r)
        else:
            self.rects.append(r)

    @contextmanager
    def shape(self):
        """Rects inside merge into fewer rects (they mustn't overlap each other)."""
        outer = self._batch is None
        if outer:
            self._batch = []
        try:
            yield
        finally:
            if outer:
                self.rects.extend(_merge(self._batch))
                self._batch = None

    def text(self, x, y, s, c, scale=1, tod=ALL):
        """Uppercase text in the 3x5 pixel font, baked into rects; `^` is a heart."""
        with self.shape():
            for i, ch in enumerate(s.upper()):
                g = FONT.get(ch)
                if g is None:
                    raise ValueError(f"no glyph for {ch!r}")
                gx = x + i * (FONT_W + 1) * scale
                for row, bits in enumerate(g):
                    for col, b in enumerate(bits):
                        if b == "#":
                            self.rect(gx + col * scale, y + row * scale, scale, scale, c, tod)

    def pixels(self, x, y, rows, colors, scale=1, tod=ALL):
        """ASCII art: one string per row, each char a key of `colors` ('.' / ' ' = clear)."""
        with self.shape():
            for ry, line in enumerate(rows):
                rx = 0
                while rx < len(line):
                    ch = line[rx]
                    if ch in ". ":
                        rx += 1
                        continue
                    run = rx
                    while run < len(line) and line[run] == ch:
                        run += 1
                    if ch not in colors:
                        raise ValueError(f"pixels: no colour for {ch!r}")
                    self.rect(x + rx * scale, y + ry * scale, (run - rx) * scale, scale,
                              colors[ch], tod)
                    rx = run

    def disc(self, cx, cy, r, c, tod=ALL):
        """A filled circle, centred between pixels (cx-1, cx) and (cy-1, cy)."""
        with self.shape():
            for j in range(2 * r):
                dy = j + 0.5 - r
                hw = int(round(math.sqrt(max(0.0, r * r - dy * dy))))
                self.rect(cx - hw, cy - r + j, 2 * hw, 1, c, tod)

    # Visibility and motion --------------------------------------------------
    @contextmanager
    def tod(self, *names):
        """Shapes inside show only at these times of day ('day', 'dusk', 'night')."""
        old = self._tod
        self._tod = old & tod_mask(names)
        try:
            yield
        finally:
            self._tod = old

    @contextmanager
    def drift(self, speed, wrap=None):
        """Shapes inside slide sideways together at `speed` px/s (negative = left).
        The group's left edge wraps within `wrap` = (x0, x1); the default
        (-group width, 320) lets it leave one side and come back from the other."""
        if self._group:
            raise ValueError("groups can't nest")
        gid = self._new_group(dict(type=G_DRIFT, speed=int(round(speed * 10)), wrap=wrap))
        self._group = gid
        try:
            yield
        finally:
            self._group = 0

    @contextmanager
    def frames(self, period_ms, phase_ms=0):
        """A cycling animation: `with f.frame():` blocks shown one at a time,
        each for `period_ms` (>= 100). `phase_ms` offsets it from the others."""
        if self._group:
            raise ValueError("groups can't nest")
        if period_ms < MIN_PERIOD_MS:
            raise ValueError(f"frames period {period_ms} ms: at least {MIN_PERIOD_MS}")
        f = _Frames(self, period_ms, phase_ms)
        yield f
        for k, gid in enumerate(f.ids):
            self.groups[gid]["n"] = len(f.ids)
            self.groups[gid]["k"] = k

    def frames_of(self, draws, period_ms, phase_ms=0):
        """frames() as a list of callables, one per frame."""
        with self.frames(period_ms, phase_ms) as f:
            for d in draws:
                with f.frame():
                    d()

    def clock(self, cx, cy, r, face_c, hand_c, rim_c=None):
        """An analog clock showing the real local time (10:10 when unknown)."""
        if rim_c is not None:
            self.disc(cx, cy, r, rim_c)
            self.disc(cx, cy, r - 2, face_c)
        else:
            self.disc(cx, cy, r, face_c)
        for a in (0, 15, 30, 45):    # 12, 3, 6, 9
            px = cx + tdiv(HAND_SIN[a] * (r - 4), 64)
            py = cy - tdiv(HAND_SIN[(a + 15) % 60] * (r - 4), 64)
            self.rect(px - 1, py - 1, 2, 2, hand_c)
        gid = self._new_group(dict(type=G_CLOCK, cx=cx, cy=cy, r=r))
        self.groups[gid]["box"] = (cx - r, cy - r, 2 * r, 2 * r)
        # The hands' slot in the layer order: drawn by the engine, not as a rect.
        self.rects.append([cx - r, cy - r, 2 * r, 2 * r, hand_c, self._tod, gid])

    def digits(self, x, y, lit_c, ghost_c=None, dw=6, dh=10, t=2):
        """An LED countdown "H:MM" (the time left in the 5-hour window, "-:--"
        when unknown) in 7-segment digits dw x dh with strokes t px, lit by the
        engine in `lit_c`; the colon blinks each second while it counts.
        `ghost_c` draws the unlit segments (8:88) under it, like a real LED
        display. Returns its width."""
        xs, dots, width = digits_layout(x, y, dw, dh, t)
        if ghost_c is not None:
            with self.shape():
                for dx in xs:
                    for r in seg_rects(dx, y, dw, dh, t, SEG7[8]):
                        self.rect(*r, ghost_c)
                for r in dots:
                    self.rect(*r, ghost_c)
        gid = self._new_group(dict(type=G_DIGITS, p=(dw, dh, t)))
        self.groups[gid]["box"] = (x, y, width, dh)
        self.rects.append([x, y, width, dh, lit_c, self._tod, gid])
        return width

    @staticmethod
    def _pack(lo, hi):
        """Two palette indices in one int16 p (scene.cpp unpacks lo | hi << 8)."""
        if not (0 <= lo < 256 and 0 <= hi < 128):
            raise ValueError(f"colour indices {lo}, {hi} don't pack into an int16")
        return lo | hi << 8

    def week_chart(self, x, y, w, h, line_c, area_c, now_c, levels):
        """The weekly poster's live burn-up in the plot area x, y, w x h (1 px
        per column, w columns over the 7-day window, 0 % on the bottom row and
        100 % on the top): the % used so far, straight lines between real
        points only, as a line (`line_c`) over a fill (`area_c`); a dashed now
        marker (`now_c`) above it; and the average rate dotted on to the reset
        in the level colour, clipped at 100 % with a mark where it runs out.
        `levels` = (on pace, ahead, out, unknown) colours. Nothing is drawn
        while the week is unknown. Axes, ticks and the even-pace line are
        plain shapes around it (shapes after it draw on top)."""
        if w < 2 or h < 2:
            raise ValueError(f"chart {w}x{h}")
        ok, ahead, out, unk = levels
        gid = self._new_group(dict(type=G_CHART, p=(self._pack(area_c, now_c), self._pack(ok, ahead),
                                                    self._pack(out, unk))))
        self.groups[gid]["box"] = (x, y, w, h)
        self.rects.append([x, y, w, h, line_c, self._tod, gid])

    def week_number(self, x, y, w, ink_c, levels, scale=2):
        """The weekly poster's live text in x..x+w from y: the weekly % used
        (`--` when unknown) right-aligned in the 3x5 font at `scale` with a 1x
        `%` on its baseline, in the
        level colour of `levels` = (on pace, ahead, out, unknown), and the
        verdict's two 3x5 lines at the left in `ink_c` (`+6`/`AHEAD`,
        `-4`/`BEHIND`, `ON`/`PACE`, `OUT`/`2D`, `OUT`/`NOW`)."""
        ok, ahead, out, unk = levels
        h = max(5 * scale, 11)
        gid = self._new_group(dict(type=G_WEEKNUM, p=(scale, self._pack(ok, ahead), self._pack(out, unk))))
        self.groups[gid]["box"] = (x, y, w, h)
        self.rects.append([x, y, w, h, ink_c, self._tod, gid])

    def anchor(self, name, x, y=0, w=0, h=0):
        """A named point/area the engine reads: DESK (x = Clawd's body-left at the
        laptop, office), SIGN (an area for live text)."""
        if name not in ANCHORS:
            raise ValueError(f"anchor {name!r}: one of {ANCHORS}")
        self.anchors[name] = (x, y, w, h)

    # Internals --------------------------------------------------------------
    def _new_group(self, g):
        self.groups.append(g)
        if len(self.groups) > MAX_GROUPS:
            raise ValueError(f"more than {MAX_GROUPS - 1} groups")
        return len(self.groups) - 1

    def finish(self):
        """Merge, then work out every group's bounding box and wrap."""
        self.rects = _merge_sequential(self.rects)
        for gid, g in enumerate(self.groups):
            if gid == 0 or g["type"] in G_LIVE:
                continue
            rs = [r for r in self.rects if r[6] == gid]
            if rs:
                x0 = min(r[0] for r in rs)
                y0 = min(r[1] for r in rs)
                x1 = max(r[0] + r[2] for r in rs)
                y1 = max(r[1] + r[3] for r in rs)
                g["box"] = (x0, y0, x1 - x0, y1 - y0)
            else:
                g["box"] = (0, 0, 0, 0)
            if g["type"] == G_DRIFT:
                bx, _, bw, _ = g["box"]
                if g["wrap"] is None:
                    g["wrap"] = (-bw, W)
                x0, x1 = g["wrap"]
                if x1 - x0 < bw:
                    raise ValueError(f"drift wrap {g['wrap']} narrower than its group ({bw} px)")
            if g["type"] == G_FRAME and g.get("n", 0) < 1:
                raise ValueError("a frame outside `with s.frames()`")
        for r in self.rects:
            for v in r[:4]:
                if not -32768 <= v <= 32767:
                    raise ValueError(f"rect {r} out of int16 range")
        return self


def _merge(rects):
    """Merge one shape's rects (they don't overlap, so order is free): runs
    with the same columns and colour on consecutive rows become one rect."""
    out, open_ = [], {}
    for r in sorted(rects, key=lambda r: (r[1], r[0])):
        key = (r[0], r[2], r[4], r[5], r[6])
        prev = open_.get(key)
        if prev is not None and prev[1] + prev[3] == r[1]:
            prev[3] += r[3]
            continue
        nr = list(r)
        open_[key] = nr
        out.append(nr)
    return out


def _merge_sequential(rects):
    """Merge neighbours in draw order that continue each other (same colour,
    times and group), which keeps the layer order intact."""
    out = []
    for r in rects:
        if out:
            p = out[-1]
            if p[4:] == r[4:]:
                if p[0] == r[0] and p[2] == r[2] and p[1] + p[3] == r[1]:
                    p[3] += r[3]
                    continue
                if p[1] == r[1] and p[3] == r[3] and p[0] + p[2] == r[0]:
                    p[2] += r[2]
                    continue
        out.append(list(r))
    return out


def load_scene(name):
    path = os.path.join(HERE, f"{name}.py")
    if HERE not in sys.path:
        sys.path.insert(0, HERE)
    spec = importlib.util.spec_from_file_location(f"scene_{name}", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    s = mod.scene()
    if s.name != name:
        raise ValueError(f"{path}: Scene name {s.name!r}, expected {name!r}")
    return s.finish()


# ─── Header ──────────────────────────────────────────────────────────────────
def emit_header(s):
    n = s.name
    lines = [
        f"// GENERATED by tools/scenes/scenekit.py from tools/scenes/{n}.py.",
        "// Do not edit: change the scene script and run",
        "//   python tools/scenes/scenekit.py build",
        f"// {len(s.rects)} rects, {len(s.groups) - 1} groups, {len(s.pal['day'])} colours.",
        "// Only scene.cpp includes this.",
        "#pragma once",
        '#include "scene_data.h"',
        "",
    ]
    for t in TODS:
        vals = ",".join(f"0x{c:06X}" for c in s.pal[t])
        lines.append(f"static const uint32_t scene_{n}_pal_{t}[{len(s.pal[t])}] = {{{vals}}};")
    lines.append("")
    lines.append(f"static const SceneRect scene_{n}_rects[{len(s.rects)}] = {{")
    for r in s.rects:
        lines.append("    {%d,%d,%d,%d,%d,%d,%d}," % tuple(r))
    lines.append("};")
    lines.append("")
    lines.append(f"static const SceneGroup scene_{n}_groups[{len(s.groups)}] = {{")
    for g in s.groups:
        t = g["type"]
        bx, by, bw, bh = g.get("box", (0, 0, 0, 0))
        if t == G_DRIFT:
            p, nk = (g["speed"], g["wrap"][0], g["wrap"][1]), (0, 0)
            note = f"drift {g['speed'] / 10:g} px/s"
        elif t == G_FRAME:
            p, nk = (g["period"], g["phase"], 0), (g["n"], g["k"])
            note = f"frame {g['k']}/{g['n']} every {g['period']} ms"
        elif t == G_CLOCK:
            p, nk = (g["cx"], g["cy"], g["r"]), (0, 0)
            note = "clock hands"
        elif t == G_DIGITS:
            p, nk = g["p"], (0, 0)
            note = "LED countdown H:MM"
        elif t == G_CHART:
            p, nk = g["p"], (0, 0)
            note = "weekly burn-up chart"
        elif t == G_WEEKNUM:
            p, nk = g["p"], (0, 0)
            note = "weekly % used and verdict"
        else:
            p, nk = (0, 0, 0), (0, 0)
            note = "static"
        lines.append("    {%d,%d,%d,{%d,%d,%d},{%d,%d,%d,%d}},   // %s" %
                     (t, nk[0], nk[1], p[0], p[1], p[2], bx, by, bw, bh, note))
    lines.append("};")
    lines.append("")
    anchors = ",".join("{%d,%d,%d,%d}" % s.anchors.get(a, (-1, -1, 0, 0)) for a in ANCHORS)
    lines += [
        f"static const SceneDef scene_{n} = {{",
        f'    "{n}",',
        f"    {{ scene_{n}_pal_day, scene_{n}_pal_dusk, scene_{n}_pal_night }},",
        f"    {len(s.pal['day'])},",
        f"    scene_{n}_rects, {len(s.rects)},",
        f"    scene_{n}_groups, {len(s.groups)},",
        f"    {{ {anchors} }},   // {', '.join(ANCHORS)}",
        "};",
        "",
    ]
    return "\n".join(lines)


def cmd_build(_args):
    for n in SCENES:
        s = load_scene(n)
        path = os.path.join(SRC, f"scene_{n}.h")
        with open(path, "w", newline="\n") as f:
            f.write(emit_header(s))
        flash = len(s.rects) * 12 + len(s.groups) * 16 + len(s.pal["day"]) * 12
        print(f"{os.path.relpath(path, REPO)}: {len(s.rects)} rects, {len(s.groups) - 1} groups, "
              f"{len(s.pal['day'])} colours, ~{flash} B flash")


# ─── Rendering (mirrors scene.cpp) ───────────────────────────────────────────
def drift_dx(g, t_ms):
    """scene.cpp drift_dx(): how far the group sits from where it was authored."""
    bx = g["box"][0]
    x0, x1 = g["wrap"]
    span = x1 - x0
    travel = tdiv(t_ms * g["speed"], 10000)
    pos = x0 + ((bx - x0) + travel) % span
    return pos - bx


def frame_on(g, t_ms):
    return ((t_ms + g["phase"]) // g["period"]) % g["n"] == g["k"]


def hand_blocks(g, minute):
    """scene.cpp draw_hands(): 2x2 blocks along the hour then the minute hand."""
    if minute is None or minute < 0:
        minute = 10 * 60 + 10
    cx, cy, r = g["cx"], g["cy"], g["r"]
    hh, mm = (minute // 60) % 12, minute % 60
    out = []
    for a, length in ((hh * 5 + mm // 12, r * 55 // 100), (mm, r - 3)):
        for i in range(length + 1):
            px = cx + tdiv(HAND_SIN[a] * i, 64)
            py = cy - tdiv(HAND_SIN[(a + 15) % 60] * i, 64)
            out.append((px - 1, py - 1))
    return out


def digits_blocks(g, x, y, mins, t_ms):
    """scene.cpp draw_digits(): the lit segments and colon as (x, y, w, h)."""
    dw, dh, t = g["p"]
    xs, dots, _ = digits_layout(x, y, dw, dh, t)
    out = []
    for dx, gl in zip(xs, countdown_glyphs(mins)):
        out += seg_rects(dx, y, dw, dh, t, SEG7[gl])
    if mins is None or mins < 0 or (t_ms // 500) % 2 == 0:    # blinks while counting
        out += list(dots)
    return out


def chart_blocks(g, x, y, w, h, week, p):
    """scene.cpp draw_chart(): (x, y, w, h, which), which 0 = area, 1 = line,
    2 = the now marker, 3 = the projection and its run-out mark."""
    out = []
    if p["level"] == WEEK_UNKNOWN:
        return out
    e = p["elapsed"]
    nc = min(w - 1, e * w // WEEK_MINS)
    for r in range(0, chart_row(p["used"], h) - 1, 2):
        out.append((x + nc, y + r, 1, 1, 2))
    prev = -1
    for c in range(nc + 1):
        v = week_used_at(week, p, e if c == nc else chart_col_mid(c, w))
        if v is None:
            continue
        top = chart_row(v, h)
        lo = hi = top
        if prev > top + 1:
            hi = prev - 1
        elif prev >= 0 and prev < top - 1:
            lo = prev + 1
        out.append((x + c, y + lo, 1, hi - lo + 1, 1))
        if hi < h - 1:
            out.append((x + c, y + hi + 1, 1, h - 1 - hi, 0))
        prev = top
    hit = False
    for c in range(nc + 1, w):
        v = week_projected_at(p, chart_col_mid(c, w))
        if v is None:
            break
        if (c - nc) % 2 == 0:
            out.append((x + c, y + chart_row(v, h), 1, 1, 3))
        if v >= 100 and not hit:
            hit = True
            out.append((x + min(c, w - 2), y, 2, 3, 3))
    return out


def text_blocks(x, y, s, scale=1):
    """scene.cpp draw_text(): the 3x5 font's lit cells as (x, y, w, h)."""
    out = []
    for i, ch in enumerate(s):
        if ch not in LIVE_GLYPHS:
            raise ValueError(f"scene.cpp has no live glyph for {ch!r}")
        for r, bits in enumerate(FONT[ch]):
            for c, b in enumerate(bits):
                if b == "#":
                    out.append((x + i * 4 * scale + c * scale, y + r * scale, scale, scale))
    return out


def week_text_blocks(g, x, y, w, p):
    """scene.cpp draw_week_text(): (x, y, w, h, which), which 0 = the number
    (level colour), 1 = the verdict (ink)."""
    scale = g["p"][0] & 255 or 2
    num = "--" if p["level"] == WEEK_UNKNOWN else str(p["used"])
    pct_x = x + w - 3                                      # a 1x "%" on the number's baseline
    out = [b + (0,) for b in text_blocks(pct_x - scale - text_width(num, scale), y, num, scale)]
    out += [b + (0,) for b in text_blocks(pct_x, y + 5 * scale - 5, "%")]
    l1, l2 = week_verdict(p)
    out += [b + (1,) for b in text_blocks(x, y, l1)]
    out += [b + (1,) for b in text_blocks(x, y + 6, l2)]
    return out


def level_index(g, p):
    """scene.cpp level_color(): the palette index for the pace level."""
    a, b = g["p"][1], g["p"][2]
    return {WEEK_ON: a & 255, WEEK_AHEAD: a >> 8, WEEK_OUT: b & 255}.get(p["level"], b >> 8)


def synthetic_week(used, wr, start_hour=8):
    """A believable week of weekly % used per hour slot before now, ending
    near `used` now: use climbs in working hours (more mid-week), a little in
    the evening, nothing overnight, and the device was off for a stretch
    (slots 52..63 hold no data). The window started at `start_hour` local
    time. None when unknown."""
    if used is None or used < 0 or wr is None or wr < 0:
        return None
    now_slot = max(0, WEEK_MINS - wr) // 60
    raw, acc = [], 0.0
    for s in range(now_slot):
        hour = (start_hour + s) % 24
        day = (start_hour + s) // 24
        if 9 <= hour < 18:
            acc += (1.0, 1.4, 1.6, 1.2, 0.9, 0.3, 0.2, 0.2)[min(day, 7)]
        elif 20 <= hour < 23:
            acc += 0.5
        raw.append(acc)
    # Scaled so the curve runs on into `used` now (the last hour's share added).
    total = (raw[-1] if raw else 0) + 0.6
    week = [None] * WEEK_SLOTS
    for s, v in enumerate(raw):
        if not 52 <= s < 64:
            week[s] = min(100, int(round(v * used / total)))
    return week


def render(s, tod="day", t_ms=0, minute=None, live=None):
    from PIL import Image
    img = Image.new("RGB", (W, H), (0, 0, 0))
    bit = TOD_BIT[tod]
    pal = [q565(c) for c in s.pal[tod]]
    live = live or {}

    def fill(x, y, w, h, col):
        x0, y0, x1, y1 = max(0, x), max(0, y), min(W, x + w), min(H, y + h)
        if x1 > x0 and y1 > y0:
            img.paste(col, (x0, y0, x1, y1))

    for x, y, w, h, c, m, gid in s.rects:
        if not m & bit:
            continue
        g = s.groups[gid]
        if g["type"] == G_FRAME and not frame_on(g, t_ms):
            continue
        if g["type"] == G_CLOCK:
            for bx, by in hand_blocks(g, minute):
                fill(bx, by, 2, 2, pal[c])
            continue
        if g["type"] == G_DIGITS:
            for r in digits_blocks(g, x, y, live.get("countdown"), t_ms):
                fill(*r, pal[c])
            continue
        if g["type"] == G_CHART:
            p = live.get("pace") or week_pace(None, None)
            cols = (pal[g["p"][0] & 255], pal[c], pal[g["p"][0] >> 8], pal[level_index(g, p)])
            for bx, by, bw, bh, which in chart_blocks(g, x, y, w, h, live.get("week"), p):
                fill(bx, by, bw, bh, cols[which])
            continue
        if g["type"] == G_WEEKNUM:
            p = live.get("pace") or week_pace(None, None)
            cols = (pal[level_index(g, p)], pal[c])
            for bx, by, bw, bh, which in week_text_blocks(g, x, y, w, p):
                fill(bx, by, bw, bh, cols[which])
            continue
        dx = drift_dx(g, t_ms) if g["type"] == G_DRIFT else 0
        fill(x + dx, y, w, h, pal[c])
    return img


# ─── Clawd (official frames, placed like stage.cpp draw()) ──────────────────
_anims = None


def load_anims():
    global _anims
    if _anims is not None:
        return _anims
    with open(os.path.join(SRC, "splash_animations.h"), encoding="utf-8") as f:
        txt = f.read()
    arrays = {}
    for m in re.finditer(r"static const uint(?:8|16)_t (\w+)\[\d+\] = \{(.*?)\};", txt, re.S):
        arrays[m.group(1)] = [int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|\d+", m.group(2))]
    _anims = {}
    row = re.compile(r'\{"([^"]+)", "\w+", (\d+), (\d+), (\d+), (\d+), (\d+), (\d+), (\d+), (\d+), '
                     r'(\w+), (\w+), (\w+)\}')
    for m in row.finditer(txt):
        name = m.group(1)
        w, h, ox, oy, fc, ls, le, _pc = (int(m.group(i)) for i in range(2, 10))
        _anims[name] = dict(w=w, h=h, ox=ox, oy=oy, frames=fc, loop_start=ls, loop_end=le,
                            palette=arrays[m.group(10)], data=arrays[m.group(11)])
    return _anims


def rgb565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def draw_clawd(img, anim_name, frame, body_x, mirror=False):
    a = load_anims()[anim_name]
    w, h = a["w"], a["h"]
    base = frame * w * h
    ox = (2 * BODY_OX + BODY_W) - (a["ox"] + w) if mirror else a["ox"]
    px = body_x + (ox - BODY_OX) * CELL
    py = FLOOR_Y - h * CELL
    for gy in range(h):
        for gx in range(w):
            code = a["data"][base + gy * w + (w - 1 - gx if mirror else gx)]
            if not code:
                continue
            x0, y0 = px + gx * CELL, py + gy * CELL
            x1, y1 = min(W, x0 + CELL), min(H, y0 + CELL)
            x0, y0 = max(0, x0), max(0, y0)
            if x1 > x0 and y1 > y0:
                img.paste(rgb565(a["palette"][code]), (x0, y0, x1, y1))


CLAWD_POSES = {"idle": ("walking", 0), "laptop": ("laptop", None)}


def compose(s, tod, t_ms, minute, clawd, x, live=None):
    img = render(s, tod, t_ms, minute, live)
    if clawd and clawd != "none":
        anim, frame = CLAWD_POSES[clawd]
        if frame is None:
            frame = load_anims()[anim]["loop_start"]     # typing
        if x is None:
            x = s.anchors["DESK"][0] if clawd == "laptop" and "DESK" in s.anchors \
                else (W - BODY_W * CELL) // 2
        draw_clawd(img, anim, frame, x)
    return img


def default_minute(tod):
    return {"day": 10 * 60 + 10, "dusk": 19 * 60 + 5, "night": 23 * 60 + 40}[tod]


def save(img, out, scale=3):
    from PIL import Image
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    img.save(out)
    root, ext = os.path.splitext(out)
    big = f"{root}_x{scale}{ext}"
    img.resize((W * scale, H * scale), Image.NEAREST).save(big)
    return big


def cmd_preview(args):
    names = SCENES if args.scene == "all" else (args.scene,)
    tods = TODS if (args.scene == "all" and not args.tod) else (args.tod or None,)
    outdir = args.outdir or PREVIEW_DIR
    sheet = []
    used = None if args.week == "none" or args.w < 0 else min(100, int(args.w + 0.5))
    wr = None if args.week == "none" or args.wr < 0 else args.wr
    pace = week_pace(used, wr)

    def even():
        e = pace["elapsed"]
        return [None if s * 60 + 60 > e else div_round(used * (s * 60 + 30), e)
                for s in range(WEEK_SLOTS)]

    week = None
    if pace["level"] != WEEK_UNKNOWN:
        week = {"synthetic": lambda: synthetic_week(used, wr), "empty": lambda: None,
                "even": even, "none": lambda: None}[args.week]()
        print("week: %d%% used, %s, verdict %s" % (
            used, ("?", "on pace", "ahead", "out")[pace["level"]], " ".join(week_verdict(pace))))
    live = dict(countdown=args.sr, week=week, pace=pace)
    for n in names:
        s = load_scene(n)
        for tod in tods:
            minute = args.minute
            if tod is None:
                tod = tod_of_minute(minute) if minute is not None else "day"
            if minute is None:
                minute = default_minute(tod)
            clawd = args.clawd or ("laptop" if n == "office" else "idle")
            if args.gif:
                frames = []
                step = 100
                for t in range(args.t, args.t + int(args.gif * 1000), step):
                    frames.append(compose(s, tod, t, minute, clawd, args.x, live)
                                  .resize((W * 2, H * 2), 0))
                out = args.out if (args.out and len(names) == 1 and len(tods) == 1) \
                    else os.path.join(outdir, f"{n}_{tod}.gif")
                os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
                frames[0].save(out, save_all=True, append_images=frames[1:], duration=step, loop=0)
                print(out)
                continue
            img = compose(s, tod, args.t, minute, clawd, args.x, live)
            out = args.out if (args.out and len(names) == 1 and len(tods) == 1) \
                else os.path.join(outdir, f"{n}_{tod}.png")
            big = save(img, out)
            print(out, "+", os.path.basename(big))
            sheet.append(img)
    if args.scene == "all" and sheet and not args.gif:
        from PIL import Image
        cols = len(tods)
        rows = (len(sheet) + cols - 1) // cols
        gap = 6
        sh = Image.new("RGB", (cols * (W * 2 + gap) - gap, rows * (H * 2 + gap) - gap), (40, 40, 40))
        for i, im in enumerate(sheet):
            sh.paste(im.resize((W * 2, H * 2), 0),
                     ((i % cols) * (W * 2 + gap), (i // cols) * (H * 2 + gap)))
        out = os.path.join(outdir, "sheet.png")
        sh.save(out)
        print(out)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("build", help="regenerate firmware/src/scene_<name>.h")
    pv = sub.add_parser("preview", help="render a scene (and Clawd) to PNG")
    pv.add_argument("scene", choices=SCENES + ("all",))
    pv.add_argument("--tod", choices=TODS, help="time of day (default: from --minute, else day)")
    pv.add_argument("--minute", type=int, help="local minute of day for the clock (0..1439)")
    pv.add_argument("--sr", type=int, default=161,
                    help="minutes left in the 5-hour window for the LED countdown (-1 = unknown)")
    pv.add_argument("--week", choices=("synthetic", "empty", "even", "none"), default="synthetic",
                    help="the weekly poster's recorded history: a made-up week (with a gap "
                         "where the device was off), no samples (a straight line from the "
                         "start), an even burn, or no payload yet (--%%, no chart)")
    pv.add_argument("--w", type=float, default=34,
                    help="weekly %% used now (`w`; -1 = unknown or spend-capped: --%%)")
    pv.add_argument("--wr", type=int, default=5600,
                    help=f"minutes to the weekly reset (`wr`, 0..{WEEK_MINS}; -1 = unknown)")
    pv.add_argument("--t", type=int, default=0, help="animation time in ms (drift, frames)")
    pv.add_argument("--clawd", choices=("idle", "laptop", "none"),
                    help="Clawd's pose (default: laptop in the office, idle outside)")
    pv.add_argument("--x", type=int, help="Clawd's body-left x (default: DESK or centre)")
    pv.add_argument("--gif", type=float, help="write an animated GIF this many seconds long")
    pv.add_argument("--out", help="output PNG (a _x3 copy is written next to it)")
    pv.add_argument("--outdir", help=f"folder for `preview all` (default {PREVIEW_DIR})")
    args = p.parse_args(argv)
    if args.cmd == "build":
        cmd_build(args)
    else:
        cmd_preview(args)


if __name__ == "__main__":
    main()
