#!/usr/bin/env python3
"""Helpers for vertical-strip and layer-based animated block textures."""

from __future__ import annotations

from PIL import Image


def strip_frame_count(width: int, height: int) -> int:
    if width <= 0 or height < width:
        return 1
    if height % width != 0:
        return 1
    return height // width


def crop_frame(img: Image.Image, frame_index: int, frame_count: int | None = None) -> Image.Image:
    """Extract one square frame from a vertical strip (or return img if already square)."""
    w, h = img.size
    count = frame_count if frame_count is not None else strip_frame_count(w, h)
    if count <= 1:
        if w == h:
            return img
        return img.crop((0, 0, w, min(w, h)))
    frame_h = h // count
    idx = max(0, min(frame_index, count - 1))
    return img.crop((0, idx * frame_h, w, (idx + 1) * frame_h))


def resample_strip(img: Image.Image, target_frames: int, size: int | None = None) -> Image.Image:
    """Build a vertical strip with target_frames, sampling source frames evenly."""
    if target_frames < 1:
        target_frames = 1
    w, h = img.size
    src_frames = strip_frame_count(w, h)
    if src_frames <= 1:
        frame = img if w == h else crop_frame(img, 0, 1)
        if size is not None and frame.size != (size, size):
            frame = frame.resize((size, size), Image.Resampling.NEAREST)
        strip = Image.new("RGBA", (frame.width, frame.height * target_frames))
        for i in range(target_frames):
            strip.paste(frame, (0, i * frame.height))
        return strip

    frame_w = w
    frame_h = h // src_frames
    if size is not None:
        frame_w = frame_h = size

    out_frames: list[Image.Image] = []
    for i in range(target_frames):
        if target_frames == 1:
            src_idx = 0
        else:
            src_idx = int(round(i * (src_frames - 1) / (target_frames - 1)))
        src_idx = max(0, min(src_idx, src_frames - 1))
        frame = img.crop((0, src_idx * (h // src_frames), w, (src_idx + 1) * (h // src_frames)))
        if frame.size != (frame_w, frame_h):
            frame = frame.resize((frame_w, frame_h), Image.Resampling.NEAREST)
        out_frames.append(frame)

    strip_h = frame_h * target_frames
    strip = Image.new("RGBA", (frame_w, strip_h))
    for i, frame in enumerate(out_frames):
        strip.paste(frame, (0, i * frame_h))
    return strip


def to_square_frame(img: Image.Image, size: int) -> Image.Image:
    """Use the first frame of a strip, or resize a square image."""
    frame = crop_frame(img, 0)
    if frame.size != (size, size):
        frame = frame.resize((size, size), Image.Resampling.NEAREST)
    return frame


def animation_mode(texture_count: int) -> str:
    """Return 'strip' for 6-face vertical strips, 'layers' for multi-frame face sets."""
    if texture_count == 6:
        return "strip"
    if texture_count > 6 and texture_count % 6 == 0:
        return "layers"
    return "static"
