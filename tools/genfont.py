#!/usr/bin/env python3
"""
Génère les polices de gros chiffres (src/bigfont.h et src/bigfontxl.h)
à partir d'une police TTF, pour l'horloge e-paper.

Rendre les chiffres à leur vraie résolution donne des courbes nettes,
contrairement à un simple agrandissement par blocs (pixelisé).

Usage :
    pip install pillow
    python3 tools/genfont.py /chemin/vers/police.ttf

Police par défaut du projet : Roboto Condensed Bold (Apache-2.0).
Format généré : glyphes '0'-'9' puis ':', bitmap ligne par ligne, MSB à gauche,
bit=1 -> pixel encre. Chaque chiffre est centré dans une cellule de largeur fixe
(chiffres tabulaires -> pas de tremblement quand l'heure change).
"""
import sys
from PIL import Image, ImageFont, ImageDraw

CHARS = "0123456789:"

def build(font_path, maxw, prefix, height_define, outfile, header_comment):
    def render_at(size):
        font = ImageFont.truetype(font_path, size)
        glyphs = {}; top, bot = 10**9, -1
        for c in CHARS:
            img = Image.new("L", (size*2, size*2), 0)
            ImageDraw.Draw(img).text((size//2, size//2), c, fill=255, font=font)
            bbox = img.getbbox(); glyphs[c] = (img, bbox)
            top = min(top, bbox[1]); bot = max(bot, bbox[3])
        H = bot - top
        digit_w = max(glyphs[c][1][2]-glyphs[c][1][0] for c in "0123456789")
        colon_w = glyphs[":"][1][2]-glyphs[":"][1][0]
        track = max(2, size//30)
        total = 4*(digit_w+track) + (colon_w+track)
        return font, glyphs, top, H, digit_w, colon_w, track, total

    size = 40
    while True:
        r = render_at(size+4)
        if r[7] > maxw: break
        size += 4
    font, glyphs, top, H, digit_w, colon_w, track, total = render_at(size)

    def emit(c, cellw):
        img, bbox = glyphs[c]; gw = bbox[2]-bbox[0]; xoff = (cellw-gw)//2
        bpr = (cellw+7)//8; data = bytearray(bpr*H); px = img.load()
        for row in range(H):
            sy = top + row
            for col in range(gw):
                if px[bbox[0]+col, sy] > 128:
                    dx = xoff+col; data[row*bpr + (dx>>3)] |= (0x80 >> (dx & 7))
        return bytes(data)

    widths, offsets, blob = [], [], bytearray()
    for c in CHARS:
        cw = (digit_w if c != ":" else colon_w) + track
        offsets.append(len(blob)); widths.append(cw); blob += emit(c, cw)

    with open(outfile, "w") as f:
        f.write(f"// {header_comment}\n")
        f.write(f"#ifndef {prefix.upper()}_H\n#define {prefix.upper()}_H\n#include <stdint.h>\n")
        f.write(f"#define {height_define} {H}\n")
        f.write(f"const uint16_t {prefix}_width[{len(widths)}] = {{{','.join(map(str,widths))}}};\n")
        f.write(f"const uint32_t {prefix}_offset[{len(offsets)}] = {{{','.join(map(str,offsets))}}};\n")
        f.write(f"const uint8_t {prefix}_bitmap[{len(blob)}] = {{{','.join(map(str,blob))}}};\n#endif\n")
    print(f"{outfile}: H={H}px, largeur HH:MM={total}px, bitmap={len(blob)} o")


if __name__ == "__main__":
    font = sys.argv[1] if len(sys.argv) > 1 else "/tmp/fonts/roboto.ttf"
    build(font, 350, "bigfont",   "BIGFONT_HEIGHT",
          "src/bigfont.h",   "Genere par tools/genfont.py - gros chiffres horloge (page principale).")
    build(font, 560, "bigfontxl", "BIGFONTXL_HEIGHT",
          "src/bigfontxl.h", "Genere par tools/genfont.py - horloge plein ecran.")
