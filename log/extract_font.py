#!/usr/bin/env python3
"""Extract glyphs from LVGL-format norns.c, convert to our column-major font format, and generate C header."""

import re
import sys

FONT_H = 5
MAX_W = 5

def parse_norns_c(path):
    with open(path) as f:
        text = f.read()

    text_no_comments = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    text_no_comments = re.sub(r'//.*', '', text_no_comments)

    m = re.search(r'static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap\[\] = \{(.*?)\};', text_no_comments, re.DOTALL)
    if not m:
        print("ERROR: glyph_bitmap not found")
        sys.exit(1)
    bitmap_bytes = [int(x.strip(), 0) for x in m.group(1).split(',') if x.strip()]

    m = re.search(r'static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc\[\] = \{(.*?)\};', text_no_comments, re.DOTALL)
    if not m:
        print("ERROR: glyph_dsc not found")
        sys.exit(1)

    glyphs = []
    for entry in re.finditer(r'\{\.bitmap_index\s*=\s*(\d+)\s*,\s*\.adv_w\s*=\s*(\d+)\s*,\s*\.box_w\s*=\s*(\d+)\s*,\s*\.box_h\s*=\s*(\d+)\s*,\s*\.ofs_x\s*=\s*([-\d]+)\s*,\s*\.ofs_y\s*=\s*([-\d]+)\s*\}', m.group(1)):
        glyphs.append({
            'bitmap_index': int(entry.group(1)),
            'adv_w': int(entry.group(2)),
            'box_w': int(entry.group(3)),
            'box_h': int(entry.group(4)),
            'ofs_x': int(entry.group(5)),
            'ofs_y': int(entry.group(6)),
        })

    cmap_ranges = []
    m = re.search(r'static const lv_font_fmt_txt_cmap_t cmaps\[\] =\s*\n\{(.*?)\n\};', text, re.DOTALL)
    if m:
        for cm in re.finditer(r'\.range_start\s*=\s*(\d+)\s*,\s*\.range_length\s*=\s*(\d+)\s*,\s*\.glyph_id_start\s*=\s*(\d+)', m.group(1)):
            cmap_ranges.append({
                'start': int(cm.group(1)),
                'length': int(cm.group(2)),
                'glyph_start': int(cm.group(3)),
            })

    glyph_to_unicode = {}
    for r in cmap_ranges:
        for i in range(r['length']):
            glyph_idx = r['glyph_start'] + i
            codepoint = r['start'] + i
            glyph_to_unicode[glyph_idx] = codepoint

    return bitmap_bytes, glyphs, glyph_to_unicode


def extract_pixels(bitmap_bytes, g):
    idx = g['bitmap_index']
    w, h = g['box_w'], g['box_h']
    total_bits = w * h
    total_bytes = (total_bits + 7) // 8
    data = bitmap_bytes[idx:idx + total_bytes]
    if not data:
        return [[0]*w for _ in range(h)]

    # Row-major MSB (confirmed correct format)
    pixels = [[0]*w for _ in range(h)]
    bit_pos = 0
    for row in range(h):
        for col in range(w):
            byte_idx = bit_pos // 8
            bit_offset = bit_pos % 8
            if byte_idx < len(data):
                val = (data[byte_idx] >> (7 - bit_offset)) & 1
                pixels[row][col] = val
            bit_pos += 1
    return pixels


def place_glyph(pixels, box_w, box_h, ofs_y):
    """Place glyph pixels into FONT_H x MAX_W output array.
    Uses LVGL baseline offset:
    - ofs_y = distance from baseline to glyph bottom (positive = above baseline)
    - Bitmap row r is at (box_h-1-r) pixels above glyph bottom
    - So distance from baseline = ofs_y + (box_h-1-r)
    - Our row = baseline_our - distance_from_baseline
    - baseline_our = FONT_H-2 (1px descender space at bottom)
    - Final: our_row = (FONT_H-2) - ofs_y - (box_h-1-r)
    """
    out = [[0]*MAX_W for _ in range(FONT_H)]
    col_offset = (MAX_W - box_w) // 2

    for r in range(box_h):
        our_row = (FONT_H - 2) - (ofs_y + (box_h - 1 - r))
        if our_row < 0 or our_row >= FONT_H:
            continue
        for c in range(box_w):
            our_col = col_offset + c
            if our_col < 0 or our_col >= MAX_W:
                continue
            if pixels[r][c]:
                out[our_row][our_col] = 1

    return out


def pixels_to_colmajor(pixels):
    """Convert FONT_H x MAX_W pixel grid to column-major bytes.
    Each byte: LSB = row 0 (top), bit 6 = row FONT_H-1 (bottom).
    Matches Display.cpp's rendering: (byte >> row) & 1 for row=0..6.
    """
    bytes_out = []
    for col in range(MAX_W):
        byte_val = 0
        for row in range(FONT_H):
            if pixels[row][col]:
                byte_val |= (1 << row)  # LSB-first: bit 0 = row 0 (top)
        bytes_out.append(byte_val)
    return bytes_out


def render_ascii(pixels):
    lines = []
    for row in range(FONT_H):
        line = ''
        for col in range(MAX_W):
            line += '#' if pixels[row][col] else '.'
        lines.append(line)
    return lines


# Main
bitmap_bytes, glyphs, glyph_to_unicode = parse_norns_c('log/norns.c')
print(f"Bitmap bytes: {len(bitmap_bytes)}")
print(f"Glyph count: {len(glyphs)}")

# Build lookup: codepoint -> glyph
cp_to_glyph = {}
for gi in range(1, len(glyphs)):
    if gi in glyph_to_unicode:
        cp_to_glyph[glyph_to_unicode[gi]] = gi

# Generate font table for ASCII 32-126
font_table = {}  # codepoint -> list of MAX_W bytes

for cp in range(32, 127):
    if cp not in cp_to_glyph:
        print(f"WARNING: U+{cp:04X} ('{chr(cp) if 32 <= cp <= 126 else '?'}') not in font, using space")
        # Use space glyph (should be at cp=32)
        gi = cp_to_glyph.get(32)
        if gi is None:
            font_table[cp] = [0]*MAX_W
            continue
    else:
        gi = cp_to_glyph[cp]

    g = glyphs[gi]
    pixels = extract_pixels(bitmap_bytes, g)
    placed = place_glyph(pixels, g['box_w'], g['box_h'], g['ofs_y'])
    font_table[cp] = pixels_to_colmajor(placed)

# Print all glyph renderings
print(f"\n{'='*60}")
print("04B_03 FONT GLYPHS (ASCII 32-126)")
print(f"Format: {MAX_W} columns, {FONT_H} rows, column-major MSB-first")
print(f"{'='*60}")

for cp in range(32, 127):
    data = font_table.get(cp, [0]*MAX_W)
    # Convert bytes back to pixels for display
    pixels = [[0]*MAX_W for _ in range(FONT_H)]
    for col, byte_val in enumerate(data):
        for row in range(FONT_H):
            if byte_val & (1 << row):
                pixels[row][col] = 1

    char_name = chr(cp) if 32 <= cp <= 126 else '?'
    art = render_ascii(pixels)
    hex_str = ', '.join(f'0x{b:02X}' for b in data)
    print(f"\nU+{cp:02X} '{char_name}': [{hex_str}]")
    for line in art:
        print(f"  |{line}|")

# Generate C header
print(f"\n{'='*60}")
print("GENERATING C HEADER...")
print(f"{'='*60}")

# First, show the space character (index 0 is for NULL/unmapped)
space_data = font_table.get(32, [0]*MAX_W)

lines = []
lines.append(f'#ifndef FONT{FONT_H}X{MAX_W}_H')
lines.append(f'#define FONT{FONT_H}X{MAX_W}_H')
lines.append('')
lines.append('#include <stdint.h>')
lines.append('')
lines.append(f'// 04B_03 font by 04, extracted from norns LVGL output')
lines.append(f'// Format: column-major, {MAX_W} cols x {FONT_H} rows, LSB-first per byte (bit 0 = row 0 = top)')
lines.append(f'// Baseline at row 3 (FONT_H-2), 1px descender at row 4')
lines.append(f'// Generated from log/norns.c')
lines.append('')
lines.append(f'#define FONT_{FONT_H}X{MAX_W}_WIDTH {MAX_W}')
lines.append(f'#define FONT_{FONT_H}X{MAX_W}_HEIGHT {FONT_H}')
lines.append(f'#define FONT_{FONT_H}X{MAX_W}_FIRST 32')
lines.append(f'#define FONT_{FONT_H}X{MAX_W}_LAST 126')
lines.append('')
lines.append(f'static const uint8_t font{FONT_H}x{MAX_W}[128][{MAX_W}] = {{')
lines.append(f'    // NULL (index 0, unmapped)')

null_parts = ', '.join(f'0x{b:02X}' for b in space_data)
lines.append(f'    {{ {null_parts} }},')

for cp in range(1, 32):
    lines.append(f'    {{ {null_parts} }}, // U+{cp:04X} (unmapped)')

for cp in range(32, 127):
    data = font_table.get(cp, [0]*MAX_W)
    parts = ', '.join(f'0x{b:02X}' for b in data)
    char = chr(cp) if 32 <= cp <= 126 else '?'
    lines.append(f'    {{ {parts} }}, // {cp} U+{cp:04X} \'{char}\'')

# Index 127 (DEL)
lines.append(f'    {{ {null_parts} }}, // 127 U+007F (unmapped)')

lines.append('};')
lines.append('')
lines.append(f'#endif // FONT{FONT_H}X{MAX_W}_H')
lines.append('')

header = '\n'.join(lines)

output_path = 'log/font5x5.h'  # 5 rows, 5 cols (WxH naming: font5x5 = 5 cols x 5 rows)
with open(output_path, 'w') as f:
    f.write(header)

print(f"Written to {output_path}")
print(f"Total glyphs: {len(range(32, 127))} printable + 33 control = 128 entries")
print(f"Memory: 128 * {MAX_W} = {128 * MAX_W} bytes")
