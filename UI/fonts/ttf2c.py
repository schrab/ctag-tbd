#!/usr/bin/env python3
"""
Convert TTF to C bitmap header for SSD1306/SSD1309 OLED.

Column-major, LSB = top pixel, left-aligned in cell with per-glyph advance.

Phase 1: render each char with freetype FT_LOAD_TARGET_MONO (or PIL),
         find tight pixel bbox, store per-glyph advance & bitmap_left.
Phase 2: cell width = max_adv (largest freetype advance), left-align
         each glyph at dst_x = bitmap_left. Per-glyph advances array
         emitted alongside glyph data for proportional spacing.
--fixed-width W / --fixed-height H: cell size in px (required, but W
         is overridden by max_adv in freetype mode).
--max-code N: highest code point (default 0x7F).
--maxpool: use maxpool downsample (default: NN).
--no-center: use baseline-relative vertical positioning (default: centered).
         Ignored in --freetype mode (always baseline-relative).
"""

import sys, os
import warnings
from PIL import Image, ImageFont, ImageDraw
warnings.filterwarnings("ignore", category=DeprecationWarning)

try:
    import freetype
    HAVE_FREETYPE = True
except ImportError:
    HAVE_FREETYPE = False


def maxpool_downsample(img, factor):
    """Downsample factor:1 — output pixel ON if ANY pixel in factor×f block is ON."""
    w, h = img.size
    dw = w // factor
    dh = h // factor
    if dw < 1 or dh < 1:
        return img
    out = Image.new('1', (dw, dh), 0)
    src = img.load()
    dst = out.load()
    for oy in range(dh):
        yo = oy * factor
        for ox in range(dw):
            xo = ox * factor
            val = 0
            for dy in range(factor):
                for dx in range(factor):
                    if src[xo + dx, yo + dy]:
                        val = 1
                        break
                if val:
                    break
            dst[ox, oy] = val
    return out


def nn_downsample(img, factor):
    """Downsample factor:1 — nearest neighbour."""
    w, h = img.size
    dw = w // factor
    dh = h // factor
    if dw < 1 or dh < 1:
        return img
    out = Image.new('1', (dw, dh), 0)
    src = img.load()
    dst = out.load()
    for oy in range(dh):
        for ox in range(dw):
            dst[ox, oy] = src[ox * factor, oy * factor]
    return out


def generate_header(ttf_path, pt, output_path, render_mult=1,
                    fixed_width=None, fixed_height=None, max_code=0x7F,
                    center=True, maxpool=False, ds=1, freetype_mode=False):
    if fixed_width is None or fixed_height is None:
        print("ERROR: --fixed-width and --fixed-height are required", file=sys.stderr)
        sys.exit(1)

    font = ImageFont.truetype(ttf_path, pt * render_mult)
    name = os.path.splitext(os.path.basename(ttf_path))[0]
    fn = name.replace('-', '_')

    glyph_w = fixed_width
    total_h = fixed_height
    h_bytes = (total_h + 7) // 8
    glyph_bytes_per = glyph_w * h_bytes
    n_chars = max_code + 1

    detail = f"rendered at {pt*render_mult}pt"
    if render_mult > 1:
        detail += f", {render_mult}:1"
    if ds > 1:
        detail += f" then ds {ds}:1"
    print(f"  Font: {name} @ {pt}pt ({detail})" if detail else f"  Font: {name} @ {pt}pt")
    print(f"    Cell: {glyph_w}x{total_h}px")
    print(f"    Height bytes/col: {h_bytes}")
    print(f"    Glyph storage: {glyph_bytes_per} bytes")
    print(f"    Advance: {glyph_w + 1}" + (" (placeholder, overridden by max_adv)" if freetype_mode else ""))
    print(f"    Code points: 0x00-0x{max_code:02X} ({n_chars} entries)")

    # Large hi-rez canvas – text origin at centre, plenty of room
    MARGIN = 100 * render_mult
    tmp_w = tmp_h = 2 * MARGIN
    OX = OY = MARGIN  # text origin in hi-rez canvas coordinates

    # Phase 1: render each char and find tight pixel bbox
    char_data = {}
    advances = [0] * n_chars  # 0 = sentinel: not yet set

    if freetype_mode:
        if not HAVE_FREETYPE:
            print("ERROR: freetype-py not installed. Install with: pip install freetype-py", file=sys.stderr)
            sys.exit(1)
        ft_face = freetype.Face(ttf_path)
        ft_face.set_pixel_sizes(0, pt)

        for code in range(n_chars):
            if code < 0x20:
                continue
            try:
                ft_face.load_char(chr(code), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
            except Exception:
                continue
            g = ft_face.glyph
            bm = g.bitmap
            if bm.rows == 0 or bm.width == 0 or not bm.buffer:
                continue

            # Convert freetype bitmap to PIL Image
            w, h = bm.width, bm.rows
            bt = g.bitmap_top
            bl = g.bitmap_left
            pil_img = Image.new('1', (w, h), 0)
            px = list(pil_img.getdata())
            for y in range(h):
                for x in range(w):
                    byte_idx = y * bm.pitch + (x >> 3)
                    bit_idx = 7 - (x & 7)
                    if byte_idx < len(bm.buffer) and ((bm.buffer[byte_idx] >> bit_idx) & 1):
                        px[y * w + x] = 1
            pil_img.putdata(px)

            adv_raw = g.advance.x / 64.0
            if ds > 1:
                pil_img = nn_downsample(pil_img, ds)
                w = pil_img.width
                h = pil_img.height
                bt = round(bt / ds)
                bl = round(bl / ds)
                adv_raw /= ds

            char_data[code] = {
                'img': pil_img,
                'mc_min': 0, 'mc_max': w - 1,
                'mr_min': 0, 'mr_max': h - 1,
                'bt': bt,
                'bl': bl,
            }
            advances[code] = max(1, round(adv_raw))
    else:
        font = ImageFont.truetype(ttf_path, pt * render_mult)
        MARGIN = 100 * render_mult
        tmp_w = tmp_h = 2 * MARGIN
        OX = OY = MARGIN

        for code in range(n_chars):
            if code < 0x20:
                continue
            ch = chr(code)
            img = Image.new('1', (tmp_w, tmp_h), 0)
            dr = ImageDraw.Draw(img)
            try:
                dr.text((OX, OY), ch, font=font, fill=1, anchor='ls')
            except Exception:
                continue

            if render_mult > 1:
                if maxpool:
                    img = maxpool_downsample(img, render_mult)
                else:
                    img = nn_downsample(img, render_mult)

            if ds > 1:
                img = nn_downsample(img, ds)

            sw, sh = img.size
            px = list(img.getdata())
            pc = [c for c in range(sw) for r in range(sh) if px[r*sw + c]]
            pr = [r for r in range(sh) for c in range(sw) if px[r*sw + c]]

            if not pc or not pr:
                continue

            mc_min, mc_max = min(pc), max(pc)
            mr_min, mr_max = min(pr), max(pr)

            # Crop to tight bbox for Phase 2 reuse
            cropped = img.crop((mc_min, mr_min, mc_max + 1, mr_max + 1))

            char_data[code] = {
                'img': cropped,
                'mc_min': mc_min, 'mc_max': mc_max,
                'mr_min': mr_min, 'mr_max': mr_max,
            }

    # Compute max_adv for freetype mode → cell width = widest advance
    if freetype_mode and char_data:
        rendered_advances = [a for a in advances if a > 0]
        if rendered_advances:
            max_adv_val = max(rendered_advances)
            if max_adv_val < 1: max_adv_val = glyph_w + 1
        else:
            max_adv_val = glyph_w + 1
        glyph_w = max_adv_val
        glyph_bytes_per = glyph_w * h_bytes
        # Non-rendered codes use max_adv as fallback advance
        for code in range(n_chars):
            if code < 0x20 or code not in char_data:
                advances[code] = max_adv_val
        print(f"    Freetype max advance: {max_adv_val} → cell: {glyph_w}x{total_h}px, storage: {glyph_bytes_per} B/glyph")

    # Phase 2: build table
    baseline_row = 0
    if freetype_mode or not center:
        if freetype_mode:
            # Baseline from standard chars using stored bt
            std_codes = list(range(0x30,0x3A)) + list(range(0x41,0x5B)) + list(range(0x61,0x7B))
            std_vals = [(cd['bt'], cd['mr_max'] - cd['mr_min'] + 1) for c, cd in char_data.items() if c in std_codes]
            if std_vals:
                max_ft_asc = max(v[0] for v in std_vals)
                max_ft_desc = max(v[1] - v[0] for v in std_vals)
                ft_h = max_ft_asc + max_ft_desc
                if ft_h <= total_h:
                    baseline_row = (total_h - ft_h) // 2 + max_ft_asc
                else:
                    baseline_row = total_h - 1 - max_ft_desc
        else:
            oy_ds = OY // render_mult if render_mult > 1 else OY
        if not freetype_mode:
            ascii_codes = [c for c in char_data if 0x20 <= c <= 0x7E]
            if not ascii_codes:
                ascii_codes = list(char_data.keys())
            union_min_r = min(char_data[c]['mr_min'] for c in ascii_codes)
            union_max_r = max(char_data[c]['mr_max'] for c in ascii_codes)
            max_ascent = oy_ds - union_min_r
            pos_descent = max(0, union_max_r - oy_ds)
            needed_h = max_ascent + pos_descent
            if needed_h <= total_h:
                baseline_row = (total_h - needed_h) // 2 + max_ascent
            else:
                baseline_row = total_h - 1 - pos_descent

    table = {}
    for code in range(n_chars):
        if code < 0x20 or code not in char_data:
            table[code] = [0] * glyph_bytes_per
            continue

        cd = char_data[code]
        cw = cd['mc_max'] - cd['mc_min'] + 1
        ch_h = cd['mr_max'] - cd['mr_min'] + 1

        out = Image.new('1', (glyph_w, total_h), 0)

        if freetype_mode:
            dst_y = baseline_row - cd['bt']
            dst_x = max(0, cd['bl'])
        elif center:
            dst_y = (total_h - ch_h) // 2
            dst_x = max(0, (glyph_w - cw) // 2)
        else:
            dst_y = baseline_row + (cd['mr_min'] - oy_ds)
            dst_x = max(0, (glyph_w - cw) // 2)

        src_x = 0
        src_y = 0

        # If glyph wider than cell, centre-crop horizontally
        if cw > glyph_w:
            excess = cw - glyph_w
            src_x = excess // 2
            dst_x = 0
            cw = glyph_w

        # Clamp vertical
        if dst_y < 0:
            src_y = -dst_y
            ch_h += dst_y
            dst_y = 0
        if dst_y + ch_h > total_h:
            ch_h = total_h - dst_y

        if cw > 0 and ch_h > 0:
            region = cd['img'].crop((src_x, src_y, src_x + cw, src_y + ch_h))
            out.paste(region, (dst_x, dst_y))

        # Column-major byte extraction
        pixels = list(out.getdata())
        cols = []
        for col in range(glyph_w):
            for byte_idx in range(h_bytes):
                byte_val = 0
                for bit in range(8):
                    row = byte_idx * 8 + bit
                    if row < total_h:
                        idx = row * glyph_w + col
                        if idx < len(pixels) and pixels[idx]:
                            byte_val |= (1 << bit)
                cols.append(byte_val)
        table[code] = cols

    # Write header
    lines = []
    lines.append(f"// Auto-generated from {os.path.basename(ttf_path)} at {pt}pt" +
                 (f" (rendered at {pt*render_mult}pt, {render_mult}:1)" if render_mult > 1 else ""))
    lines.append("// Column-major, LSB = top pixel")
    lines.append("#pragma once")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append(f"static constexpr int FONT_{fn.upper()}_W = {glyph_w};")
    lines.append(f"static constexpr int FONT_{fn.upper()}_H = {total_h};")
    bl_val = baseline_row if (not center or freetype_mode) else 0
    lines.append(f"static constexpr int FONT_{fn.upper()}_BASELINE = {bl_val};")
    lines.append(f"static constexpr int FONT_{fn.upper()}_H_BYTES = {h_bytes};")
    lines.append(f"static constexpr int FONT_{fn.upper()}_ADVANCE = {glyph_w};")
    lines.append(f"static constexpr int FONT_{fn.upper()}_N_CHARS = {n_chars};")
    lines.append("")
    lines.append(f"static const uint8_t font_{fn.lower()}[{n_chars}][{glyph_bytes_per}] = {{")

    for code in range(n_chars):
        cols = table[code]
        hex_bytes = ", ".join(f"0x{b:02x}" for b in cols)
        ch = chr(code) if 0x20 <= code <= 0x7E else ''
        comment = f" // 0x{code:02X} '{ch}'" if ch else f" // 0x{code:02X}"
        lines.append(f"    {{ {hex_bytes} }},{comment}")

    lines.append("};")
    lines.append("")

    # Emit per-glyph advances array
    lines.append(f"static constexpr uint8_t font_{fn.lower()}_advances[{n_chars}] = {{")
    for code in range(n_chars):
        a = advances[code]
        ch = chr(code) if 0x20 <= code <= 0x7E else ''
        comment = f" // 0x{code:02X} '{ch}'" if ch else f" // 0x{code:02X}"
        lines.append(f"    {a},{comment}")
    lines.append("};")
    lines.append("")

    content = "\n".join(lines) + "\n"
    with open(output_path, 'w') as f:
        f.write(content)

    sz = os.path.getsize(output_path)
    print(f"  Wrote {output_path} ({sz} bytes)")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Convert TTF to C bitmap font header")
    parser.add_argument("input", help="Input .ttf file")
    parser.add_argument("output", help="Output .h file")
    parser.add_argument("pt", type=int, help="Point size")
    parser.add_argument("--render-mult", type=int, default=1,
                        help="Render at pt*N then NN-downsample N:1")
    parser.add_argument("--ds", type=int, default=1,
                        help="Post-render NN downsample factor (e.g. --ds 2 for fonts with 2x2 pixels)")
    parser.add_argument("--fixed-width", type=int, required=True,
                        help="Cell width in px")
    parser.add_argument("--fixed-height", type=int, required=True,
                        help="Cell height in px")
    parser.add_argument("--max-code", type=lambda x: int(x, 0), default=0x7F,
                        help="Highest code point (default 0x7F)")
    parser.add_argument("--maxpool", action="store_true",
                        help="Use maxpool downsample (default: NN)")
    parser.add_argument("--no-center", action="store_true",
                        help="Use baseline-relative positioning (default: centered)")
    parser.add_argument("--freetype", action="store_true",
                        help="Use freetype-py with FT_LOAD_TARGET_MONO (default: PIL)")
    args = parser.parse_args()

    if args.freetype and not HAVE_FREETYPE:
        print("ERROR: --freetype requires freetype-py. Install: pip install freetype-py", file=sys.stderr)
        sys.exit(1)

    generate_header(args.input, args.pt, args.output, args.render_mult,
                    args.fixed_width, args.fixed_height, args.max_code,
                    center=not args.no_center, maxpool=args.maxpool, ds=args.ds,
                    freetype_mode=args.freetype)
