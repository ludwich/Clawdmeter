# Asset tools

Run the commands from the repository root. They need Node.js; the splash
converter also needs ImageMagick, and the icon converter `pngjs`
(`npm install --no-save pngjs` in `tools/`, which is git-ignored).

## Splash animations

```bash
node tools/convert_official_clawd.js
node tools/convert_official_clawd.js --verify /tmp/verify   # + per-animation PNGs
```

Converts the official Anthropic Clawd animations archived in
`research/clawd-official/` (GIFs decoded via ImageMagick, the Laptop and
Soccer Lottie exports read directly) into a single
`firmware/src/splash_animations.h`:

- frames as bounding-box crops on the shared 55×37 art stage, one byte per
  cell into a per-animation ≤16-color RGB565 palette (index 0 = background)
- per-frame hold in ms, with consecutive duplicate frames collapsed
- a detected loop region per animation (gait cycles, scene middles) that the
  engine can hold or release for walk-to-target and timed scenes
- the eyes — transparent holes in the source GIFs — inked as `#141413`
- contrast recolors (trumpet notes → ivory, magnifier fedora → gray) and the
  sailing-loop cross-match that defines the sailing scene's loop window

See `research/clawd-official/CLAUDE.md` for asset provenance and the format
details, and `--in` / `--out` to override paths. Rebuild firmware after
running.

## Icons

```bash
node tools/png_to_lvgl.js input.png symbol_name [W_MACRO] [H_MACRO] [--tint=RRGGBB | --no-tint]
```

Converts an alpha PNG to an LVGL RGB565A8 C array. Default tint is white —
Lucide PNGs ship black-on-transparent and would render invisible without it.
Paste the output into `firmware/src/icons.h`.

## Clawd page scenes

```bash
python tools/scenes/scenekit.py build                       # → firmware/src/scene_<name>.h
python tools/scenes/scenekit.py preview office --tod night --out /tmp/office.png
python tools/scenes/scenekit.py preview outdoor --t 20000 --clawd idle --x 40 --out /tmp/out.png
python tools/scenes/scenekit.py preview office --gif 3 --out /tmp/office.gif
python tools/scenes/scenekit.py preview office --w 62 --wr 5000     # weekly poster: 62 % half way
python tools/scenes/scenekit.py preview office --sr -1 --week none   # unknown windows: -:--, --%
python tools/scenes/scenekit.py preview all                 # every scene × time of day + sheet.png
```

The scenery behind Clawd on the Clawd page (`firmware/src/scene.cpp`,
`docs/clawd-director.md` "Pages"): `office.py` while Claude works,
`outdoor.py` while he plays. Needs Python 3 and Pillow. Each script defines
`scene()` returning a `Scene`; the same data writes the firmware header and
renders the previews (320×172 plus a ×3 copy, colours through RGB565), with
Clawd's official frames composited exactly where `stage.cpp` puts him: the
idle pose, or the laptop act's typing frame at the `DESK` anchor.
`preview all` writes to `../scene-previews/` beside the checkout unless
`--outdir` says otherwise. The office's live wall gets fake data:
`--sr 161` (minutes left in the 5-hour window, `-1` = unknown, `-:--`),
`--w 34` (weekly % used now, `-1` = unknown or spend-capped, `--%`),
`--wr 5600` (minutes to the weekly reset, `-1` = unknown) and
`--week synthetic|empty|even|none` (the recorded history: a made-up week
ending at `--w` with a gap where the device was off, no samples so a
straight line from the start, an even burn, or no payload yet). The preview
prints the pace level and verdict it drew. Rebuild the firmware after
`build`.

The canvas is 320×172 px (`W`, `H`); his feet are on `FLOOR_Y` = 162 and a
cell of his art is 5 px. Shapes are flat rectangles drawn in order (later on
top), each naming a palette index:

- `c = s.color(day, dusk, night)`: one colour with its three time-of-day
  variants (0xRRGGBB; omitted ones default to `day`). `s.ambient(day)`
  derives dusk and night from the day colour; `mix`, `shade` help.
- `s.rect(x, y, w, h, c)`, `s.text(x, y, "TEXT", c, scale)` (3×5 pixel font,
  uppercase, digits, `.,:!?-+/%'=<>()*_#`, `^` = a heart; `text_width()`),
  `s.pixels(x, y, rows, {"a": c, ...}, scale)` (ASCII art, `.` = clear),
  `s.disc(cx, cy, r, c)`. A `tod=` argument narrows when a shape shows.
- `with s.tod("night"):` shapes inside show only then.
- `with s.drift(speed=4):` shapes inside slide together at px/s and wrap
  (`wrap=(x0, x1)` for their left edge; default off one side, back on the other).
- `with s.frames(period_ms=400, phase_ms=0) as f:` then `with f.frame():` per
  frame (an empty frame = blink off); or `s.frames_of([draw_a, draw_b], 400)`.
  Periods ≥ 100 ms.
- `s.clock(cx, cy, r, face_c, hand_c, rim_c)`: hands at the real local time.
- `s.digits(x, y, lit_c, ghost_c, dw=6, dh=10, t=2)`: an LED countdown,
  H:MM in 7-segment digits (dw x dh, strokes t px) showing the time left in
  the 5-hour window, `-:--` when unknown; the colon blinks each second.
  `ghost_c` draws the unlit 8:88 under it.
- `s.week_number(x, y, w, ink_c, levels, scale=2)`: the weekly poster's
  live text: the weekly % used right-aligned at `scale` (with a 1x `%`) in
  the pace level's colour, `levels` = (on pace, ahead, out, unknown), and
  the two-line verdict at the left (`burndown_verdict`; the first line in
  the level colour, the second in `ink_c`).
- `s.week_chart(x, y, w, h, line_c, area_c, now_c, levels)`: the live
  burn-up in a plot area, one px column per 1/w of the week, 0 % on the
  bottom row and 100 % on the top: the % used so far (straight lines between
  real points) as `line_c` over `area_c`, a dashed now marker in `now_c`, and
  the average rate dotted on to the reset in the level colour, clipped at
  100 %, with a 2×3 run-out tick under the x axis (the group's box is the
  plot plus `CHART_UNDER` = 4 rows for it). Axes, ticks and the even-pace
  staircase are plain shapes around it (`chart_row`, `chart_col_mid` place
  them on its grid; shapes after it draw on top).
- `s.anchor("DESK", x)` (office: Clawd's body-left at the laptop),
  `s.anchor("SIGN", x, y, w, h)` (an area for live text).

Rules: scenery only, never a drawing of Clawd or any other creature (one Clawd
on screen, ever); at most 47 groups; keep rects in the hundreds (each costs
12 B of flash and a fill whenever its area redraws); keep animated groups
small, since each change redraws its whole box over SPI.

## Simulator grid

```bash
python3 tools/simgrid/simgrid.py serve        # web GUI + action API
python3 tools/simgrid/simgrid.py run SCRIPT   # headless, deterministic
```

Every supported panel's simulator on one clock, driven by one set of
actions. Needs SDL2 dev files and Pillow (ffmpeg for `.mp4`). See
[docs/sim-grid.md](../docs/sim-grid.md); tests:
`python3 -m unittest discover -s tools/simgrid`.
