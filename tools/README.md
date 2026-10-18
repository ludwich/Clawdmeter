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
`--outdir` says otherwise. Rebuild the firmware after `build`.

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
