"""Outside: where Clawd plays when Claude isn't working.

A park in front of snowy mountains: banded sky (blue by day, a purple to
peach sunset at dusk, navy and starry at night), a sun with twinkling rays /
a striped setting sun / a full moon, drifting clouds, two layers of hills, a
big oak with a swing and a kite snagged by its string, and an "OUT TO PLAY"
sign. The band he walks in (y ~70..162) is
kept to big calm shapes in blues and greens so his terra-cotta stays the most
saturated thing on screen; nothing orange sits behind him at any time of day.
"""
import math

from scenekit import Scene, W, H, FLOOR_Y, text_width

GRASS_TOP = 138          # the lawn starts here; his feet are at FLOOR_Y
FAR_BASE = 124           # the far mountains' foot (hidden by the hills)

# Sky bands, top to horizon: (start y, day, dusk, night). Dusk is a real
# sunset (purple, magenta, orange, pale peach at the horizon): the warm bands
# sit above his head, and the mountains hide most of the peach below them.
SKY = (
    (0,  0x3A78D4, 0x4A2A6E, 0x060B20),
    (16, 0x4686DC, 0x763878, 0x09102A),
    (30, 0x5595E2, 0xB04A7A, 0x0C1534),
    (44, 0x66A5E8, 0xE8865A, 0x101B3E),
    (58, 0x79B5EE, 0xF2C48C, 0x14224A),
    (74, 0x8EC5F2, 0xF6D4A2, 0x192A55),
    (92, 0xA6D4F5, 0xF8E0B8, 0x1F3260),
)


def terrain(s, y0, y1, step, cell):
    """Row-by-row run-length fill: `cell(x, y)` gives a colour (or None) for
    the `step`-px row starting at y; equal runs become one rect."""
    with s.shape():
        for y in range(y0, y1, step):
            x = 0
            while x < W:
                c = cell(x, y)
                run = x + 1
                while run < W and cell(run, y) == c:
                    run += 1
                if c is not None:
                    s.rect(x, y, run - x, min(step, y1 - y), c)
                x = run


def blob(s, cx, cy, r, c, step=2):
    """A disc built from `step`-px rows: chunkier than s.disc and half the rects."""
    with s.shape():
        for j in range(0, 2 * r, step):
            dy = j + step / 2 - r
            hw = int(round(math.sqrt(max(0.0, r * r - dy * dy))))
            s.rect(cx - hw, cy - r + j, 2 * hw, min(step, 2 * r - j), c)


def scene():
    s = Scene("outdoor")
    sky = [s.color(d, k, n) for _, d, k, n in SKY]
    sun      = s.color(0xFFE066, 0xFFE08A, 0xFFE066)
    sun_hi   = s.color(0xFFF6C2, 0xFFF2C8, 0xFFF6C2)
    sun_lo   = s.color(0xFFC94A, 0xFFB8A0, 0xFFC94A)    # dusk: a peach rim, not orange
    moon     = s.color(0xF4F0DA)
    moon_lo  = s.color(0xD6D0B6)
    halo     = s.color(0x24365F)
    star     = s.color(0xFFFFFF)
    star_dim = s.color(0x8E9CC4)
    cloud    = s.color(0xFFFFFF, 0xF2B8C6, 0x2A3A66)
    cloud_lo = s.color(0xD4E6F6, 0xD08AA8, 0x1F2C54)
    snow     = s.color(0xF4F8FC, 0xF0C8D8, 0x3A4A78)    # night: dim caps on dark peaks,
    snow_lo  = s.color(0xC8D8EC, 0xC09AC0, 0x2E3C68)    # so they never float like clouds
    mtn      = s.color(0x7F9CC8, 0x5A3F7E, 0x111A3A)
    mtn_lo   = s.color(0x6886B6, 0x4A346C, 0x0C1330)
    hill_far = s.color(0x6FAF8E, 0x4E5A7A, 0x15283A)
    tree_far = s.color(0x4E9474, 0x3E4A6A, 0x0F1F2E)
    hill     = s.color(0x5AA552, 0x4A7A54, 0x173A2A)
    hill_hi  = s.color(0x6EB860, 0x58885A, 0x1C4432)
    grass    = s.color(0x5CB848, 0x40824A, 0x1A4228)
    grass_hi = s.color(0x84D266, 0x56985A, 0x245434)
    grass_lo = s.color(0x48A03C, 0x326A40, 0x133620)
    trunk    = s.color(0x7A4E30, 0x5E3C30, 0x2A2030)
    trunk_lo = s.color(0x5C3822, 0x482C26, 0x1E1826)
    leaf     = s.color(0x3E8E3A, 0x356E40, 0x12301E)
    leaf_hi  = s.color(0x5AAE48, 0x4A8848, 0x1A4028)
    leaf_lo  = s.color(0x2C6E2E, 0x285636, 0x0C2416)
    moonlit  = s.color(0x264E36)                          # night only: the crown's top edge
    kite_a   = s.color(0x3A6FE0, 0x4A5CC8)                # the kite is put away at night
    kite_b   = s.color(0xFFD23A, 0xF0C050)
    bow      = s.color(0xE8404A, 0xD84058)
    string   = s.color(0xF4F0E0, 0xF4E0E8)
    rope     = s.color(0xE2CC98, 0xC8A890, 0x5A5060)
    wood     = s.color(0xC8955C, 0xB07A60, 0x4A3A40)
    wood_hi  = s.color(0xDDB078, 0xC4906C, 0x5A4850)
    wood_lo  = s.color(0x8A5A34, 0x744838, 0x2C2230)
    ink      = s.color(0x3A2416, 0x3A2028, 0xE8E0C8)    # night: the board is dark, letters pale
    daisy    = s.color(0xFFFFFF, 0xF4D8E4, 0x9AA4C8)
    eye      = s.color(0xFFD23A, 0xF0C050, 0x8A7A48)
    tulip    = s.color(0xE8406A, 0xD8406A, 0x6A2A48)
    blue_fl  = s.color(0x6A8CFF, 0x7A80E8, 0x3A4A9A)
    gore_a   = s.color(0x4A7CD8, 0x5A68C4)               # the balloon: blue with one wide
    gore_b   = s.color(0xF6F4EC, 0xF2DCE4)               # white band, cool so Clawd stays
    gore_lo  = s.color(0x3458A8, 0x44509C)               # the warm one
    meteor   = s.color(0xFFFFFF)

    # Sky bands, flat and clean.
    for i, (y, *_rest) in enumerate(SKY):
        y_end = SKY[i + 1][0] if i + 1 < len(SKY) else GRASS_TOP
        s.rect(0, y, W, y_end - y, sky[i])

    # Night: stars, a few of them twinkling (each its own tiny group, so a
    # twinkle redraws a few pixels, not the whole sky).
    with s.tod("night"):
        with s.shape():
            for x, y, big in ((14, 10, 0), (38, 34, 1), (62, 6, 0), (88, 22, 0), (112, 44, 0),
                              (134, 8, 1), (158, 30, 0), (186, 14, 0), (204, 52, 0),
                              (228, 6, 0), (300, 50, 0), (312, 8, 0), (8, 56, 0), (74, 60, 0),
                              (148, 62, 0), (176, 72, 0), (52, 78, 0), (290, 84, 0)):
                s.rect(x, y, 2, 2, star if big else star_dim)
        for i, (x, y) in enumerate(((26, 22), (100, 30), (170, 46), (214, 26), (306, 30))):
            with s.frames(period_ms=600, phase_ms=i * 470) as f:
                with f.frame():
                    s.rect(x - 2, y, 6, 2, star)
                    s.rect(x, y - 2, 2, 6, star)
                with f.frame():
                    s.rect(x, y, 2, 2, star)
                with f.frame():
                    s.rect(x, y, 2, 2, star_dim)
        # A full moon with a soft halo and a few craters.
        blob(s, 262, 30, 18, halo)
        blob(s, 262, 30, 12, moon)
        s.disc(258, 26, 3, moon_lo)
        s.disc(266, 34, 2, moon_lo)
        s.rect(265, 23, 2, 2, moon_lo)

    # Day: the sun high on the right, its rays twinkling.
    with s.tod("day"):
        cx, cy = 266, 30
        blob(s, cx, cy, 14, sun_lo)
        blob(s, cx - 1, cy - 1, 12, sun)
        blob(s, cx - 4, cy - 4, 5, sun_hi)
        with s.frames(period_ms=500) as f:
            with f.frame():
                for x, y, w, h in ((-1, -24, 2, 6), (-1, 18, 2, 6), (-24, -1, 6, 2), (18, -1, 6, 2)):
                    s.rect(cx + x, cy + y, w, h, sun)
            with f.frame():
                for x, y in ((-19, -19), (15, -19), (-19, 15), (15, 15)):
                    s.rect(cx + x, cy + y, 4, 4, sun)
                for x, y, w, h in ((-1, -22, 2, 4), (-1, 18, 2, 4), (-22, -1, 4, 2), (18, -1, 4, 2)):
                    s.rect(cx + x, cy + y, w, h, sun)

    # Dusk: a big low sun on the right, sinking behind the last peak (drawn
    # before the mountains, so the ridge cuts its lower edge).
    with s.tod("dusk"):
        cx, cy = 298, 58
        blob(s, cx, cy, 16, sun_lo)
        blob(s, cx, cy - 1, 13, sun)
        blob(s, cx - 5, cy - 6, 4, sun_hi)

    # Clouds drifting right at three speeds (parallax); small, so cheap to move.
    # The upper ones wrap back in behind the oak's crown and slide out of it,
    # so they never cross the leaves or the kite.
    puff = {"c": cloud, "d": cloud_lo}
    with s.drift(speed=2, wrap=(28, W)):
        s.pixels(96, 38, ["....cccc......", "..cccccccc.cc.", ".cccccccccccccc",
                          "cccccccccccccc.", ".dddddddddddd.."], puff, scale=3)
    with s.drift(speed=3, wrap=(30, W)):
        s.pixels(214, 30, ["..cc...", ".cccccc", "ddddddd"], puff, scale=3)
    with s.drift(speed=4, wrap=(30, W)):
        s.pixels(160, 44, ["..ccc...", ".cccccc.", "cccccccc", ".dddddd."], puff, scale=3)
    with s.drift(speed=7), s.tod("day", "dusk"):
        s.pixels(206, 60, [".cc..", "ccccc", "ddddd"], puff, scale=2)

    # A hot-air balloon hanging in the open sky, bobbing gently (a 24x26 box
    # every 900 ms): blue with one wide white band, a shaded right side.
    rows = ((3, 8), (1, 10), (0, 11), (0, 11), (1, 10), (2, 9), (3, 8), (4, 7))
    art = []
    for a, b in rows:
        art.append("".join("." if not a <= c <= b else "d" if c >= b - 1
                           else "b" if 4 <= c <= 6 else "a" for c in range(12)))
    art += ["....r..r....", "....kkkk....", "....kkkk...."]
    balloon = {"a": gore_a, "b": gore_b, "d": gore_lo, "r": rope, "k": wood_lo}
    with s.tod("day", "dusk"):
        s.frames_of([lambda dy=dy: s.pixels(146, 6 + dy, art, balloon, scale=2) for dy in (0, 2)],
                    period_ms=900)

    # Night: now and then a shooting star streaks across, high above the
    # mountains (a fast drift that spends most of its loop off screen).
    with s.drift(speed=-150, wrap=(-40, 2200)), s.tod("night"):
        s.rect(100, 4, 4, 2, meteor)
        s.rect(104, 4, 8, 2, star)
        s.rect(112, 4, 10, 2, star_dim)
        s.rect(122, 4, 10, 2, halo)

    # Far mountains: lit left faces, shaded right faces, toothed snow caps.
    peaks = ((44, 62, 1.3), (128, 74, 1.5), (212, 56, 1.25), (300, 70, 1.4))

    def mountain(x, y):
        best = None
        for px, top, slope in peaks:
            t = top + max(0.0, abs(x + 0.5 - px) - 1.5) / slope
            if best is None or t < best[0]:
                best = (t, px, top)
        t, px, top = best
        if t > y + 1:
            return None
        lit = x < px + 2 + ((y - top) // 6) * 2      # the ridge leans right going down
        if y < top + 10 - abs(x - px) // 3:          # a cap that points down the middle
            return snow if lit else snow_lo
        return mtn if lit else mtn_lo
    terrain(s, 56, 116, 2, mountain)

    # Far hills (hazy green) with a few little pines on top.
    steps = ((40, 104), (62, 106), (90, 108), (112, 110), (208, 112), (230, 110),
             (252, 108), (272, 106), (W, 104))                 # (right edge, top): flat steps

    def far_hill(x, y):
        t = next(top for x1, top in steps if x < x1)
        return hill_far if y >= t else None
    terrain(s, 104, 116, 2, far_hill)
    s.rect(0, 116, W, GRASS_TOP - 116, hill_far)
    for x, y in ((64, 98), (76, 102), (232, 102), (244, 98)):
        s.pixels(x, y, ["..t..", ".ttt.", ".ttt.", "ttttt", "ttttt", "..t.."], {"t": tree_far}, scale=2)

    # Near hills, rolling down to the lawn.
    def near_hill(x, y):
        bumps = ((30, 112, 70), (160, 122, 120), (290, 114, 70))
        t = min(top + ((x - cx) * (x - cx)) // (r * 4) for cx, top, r in bumps)
        if y + 1 < t:
            return None
        return hill_hi if y < t + 3 else hill
    terrain(s, 110, GRASS_TOP, 2, near_hill)

    # The lawn: a light lip, calm grass where he walks, darker grass in front.
    s.rect(0, GRASS_TOP, W, H - GRASS_TOP, grass)
    s.rect(0, GRASS_TOP, W, 2, grass_hi)
    s.rect(0, FLOOR_Y + 3, W, H - FLOOR_Y - 3, grass_lo)

    # The oak on the left: a trunk up into a round crown, a branch growing out
    # of it to the right (thick at the root, kinked, tapering to a leafy tip)
    # with a swing hanging from it, and a kite snagged by its string on top.
    tx = 20
    s.rect(tx, 56, 12, GRASS_TOP + 4 - 56, trunk)
    s.rect(tx + 8, 56, 4, GRASS_TOP + 4 - 56, trunk_lo)
    s.rect(tx - 4, GRASS_TOP + 2, 20, 3, trunk_lo)                     # roots
    for x, y, w, h in ((tx + 10, 54, 18, 8), (tx + 26, 57, 14, 6),     # the swing's branch
                       (tx + 38, 59, 12, 5), (tx + 48, 60, 12, 4)):
        s.rect(x, y, w, h, trunk)
        s.rect(x, y + h - 2, w, 2, trunk_lo)
    s.rect(tx + 24, 52, 6, 3, trunk)                                   # a knot at the kink
    s.rect(tx + 26, 50, 2, 2, trunk_lo)
    crown = (
        "......hhhh..........",
        "...hhhhhhhhll.......",
        "..hhhhhhhllllll.....",
        ".hhhhhhllllllllll...",
        "hhhhhllllllllllllo..",
        "hhhlllllllllllllooo.",
        "hhllllllllllloooooo.",
        ".llllllooollloooooo.",
        "..lllooooooooooooo..",
        "....oooo....oooo....",
    )
    s.pixels(0, 18, crown, {"h": leaf_hi, "l": leaf, "o": leaf_lo}, scale=4)
    s.pixels(62, 50, ["..hh..", ".hllo.", "hlllo.", ".loo.."], {"h": leaf_hi, "l": leaf, "o": leaf_lo},
             scale=3)                                                  # leaves at the branch tip
    # Night: the moon (top right) catches the crown's upper edge.
    with s.tod("night"), s.shape():
        for r, row in enumerate(crown):
            for c, ch in enumerate(row):
                if ch != "." and c >= 6 and (r == 0 or crown[r - 1][c] == "."):
                    s.rect(c * 4, 18 + r * 4, 4, 2, moonlit)
    # The kite: a diamond in the open sky right of the crown, its string
    # running down into the leaves where it snagged, its tail fluttering
    # (taken in at night).
    kx, ky = 82, 6
    with s.tod("day", "dusk"):
        s.pixels(kx, ky, ["...a...", "..aab..", ".aaabb.", "aaaabbb", ".bbbaa.", "..bba..",
                          "...b..."], {"a": kite_a, "b": kite_b}, scale=3)
        with s.shape():
            for i in range(8):
                s.rect(kx - 2 - i * 2, ky + 11 + i * 2, 2, 2, string)
    with s.tod("day", "dusk"), s.frames(period_ms=350) as f:
        with f.frame():
            s.pixels(kx + 8, ky + 21, ["s..", ".r.", "s..", ".s.", "r.."], {"s": string, "r": bow}, scale=2)
        with f.frame():
            s.pixels(kx + 8, ky + 21, ["s..", "r..", ".s.", "..r", ".s."], {"s": string, "r": bow}, scale=2)

    # The swing, gently going back and forth (ropes stepped 1 px per segment),
    # hung close to the trunk so it peeks out beside him when he stands left.
    def swing(lean):
        bottom = 120
        for rx, top in ((37, 62), (55, 63)):
            seg = (bottom - top) // 5 + 1
            for i in range(5):
                s.rect(rx + (lean * i) // 4, top + i * seg, 2, min(seg, bottom - top - i * seg), rope)
        sx = 35 + lean
        s.rect(sx, bottom, 24, 3, wood)
        s.rect(sx, bottom + 3, 24, 2, wood_lo)
    s.frames_of([lambda: swing(0), lambda: swing(3), lambda: swing(0), lambda: swing(-3)],
                period_ms=450)

    # Grass tufts swaying along the back of the lawn (one small strip): hand
    # placed clumps of 1, 2 or 3 blades, bottoms aligned.
    blades = {
        1: (("g", "g"), (".g", "g.")),
        2: (("g.g", "g.g"), (".g.g", "g.g.")),
        3: (("..g..", "g.g.g", "g.g.g"), ("...g..", ".g.g.g", "g.g.g.")),
    }
    tufts = ((52, 2), (71, 1), (101, 3), (138, 2), (176, 1), (194, 2), (229, 3), (262, 1), (297, 2))
    blade = {"g": grass_hi}
    s.frames_of([
        lambda k=k: [s.pixels(x, GRASS_TOP + 6 - 2 * len(blades[n][k]), blades[n][k], blade, scale=2)
                     for x, n in tufts]
        for k in (0, 1)
    ], period_ms=900)

    # Flowers along the front edge, below his feet, in small loose clusters.
    for x, dy, kind in ((14, 0, 0), (38, 0, 1), (45, 2, 1), (96, 1, 2), (150, 0, 0), (205, 1, 2),
                        (212, 0, 0), (236, 2, 1), (281, 0, 0), (306, 1, 2)):
        y = FLOOR_Y + 1 + dy
        if kind == 0:
            s.pixels(x, y, [".w.", "wew", ".w."], {"w": daisy, "e": eye}, scale=2)
        elif kind == 1:
            s.pixels(x, y, ["t.t", "ttt", ".g."], {"t": tulip, "g": grass_hi}, scale=2)
        else:
            s.pixels(x, y, ["b.b", ".b.", ".g."], {"b": blue_fl, "g": grass_hi}, scale=2)

    # The signpost on the right: OUT TO / PLAY! in the 2x font.
    l1, l2 = "OUT TO", "PLAY!"
    tw = text_width(l1, 2)
    bx, by, bw, bh = W - 2 - (tw + 10), 86, tw + 10, 30
    for px in (bx + 6, bx + bw - 10):
        s.rect(px, by + bh, 4, GRASS_TOP + 3 - by - bh, wood_lo)
    s.rect(bx, by, bw, bh, wood_lo)
    s.rect(bx + 2, by + 2, bw - 4, bh - 4, wood)
    s.rect(bx + 2, by + 2, bw - 4, 2, wood_hi)
    s.text(bx + 5, by + 6, l1, ink, scale=2)
    s.text(bx + 5 + (tw - text_width(l2, 2)) // 2, by + 18, l2, ink, scale=2)
    s.anchor("SIGN", bx + 5, by + 6, tw, 22)

    # Not used outside (he only sits at the laptop in the office).
    s.anchor("DESK", (W - 24 * 5) // 2)
    return s
