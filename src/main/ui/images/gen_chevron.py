#!/usr/bin/env python3
"""Generate a tiny LVGL alpha sprite that is a solid-white forward
chevron (triangle pointing up, apex at the top-centre). Used by
DrawAircraft in radar.c — the sprite is rotated by lv_draw_img_dsc_t
.angle to match the aircraft's heading. The triangle is sized so
that even at 45° rotation the rotated sampling stays inside the
sprite bounds (with margin on the sides and at the bottom).
"""
from pathlib import Path

SIZE = 32           # sprite is SIZE x SIZE
APEX_Y = 4          # tip row (close to the top edge — small margin)
BASE_Y = SIZE - 4   # base row (4px margin from bottom for rotation)
HALF_BASE = 10      # half-width at the base (so wings spread to ±10)

HEADER = """\
// Auto-generated chevron arrow sprite
// LVGL image: LV_IMG_CF_TRUE_COLOR_ALPHA at LV_COLOR_DEPTH=16
// {size}x{size} solid-white forward-pointing triangle.
// Used by radar.c DrawAircraft: rotated by lv_draw_img_dsc_t.angle
// to match the aircraft heading.

#include "../ui.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
    #define LV_ATTRIBUTE_MEM_ALIGN
#endif

const LV_ATTRIBUTE_MEM_ALIGN uint8_t ui_img_chevron_png_data[] = {{
"""

FOOTER = """\
}};

const lv_img_dsc_t ui_img_chevron_png = {{
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .header.w = {size},
    .header.h = {size},
    .data_size = sizeof(ui_img_chevron_png_data),
    .data = ui_img_chevron_png_data,
}};
"""


def gen_triangle():
    rows = []
    # rows from y=0 to y=SIZE-1
    for y in range(SIZE):
        chunks = []
        for x in range(SIZE):
            # Triangle region:
            #   - tip at (SIZE/2, APEX_Y)
            #   - base at y=BASE_Y, x in [SIZE/2 - HALF_BASE, SIZE/2 + HALF_BASE]
            cx = SIZE / 2.0
            # At row y, the half-width of the triangle is:
            #   t = (y - APEX_Y) / (BASE_Y - APEX_Y)   (0 at apex, 1 at base)
            # half_width(t) = HALF_BASE * t
            if y < APEX_Y or y > BASE_Y:
                inside = False
            else:
                t = (y - APEX_Y) / (BASE_Y - APEX_Y)
                half_w = HALF_BASE * t
                inside = abs(x - cx + 0.5) <= half_w
            if inside:
                # opaque white pixel
                chunks.append("0xFF, 0xFF, 0xFF")
            else:
                chunks.append("0x00, 0x00, 0x00")
        rows.append("    " + ", ".join(chunks) + ",")
    return "\n".join(rows)


def main():
    out = Path(__file__).resolve().parent / "ui_img_chevron_png.c"
    body = gen_triangle()
    text = HEADER.format(size=SIZE) + body + "\n" + FOOTER.format(size=SIZE)
    out.write_text(text)
    print(f"wrote {len(text)} bytes to {out}")


if __name__ == "__main__":
    main()