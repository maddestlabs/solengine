#!/usr/bin/env python3
"""
tools/package_deck_eats.py
Packages loose SVG/PNG deck assets into self-contained .eats deck recipes
with compressed binary blobs (deflate + base64).
"""

import os
import sys
import zlib
import base64

def package_svg_deck(svg_path, out_eats_path, name="Classic Victorian", theme_id="classic"):
    if not os.path.exists(svg_path):
        print(f"Error: {svg_path} not found.")
        return False
    with open(svg_path, "rb") as f:
        svg_data = f.read()

    comp_svg = zlib.compress(svg_data, level=9)
    b64_svg = base64.b64encode(comp_svg).decode("ascii")

    content = f'''# SolEngine Deck Recipe — {name}
# Connects Hardwood Solitaire 3.0 vector elegance with compressed binary asset embedding.

sol.set_theme({{
    id: "{theme_id}",
    name: "{name}",
    author: "Public Domain CC0",
    description: "Lossless Deflate-compressed SVG vector master sheet.",
    version: 1
}})

deck_blob = """{b64_svg}"""

sol.set_deck(deck_blob, {{
    format: "svgz",
    card_w: 360,
    card_h: 504,
    theme_id: "{theme_id}"
}})
'''
    with open(out_eats_path, "w", encoding="utf-8") as f:
        f.write(content)

    print(f"[SolEngine] Packaged {svg_path} ({len(svg_data):,} bytes) -> {out_eats_path} ({os.path.getsize(out_eats_path):,} bytes)")
    return True

def package_bitmap_deck(img_path, out_eats_path, name="Custom Bitmap", theme_id="custom", format_override=None):
    if not os.path.exists(img_path):
        print(f"Error: {img_path} not found.")
        return False
    with open(img_path, "rb") as f:
        img_data = f.read()

    b64_img = base64.b64encode(img_data).decode("ascii")

    fmt = format_override
    if not fmt:
        ext = os.path.splitext(img_path)[1].lower()
        if ext in (".jpg", ".jpeg"):
            fmt = "jpg"
        else:
            fmt = "png"

    desc_type = "JPG" if fmt == "jpg" else "PNG"
    content = f'''# SolEngine Deck Recipe — {name}
# Connects Hardwood Solitaire 3.0 tactile style with high-res raster asset embedding.

sol.set_theme({{
    id: "{theme_id}",
    name: "{name}",
    author: "SolEngine / AI",
    description: "High-resolution 3240x3024 9x6 {desc_type} master sheet embedded in Eatscript.",
    version: 1
}})

deck_blob = """{b64_img}"""

sol.set_deck(deck_blob, {{
    format: "{fmt}",
    card_w: 360,
    card_h: 504,
    theme_id: "{theme_id}"
}})
'''
    with open(out_eats_path, "w", encoding="utf-8") as f:
        f.write(content)

    print(f"[SolEngine] Packaged {img_path} ({len(img_data):,} bytes) -> {out_eats_path} ({os.path.getsize(out_eats_path):,} bytes)")
    return True

def package_png_deck(png_path, out_eats_path, name="Steampunk AI", theme_id="steampunk"):
    return package_bitmap_deck(png_path, out_eats_path, name=name, theme_id=theme_id, format_override="png")

def package_jpg_deck(jpg_path, out_eats_path, name="Baroque Arches", theme_id="baroque_noir"):
    return package_bitmap_deck(jpg_path, out_eats_path, name=name, theme_id=theme_id, format_override="jpg")

if __name__ == "__main__":
    package_svg_deck("decks/classic_svg.svg", "decks/classic.eats", name="Classic Victorian", theme_id="classic")
    package_png_deck("decks/steampunk.png", "decks/steampunk.eats", name="Steampunk AI", theme_id="steampunk")

