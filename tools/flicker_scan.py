#!/usr/bin/env python3
"""Detect brightness flicker in flight-sim captures.

Usage: python tools/flicker_scan.py <capture_dir> [--tile 40] [--thr 5]
Reads frame_*.png (consecutive captures, e.g. CUBA_FLIGHT_CAPTURE_INTERVAL_SEC=0.2),
masks the HUD (right/top-right), splits each frame into square tiles, and
reports tiles whose mean luminance jumps between consecutive frames.
A tile that drops then recovers (or rises then falls) within a few frames is a
flicker event. With a static camera and frozen time/weather every event is a
light/mesh change, not motion.
"""
import sys, glob, os, argparse
import numpy as np
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument("dir")
ap.add_argument("--tile", type=int, default=40)
ap.add_argument("--thr", type=float, default=5.0)
ap.add_argument("--skip", type=int, default=0, help="skip first N frames (loading)")
args = ap.parse_args()

files = sorted(glob.glob(os.path.join(args.dir, "frame_*.png")))
files = [f for f in files if "_front" not in f][args.skip:]
if len(files) < 3:
    print("not enough frames", len(files)); sys.exit(1)

lums = []
for f in files:
    im = np.asarray(Image.open(f).convert("RGB"), dtype=np.float32)
    h, w, _ = im.shape
    lum = 0.299 * im[..., 0] + 0.587 * im[..., 1] + 0.114 * im[..., 2]
    th, tw = h // args.tile, w // args.tile
    lum = lum[: th * args.tile, : tw * args.tile]
    tiles = lum.reshape(th, args.tile, tw, args.tile).mean(axis=(1, 3))
    # Mask HUD: right 30% of width, top 66% of height.
    mask = np.ones_like(tiles, dtype=bool)
    mask[: int(th * 0.66), int(tw * 0.70):] = False
    mask[int(th * 0.90):, :] = False  # hotbar
    lums.append((tiles, mask))

T = np.stack([t for t, _ in lums])          # frames x th x tw
M = lums[0][1]
D = np.abs(np.diff(T, axis=0))              # change between consecutive frames
D[:, ~M] = 0
events = (D > args.thr)
ev_per_tile = events.sum(axis=0)
print(f"frames={len(files)} tiles={T.shape[1]}x{T.shape[2]} thr={args.thr}")
print(f"tiles with >=1 event: {(ev_per_tile>0).sum()} / {M.sum()}   total events: {events.sum()}")
# Flicker = a change followed by a reverse change within 8 frames.
flick = 0
for y in range(T.shape[1]):
    for x in range(T.shape[2]):
        if not M[y, x]:
            continue
        s = T[:, y, x]
        d = np.diff(s)
        for i in np.where(np.abs(d) > args.thr)[0]:
            j = slice(i + 1, min(len(d), i + 9))
            if np.any((np.sign(d[j]) == -np.sign(d[i])) & (np.abs(d[j]) > args.thr / 2)):
                flick += 1
                break
print("tiles with reversing change (flicker) within 8 frames:", flick)
# Top frames by number of changed tiles.
per_frame = events.reshape(len(events), -1).sum(axis=1)
order = np.argsort(-per_frame)[:8]
print("frames with most changed tiles (frame index -> n):",
      [(int(i) + 1, int(per_frame[i])) for i in order])
big = np.unravel_index(np.argmax(D.max(axis=0)), D.shape[1:])
print("largest single jump tile (row,col):", big, "max delta", float(D.max()))
np.save(os.path.join(args.dir, "tile_lum.npy"), T)

# --- spike metric (moving camera): a tile that dips/peaks and comes back ---
# amp = min(|x_t - x_{t-a}|, |x_t - x_{t+b}|) when both deviations have the same
# sign. Smooth motion and single step edges give ~0; a chunk blink gives a
# large value. Reported as counts over several thresholds.
a, b = 2, 4
S = T[:, :, :]
if S.shape[0] > a + b + 1:
    prev = S[: -(a + b)]
    cur = S[a : -b]
    nxt = S[a + b :]
    d1 = cur - prev
    d2 = cur - nxt
    same = (np.sign(d1) == np.sign(d2))
    amp = np.where(same, np.minimum(np.abs(d1), np.abs(d2)), 0.0)
    amp[:, ~M] = 0
    for thr in (3, 5, 8, 12):
        n = int((amp > thr).sum())
        print(f"spike tile-frames amp>{thr}: {n}  per-1000-tile-frames: {1000.0*n/amp.size:.2f}")
    print("max spike amp:", float(amp.max()))
