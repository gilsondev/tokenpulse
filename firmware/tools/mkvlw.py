#!/usr/bin/env python3
"""Gera fontes VLW (anti-aliased, 8 bits por pixel) para a LovyanGFX a partir de um TTF.

Saída: src/ui/fonts/<nome>.h com o array em flash. Rode a partir de firmware/:
    uv run --with pillow tools/mkvlw.py
"""
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

TTF = Path(__file__).parent / "fonts" / "Inter.ttf"
OUT = Path(__file__).parent.parent / "src" / "ui" / "fonts"

TEXT = "".join(chr(c) for c in range(0x20, 0x7F)) + "áàâãéêíóôõúçÁÀÂÃÉÊÍÓÔÕÚÇ·…°–‹›"
DIGITS = "0123456789%-, .kKMB"

# nome, tamanho em px, peso (100 a 900), caracteres
FONTS = [
    ("inter_small", 14, 500, TEXT),
    ("inter_label", 16, 500, TEXT),
    ("inter_title", 20, 600, TEXT),
    ("inter_num", 30, 700, TEXT),
    ("inter_hero", 58, 700, DIGITS),  # 58 e não 64: "100%" cabe dentro do anel
]


def vlw(size, weight, chars):
    font = ImageFont.truetype(str(TTF), size)
    font.set_variation_by_axes([min(max(size, 14), 32), weight])
    ascent, descent = font.getmetrics()
    head, glyphs, bitmaps = b"", b"", b""
    for ch in sorted(set(chars), key=ord):
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        w, h = max(x1 - x0, 0), max(y1 - y0, 0)
        img = Image.new("L", (w, h))
        if w and h:
            ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
        advance = round(font.getlength(ch))
        glyphs += struct.pack(">7i", ord(ch), h, w, advance, -y0, x0, 0)
        bitmaps += img.tobytes()
    head = struct.pack(">6i", len(set(chars)), 11, size, 0, ascent, descent)
    return head + glyphs + bitmaps


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, size, weight, chars in FONTS:
        data = vlw(size, weight, chars)
        rows = ",\n".join(", ".join(f"0x{b:02x}" for b in data[i:i + 24]) for i in range(0, len(data), 24))
        (OUT / f"{name}.h").write_text(
            f"// Gerado por tools/mkvlw.py: Inter {size} px, peso {weight}. Licença OFL em tools/fonts/OFL.txt.\n"
            f"#pragma once\n#include <stdint.h>\n\nconst uint8_t {name}[] = {{\n{rows}}};\n")
        print(f"{name}: {len(data)} bytes")


if __name__ == "__main__":
    main()
