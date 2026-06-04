#!/usr/bin/env python3
"""
Generate norns extended glyph header with sparse codepoint lookup.

Scans norns.ttf for all non-empty glyphs above a threshold codepoint,
renders them with freetype FT_LOAD_TARGET_MONO, left-aligns in a
cell width = max_adv, matching the baseline of the base font header.

Output: C header with sorted codepoints + glyph data + binary search.
"""

import sys, os
from PIL import Image
import freetype

def generate(ttf_path, output_path, pt=8, threshold=0x7F, max_code=0x10000,
             cell_h=7, ds=1):
    face = freetype.Face(ttf_path)
    face.set_pixel_sizes(0, pt)
    name = os.path.splitext(os.path.basename(ttf_path))[0]
    fn = name.replace('-', '_')

    # Phase 1: render all extended glyphs
    chars = {}  # codepoint -> {img, bl, bt, adv, w, h}
    baseline_vals = []  # (bt, h) from standard glyphs for baseline computation

    for code, gid in face.get_chars():
        if code > max_code:
            continue
        try:
            face.load_char(chr(code), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
        except Exception:
            continue
        g = face.glyph
        bm = g.bitmap
        # Collect standard chars for baseline regardless of threshold
        if (0x30 <= code <= 0x39) or (0x41 <= code <= 0x5A) or (0x61 <= code <= 0x7A):
            baseline_vals.append((g.bitmap_top, bm.rows))

        if code < threshold or code > max_code:
            continue
        if bm.rows == 0 or bm.width == 0 or not bm.buffer:
            continue

        w, h = bm.width, bm.rows
        bt = g.bitmap_top
        bl = g.bitmap_left
        adv_raw = g.advance.x / 64.0

        # Convert freetype bitmap to PIL image
        pil = Image.new('L', (w, h), 0)
        for y in range(h):
            for x in range(w):
                byte_idx = y * bm.pitch + (x >> 3)
                bit_idx = 7 - (x & 7)
                if byte_idx < len(bm.buffer) and ((bm.buffer[byte_idx] >> bit_idx) & 1):
                    pil.putpixel((x, y), 255)
        pil = pil.point(lambda p: 255 if p > 127 else 0, mode='1')

        if ds > 1:
            pil = pil.resize((max(1, w // ds), max(1, h // ds)), Image.NEAREST)
            w, h = pil.width, pil.height
            bt = round(bt / ds)
            bl = round(bl / ds)
            adv_raw /= ds

        chars[code] = {
            'img': pil, 'bl': bl, 'bt': bt,
            'adv': max(1, round(adv_raw)),
            'w': w, 'h': h,
        }

        # Collect standard chars for baseline
        if (0x30 <= code <= 0x39) or (0x41 <= code <= 0x5A) or (0x61 <= code <= 0x7A):
            baseline_vals.append((bt, h))

    if not chars:
        print("ERROR: no extended glyphs found", file=sys.stderr)
        sys.exit(1)

    # Compute baseline (same logic as ttf2c.py)
    baseline_row = 0
    if baseline_vals:
        max_asc = max(v[0] for v in baseline_vals)
        max_desc = max(v[1] - v[0] for v in baseline_vals)
        ft_h = max_asc + max_desc
        if ft_h <= cell_h:
            baseline_row = (cell_h - ft_h) // 2 + max_asc
        else:
            baseline_row = cell_h - 1 - max_desc

    # Compute max advance for cell width
    max_adv = max(c['adv'] for c in chars.values())
    glyph_w = max_adv
    h_bytes = (cell_h + 7) // 8
    glyph_bytes_per = glyph_w * h_bytes

    # Sort by codepoint
    sorted_codes = sorted(chars.keys())
    n_chars = len(sorted_codes)

    print(f"  Extended glyphs: {n_chars}")
    print(f"  Codepoint range: 0x{sorted_codes[0]:04X} - 0x{sorted_codes[-1]:04X}")
    print(f"  Cell: {glyph_w}x{cell_h}px")
    print(f"  Baseline: {baseline_row}")
    print(f"  Max advance: {max_adv}")

    # Phase 2: build table
    table = []
    advances_out = []
    codepoints_out = sorted_codes[:]

    for code in sorted_codes:
        cd = chars[code]
        cw = cd['w']
        ch_h = cd['h']

        out = Image.new('1', (glyph_w, cell_h), 0)
        dst_y = baseline_row - cd['bt']
        dst_x = max(0, cd['bl'])

        src_x = 0
        src_y = 0

        # Crop if wider than cell
        if cw > glyph_w:
            src_x = (cw - glyph_w) // 2
            dst_x = 0
            cw = glyph_w

        if dst_y < 0:
            src_y = -dst_y
            ch_h += dst_y
            dst_y = 0
        if dst_y + ch_h > cell_h:
            ch_h = cell_h - dst_y

        if cw > 0 and ch_h > 0:
            region = cd['img'].crop((src_x, src_y, src_x + cw, src_y + ch_h))
            out.paste(region, (dst_x, dst_y))

        # Column-major extraction
        cols = []
        for col in range(glyph_w):
            for byte_idx in range(h_bytes):
                byte_val = 0
                for bit in range(8):
                    row = byte_idx * 8 + bit
                    if row < cell_h:
                        p = out.getpixel((col, row))
                        if p:
                            byte_val |= (1 << bit)
                cols.append(byte_val)
        table.append(cols)
        advances_out.append(cd['adv'])

    # Write header
    lines = []
    lines.append(f"// Auto-generated from {os.path.basename(ttf_path)} at {pt}pt extended range")
    lines.append(f"// Codepoints > 0x{threshold:02X}, sorted sparse lookup")
    lines.append("// Column-major, LSB = top pixel")
    lines.append("#pragma once")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append(f"static constexpr int FONT_{fn.upper()}_EXT_W = {glyph_w};")
    lines.append(f"static constexpr int FONT_{fn.upper()}_EXT_H = {cell_h};")
    lines.append(f"static constexpr int FONT_{fn.upper()}_EXT_BASELINE = {baseline_row};")
    lines.append(f"static constexpr int FONT_{fn.upper()}_EXT_H_BYTES = {h_bytes};")
    lines.append(f"static constexpr int FONT_{fn.upper()}_EXT_ADVANCE = {glyph_w};")
    lines.append(f"static constexpr int FONT_{fn.upper()}_EXT_N_CHARS = {n_chars};")
    lines.append("")

    fn_lower = fn.lower()
    lines.append(f"static const uint8_t font_{fn_lower}_ext_data[{n_chars}][{glyph_bytes_per}] = {{")
    for i, cols in enumerate(table):
        hex_bytes = ", ".join(f"0x{b:02x}" for b in cols)
        code = sorted_codes[i]
        ch = chr(code) if 0x20 <= code <= 0x7E else ''
        comment = f" // 0x{code:04X} '{ch}'" if ch else f" // 0x{code:04X}"
        lines.append(f"    {{ {hex_bytes} }},{comment}")
    lines.append("};")
    lines.append("")

    lines.append(f"static constexpr uint8_t font_{fn_lower}_ext_advances[{n_chars}] = {{")
    for i, a in enumerate(advances_out):
        code = sorted_codes[i]
        ch = chr(code) if 0x20 <= code <= 0x7E else ''
        comment = f" // 0x{code:04X} '{ch}'" if ch else f" // 0x{code:04X}"
        lines.append(f"    {a},{comment}")
    lines.append("};")
    lines.append("")

    lines.append(f"static constexpr uint16_t font_{fn_lower}_ext_codepoints[{n_chars}] = {{")
    for i, code in enumerate(sorted_codes):
        ch = chr(code) if 0x20 <= code <= 0x7E else ''
        comment = f" // 0x{code:04X} '{ch}'" if ch else f" // 0x{code:04X}"
        lines.append(f"    0x{code:04X},{comment}")
    lines.append("};")
    lines.append("")

    lines.append("// Binary search: returns index or FONT_NORNS_EXT_N_CHARS if not found")
    lines.append(f"inline int FontNornsExtLookup(uint16_t cp) {{")
    lines.append(f"    int lo = 0, hi = FONT_{fn.upper()}_EXT_N_CHARS - 1;")
    lines.append(f"    while (lo <= hi) {{")
    lines.append(f"        int mid = (lo + hi) >> 1;")
    lines.append(f"        if (font_{fn_lower}_ext_codepoints[mid] < cp) lo = mid + 1;")
    lines.append(f"        else if (font_{fn_lower}_ext_codepoints[mid] > cp) hi = mid - 1;")
    lines.append(f"        else return mid;")
    lines.append(f"    }}")
    lines.append(f"    return FONT_{fn.upper()}_EXT_N_CHARS;")
    lines.append(f"}}")
    lines.append("")

    content = "\n".join(lines) + "\n"
    with open(output_path, 'w') as f:
        f.write(content)

    sz = os.path.getsize(output_path)
    print(f"  Wrote {output_path} ({sz} bytes)")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Generate norns extended glyph header")
    parser.add_argument("input", help="Input .ttf file")
    parser.add_argument("output", help="Output .h file")
    parser.add_argument("--pt", type=int, default=8, help="Point size")
    parser.add_argument("--threshold", type=lambda x: int(x, 0), default=0x7F,
                        help="Codepoint threshold (default 0x7F)")
    parser.add_argument("--max-code", type=lambda x: int(x, 0), default=0x10000,
                        help="Max codepoint to scan (default 0x10000)")
    parser.add_argument("--cell-h", type=int, default=7, help="Cell height (default 7)")
    parser.add_argument("--ds", type=int, default=1, help="Downsample factor")
    args = parser.parse_args()

    generate(args.input, args.output, args.pt, args.threshold,
             args.max_code, args.cell_h, args.ds)
