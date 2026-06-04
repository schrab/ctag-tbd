/*******************************************************************************
 * Size: 8 px
 * Bpp: 1
 * Opts: --bpp 1 --size 8 --no-compress --stride 1 --align 1 --font norns.ttf --symbols  «°±²³µ¹º»×Ø÷øʰʲʳʷʸˡˢˣ̃ᴹᵃᵇᵈᵉᵍᵏᵐᵒᵖᵗᵘᵛᶜᶠᶻ   ‐•⁰ⁱ⁴⁵⁶⁷⁸⁹⁺⁻⁽⁾ⁿⅠⅡⅢⅣⅤⅥⅦⅰⅱⅲⅳⅴⅵⅶ←↑→↓↲↳↺↻∆∓∕∞⏎⏏⏩⏪⏭⏮⏯⏴⏵⏸⏹⏺␣▦▮▲▶►▼◀◆◉○●◢◣◤◥☐☑☒☰☱☲☳☴☵☶☷♩♪♫♬♭♮♯✓ --format lvgl -o Norns.c
 ******************************************************************************/

#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif



#ifndef NORNS
#define NORNS 1
#endif

#if NORNS

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+00A0 " " */
    0x0,

    /* U+00AB "«" */
    0x4c, 0x92,

    /* U+00B0 "°" */
    0x55, 0x0,

    /* U+00B1 "±" */
    0x5d, 0xe,

    /* U+00B2 "²" */
    0xc5, 0x70,

    /* U+00B3 "³" */
    0xcc, 0xe0,

    /* U+00B5 "µ" */
    0xaa, 0xad, 0x88,

    /* U+00B9 "¹" */
    0xd5,

    /* U+00BA "º" */
    0x55, 0x0,

    /* U+00BB "»" */
    0x92, 0x64,

    /* U+00D7 "×" */
    0xaa, 0x80,

    /* U+00D8 "Ø" */
    0x74, 0xeb, 0x97, 0x0,

    /* U+00F7 "÷" */
    0x43, 0x84,

    /* U+00F8 "ø" */
    0x16, 0xbd, 0x68,

    /* U+02B0 "ʰ" */
    0x9a, 0xd0,

    /* U+02B2 "ʲ" */
    0x45, 0x80,

    /* U+02B3 "ʳ" */
    0xba, 0x0,

    /* U+02B7 "ʷ" */
    0xad, 0x54,

    /* U+02B8 "ʸ" */
    0xb5, 0x94,

    /* U+02E1 "ˡ" */
    0xf0,

    /* U+02E2 "ˢ" */
    0x6b, 0x0,

    /* U+02E3 "ˣ" */
    0xaa, 0x80,

    /* U+0303 "̃" */
    0x5a,

    /* U+1D39 "ᴹ" */
    0x8e, 0xeb, 0x10,

    /* U+1D43 "ᵃ" */
    0x75, 0x80,

    /* U+1D47 "ᵇ" */
    0x9a, 0xe0,

    /* U+1D48 "ᵈ" */
    0x2e, 0xb0,

    /* U+1D49 "ᵉ" */
    0x57, 0x30,

    /* U+1D4D "ᵍ" */
    0x75, 0x94,

    /* U+1D4F "ᵏ" */
    0x97, 0x50,

    /* U+1D50 "ᵐ" */
    0xf5, 0x6a,

    /* U+1D52 "ᵒ" */
    0x55, 0x0,

    /* U+1D56 "ᵖ" */
    0xd7, 0x40,

    /* U+1D57 "ᵗ" */
    0x5d, 0x20,

    /* U+1D58 "ᵘ" */
    0xb5, 0x80,

    /* U+1D5B "ᵛ" */
    0xb7, 0x0,

    /* U+1D9C "ᶜ" */
    0x71, 0x80,

    /* U+1DA0 "ᶠ" */
    0x2b, 0xa0,

    /* U+1DBB "ᶻ" */
    0xc9, 0x80,

    /* U+2006 " " */
    0x0,

    /* U+2009 " " */
    0x0,

    /* U+200A " " */
    0x0,

    /* U+2010 "‐" */
    0xe0,

    /* U+2022 "•" */
    0xf0,

    /* U+2070 "⁰" */
    0x56, 0xa0,

    /* U+2071 "ⁱ" */
    0xb0,

    /* U+2074 "⁴" */
    0xbc, 0x90,

    /* U+2075 "⁵" */
    0xf8, 0xe0,

    /* U+2076 "⁶" */
    0x7a, 0xa0,

    /* U+2077 "⁷" */
    0xe5, 0x20,

    /* U+2078 "⁸" */
    0xfe, 0xf0,

    /* U+2079 "⁹" */
    0x55, 0xe0,

    /* U+207A "⁺" */
    0x5d, 0x0,

    /* U+207B "⁻" */
    0xe0,

    /* U+207D "⁽" */
    0x69,

    /* U+207E "⁾" */
    0x96,

    /* U+207F "ⁿ" */
    0xd6, 0x80,

    /* U+2160 "Ⅰ" */
    0xf8,

    /* U+2161 "Ⅱ" */
    0xb6, 0xda,

    /* U+2162 "Ⅲ" */
    0xad, 0x6b, 0x5a, 0x80,

    /* U+2163 "Ⅳ" */
    0xa6, 0x9a, 0xaa, 0x90,

    /* U+2164 "Ⅴ" */
    0x99, 0xaa, 0x40,

    /* U+2165 "Ⅵ" */
    0x96, 0x5a, 0x69, 0x44,

    /* U+2166 "Ⅶ" */
    0x95, 0x95, 0xa5, 0xa5, 0x45,

    /* U+2170 "ⅰ" */
    0xb8,

    /* U+2171 "ⅱ" */
    0xa2, 0xda,

    /* U+2172 "ⅲ" */
    0xa8, 0x2b, 0x5a, 0x80,

    /* U+2173 "ⅳ" */
    0x80, 0x9a, 0x6a, 0x90,

    /* U+2174 "ⅴ" */
    0x99, 0xa4,

    /* U+2175 "ⅵ" */
    0x6, 0x49, 0x69, 0x44,

    /* U+2176 "ⅶ" */
    0x5, 0x90, 0x95, 0xa5, 0x45,

    /* U+2190 "←" */
    0x23, 0x3e, 0xc2, 0x0,

    /* U+2191 "↑" */
    0x23, 0xbe, 0x42, 0x0,

    /* U+2192 "→" */
    0x21, 0xbe, 0x62, 0x0,

    /* U+2193 "↓" */
    0x21, 0x3e, 0xe2, 0x0,

    /* U+21B2 "↲" */
    0x25, 0x9f, 0xd8, 0x20,

    /* U+21B3 "↳" */
    0x92, 0x6f, 0xc6, 0x10,

    /* U+21BA "↺" */
    0x21, 0x8f, 0x99, 0x24, 0x20,

    /* U+21BB "↻" */
    0x10, 0x67, 0xe6, 0x91, 0x0,

    /* U+2206 "∆" */
    0x22, 0xa3, 0xf0,

    /* U+2213 "∓" */
    0xe1, 0x74,

    /* U+2215 "∕" */
    0x25, 0x29, 0x0,

    /* U+221E "∞" */
    0x55, 0x54,

    /* U+23CE "⏎" */
    0x4, 0x96, 0x7f, 0x60, 0x80,

    /* U+23CF "⏏" */
    0x23, 0xbe, 0xf, 0x80,

    /* U+23E9 "⏩" */
    0x93, 0x6f, 0xf6, 0x90,

    /* U+23EA "⏪" */
    0x25, 0xbf, 0xdb, 0x24,

    /* U+23ED "⏭" */
    0x9d, 0xfd, 0x90,

    /* U+23EE "⏮" */
    0x9b, 0xfb, 0x90,

    /* U+23EF "⏯" */
    0x97, 0x5f, 0x75, 0x94,

    /* U+23F4 "⏴" */
    0x37, 0xf7, 0x30,

    /* U+23F5 "⏵" */
    0xce, 0xfe, 0xc0,

    /* U+23F8 "⏸" */
    0xde, 0xf7, 0xbd, 0x80,

    /* U+23F9 "⏹" */
    0xff, 0xff, 0xff, 0x80,

    /* U+23FA "⏺" */
    0x77, 0xff, 0xf7, 0x0,

    /* U+2423 "␣" */
    0x8f, 0xc0,

    /* U+25A6 "▦" */
    0xa8, 0x2a, 0xa, 0x80,

    /* U+25AE "▮" */
    0xff, 0xfe,

    /* U+25B2 "▲" */
    0x23, 0xbe,

    /* U+25B6 "▶" */
    0x9b, 0xe8,

    /* U+25BA "►" */
    0x9b, 0xe8,

    /* U+25BC "▼" */
    0xfb, 0x88,

    /* U+25C0 "◀" */
    0x2f, 0xb2,

    /* U+25C6 "◆" */
    0x23, 0xbe, 0xe2, 0x0,

    /* U+25C9 "◉" */
    0x74, 0x6b, 0x17, 0x0,

    /* U+25CB "○" */
    0x74, 0x63, 0x17, 0x0,

    /* U+25CF "●" */
    0x77, 0xff, 0xf7, 0x0,

    /* U+25E2 "◢" */
    0x13, 0x7f,

    /* U+25E3 "◣" */
    0x8c, 0xef,

    /* U+25E4 "◤" */
    0xfe, 0xc8,

    /* U+25E5 "◥" */
    0xf7, 0x31,

    /* U+2610 "☐" */
    0xfc, 0x63, 0x1f, 0x80,

    /* U+2611 "☑" */
    0xfc, 0x6b, 0x1f, 0x80,

    /* U+2612 "☒" */
    0xfd, 0x77, 0x5f, 0x80,

    /* U+2630 "☰" */
    0xf8, 0x3e, 0xf, 0x80,

    /* U+2631 "☱" */
    0xd8, 0x3e, 0xf, 0x80,

    /* U+2632 "☲" */
    0xf8, 0x36, 0xf, 0x80,

    /* U+2633 "☳" */
    0xd8, 0x36, 0xf, 0x80,

    /* U+2634 "☴" */
    0xf8, 0x3e, 0xd, 0x80,

    /* U+2635 "☵" */
    0xd8, 0x3e, 0xd, 0x80,

    /* U+2636 "☶" */
    0xf8, 0x36, 0xd, 0x80,

    /* U+2637 "☷" */
    0xd8, 0x36, 0xd, 0x80,

    /* U+2669 "♩" */
    0x24, 0xbe, 0x80,

    /* U+266A "♪" */
    0x31, 0x48, 0xce, 0x20,

    /* U+266B "♫" */
    0x3c, 0x92, 0xdf, 0xe9, 0x0,

    /* U+266C "♬" */
    0x3c, 0x92, 0xdf, 0xe9, 0x0,

    /* U+266D "♭" */
    0x93, 0xdc,

    /* U+266E "♮" */
    0x9e, 0xf2,

    /* U+266F "♯" */
    0x57, 0xd5, 0xf5, 0x0,

    /* U+2713 "✓" */
    0x12, 0xa4,

    /* U+E260 "" */
    0x93, 0x5d, 0x0,

    /* U+E870 "" */
    0x69, 0x96,

    /* U+E871 "" */
    0x16, 0xbd, 0x68,

    /* U+ED60 "" */
    0x9e, 0xe0,

    /* U+ED61 "" */
    0x9e, 0xf2,

    /* U+ED62 "" */
    0x57, 0xd5, 0xf5, 0x0,

    /* U+ED63 "" */
    0xaa, 0x80,

    /* U+ED64 "" */
    0x93, 0xfb, 0x76
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 64, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 3, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 5, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 7, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 9, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 11, .adv_w = 80, .box_w = 4, .box_h = 6, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 14, .adv_w = 48, .box_w = 2, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 15, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 17, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 19, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 21, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 25, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 27, .adv_w = 80, .box_w = 4, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 30, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 32, .adv_w = 48, .box_w = 2, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 34, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 36, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 38, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 40, .adv_w = 32, .box_w = 1, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 41, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 43, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 45, .adv_w = 80, .box_w = 4, .box_h = 2, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 46, .adv_w = 96, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 49, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 51, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 53, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 55, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 57, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 59, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 61, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 63, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 65, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 67, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 69, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 71, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 73, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 75, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 77, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 79, .adv_w = 48, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 80, .adv_w = 32, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 81, .adv_w = 16, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 82, .adv_w = 64, .box_w = 3, .box_h = 1, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 83, .adv_w = 48, .box_w = 2, .box_h = 2, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 84, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 86, .adv_w = 32, .box_w = 1, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 87, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 89, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 91, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 93, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 95, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 97, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 99, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 101, .adv_w = 64, .box_w = 3, .box_h = 1, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 102, .adv_w = 48, .box_w = 2, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 103, .adv_w = 48, .box_w = 2, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 104, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 106, .adv_w = 32, .box_w = 1, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 107, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 109, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 113, .adv_w = 112, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 117, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 120, .adv_w = 112, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 124, .adv_w = 144, .box_w = 8, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 129, .adv_w = 32, .box_w = 1, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 130, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 132, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 136, .adv_w = 112, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 140, .adv_w = 80, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 142, .adv_w = 112, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 146, .adv_w = 144, .box_w = 8, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 151, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 155, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 159, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 163, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 167, .adv_w = 112, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 171, .adv_w = 112, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 175, .adv_w = 112, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 180, .adv_w = 112, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 185, .adv_w = 96, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 188, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 190, .adv_w = 64, .box_w = 3, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 193, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 195, .adv_w = 112, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 200, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 204, .adv_w = 112, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 208, .adv_w = 112, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 212, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 215, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 218, .adv_w = 112, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 222, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 225, .adv_w = 80, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 228, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 232, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 236, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 240, .adv_w = 96, .box_w = 5, .box_h = 2, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 242, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 246, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 248, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 250, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 252, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 254, .adv_w = 96, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 256, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 258, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 262, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 266, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 270, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 274, .adv_w = 80, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 276, .adv_w = 80, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 278, .adv_w = 80, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 280, .adv_w = 80, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 282, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 286, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 290, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 294, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 298, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 302, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 306, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 310, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 314, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 318, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 322, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 326, .adv_w = 64, .box_w = 3, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 329, .adv_w = 96, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 333, .adv_w = 112, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 338, .adv_w = 112, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 343, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 345, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 347, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 351, .adv_w = 80, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 353, .adv_w = 64, .box_w = 3, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 356, .adv_w = 80, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 358, .adv_w = 80, .box_w = 4, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 361, .adv_w = 64, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 363, .adv_w = 64, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 365, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 369, .adv_w = 64, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 371, .adv_w = 112, .box_w = 6, .box_h = 4, .ofs_x = 0, .ofs_y = 2}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0xb, 0x10, 0x11, 0x12, 0x13, 0x15, 0x19,
    0x1a, 0x1b, 0x37, 0x38, 0x57, 0x58, 0x210, 0x212,
    0x213, 0x217, 0x218, 0x241, 0x242, 0x243, 0x263, 0x1c99,
    0x1ca3, 0x1ca7, 0x1ca8, 0x1ca9, 0x1cad, 0x1caf, 0x1cb0, 0x1cb2,
    0x1cb6, 0x1cb7, 0x1cb8, 0x1cbb, 0x1cfc, 0x1d00, 0x1d1b, 0x1f66,
    0x1f69, 0x1f6a, 0x1f70, 0x1f82, 0x1fd0, 0x1fd1, 0x1fd4, 0x1fd5,
    0x1fd6, 0x1fd7, 0x1fd8, 0x1fd9, 0x1fda, 0x1fdb, 0x1fdd, 0x1fde,
    0x1fdf, 0x20c0, 0x20c1, 0x20c2, 0x20c3, 0x20c4, 0x20c5, 0x20c6,
    0x20d0, 0x20d1, 0x20d2, 0x20d3, 0x20d4, 0x20d5, 0x20d6, 0x20f0,
    0x20f1, 0x20f2, 0x20f3, 0x2112, 0x2113, 0x211a, 0x211b, 0x2166,
    0x2173, 0x2175, 0x217e, 0x232e, 0x232f, 0x2349, 0x234a, 0x234d,
    0x234e, 0x234f, 0x2354, 0x2355, 0x2358, 0x2359, 0x235a, 0x2383,
    0x2506, 0x250e, 0x2512, 0x2516, 0x251a, 0x251c, 0x2520, 0x2526,
    0x2529, 0x252b, 0x252f, 0x2542, 0x2543, 0x2544, 0x2545, 0x2570,
    0x2571, 0x2572, 0x2590, 0x2591, 0x2592, 0x2593, 0x2594, 0x2595,
    0x2596, 0x2597, 0x25c9, 0x25ca, 0x25cb, 0x25cc, 0x25cd, 0x25ce,
    0x25cf, 0x2673, 0xe1c0, 0xe7d0, 0xe7d1, 0xecc0, 0xecc1, 0xecc2,
    0xecc3, 0xecc4
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 160, .range_length = 60613, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 138, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif

};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t Norns = {
#else
lv_font_t Norns = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 8,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .static_bitmap = 0,
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if NORNS*/
