---
name: capturing-device-screenshots
description: Captures a connected Clawdmeter board's screen as a PNG over USB serial, with the bundled screenshot.py (the firmware's `screenshot` dump, decoded; C6 and other PSRAM-free boards included). Use when asked to screenshot a board or the device, to check UI on real hardware, or to attach device output to a PR.
---

# Capturing device screenshots

```bash
python3 ${CLAUDE_SKILL_DIR}/scripts/screenshot.py /path/out.png
python3 ${CLAUDE_SKILL_DIR}/scripts/screenshot.py /path/out.png --port COM3 \
    --cmd "page 1" --cmd "work 0" --cmd "tod 1380" --wait 2 --scale 3
```

Then Read the PNG. It's sized to what the board drew (480×480 on the
AMOLED-2.16, 172×320 meters / 320×172 pages on the LCD-1.47 C6) and takes
about 2 s.
Write it outside the repo unless it belongs in the PR.

- **Port**: the only `/dev/ttyACM*` or `/dev/cu.usbmodem*` attached (on
  Windows the only COM port); pass `--port` when there are several. Close any
  `pio device monitor` first, it holds the port. The script keeps DTR/RTS
  low, so opening and closing the port doesn't reset the board.
- **pyserial missing**: run it with `~/.platformio/penv/bin/python`.
- **A specific state**: `--cmd` sends serial commands first, in order, each
  followed by `--wait` seconds (default 1.5): `page <n>`, `work 0|1`,
  `tod <minute>`, `clawd show|screensaver|attention|…` (the full set is in
  the `flashing-and-probing-boards` skill). The state persists after the
  capture; send `work -1`, `tod -1`, `page 0` to undo.
- **`--scale N`** also writes `out_xN.png`, nearest-neighbour, for small
  panels where details are hard to see at 1×.
- **Boards without PSRAM** (C6, classic-ESP32 CYDs) answer with strips: the
  firmware redraws the whole screen once and streams every LVGL flush
  (`SCREENSHOT_START w h strips`, `SCREENSHOT_STRIP x y w h` + raw RGB565,
  `SCREENSHOT_END`), and the script assembles them. No extra RAM. It warns if
  strips left pixels uncovered (log text interleaved with the stream).
  Boards with PSRAM send one snapshot frame (`SCREENSHOT_START w h bytes`).
- **Orientation**: the PNG is in LVGL's coordinates. A strip capture follows
  the current resolution, so landscape pages come out landscape and the
  portrait meters portrait, however the board stands. (The PSRAM snapshot
  is always `BoardCaps` width × height.)

Device captures show what the firmware drew, not the panel's colours,
brightness or offsets; a photo is still the proof for those.
