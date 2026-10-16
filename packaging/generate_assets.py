#!/usr/bin/env python3
"""Generate original PocketShelf 3DS icon, banner, and brief chime."""

from __future__ import annotations

import argparse
import math
import struct
import wave
import zlib
from pathlib import Path

Color = tuple[int, int, int, int]

FONT: dict[str, tuple[str, ...]] = {
    " ": ("000",) * 7,
    "-": ("000", "000", "000", "111", "000", "000", "000"),
    "A": ("010", "101", "101", "111", "101", "101", "101"),
    "B": ("110", "101", "101", "110", "101", "101", "110"),
    "C": ("011", "100", "100", "100", "100", "100", "011"),
    "E": ("111", "100", "100", "110", "100", "100", "111"),
    "F": ("111", "100", "100", "110", "100", "100", "100"),
    "H": ("101", "101", "101", "111", "101", "101", "101"),
    "I": ("111", "010", "010", "010", "010", "010", "111"),
    "K": ("101", "101", "110", "100", "110", "101", "101"),
    "L": ("100", "100", "100", "100", "100", "100", "111"),
    "M": ("10001", "11011", "10101", "10101", "10001", "10001", "10001"),
    "N": ("1001", "1101", "1101", "1011", "1011", "1001", "1001"),
    "O": ("010", "101", "101", "101", "101", "101", "010"),
    "P": ("110", "101", "101", "110", "100", "100", "100"),
    "R": ("110", "101", "101", "110", "101", "101", "101"),
    "S": ("011", "100", "100", "010", "001", "001", "110"),
    "T": ("111", "010", "010", "010", "010", "010", "010"),
}


def canvas(width: int, height: int, color: Color) -> list[list[Color]]:
    return [[color for _ in range(width)] for _ in range(height)]


def rect(image: list[list[Color]], x: int, y: int, width: int, height: int, color: Color) -> None:
    for py in range(max(0, y), min(len(image), y + height)):
        for px in range(max(0, x), min(len(image[0]), x + width)):
            image[py][px] = color


def text(image: list[list[Color]], x: int, y: int, value: str, scale: int, color: Color) -> None:
    cursor = x
    for character in value:
        glyph = FONT[character]
        for gy, row in enumerate(glyph):
            for gx, bit in enumerate(row):
                if bit == "1":
                    rect(image, cursor + gx * scale, y + gy * scale, scale, scale, color)
        cursor += (len(glyph[0]) + 1) * scale


def chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))


def write_png(path: Path, image: list[list[Color]]) -> None:
    height = len(image)
    width = len(image[0])
    raw = b"".join(b"\x00" + b"".join(bytes(pixel) for pixel in row) for row in image)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def draw_mark(image: list[list[Color]], x: int, y: int, scale: int) -> None:
    dark = (15, 28, 47, 255)
    cream = (246, 241, 224, 255)
    cyan = (61, 203, 201, 255)
    gold = (250, 174, 72, 255)
    coral = (239, 99, 92, 255)
    rect(image, x, y, 32 * scale, 32 * scale, dark)
    rect(image, x + 4 * scale, y + 7 * scale, 6 * scale, 17 * scale, cyan)
    rect(image, x + 11 * scale, y + 4 * scale, 6 * scale, 20 * scale, cream)
    rect(image, x + 18 * scale, y + 9 * scale, 5 * scale, 15 * scale, gold)
    rect(image, x + 24 * scale, y + 6 * scale, 4 * scale, 18 * scale, coral)
    rect(image, x + 3 * scale, y + 24 * scale, 26 * scale, 3 * scale, cream)
    rect(image, x + 6 * scale, y + 28 * scale, 20 * scale, 1 * scale, cyan)


def make_icon(path: Path) -> None:
    image = canvas(48, 48, (12, 23, 40, 255))
    for y in range(48):
        shade = y // 3
        rect(image, 0, y, 48, 1, (12, 23 + shade, 40 + shade, 255))
    rect(image, 5, 5, 38, 38, (24, 45, 69, 255))
    draw_mark(image, 8, 8, 1)
    rect(image, 8, 40, 32, 2, (61, 203, 201, 255))
    write_png(path, image)


def make_banner(path: Path) -> None:
    image = canvas(256, 128, (11, 22, 39, 255))
    for y in range(128):
        rect(image, 0, y, 256, 1, (11, 22 + y // 9, 39 + y // 5, 255))
    rect(image, 0, 0, 256, 5, (61, 203, 201, 255))
    rect(image, 0, 118, 256, 10, (8, 16, 29, 255))
    rect(image, 14, 24, 80, 80, (24, 45, 69, 255))
    draw_mark(image, 22, 32, 2)
    text(image, 108, 35, "POCKET", 4, (246, 241, 224, 255))
    text(image, 108, 69, "SHELF", 4, (61, 203, 201, 255))
    text(image, 109, 101, "ROMM LAN CLIENT", 1, (250, 174, 72, 255))
    write_png(path, image)


def make_chime(path: Path) -> None:
    sample_rate = 32000
    duration = 0.45
    frame_count = int(sample_rate * duration)
    frequencies = (523.25, 659.25, 783.99)
    frames = bytearray()
    for index in range(frame_count):
        time = index / sample_rate
        attack = min(1.0, time / 0.025)
        release = max(0.0, 1.0 - time / duration) ** 2
        sample = sum(math.sin(2.0 * math.pi * frequency * time) for frequency in frequencies) / len(frequencies)
        value = round(32767 * 0.16 * attack * release * sample)
        frames.extend(struct.pack("<h", value))
    with wave.open(str(path), "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(sample_rate)
        audio.writeframes(frames)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("assets"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    make_icon(args.output / "icon.png")
    make_banner(args.output / "banner.png")
    make_chime(args.output / "banner.wav")


if __name__ == "__main__":
    main()
