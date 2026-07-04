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


# Jeu de caractères pour le texte : ASCII imprimable + accents français + °
TEXT_CHARS = [chr(c) for c in range(0x20, 0x7F)] + list(
    "àâäçèéêëîïôöùûüÀÂÇÈÉÊËÎÏÔÙÛ°œŒ")

def build_text(font_path, target_h, outfile):
    """Police de texte proportionnelle avec accents (renderer UTF-8)."""
    # choisit la taille pour approcher la hauteur cible
    size = 12
    while True:
        f = ImageFont.truetype(font_path, size+2)
        top, bot = 10**9, -1
        for ch in TEXT_CHARS:
            im = Image.new("L", (size*3, size*3), 0)
            ImageDraw.Draw(im).text((size, size), ch, fill=255, font=f)
            bb = im.getbbox()
            if bb: top = min(top, bb[1]); bot = max(bot, bb[3])
        if bot - top > target_h: break
        size += 2
    font = ImageFont.truetype(font_path, size)
    # bande verticale commune (union des encres)
    top, bot = 10**9, -1
    render = {}
    for ch in TEXT_CHARS:
        im = Image.new("L", (size*3, size*3), 0)
        ImageDraw.Draw(im).text((size, size), ch, fill=255, font=font)
        bb = im.getbbox()
        render[ch] = (im, bb)
        if bb: top = min(top, bb[1]); bot = max(bot, bb[3])
    H = bot - top

    cps, widths, offsets, blob = [], [], [], bytearray()
    for ch in TEXT_CHARS:
        im, bb = render[ch]
        adv = max(1, round(font.getlength(ch)))
        bpr = (adv + 7) // 8
        data = bytearray(bpr * H)
        if bb:  # glyphe non vide (l'espace est vide -> juste une avance)
            px = im.load()
            # position horizontale : l'encre commence à bb[0], le pen était à size
            xoff = bb[0] - size
            for row in range(H):
                sy = top + row
                for col in range(bb[2] - bb[0]):
                    if px[bb[0] + col, sy] > 128:
                        dx = xoff + col
                        if 0 <= dx < adv:
                            data[row * bpr + (dx >> 3)] |= (0x80 >> (dx & 7))
        cps.append(ord(ch)); widths.append(adv); offsets.append(len(blob)); blob += data

    with open(outfile, "w") as f:
        f.write("// Genere par tools/genfont.py - police texte accentuee (renderer UTF-8).\n")
        f.write("#ifndef TEXTFONT_H\n#define TEXTFONT_H\n#include <stdint.h>\n")
        f.write(f"#define TEXTFONT_HEIGHT {H}\n#define TEXTFONT_COUNT {len(cps)}\n")
        f.write(f"const uint16_t textfont_cp[{len(cps)}] = {{{','.join(map(str,cps))}}};\n")
        f.write(f"const uint16_t textfont_width[{len(widths)}] = {{{','.join(map(str,widths))}}};\n")
        f.write(f"const uint32_t textfont_offset[{len(offsets)}] = {{{','.join(map(str,offsets))}}};\n")
        f.write(f"const uint8_t textfont_bitmap[{len(blob)}] = {{{','.join(map(str,blob))}}};\n#endif\n")
    print(f"{outfile}: H={H}px, {len(cps)} glyphes, bitmap={len(blob)} o")


if __name__ == "__main__":
    font = sys.argv[1] if len(sys.argv) > 1 else "/tmp/fonts/roboto.ttf"
    build(font, 350, "bigfont",   "BIGFONT_HEIGHT",
          "src/bigfont.h",   "Genere par tools/genfont.py - gros chiffres horloge (page principale).")
    build(font, 560, "bigfontxl", "BIGFONTXL_HEIGHT",
          "src/bigfontxl.h", "Genere par tools/genfont.py - horloge plein ecran.")
    build_text(font, 26, "src/textfont.h")
