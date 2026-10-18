"""The office: where Clawd sits at his laptop while Claude works.

A cosy study on a 2 px grid: teal walls over panelled wainscot, cream trim, a
plank floor with a navy rug where he sits. Left, a big window onto the city
(blue sky and drifting clouds by day, a setting sun at dusk, moon, stars and
lit windows at night) above a bookcase; centre, a red "CODING" lightbox and a
green banker's pendant lamp right over him; right, a framed poster of this
week's usage (live, scene.cpp draws it from burndown.h: the % used now in its
pace colour with a verdict, over a burn-up of the week so far), an LED
display counting down to the next 5-hour window over a water cooler that
glugs, and sticky notes. By day the
lamp is off and the window throws two panes of sun on the planks. When the lamp
is on (dusk and night) the bulb glows amber and a narrow cone lifts the wall
behind him in three dithered steps of its own cool hue, down to a pool on the
rug at his feet, so he sits in the light and stays the focal point in the dark;
now and then it flickers. The band behind him (x 112..282, y 47..162) is kept
to big calm shapes; nothing orange or tan sits near him.
"""
from scenekit import (Scene, W, H, FLOOR_Y, WEEK_MINS, text_width, mix, shade, div_round,
                      chart_row, chart_col_mid)

CEIL = 6                 # ceiling and crown trim above this
RAIL_Y = 104             # chair rail: plain wall above, panelled wainscot below
FLOOR_TOP = 146          # the wall meets the floor here; his feet are at FLOOR_Y
DESK_X = 112             # his body-left at the laptop (the laptop reaches ~DESK_X + 165)
LAMP_X = 192             # the pendant lamp's centre, over his head at the laptop (x 142..221)
BEAM_Y = 30              # the beam starts under the bulb
# The cone fades as it falls: (above this y, step), brightest by the bulb. Each
# seam is a 4-row checker dither, so it reads as falloff, not stripes.
CONE_STEPS = ((40, 3), (58, 2), (RAIL_Y, 1))
FLICKER_Y = 48           # the flicker dims the cone's bright top only (a small redraw)
WARM = 0xFFCF8A          # the lamp's light
# The light, in steps: 1 = faint .. 3 = brightest (the cone by the bulb).
# On the wall it is a soft lift in the wall's own cool hue, never warmed (warm
# tans there would sit in his terra-cotta's family, round his head); the amber
# stays in the bulb's glow and the pool on the rug. The floor is mixed toward
# the light; the rug keeps its blue and only lifts.
# Small, uneven lifts: the bulk of the cone nearly vanishes into the wall and
# the value gathers by the bulb (step 3 takes a whisper of the bulb's warmth).
WALL_RAMP = ((0x55506E, 0x605A78, 0x706886),     # dusk
             (0x22304A, 0x2C3A56, 0x384864))     # night
LIGHT = (0xFFC888, 0xFFB878)
FLOOR_T = (0.0, 0.22, 0.34)
RUG_LIFT = ((1.0, 0.0), (1.3, 0.08), (1.55, 0.12))     # (lift, warm mix) at dusk;
RUG_NIGHT = 1.1                                         # night lifts this much more
SUN = 0xFFE4AC           # by day the window throws two slanted panes of sun on the planks
SUN_T = 0.2


def sun_panes():
    """Where the window's daylight lands on the floor in front of the bookcase,
    as (y0, y1, x0, x1) rects: two panes split by the mullion's shadow, sliding
    left 2 px every 4 rows (the sun is up and to the right)."""
    out = []
    for y in range(FLOOR_TOP + 2, H, 4):
        sh = (y - FLOOR_TOP - 2) // 4 * 2
        out.append((y, y + 4, 22 - sh, 50 - sh))
        out.append((y, y + 4, 56 - sh, 88 - sh))
    return out


def beam_steps():
    """Where the lamp's light falls, as (y0, y1, x0, x1, step) bands on the 2 px
    grid. A narrow cone from the bulb down the wall (a clean 2:1 stair, x
    152..232 at the rail) that fades in three steps and stops at the chair
    rail; where two steps meet, two rows of 2 px cells checker into each other.
    Below his feet the light lands as a pool: one band on the rug with a
    brighter core, wider where it spills onto the planks below the rug."""
    out = []
    seams = [y_end for y_end, _ in CONE_STEPS[:-1]]
    for y in range(BEAM_Y, RAIL_Y, 2):
        half = 4 + (y - BEAM_Y) // 4 * 2
        x0, x1 = LAMP_X - half, LAMP_X + half
        step = next(st for y_end, st in CONE_STEPS if y < y_end)
        # Across a seam the odd cells take the other step: a 4-row checker.
        other = (step - 1 if (y + 2 in seams or y + 4 in seams) else
                 step + 1 if (y in seams or y - 2 in seams) else 0)
        out.append((y, y + 2, x0, x1, step))
        if other:
            for x in range(x0 + (x0 // 2 + y // 2) % 2 * 2, x1, 4):    # the odd cells
                out.append((y, y + 2, x, x + 2, other))
        # Soft edges: every other row the outer cell each side is a step
        # dimmer, so the outline is a checker, not a hard stair.
        if (y // 2) % 2:
            out.append((y, y + 2, x0, x0 + 2, step - 1))
            out.append((y, y + 2, x1 - 2, x1, step - 1))
    for y0, y1, half, core in ((FLOOR_Y, 168, 60, 34), (168, H, 76, 42)):
        out.append((y0, y1, LAMP_X - half, LAMP_X + half, 1))
        out.append((y0, y1, LAMP_X - core, LAMP_X + core, 2))
    return out


def scene():
    s = Scene("office")
    beam = beam_steps()
    panes = sun_panes()
    tones = {}               # colour -> its (dusk, night) values, for the lit steps
    lit_cache = {}
    lit_wall = []            # the cone's top, re-drawn a step dimmer when it flickers

    def col(day, dusk, night, kind="floor"):
        """A colour the lamp's light falls on, of kind wall, floor or rug (its
        lit steps are made on demand and only show at dusk and night)."""
        c = s.color(day, dusk, night)
        tones[c] = (day, dusk, night, kind)
        return c

    def lit(c, step):
        """c lit by the lamp, step 0 (unlit) .. 5 (the floor and rug go to 2)."""
        if step <= 0:
            return c
        if (c, step) not in lit_cache:
            day, dusk, night, kind = tones[c]
            if kind == "wall":
                d, n = WALL_RAMP[0][step - 1], WALL_RAMP[1][step - 1]
            elif kind == "rug":
                lift, t = RUG_LIFT[step]
                d = mix(shade(dusk, lift), WARM, t)
                n = mix(shade(night, 1 + (lift - 1) * RUG_NIGHT), WARM, t)
            else:
                d, n = mix(dusk, LIGHT[0], FLOOR_T[step]), mix(night, LIGHT[1], FLOOR_T[step])
            lit_cache[c, step] = s.color(day, d, n)
        return lit_cache[c, step]

    def paint(x, y, w, h, c):
        """A rect, then the parts of it in the lamp's light in the lit colours."""
        s.rect(x, y, w, h, c)
        if c not in tones:
            return
        for y0, y1, x0, x1, step in beam:
            ix0, iy0 = max(x, x0), max(y, y0)
            ix1, iy1 = min(x + w, x1), min(y + h, y1)
            if ix1 > ix0 and iy1 > iy0:
                s.rect(ix0, iy0, ix1 - ix0, iy1 - iy0, lit(c, step), tod=("dusk", "night"))
                if c == wall and iy0 < FLICKER_Y:
                    lit_wall.append((ix0, iy0, ix1 - ix0, iy1 - iy0, step))
        if tones[c][3] != "floor":
            return
        for y0, y1, x0, x1 in panes:
            ix0, iy0 = max(x, x0), max(y, y0)
            ix1, iy1 = min(x + w, x1), min(y + h, y1)
            if ix1 > ix0 and iy1 > iy0:
                if ("sun", c) not in lit_cache:
                    lit_cache["sun", c] = s.color(mix(tones[c][0], SUN, SUN_T))
                s.rect(ix0, iy0, ix1 - ix0, iy1 - iy0, lit_cache["sun", c], tod="day")

    # ─── Palette ─────────────────────────────────────────────────────────────
    # Room (the wall, floor and rug take the lamp's light after dark).
    wall      = col(0x3F6470, 0x4E4866, 0x1E2B3E, "wall")
    wall_dk   = s.color(0x2F4C57, 0x3E3852, 0x172234)
    ceil      = s.color(0x294049, 0x322D45, 0x121A27)
    trim      = s.color(0xE4DCC8, 0xD8BFA8, 0x6C6E78)
    wain      = s.color(0x35545F, 0x433D58, 0x19253A)
    wain_dk   = s.color(0x284049, 0x342F48, 0x111A2A)
    wain_hi   = s.color(0x4C7480, 0x5A5274, 0x26354C)
    skirt     = s.color(0x22363E, 0x2C283C, 0x0F1520)
    floor     = col(0x7A5A3E, 0x6C4E3E, 0x382C2A)
    floor_dk  = col(0x5E432E, 0x553C31, 0x2A211F)
    floor_hi  = s.color(0x8C6B4B, 0x7E5C49, 0x433530)
    rug       = col(0x2C376A, 0x30305E, 0x18203E, "rug")
    rug_bd    = col(0x5868A6, 0x5A5890, 0x2C3866, "rug")
    rug_in    = col(0x44529A, 0x484688, 0x28325E, "rug")
    fringe    = s.color(0xDCD2BA, 0xCDB8A2, 0x5E5E68)
    # The view: sky by time of day.
    sky1      = s.color(0x5FB0EA, 0x4B3E86, 0x0C1230)
    sky2      = s.color(0x7EC4F0, 0xB25E8E, 0x111A40)
    sky3      = s.color(0xA8DCF8, 0xF0905E, 0x18234E)
    cloud     = s.color(0xFFFFFF, 0xF6B6A0, 0xFFFFFF)
    cloud_lo  = s.color(0xD6ECF8, 0xD9849A, 0xD6ECF8)
    sun       = s.color(0xFFF2A8, 0xF8C878, 0xFFF2A8)
    moon      = s.color(0xF4EECC)
    star      = s.color(0xFFFFFF, 0xFFFFFF, 0xE8ECFF)
    far       = s.color(0x88B4D0, 0x8E5A8C, 0x1B2552)
    near      = s.color(0x5F86A6, 0x4A2F62, 0x0B1130)
    glass     = s.color(0xC8E4F4, 0xFFC46A, 0xFFD27A)
    beacon    = s.color(0xFF4A3A)
    blind     = s.color(0xEDE6D4, 0xE0CBB4, 0x777784)
    blind_dk  = s.color(0xC9C0AA, 0xBBA48E, 0x5A5A66)
    # Things.
    wood      = s.color(0x8A5E3C, 0x7C5240, 0x4A3834)
    wood_dk   = s.color(0x5E3E28, 0x553628, 0x30231F)
    wood_hi   = s.color(0xA8784E, 0x996A50, 0x5C4640)
    shelf_bk  = s.color(0x2A211C, 0x2A1F20, 0x151112)
    bk_red    = s.color(0xB0473C, 0xA4423E, 0x5A2A2C)
    bk_gold   = s.color(0xD6A43C, 0xCC9440, 0x6A5532)
    bk_teal   = s.color(0x3A9088, 0x3E7C7C, 0x1F4A4A)
    bk_cream  = s.color(0xE6DABC, 0xD9C4A8, 0x6E6A64)
    bk_navy   = s.color(0x34497E, 0x383E72, 0x1C2644)
    bk_green  = s.color(0x5C8E48, 0x58804A, 0x2C4630)
    bk_plum   = s.color(0x7E4A72, 0x76446C, 0x3E2840)
    pot       = s.color(0xB86A45, 0xA85F40, 0x5E3A2A)
    pot_dk    = s.color(0x8E4C30, 0x7E4430, 0x44281E)
    leaf      = s.color(0x3F8A44, 0x417A44, 0x1E3E26)
    leaf_hi   = s.color(0x6CBF5E, 0x62A856, 0x2D5A34)
    globe_sea = s.color(0x3E7EC8, 0x4A6CB0, 0x1E3460)
    globe_lnd = s.color(0x6CBF5E, 0x6AA858, 0x2E5434)
    brass     = s.color(0xD4A94E, 0xC8964E, 0x6E5A38)
    mug_c     = s.color(0xF2EFE8, 0xE6D8C8, 0x8C8780)
    mug_dk    = s.color(0xC8C2B6, 0xBBAA9A, 0x66625E)
    steam     = s.color(0xE4ECF2, 0xE8D2C4, 0x8A92AA)
    heart     = s.color(0xD04A4A, 0xC4464A, 0x7A3030)
    shade_c   = s.color(0x2E6A52, 0x2E5A4C, 0x1C3E34)
    shade_hi  = s.color(0x4E9474, 0x4A806C, 0x2E5A4C)
    cord      = s.color(0x1C2228, 0x1C1C26, 0x0A0E14)
    bulb_on   = s.color(0xFFF0B0, 0xFFF0B0, 0xFFF4C0)
    bulb_dim  = s.color(0xD8D4C0, 0xE8D49A, 0xE8D49A)
    halo      = s.color(0xFFE6A0)
    halo_dim  = s.color(0xD8BE84)
    box       = s.color(0x16191E, 0x16161C, 0x0C0E12)
    box_in    = s.color(0x2A1618, 0x2C1618, 0x2A1214)
    led       = s.color(0xFF5040, 0xFF5A44, 0xFF5A44)
    led_dk    = s.color(0x7A1E1A, 0x7A1E1A, 0x701A18)    # under the glyphs
    led_glow  = s.color(0x3F6470, 0x6A3E50, 0x4A2230)
    paper     = s.color(0xF4F1E8, 0xEADCCB, 0xB8B4AC)
    ink       = s.color(0x2A2E36, 0x2A2A34, 0x1A1C22)
    bar_a     = s.color(0x3A9088, 0x3E7C7C, 0x2F6B66)
    bar_b     = s.color(0x34497E, 0x383E72, 0x26305A)
    pace_c    = s.color(0x8F8A7E, 0x8C7E72, 0x5C5A58)    # the ideal pace dots on the paper
    lvl_ok    = s.color(0x2A8A3A, 0x2E7A3A, 0x1C4E26)    # the weekly number by pace (ui.cpp
    lvl_ahead = s.color(0xC27406, 0xB46A10, 0x6A420C)    # bar_color's green / amber / red,
    lvl_out   = s.color(0xCC3428, 0xBC342C, 0x7C221E)    # dark enough to read on the paper)
    seg_off   = s.color(0x381A1A, 0x3A1A1A, 0x381818)    # the LED's unlit segments
    jug       = s.color(0x8CCCEE, 0x9CB8DA, 0x3C5C7A)
    jug_hi    = s.color(0xD4F0FC, 0xD8E0F0, 0x9ABEDC)
    jug_air   = s.color(0xBEE4F6, 0xC4D2E8, 0x6A8CAA)
    jug_cap   = s.color(0x2E5E9A, 0x30508A, 0x1A2E50)
    cool      = s.color(0xE2E6EA, 0xD8D0D0, 0x6A7080)
    cool_dk   = s.color(0xB0B8C0, 0xA8A0A6, 0x4A5060)
    tap_r     = s.color(0xD84A3A, 0xCC463C, 0x7A3028)
    tap_b     = s.color(0x3A78C8, 0x3A68B0, 0x24406E)
    note_y    = s.color(0xF2D86A, 0xE6C468, 0x6E6440)
    note_y_dk = s.color(0xC8AE48, 0xB89A4C, 0x524A30)
    note_p    = s.color(0xF0A0B4, 0xE092A6, 0x6E4A58)
    note_p_dk = s.color(0xC87A8E, 0xB87084, 0x523844)

    # ─── The view (drawn first: the wall's window hole clips the clouds) ─────
    gx0, gy0, gx1, gy1 = 12, 14, 100, 74          # the glass
    s.rect(gx0, gy0, gx1 - gx0, 24, sky1)
    s.rect(gx0, gy0 + 24, gx1 - gx0, 12, sky2)
    s.rect(gx0, gy0 + 36, gx1 - gx0, gy1 - gy0 - 36, sky3)
    for x in range(gx0, gx1, 12):                 # a 2 px saw where the top bands meet
        s.rect(x, gy0 + 22, 6, 2, sky2)
    # Sun, setting sun and moon as 2 px-grid glyphs (like everything else).
    s.pixels(80, 26, [".sss.", "sssss", "sssss", "sssss", ".sss."], {"s": sun},
             scale=2, tod="day")
    s.pixels(62, 52, ["..sss..", ".sssss.", "sssssss", "sssssss", "sssssss", ".sssss.",
                      "..sss.."], {"s": sun}, scale=2, tod="dusk")   # setting behind the city
    with s.tod("night"):
        s.pixels(78, 24, ["..mmm.", ".mmm..", "mmm...", "mmm...", ".mmm..", "..mmm."],
                 {"m": moon}, scale=2)            # a crescent
        for x, y in ((20, 32), (40, 24), (62, 36), (30, 46), (94, 44)):
            s.rect(x, y, 2, 2, star)
    with s.frames(period_ms=700) as f:            # two stars twinkle in turn
        with f.frame():
            s.rect(50, 30, 2, 2, star, tod="night")
        with f.frame():
            s.rect(72, 46, 2, 2, star, tod="night")
    puff = ["..aaa...", ".aaaaaa.", "abbbbbbb"]
    for x, y, rows, v in ((14, 30, puff, 2), (60, 42, puff[1:], 3)):   # each wraps on its own
        with s.tod("day", "dusk"), s.drift(speed=v, wrap=(gx0 - 16, gx1)):
            s.pixels(x, y, rows, {"a": cloud, "b": cloud_lo}, scale=2)
    # Far towers, then near ones with windows (more of them lit at night).
    with s.shape():
        for x, y, w in ((12, 50, 10), (22, 42, 8), (30, 54, 12), (48, 56, 8), (80, 50, 8),
                        (88, 40, 12)):
            s.rect(x, y, w, gy1 - y, far)
    near_towers = ((12, 58, 14), (26, 50, 12), (40, 60, 14), (58, 66, 18), (78, 56, 22))
    with s.shape():
        for x, y, w in near_towers:
            s.rect(x, y, w, gy1 - y, near)
    s.rect(30, 42, 2, 8, near)                    # antenna
    for i, (x, y, w) in enumerate(near_towers):
        for wy in range(y + 4, gy1 - 2, 6):
            for k, wx in enumerate(range(x + 2, x + w - 2, 4)):
                v = (i + k + wy // 6) % 3
                if v == 0 and (wy // 6 + k) % 2 == 0:  # a few glint by day
                    s.rect(wx, wy, 2, 2, glass)
                elif v == 0:
                    s.rect(wx, wy, 2, 2, glass, tod=("dusk", "night"))
                elif v == 1:
                    s.rect(wx, wy, 2, 2, glass, tod="night")
    with s.frames(period_ms=600) as f:            # the antenna's beacon
        with f.frame():
            s.rect(30, 40, 2, 2, beacon, tod=("dusk", "night"))
        with f.frame():
            pass
    # Blinds pulled up into a bundle, with the cord hanging down the right edge.
    s.rect(gx0, gy0, gx1 - gx0, 10, blind)
    s.rect(gx0, gy0 + 4, gx1 - gx0, 2, blind_dk)
    s.rect(gx0, gy0 + 10, gx1 - gx0, 2, blind_dk)
    s.rect(gx1 - 2, gy0 + 12, 2, 24, blind_dk)
    s.rect(gx1 - 4, gy0 + 36, 4, 4, blind_dk)

    # ─── Walls ───────────────────────────────────────────────────────────────
    paint(0, 0, W, CEIL - 2, ceil)
    paint(0, CEIL - 2, W, 2, trim)
    paint(0, CEIL, W, 2, wall_dk)                 # the trim's shadow
    paint(0, CEIL + 2, W, gy0 - 4 - CEIL - 2, wall)
    paint(0, gy0 - 4, gx0 - 4, gy1 - gy0 + 8, wall)
    paint(gx1 + 4, gy0 - 4, W - gx1 - 4, gy1 - gy0 + 8, wall)
    paint(0, gy1 + 4, W, RAIL_Y - gy1 - 4, wall)
    # Chair rail, wainscot panels, skirting.
    paint(0, RAIL_Y, W, 2, trim)
    paint(0, RAIL_Y + 2, W, 2, wain_dk)
    paint(0, RAIL_Y + 4, W, FLOOR_TOP - 8 - RAIL_Y - 4, wain)
    for x in range(4, W, 40):
        paint(x, RAIL_Y + 8, 32, 24, wain_dk)
        paint(x + 2, RAIL_Y + 10, 30, 22, wain_hi)
        paint(x + 2, RAIL_Y + 10, 28, 20, wain)
    paint(0, FLOOR_TOP - 8, W, 2, trim)
    paint(0, FLOOR_TOP - 6, W, 6, skirt)
    # Two sticky notes pinned to the panel right of him (above the laptop),
    # each with a curled corner.
    for x, y, c, dk, pin in ((248, 108, note_y, note_y_dk, tap_r), (262, 112, note_p, note_p_dk, tap_b)):
        s.pixels(x, y, ["cpcc", "cccc", "cccc", "cccd"], {"c": c, "d": dk, "p": pin}, scale=2)

    # Window frame, mullion and sill.
    with s.shape():
        s.rect(gx0 - 4, gy0 - 4, gx1 - gx0 + 8, 4, trim)
        s.rect(gx0 - 4, gy0, 4, gy1 - gy0, trim)
        s.rect(gx1, gy0, 4, gy1 - gy0, trim)
        s.rect(gx0 - 4, gy1, gx1 - gx0 + 8, 4, trim)
        s.rect(54, gy0 + 12, 4, gy1 - gy0 - 12, trim)
    s.rect(gx0 - 8, gy1 + 4, gx1 - gx0 + 16, 4, trim)
    s.rect(gx0 - 6, gy1 + 8, gx1 - gx0 + 12, 2, wall_dk)

    # ─── Floor: planks, then the rug he sits on ──────────────────────────────
    paint(0, FLOOR_TOP, W, H - FLOOR_TOP, floor)
    paint(0, FLOOR_TOP, W, 2, floor_hi)
    for i, y in enumerate((FLOOR_TOP + 8, FLOOR_TOP + 16, FLOOR_TOP + 24)):
        paint(0, y, W, 2, floor_dk)
        for x in range(18 + (i % 2) * 34 - 34 * (i // 2), W, 68):
            if x >= 0:
                paint(x, y - 8 + 2, 2, 6, floor_dk)
    rx0, rx1, ry = 104, 290, 154
    paint(rx0, ry, rx1 - rx0, 14, rug_bd)
    paint(rx0 + 2, ry + 2, rx1 - rx0 - 4, 10, rug)
    paint(rx0 + 4, ry + 4, rx1 - rx0 - 8, 6, rug_in)        # an inner border line
    paint(rx0 + 6, ry + 6, rx1 - rx0 - 12, 2, rug)
    with s.shape():
        for y in range(ry, ry + 14, 4):
            s.rect(rx0 - 4, y, 4, 2, fringe)
            s.rect(rx1, y, 4, 2, fringe)

    # ─── Bookcase under the window ───────────────────────────────────────────
    bx0, bx1, by0 = 4, 104, 108
    s.rect(bx0, by0, bx1 - bx0, FLOOR_TOP - by0, wood)
    s.rect(bx0 - 2, by0 - 4, bx1 - bx0 + 4, 4, wood_hi)       # top board
    s.rect(bx0 - 2, by0, bx1 - bx0 + 4, 2, wood_dk)
    s.rect(bx0 + 4, by0 + 4, bx1 - bx0 - 8, 14, shelf_bk)
    s.rect(bx0 + 4, by0 + 22, bx1 - bx0 - 8, 12, shelf_bk)
    s.rect(bx0 + 2, FLOOR_TOP - 4, bx1 - bx0 - 4, 2, wood_dk)
    spines = (bk_red, bk_gold, bk_teal, bk_cream, bk_navy, bk_green, bk_plum)
    widths = (6, 4, 6, 4, 6, 6, 4, 6, 4, 6, 6, 4, 6, 6, 4)
    for row, (top, depth) in enumerate(((by0 + 4, 14), (by0 + 22, 12))):
        x, k = bx0 + 6, row * 3
        for i, w in enumerate(widths):
            if x + w > bx1 - 6:
                break
            if (i + row) % 5 == 4:              # a gap, or a book leaning in it
                if row == 0:
                    s.pixels(x, top + depth - 10, [".cc", ".cc", "cc.", "cc.", "cc."],
                             {"c": spines[(k + 3) % 7]}, scale=2)
                x += 6
                continue
            h = depth - (2 if (i * 7 + row) % 3 == 0 else 0)
            c = spines[k % 7]
            s.rect(x, top + depth - h, w, h, c)
            if w == 6:
                s.rect(x, top + depth - h + 2, w, 2, bk_gold if c != bk_gold else bk_cream)
            x += w
            k += 1

    # On top: a plant, a pile of books, his coffee, a globe.
    top = by0 - 4                                 # everything stands on the top board
    s.pixels(10, top - 26, [
        "..l...h.....",
        ".ll..hh..l..",
        ".lll.hh.ll..",
        "..lllhhhl...",
        "l..lhhhll.h.",
        "ll.llhhlll..",
        ".lllhhhll...",
        "...lhhl.....",
    ], {"l": leaf, "h": leaf_hi}, scale=2)
    s.pixels(12, top - 10, ["pppppppppp", "dddddddddd", ".pppppppp.", ".pppppppp.",
                            "..pppppp.."], {"p": pot, "d": pot_dk}, scale=2)
    s.rect(34, top - 6, 20, 6, bk_navy)
    s.rect(36, top - 6, 16, 2, bk_cream)
    s.rect(36, top - 10, 18, 4, bk_red)
    s.rect(38, top - 14, 14, 4, bk_teal)
    # The coffee, steaming up the wall.
    mx, my = 58, top - 10
    s.pixels(mx, my, ["mmmm..", "mmmmmm", "mhmm.m", "mmmmmm", "dddd.."],
             {"m": mug_c, "h": heart, "d": mug_dk}, scale=2)
    s.frames_of([
        lambda: (s.rect(mx + 2, my - 4, 2, 2, steam), s.rect(mx + 4, my - 8, 2, 2, steam)),
        lambda: (s.rect(mx + 4, my - 4, 2, 2, steam), s.rect(mx + 2, my - 8, 2, 2, steam)),
        lambda: (s.rect(mx + 4, my - 6, 2, 2, steam), s.rect(mx + 6, my - 10, 2, 2, steam)),
    ], period_ms=300)
    # The globe on its stand, in brass.
    s.pixels(74, top - 22, [
        "..sssss...",
        ".ssgsssb..",
        "sggggss.b.",
        "ssgggsgs.b",
        "sssgsggs.b",
        "ssssggss.b",
        "sssgssss.b",
        ".sssssss.b",
        "..sssss.b.",
        "...bbbbb..",
    ], {"s": globe_sea, "g": globe_lnd, "b": brass}, scale=2)
    s.rect(78, top - 2, 14, 2, wood_dk)

    # ─── "CODING" lightbox, its dot blinking like a recording light ──────────
    label = "CODING"
    lx, ly = 110, 12
    lw = text_width(label, 2) + 20
    s.rect(lx - 2, ly - 2, lw + 4, 24, led_glow, tod=("dusk", "night"))
    s.rect(lx, ly, lw, 20, box)
    s.rect(lx + 2, ly + 2, lw - 4, 16, box_in)
    s.text(lx + 16, ly + 6, label, led_dk, scale=2)   # a 1 px dark edge under the glyphs
    s.text(lx + 15, ly + 5, label, led, scale=2)
    with s.frames(period_ms=500) as f:
        with f.frame():
            s.disc(lx + 9, ly + 10, 3, led)
        with f.frame():
            pass

    # ─── Banker's pendant lamp over him; the beam is painted with the room ───
    s.rect(LAMP_X - 1, CEIL, 2, 14, cord)
    s.pixels(LAMP_X - 12, CEIL + 12, [
        "....dddd....",
        "..ddhhdddd..",
        ".ddhhddddd..",
        "dddddddddddd",
        "bbbbbbbbbbbb",
    ], {"d": shade_c, "h": shade_hi, "b": brass}, scale=2)
    def bulb(c_halo, c_bulb):
        """The lit bulb with a glow round it under the shade (the lamp is off by
        day)."""
        with s.tod("dusk", "night"), s.shape():
            s.rect(LAMP_X - 6, BEAM_Y - 2, 2, 2, c_halo)
            s.rect(LAMP_X + 4, BEAM_Y - 2, 2, 2, c_halo)
            s.rect(LAMP_X - 6, BEAM_Y, 12, 2, c_halo)
            s.rect(LAMP_X - 4, BEAM_Y + 2, 8, 2, c_halo)
            s.rect(LAMP_X - 4, BEAM_Y - 2, 8, 2, c_bulb)

    bulb(halo, bulb_on)
    s.rect(LAMP_X - 4, BEAM_Y - 2, 8, 2, bulb_dim, tod="day")   # by day the bulb is off

    # ─── Poster: this week's usage ───────────────────────────────────────────
    # Top: the weekly % used now, big (3x5 at 2x, 10 px), in its pace colour
    # (green at or behind the even pace, amber ahead, red from 90 % or when
    # the average rate runs out a day or more before the reset), and a
    # two-line verdict at the left ("+6 AHEAD", "-4 BEHIND", "ON PACE",
    # "OUT 2D"). Under it a burn-up of the week: x = the 7-day window (a tick
    # per day), y = 0..100 % used. Faint dots trace the even pace corner to
    # corner; the engine fills what was used so far (straight lines between
    # real points only: 0 at the window's start, the hours the device
    # recorded, and now), dashes a now line above it, and dots the average
    # rate on to the reset, clipped at 100 % with a mark where it runs out.
    px0, py0, pw, ph = 208, 10, 56, 48            # the frame (clear of the lamp's cone)
    s.rect(px0, py0, pw, ph, wood_dk)
    s.rect(px0 + 2, py0 + 2, pw - 4, ph - 4, paper)
    levels = (lvl_ok, lvl_ahead, lvl_out, pace_c)
    s.week_number(px0 + 4, py0 + 3, pw - 8, ink, levels, scale=2)
    PW, PH = 42, 26                               # the plot: 1 px per 4 hours, ~4 % per row
    ax, ay = px0 + 7, py0 + 16                    # its top-left
    s.rect(ax - 1, ay, 1, PH + 1, ink)            # y axis
    s.rect(ax - 1, ay + PH, PW + 1, 1, ink)       # x axis
    s.rect(ax - 2, ay, 1, 1, ink)                 # 100 % tick
    for d in range(1, 8):                         # day ticks under it (the last = the reset)
        s.rect(ax + d * PW // 7 - 1, ay + PH + 1, 1, 2, ink)
    s.week_chart(ax, ay, PW, PH, bar_b, bar_a, ink, levels)
    with s.shape():                               # the even pace, over the fill
        for c in range(0, PW, 2):
            v = div_round(100 * chart_col_mid(c, PW), WEEK_MINS)
            s.rect(ax + c, ay + chart_row(v, PH), 1, 1, pace_c)

    # ─── LED countdown to the next 5-hour window, over the water cooler ──────
    # 7-segment digits 8 x 14 (the largest that fit beside the poster), red
    # like the CODING sign, with the unlit segments faintly showing.
    dx0, dy0 = 270, 18
    s.rect(dx0 - 2, dy0 - 2, 44, 26, led_glow, tod=("dusk", "night"))
    s.rect(dx0, dy0, 40, 22, box)
    s.rect(dx0 + 2, dy0 + 2, 36, 18, box_in)
    s.digits(dx0 + 4, dy0 + 4, led, seg_off, dw=8, dh=14)

    # Water cooler, standing on the floor: the jug glugs a bubble now and then.
    cx0, cy0 = 282, 78
    s.pixels(cx0, cy0, [
        "...ccccccccc...",
        "..ahhaaaaaaaa..",
        "..ahjjjjjjjjj..",
        "..ahjjjjjjjjj..",
        "..ahjjjjjjjjj..",
        "..ahjjjjjjjjj..",
        "..ahjjjjjjjjj..",
        "..ahjjjjjjjjj..",
        "..ahjjjjjjjjj..",
        "..ahjjjjjjjjj..",
        "...hjjjjjjjj...",
        ".....jjjjj.....",
        "......ccc......",
        "wwwwwwwwwwwwwkk",
        "wwwwwwwwwwwwwkk",
        "kkkkkkkkkkkkkkk",
        "wwrrwwwwwwbbwkk",
        "wwrrwwwwwwbbwkk",
        "wwwwwwwwwwwwwkk",
        "wkkkkkkkkkkkkkk",
        "wwwwwwwwwwwwwkk",
        "wwwwwwwwwwwwwkk",
        "wwwwwwwwwwwwwkk",
        "wwwwwwwwwwwwwkk",
        "wwwwwwwwwwwwwkk",
        "wwwwwwwwwwwwwkk",
        "wwwwwwwwwwwwwkk",
        "wwwwwwwwwwwwwkk",
        "wwwwwwwwwwwwwkk",
        "wwwwwwwwwwwwwkk",
        "wwwwwwwwwwwwwkk",
        "wwwwwwwwwwwwwkk",
        "kkkkkkkkkkkkkkk",
        "kk...........kk",
    ], {"c": jug_cap, "a": jug_air, "h": jug_hi, "j": jug, "w": cool, "k": cool_dk,
        "r": tap_r, "b": tap_b}, scale=2)
    s.frames_of([
        lambda: s.rect(cx0 + 16, cy0 + 18, 2, 2, jug_hi),
        lambda: s.rect(cx0 + 14, cy0 + 12, 2, 2, jug_hi),
        lambda: s.rect(cx0 + 16, cy0 + 6, 2, 2, jug_hi),
        lambda: None,
        lambda: None,
    ], period_ms=280)

    # The lamp flickers now and then (steady for a few seconds, then dim, on,
    # dim): the bulb and the bright top of the cone drop a step.
    def flicker():
        with s.tod("dusk", "night"), s.shape():
            for x, y, w, h, step in lit_wall:
                s.rect(x, y, w, h, lit(wall, step - 1))
        bulb(halo_dim, bulb_dim)

    s.frames_of([lambda: None] * 13 + [flicker, lambda: None, flicker], period_ms=250)

    s.anchor("DESK", DESK_X)
    return s
