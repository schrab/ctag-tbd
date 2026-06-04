#!/usr/bin/env python3
"""Analyze TTF font glyph dimensions and produce a C header."""
import sys, os
from PIL import Image, ImageFont, ImageDraw

def analyze_glyph(font, char):
    """Get precise glyph metrics including baseline offset."""
    img = Image.new('1', (200, 200), 0)
    draw = ImageDraw.Draw(img)
    bbox = draw.textbbox((0, 0), char, font=font)
    # bbox = (left, top, right, bottom) in pixels
    # top is negative for ascenders above baseline
    # bottom is positive for descenders below baseline
    x1, y1, x2, y2 = bbox
    w = x2 - x1
    h = y2 - y1
    # baseline of the font = -y1 pixels from the top of the bbox
    # So the actual top (including ascender) = offset from rendered top
    # If we render at (0,0), the glyph starts at (x1, y1)
    # Positive y1 means the glyph sits below the baseline
    # Negative y1 means the glyph has ascenders above baseline
    return w, h, x1, y1, x2

def analyze_font(ttf_path, pt):
    """Full font analysis."""
    font = ImageFont.truetype(ttf_path, pt)
    name = os.path.splitext(os.path.basename(ttf_path))[0]
    
    metrics = {}
    max_w = 0
    max_h = 0
    min_top = 0   # most negative y1 (ascender height)
    max_bottom = 0 # most positive y2 (descender depth)
    
    print(f"\n=== {name} @ {pt}pt ===")
    
    for code in range(0x20, 0x7F):
        ch = chr(code)
        w, h, x1, y1, x2 = analyze_glyph(font, ch)
        metrics[code] = (w, h, x1, y1)
        if w > max_w: max_w = w
        if h > max_h: max_h = h
        if y1 < min_top: min_top = y1
        if (y1 + h) > max_bottom: max_bottom = y1 + h
    
    total_h = max_bottom - min_top  # full glyph height including ascenders/descenders
    baseline_offset = -min_top  # pixels from top of bounding box to baseline
    
    print(f"  Max glyph width: {max_w}px")
    print(f"  Max glyph height (bbox): {max_h}px")
    print(f"  Min top (ascender): {min_top}")
    print(f"  Max bottom (descender): {max_bottom}")
    print(f"  Total font height: {total_h}px")
    print(f"  Baseline offset from top: {baseline_offset}px")
    print(f"  Height in bytes (ceil/8): {(total_h + 7) // 8}")
    
    # Show problematic chars
    print(f"  Characters with descenders (y1+h > baseline):")
    for code in range(0x20, 0x7F):
        w, h, x1, y1, = metrics[code]
        if y1 > 0:
            print(f"    '{chr(code)}': y1={y1}, descender={y1}")
    
    print(f"  Characters with ascenders (y1 < 0):")
    for code in range(0x20, 0x7F):
        w, h, x1, y1, = metrics[code]
        if y1 < 0:
            print(f"    '{chr(code)}': y1={y1}, ascender={-y1}")
    
    # Generate glyph data
    h_bytes = (total_h + 7) // 8
    
    # Build glyph table: for each glyph, store width + h_bytes * actualHeight bytes
    glyph_data = {}
    for code in range(0x20, 0x7F):
        ch = chr(code)
        w, h, x1, y1, _ = analyze_glyph(font, ch)
        
        # Render glyph at its natural position within a total_h tall canvas
        glyph_img = Image.new('1', (max(1, w), total_h), 0)
        draw = ImageDraw.Draw(glyph_img)
        draw.text((-x1, -min_top), ch, font=font, fill=1)
        
        pixels = list(glyph_img.getdata())
        gw, gh = glyph_img.size
        
        # Convert to column-major: byte[col * h_bytes + byte_idx]
        # For each column, pack rows 0-7 into byte 0, rows 8-15 into byte 1, etc.
        cols = []
        for col in range(gw):
            for byte_idx in range(h_bytes):
                byte = 0
                for bit in range(8):
                    row = byte_idx * 8 + bit
                    if row < gh:
                        idx = row * gw + col
                        if idx < len(pixels) and pixels[idx]:
                            byte |= (1 << bit)
                cols.append(byte)
        
        glyph_data[code] = (gw, cols)
    
    return {
        'name': name,
        'pt': pt,
        'max_w': max_w,
        'total_h': total_h,
        'baseline_offset': baseline_offset,
        'h_bytes': h_bytes,
        'glyph_data': glyph_data,
    }

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <font.ttf> [pt_size]")
        sys.exit(1)
    
    ttf_path = sys.argv[1]
    pt = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    analyze_font(ttf_path, pt)
