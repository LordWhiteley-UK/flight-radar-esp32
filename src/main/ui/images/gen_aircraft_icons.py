#!/usr/bin/env python3
"""
Generate LVGL TRUE_COLOR_ALPHA image assets for the radar aircraft icons.

Reads three SVG files from the user-supplied Plane Icons folder, rasterizes
them at the three radar-class sizes (using macOS `qlmanage` for the SVG ->
PNG conversion), and emits C arrays compatible with the existing
ui_img_aircraft_*_png.c file format.

Pattern per pixel (3 bytes): RGB565 (16-bit LE) + alpha (8-bit).
Opaque pixels are emitted as solid white (0xFF, 0xFF, 0xFF) so the sprite
reads as a clean silhouette; class colour is overlaid by the renderer as a
separate rect / arc behind / over the icon (since lv_color_mix_premult has
no depth-16 implementation, recolour at draw time is disabled).

Run: python3 gen_aircraft_icons.py
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
from io import BytesIO

from PIL import Image

SRC_DIR = Path(__file__).resolve().parents[4] / "Plane Icons"
OUT_DIR = Path(__file__).resolve().parent

# (svg filename, output C filename, var basename, render size)
#
# `render_size` is the pixel size of the generated sprite. This must match
# `half * 2` in radar.c DrawAircraft so the lv_draw_sw_img convert_cb stride
# assumption holds (src_stride == dest_area width).
#
# The SVG silhouettes fill roughly 85% of the source viewBox; at the sizes
# below the sprite canvas is generous enough that the silhouette has a
# margin of pixels around the wingtips, so a non-zero dsc.angle never
# samples off the source buffer and the rotation stays symmetric.
JOBS = [
    ("cessna.svg", "ui_img_aircraft_small_png.c",  "ui_img_aircraft_small_png",  72),
    ("b737.svg",   "ui_img_aircraft_medium_png.c", "ui_img_aircraft_medium_png", 96),
    ("b767.svg",   "ui_img_aircraft_large_png.c",  "ui_img_aircraft_large_png", 144),
]

HEADER = """\
// Auto-generated aircraft icon ({label})
// LVGL image: LV_IMG_CF_TRUE_COLOR_ALPHA at LV_COLOR_DEPTH=16 (RGB565 + A, 3 bytes/px)
// {size}x{size} silhouette rasterized from Plane Icons/{svg}.
// Opaque pixels are emitted as solid white (RGB565=0xFFFF, alpha=0xFF).
// Recolour at draw time is disabled because lv_color_mix_premult has no
// depth-16 implementation. Cloth colour is supplied by the renderer as a
// separate rect / arc behind / over the icon.

#include "../ui.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
    #define LV_ATTRIBUTE_MEM_ALIGN
#endif

const LV_ATTRIBUTE_MEM_ALIGN uint8_t {var}_data[] = {{
"""

FOOTER = """\
}};

const lv_img_dsc_t {var} = {{
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .header.w = {size},
    .header.h = {size},
    .data_size = sizeof({var}_data),
    .data = {var}_data,
}};
"""


def render_svg(svg_path: Path, size: int) -> Image.Image:
    """Rasterize an SVG and return a square RGBA image at the given pixel
    size, with the silhouette CENTERED inside a transparent margin so a
    subsequent rotation never samples off the source buffer.

    qlmanage produces a black silhouette on white background that fills
    ~100% of the rendered canvas (the source SVGs have viewBox 0 0 512
    512 with the silhouette touching all four edges). To get a margin
    around the wings, we render at a large intermediate size, crop to
    the actual silhouette bbox, then composite that crop onto a square
    transparent canvas of the requested size, centered. The crop is
    scaled with PIL so the silhouette's longest dimension fits the
    canvas with a fixed pixel margin on every side."""
    # Render at 4x the target size so the crop has enough detail and the
    # final downscale smooths antialiasing on the silhouette edge.
    oversample = size * 4
    with tempfile.TemporaryDirectory() as td:
        td_path = Path(td)
        scratch_svg = td_path / svg_path.name
        shutil.copy(svg_path, scratch_svg)
        subprocess.run(
            ["qlmanage", "-t", "-s", str(oversample), "-o", str(td_path), str(scratch_svg)],
            check=True,
            capture_output=True,
        )
        rendered = td_path / (svg_path.name + ".png")
        if not rendered.exists():
            raise RuntimeError(f"qlmanage did not produce {rendered}")
        # Convert to RGBA so we can find the alpha-tight silhouette bbox.
        # qlmanage renders solid black on solid white — we use luminance
        # as a proxy for "is part of the silhouette".
        gray = Image.open(rendered).convert("L")

    # Find the tight bounding box of the silhouette (any pixel darker
    # than 200/255 is part of the path). Crop to that bbox with a tiny
    # 1px pad so the silhouette edge itself isn't trimmed by an
    # antialiased fringe at the exact threshold.
    bbox = gray.point(lambda v: 0 if v < 200 else 255).getbbox()
    if bbox is None:
        raise RuntimeError(f"silhouette bbox not found for {svg_path}")
    x0, y0, x1, y1 = bbox
    crop = gray.crop(bbox)
    cw, ch = crop.size

    # Scale the crop to fit inside (size - 2*margin) with PIL.LANCZOS so
    # the long axis exactly fills the inner box, preserving aspect.
    margin = max(4, size // 8)  # at least 4px, ~12% of sprite
    inner_size = size - 2 * margin
    scale = inner_size / max(cw, ch)
    new_w = max(1, int(round(cw * scale)))
    new_h = max(1, int(round(ch * scale)))
    scaled = crop.resize((new_w, new_h), Image.LANCZOS)

    # Paste the scaled silhouette centered onto a transparent canvas.
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ox = (size - new_w) // 2
    oy = (size - new_h) // 2
    canvas.paste(scaled, (ox, oy))
    return canvas


def emit_c_array(img: Image.Image, var: str, size: int, label: str, svg: str) -> str:
    """Emit the C source file contents for the rasterized image.

    The SVGs are drawn as a black silhouette on a white background by
    qlmanage. We invert that: opaque pixels in the output are the dark
    silhouette, transparent pixels are the white background. The output
    is always solid white where opaque, regardless of source colour."""
    pixels = img.load()

    rows = []
    for y in range(size):
        chunks = []
        for x in range(size):
            r, g, b, a = pixels[x, y]
            # Silhouette = dark pixel (the path) AND opaque. The canvas
            # margin is transparent black (0,0,0,0) which would otherwise
            # look like silhouette under a luminance-only check, so we
            # also require alpha > 128.
            is_silhouette = (a > 128) and (r + g + b) < 600
            if is_silhouette:
                # opaque → solid white, alpha = 0xFF
                chunks.append("0xFF, 0xFF, 0xFF")
            else:
                # transparent → black + alpha = 0
                chunks.append("0x00, 0x00, 0x00")
        rows.append("    " + ", ".join(chunks) + ",")

    body = "\n".join(rows)
    text = (
        HEADER.format(label=label, size=size, svg=svg, var=var)
        + body
        + "\n"
        + FOOTER.format(var=var, size=size)
    )
    return text


def main():
    if shutil.which("qlmanage") is None:
        raise SystemExit("qlmanage not found (this script needs macOS)")

    for svg, out_name, var, size in JOBS:
        svg_path = SRC_DIR / svg
        if not svg_path.exists():
            print(f"WARN: missing {svg_path}, skipping")
            continue
        print(f"Rendering {svg} -> {out_name} at {size}x{size} ...", end=" ", flush=True)
        img = render_svg(svg_path, size)
        text = emit_c_array(img, var, size, label=svg.replace(".svg", ""), svg=svg)
        out_path = OUT_DIR / out_name
        out_path.write_text(text)
        print(f"wrote {len(text)} bytes to {out_path}")


if __name__ == "__main__":
    main()
