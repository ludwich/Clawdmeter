#!/usr/bin/env python3
"""Capture a connected Clawdmeter board's screen as a PNG.

    screenshot.py [out.png] [--port PORT] [--timeout 15]
                  [--cmd "page 1" --cmd "work 1" ...] [--wait 1.5] [--scale 3]

Sends the `screenshot` serial command and writes a PNG sized to what the
board drew. Two answers are understood:

- frame (boards with PSRAM): `SCREENSHOT_START w h bytes`, the whole RGB565
  frame, `SCREENSHOT_END`;
- strips (boards without PSRAM, e.g. the C6 boards): `SCREENSHOT_START w h
  strips`, then for every strip LVGL redraws `SCREENSHOT_STRIP x y w h` and
  w*h*2 raw RGB565 bytes, then `SCREENSHOT_END`. Strips follow the current
  resolution, so landscape pages come out landscape.

--cmd sends serial commands first (in order, each followed by --wait
seconds), e.g. `--cmd "page 1" --cmd "work 1"`. --scale N also writes a
nearest-neighbour copy N times larger next to the PNG (out_x3.png).

No dependencies beyond pyserial (PlatformIO's Python has it:
~/.platformio/penv/bin/python). The port defaults to the only
/dev/ttyACM* or /dev/cu.usbmodem* present, or the only COM port on Windows.
Opening the port doesn't reset the board.
"""

import argparse
import glob
import os
import struct
import sys
import time
import zlib

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial missing: run with ~/.platformio/penv/bin/python, "
             "or pip install pyserial")

BAUD = 115200  # monitor_speed of every env in firmware/platformio.ini


def default_port():
    found = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/cu.usbmodem*"))
    if not found and os.name == "nt":
        found = sorted(p.device for p in list_ports.comports())
    if len(found) == 1:
        return found[0]
    if not found:
        sys.exit("no board found (no /dev/ttyACM*, /dev/cu.usbmodem* or COM port); "
                 "plug one in or pass --port")
    sys.exit(f"several serial ports: {', '.join(found)}; pass --port")


def read_exact(port, size, deadline):
    data = bytearray()
    while len(data) < size:
        if time.monotonic() > deadline:
            sys.exit(f"timed out after {len(data)} of {size} bytes")
        data += port.read(size - len(data))
    return bytes(data)


def read_line(port, deadline):
    if time.monotonic() > deadline:
        return None
    return port.readline().decode("utf-8", errors="replace").strip()


def read_strips(port, w, h, deadline):
    """Assemble SCREENSHOT_STRIP blocks until SCREENSHOT_END."""
    frame = bytearray(w * h * 2)
    covered = bytearray(w * h)
    strips = 0
    while True:
        line = read_line(port, deadline)
        if line is None:
            sys.exit(f"no SCREENSHOT_END within the timeout ({strips} strips read)")
        if line == "SCREENSHOT_END":
            break
        if not line.startswith("SCREENSHOT_STRIP"):
            continue  # a log line, or the newline after a strip's bytes
        _, x, y, sw, sh = line.split()
        x, y, sw, sh = int(x), int(y), int(sw), int(sh)
        raw = read_exact(port, sw * sh * 2, deadline)
        strips += 1
        for row in range(sh):
            fy = y + row
            if not 0 <= fy < h:
                continue
            x0, x1 = max(x, 0), min(x + sw, w)
            if x1 <= x0:
                continue
            src = raw[(row * sw + (x0 - x)) * 2:(row * sw + (x1 - x)) * 2]
            frame[(fy * w + x0) * 2:(fy * w + x1) * 2] = src
            covered[fy * w + x0:fy * w + x1] = b"\x01" * (x1 - x0)
    missing = w * h - sum(covered)
    if missing:
        print(f"warning: {missing} of {w * h} pixels not covered by the "
              f"{strips} strips (left black)", file=sys.stderr)
    return bytes(frame), strips


def read_frame(port, timeout):
    """(w, h, raw RGB565 bytes, strips or 0); log lines before the header are skipped."""
    deadline = time.monotonic() + timeout
    while True:
        line = read_line(port, deadline)
        if line is None:
            sys.exit(f"no SCREENSHOT_START within {timeout}s (is the firmware running?)")
        if line.startswith("SCREENSHOT_START"):
            _, w, h, size = line.split()
            w, h = int(w), int(h)
            break
        if line == "SCREENSHOT_UNSUPPORTED":
            sys.exit("this board's firmware can't take screenshots (flash a newer build)")
        if line == "SCREENSHOT_ERR":
            sys.exit("the board failed to take the snapshot (out of PSRAM?)")
    if size == "strips":
        data, strips = read_strips(port, w, h, deadline)
        return w, h, data, strips
    return w, h, read_exact(port, int(size), deadline), 0


def to_rgb(w, h, rgb565):
    """Rows of 8-bit RGB for little-endian RGB565 pixels."""
    # 5/6-bit channel -> 8 bits, replicating the high bits into the low ones.
    lut5 = bytes((v << 3) | (v >> 2) for v in range(32))
    lut6 = bytes((v << 2) | (v >> 4) for v in range(64))
    rows = []
    for y in range(h):
        row = rgb565[y * w * 2:(y + 1) * w * 2]
        out = bytearray()
        for i in range(0, len(row), 2):
            v = row[i] | (row[i + 1] << 8)
            out += bytes((lut5[v >> 11], lut6[(v >> 5) & 63], lut5[v & 31]))
        rows.append(bytes(out))
    return rows


def png(w, h, rows):
    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    raw = b"".join(b"\x00" + r for r in rows)  # filter: none
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6))
            + chunk(b"IEND", b""))


def scaled(rows, n):
    out = []
    for r in rows:
        big = b"".join(r[i:i + 3] * n for i in range(0, len(r), 3))
        out.extend([big] * n)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("out", nargs="?", default="screenshot.png")
    ap.add_argument("--port")
    ap.add_argument("--timeout", type=float, default=15.0,
                    help="seconds for the capture itself (a 480x480 frame takes ~4 s at 115200 baud)")
    ap.add_argument("--cmd", action="append", default=[],
                    help="serial command to send before capturing (repeatable, in order)")
    ap.add_argument("--wait", type=float, default=1.5,
                    help="seconds to wait after each --cmd (default 1.5)")
    ap.add_argument("--scale", type=int, default=0,
                    help="also write an N-times nearest-neighbour copy (out_xN.png)")
    args = ap.parse_args()

    port_path = args.port or default_port()
    # DTR and RTS stay low: on the ESP32 USB-Serial-JTAG (C6, S3) some
    # DTR/RTS sequences reset the chip, and Windows drops them one at a time
    # when the port closes, which reboots the board after every capture.
    port = serial.Serial(None, BAUD, timeout=1)
    port.port = port_path
    port.dtr = False
    port.rts = False
    try:
        port.open()
    except serial.SerialException as e:
        sys.exit(f"cannot open {port_path}: {e} (is a serial monitor holding it?)")
    for cmd in args.cmd:
        port.write(cmd.encode() + b"\n")
        port.flush()
        time.sleep(args.wait)
    port.reset_input_buffer()
    port.write(b"screenshot\n")
    port.flush()
    w, h, raw, strips = read_frame(port, args.timeout)
    port.close()

    rows = to_rgb(w, h, raw)
    with open(args.out, "wb") as f:
        f.write(png(w, h, rows))
    how = f", {strips} strips" if strips else ""
    print(f"saved {args.out} ({w}x{h}{how})")
    if args.scale > 1:
        base, ext = os.path.splitext(args.out)
        big = f"{base}_x{args.scale}{ext or '.png'}"
        with open(big, "wb") as f:
            f.write(png(w * args.scale, h * args.scale, scaled(rows, args.scale)))
        print(f"saved {big} ({w * args.scale}x{h * args.scale})")


if __name__ == "__main__":
    main()
