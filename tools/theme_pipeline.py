#!/usr/bin/env python3
"""
SolEngine Theme Pipeline Tool (tools/theme_pipeline.py)
Author: Google Deepmind / SolEngine
License: MIT / SolEngine

Features:
- Channels packing: Merges Ambient Occlusion (R), Roughness (G), and Displacement/Metallic (B) into a single ORM texture.
- Single-file container: Packages Eatscript text recipe header + binary EBKG blob containing Color, Normal, and ORM textures.
- Commands:
    pack        - Pack a single AmbientCG texture set into a .eats theme file
    batch-pack  - Pack all AmbientCG materials in textures/ into themes/ directory
    unpack      - Unpack a .eats theme back into loose image files and recipe
    info        - Display detailed Eatscript header and binary chunk metadata
"""

import os
import sys
import argparse
import struct
import io
import re
from pathlib import Path
from PIL import Image

# Magic signature for Eatscript Background Blob
EBKG_MAGIC = b"EBKG"
EBKG_VERSION = 1
EBKG_HEADER_STRUCT = "<4s7I"  # magic[4], version, colorOff, colorSize, normOff, normSize, ormOff, ormSize
EBKG_HEADER_SIZE = struct.calcsize(EBKG_HEADER_STRUCT)

THEME_PRESETS = {
    "Wood049": {
        "slug": "rosewood_parlor",
        "name": "Rosewood Parlor",
        "uv_scale": 2.0,
        "normal_strength": 1.35,
        "specular_intensity": 0.65,
        "ambient_level": 0.22,
        "diffuse_level": 0.88,
        "tint_r": 1.00,
        "tint_g": 0.95,
        "tint_b": 0.92,
        "description": "Deep exotic rosewood grain with satin polyurethane sheen and growth ring relief."
    },
    "WoodFloor051": {
        "slug": "tavern_heartwood",
        "name": "Tavern Heartwood",
        "uv_scale": 2.2,
        "normal_strength": 1.45,
        "specular_intensity": 0.50,
        "ambient_level": 0.20,
        "diffuse_level": 0.90,
        "tint_r": 1.00,
        "tint_g": 0.98,
        "tint_b": 0.95,
        "description": "Warm chiseled parquet heartwood planks with oiled oak sheen."
    },
    "Fabric037": {
        "slug": "emerald_lawn",
        "name": "Emerald Lawn",
        "uv_scale": 2.8,
        "normal_strength": 1.25,
        "specular_intensity": 0.18,
        "ambient_level": 0.26,
        "diffuse_level": 0.85,
        "tint_r": 0.50,
        "tint_g": 1.15,
        "tint_b": 0.65,
        "description": "Rich woven emerald baize felt with soft micro-fiber scatter."
    },
    "Fabric063": {
        "slug": "hopsack_tweed",
        "name": "Hopsack Tweed",
        "uv_scale": 2.5,
        "normal_strength": 1.50,
        "specular_intensity": 0.15,
        "ambient_level": 0.24,
        "diffuse_level": 0.85,
        "tint_r": 1.00,
        "tint_g": 1.00,
        "tint_b": 1.00,
        "description": "Tactile 2x2 multi-ply wool yarn basketweave with subtle neps."
    },
    "Fabric077": {
        "slug": "denim_chambray",
        "name": "Denim Chambray",
        "uv_scale": 3.0,
        "normal_strength": 1.25,
        "specular_intensity": 0.18,
        "ambient_level": 0.22,
        "diffuse_level": 0.85,
        "tint_r": 0.65,
        "tint_g": 0.80,
        "tint_b": 1.15,
        "description": "Indigo twill chambray denim with pronounced weft ribbing."
    },
    "Fabric079": {
        "slug": "striped_jersey",
        "name": "Striped Jersey",
        "uv_scale": 2.6,
        "normal_strength": 1.40,
        "specular_intensity": 0.20,
        "ambient_level": 0.24,
        "diffuse_level": 0.85,
        "tint_r": 1.00,
        "tint_g": 1.00,
        "tint_b": 1.00,
        "description": "Sporting ribbed knit jersey weave with elastic horizontal grain."
    },
    "Tiles052": {
        "slug": "reflective_tile",
        "name": "Reflective Tile",
        "uv_scale": 2.4,
        "normal_strength": 1.55,
        "specular_intensity": 0.90,
        "ambient_level": 0.20,
        "diffuse_level": 0.88,
        "tint_r": 1.00,
        "tint_g": 1.00,
        "tint_b": 1.00,
        "description": "Glazed ceramic subway tile with glossy specular hotspot and grout lines."
    },
    "Rocks011": {
        "slug": "carved_stone",
        "name": "Carved Stone",
        "uv_scale": 1.8,
        "normal_strength": 1.65,
        "specular_intensity": 0.22,
        "ambient_level": 0.20,
        "diffuse_level": 0.90,
        "tint_r": 0.95,
        "tint_g": 0.95,
        "tint_b": 0.98,
        "description": "Weathered mineral pavers with chiseled crevices and deep AO grooves."
    },
    "Onyx013": {
        "slug": "sunken_sanctum",
        "name": "Sunken Sanctum",
        "uv_scale": 1.7,
        "normal_strength": 1.15,
        "specular_intensity": 0.85,
        "ambient_level": 0.16,
        "diffuse_level": 0.84,
        "tint_r": 0.75,
        "tint_g": 0.88,
        "tint_b": 1.05,
        "description": "Polished obsidian slate with vitreous specular sheen and subterranean cyan tint."
    },
    "Ground091": {
        "slug": "perilous_shores",
        "name": "Perilous Shores",
        "uv_scale": 2.0,
        "normal_strength": 1.50,
        "specular_intensity": 0.20,
        "ambient_level": 0.22,
        "diffuse_level": 0.88,
        "tint_r": 1.00,
        "tint_g": 0.98,
        "tint_b": 0.92,
        "description": "Weathered coastal earth and packed sandy loam with tactile aggregate texture."
    }
}

def find_file(directory: Path, pattern: str) -> Path | None:
    matches = list(directory.glob(pattern))
    return matches[0] if matches else None

def pack_orm_image(roughness_path: Path, ao_path: Path | None, disp_path: Path | None, quality: int = 92) -> bytes:
    """
    Channel-packs:
    R = Ambient Occlusion (AO, 1.0/255 if not present)
    G = Roughness
    B = Displacement / Metallic (0.0/0 if not present)
    """
    with Image.open(roughness_path) as r_img:
        rough_l = r_img.convert("L")
        w, h = rough_l.size

    if ao_path and ao_path.exists():
        with Image.open(ao_path) as a_img:
            ao_l = a_img.convert("L").resize((w, h), Image.Resampling.BILINEAR)
    else:
        # Default full ambient illumination (white = 255)
        ao_l = Image.new("L", (w, h), 255)

    if disp_path and disp_path.exists():
        with Image.open(disp_path) as d_img:
            disp_l = d_img.convert("L").resize((w, h), Image.Resampling.BILINEAR)
    else:
        disp_l = Image.new("L", (w, h), 0)

    orm_img = Image.merge("RGB", (ao_l, rough_l, disp_l))
    buf = io.BytesIO()
    orm_img.save(buf, format="JPEG", quality=quality, optimize=True)
    return buf.getvalue()

def pack_theme(
    material_id: str,
    textures_dir: Path,
    output_path: Path,
    name: str | None = None,
    uv_scale: float = 2.0,
    normal_strength: float = 1.35,
    specular_intensity: float = 0.65,
    ambient_level: float = 0.22,
    diffuse_level: float = 0.88,
    tint_r: float = 1.0,
    tint_g: float = 1.0,
    tint_b: float = 1.0,
    description: str = "",
    author: str = "AmbientCG / CC0",
    quality: int = 92
) -> bool:
    """
    Packs an AmbientCG material into a single .eats theme file.
    """
    textures_dir = Path(textures_dir)
    output_path = Path(output_path)

    # Locate required textures
    color_file = find_file(textures_dir, f"{material_id}_*Color.jpg") or find_file(textures_dir, f"{material_id}_*Color.png")
    normal_file = find_file(textures_dir, f"{material_id}_*NormalGL.jpg") or find_file(textures_dir, f"{material_id}_*NormalDX.jpg")
    roughness_file = find_file(textures_dir, f"{material_id}_*Roughness.jpg") or find_file(textures_dir, f"{material_id}_*Roughness.png")
    ao_file = find_file(textures_dir, f"{material_id}_*AmbientOcclusion.jpg") or find_file(textures_dir, f"{material_id}_*AmbientOcclusion.png")
    disp_file = find_file(textures_dir, f"{material_id}_*Displacement.jpg") or find_file(textures_dir, f"{material_id}_*Displacement.png")

    if not color_file:
        print(f"[Error] Color texture for '{material_id}' not found in {textures_dir}")
        return False
    if not normal_file:
        print(f"[Error] Normal texture for '{material_id}' not found in {textures_dir}")
        return False
    if not roughness_file:
        print(f"[Error] Roughness texture for '{material_id}' not found in {textures_dir}")
        return False

    display_name = name or THEME_PRESETS.get(material_id, {}).get("name", material_id)
    desc = description or THEME_PRESETS.get(material_id, {}).get("description", f"AmbientCG {material_id} PBR Material")

    # Read color bytes
    with open(color_file, "rb") as f:
        color_bytes = f.read()

    # Read normal bytes
    with open(normal_file, "rb") as f:
        normal_bytes = f.read()

    # Generate packed ORM image
    print(f"  -> Channel packing ORM (AO={ao_file is not None}, Rough={roughness_file.name}, Disp={disp_file is not None})...")
    orm_bytes = pack_orm_image(roughness_file, ao_file, disp_file, quality=quality)

    # Calculate blob offsets
    color_offset = EBKG_HEADER_SIZE
    color_size = len(color_bytes)
    normal_offset = color_offset + color_size
    normal_size = len(normal_bytes)
    orm_offset = normal_offset + normal_size
    orm_size = len(orm_bytes)

    blob_header = struct.pack(
        EBKG_HEADER_STRUCT,
        EBKG_MAGIC,
        EBKG_VERSION,
        color_offset,
        color_size,
        normal_offset,
        normal_size,
        orm_offset,
        orm_size
    )

    # Construct Eatscript Header
    recipe_header = f"""# SolEngine Background Theme Package — {display_name}
# Channel-Packed PBR Surface with Tangent Normal Relief and Specular Sheen

sol.set_theme({{
    id: "{material_id}",
    name: "{display_name}",
    author: "{author}",
    description: "{desc}",
    version: 1
}})

sol.set_background(blob = "", {{
    format: "ebkg",
    uv_scale: {uv_scale:.2f},
    normal_strength: {normal_strength:.2f},
    specular_intensity: {specular_intensity:.2f},
    ambient_level: {ambient_level:.2f},
    diffuse_level: {diffuse_level:.2f},
    tint: [{tint_r:.2f}, {tint_g:.2f}, {tint_b:.2f}]
}})

__BLOB__
"""
    recipe_bytes = recipe_header.encode("utf-8")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(recipe_bytes)
        f.write(blob_header)
        f.write(color_bytes)
        f.write(normal_bytes)
        f.write(orm_bytes)

    total_size = output_path.stat().st_size
    orig_sum = color_size + normal_size + (ao_file.stat().st_size if ao_file else 0) + roughness_file.stat().st_size + (disp_file.stat().st_size if disp_file else 0)

    print(f"[SolEngine] Packaged theme -> {output_path}")
    print(f"  Total Package Size : {total_size:,} bytes ({total_size / 1024 / 1024:.2f} MB)")
    print(f"  Original Loose Sum : {orig_sum:,} bytes ({orig_sum / 1024 / 1024:.2f} MB)")
    print(f"  Channel-Pack Saving: {(1.0 - (total_size / orig_sum)) * 100.0:.1f}% reduction\n")
    return True

def cmd_pack(args):
    preset = THEME_PRESETS.get(args.material, {})
    uv_scale = args.uv_scale if args.uv_scale is not None else preset.get("uv_scale", 2.0)
    normal_strength = args.normal_strength if args.normal_strength is not None else preset.get("normal_strength", 1.35)
    specular_intensity = args.specular_intensity if args.specular_intensity is not None else preset.get("specular_intensity", 0.65)
    ambient_level = args.ambient_level if args.ambient_level is not None else preset.get("ambient_level", 0.22)
    diffuse_level = args.diffuse_level if args.diffuse_level is not None else preset.get("diffuse_level", 0.88)
    tint_r = args.tint_r if args.tint_r is not None else preset.get("tint_r", 1.0)
    tint_g = args.tint_g if args.tint_g is not None else preset.get("tint_g", 1.0)
    tint_b = args.tint_b if args.tint_b is not None else preset.get("tint_b", 1.0)
    name = args.name or preset.get("name", args.material)

    out_file = args.output
    if not out_file:
        slug = preset.get("slug", args.material.lower())
        out_file = Path("themes") / f"{slug}.eats"

    pack_theme(
        material_id=args.material,
        textures_dir=Path(args.textures_dir),
        output_path=Path(out_file),
        name=name,
        uv_scale=uv_scale,
        normal_strength=normal_strength,
        specular_intensity=specular_intensity,
        ambient_level=ambient_level,
        diffuse_level=diffuse_level,
        tint_r=tint_r,
        tint_g=tint_g,
        tint_b=tint_b,
        description=args.description or preset.get("description", ""),
        author=args.author,
        quality=args.quality
    )

def cmd_batch_pack(args):
    tex_dir = Path(args.textures_dir)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    if not tex_dir.exists():
        print(f"[Error] Textures directory '{tex_dir}' does not exist.")
        return

    # Find all unique material prefixes
    materials = set()
    for f in tex_dir.iterdir():
        m = re.match(r"^([A-Za-z0-9]+)_1K-JPG_Color\.", f.name)
        if m:
            materials.add(m.group(1))

    print(f"[SolEngine] Found {len(materials)} materials in {tex_dir}. Packing into {out_dir}...")

    success = 0
    for mat in sorted(materials):
        preset = THEME_PRESETS.get(mat, {})
        slug = preset.get("slug", mat.lower())
        out_path = out_dir / f"{slug}.eats"

        print(f"\nProcessing [{mat}] -> {out_path.name}...")
        ok = pack_theme(
            material_id=mat,
            textures_dir=tex_dir,
            output_path=out_path,
            name=preset.get("name", mat),
            uv_scale=preset.get("uv_scale", 2.0),
            normal_strength=preset.get("normal_strength", 1.35),
            specular_intensity=preset.get("specular_intensity", 0.65),
            ambient_level=preset.get("ambient_level", 0.22),
            diffuse_level=preset.get("diffuse_level", 0.88),
            tint_r=preset.get("tint_r", 1.0),
            tint_g=preset.get("tint_g", 1.0),
            tint_b=preset.get("tint_b", 1.0),
            description=preset.get("description", ""),
            quality=args.quality
        )
        if ok:
            success += 1

    print(f"\n[SolEngine] Batch packing complete: {success}/{len(materials)} themes packaged into '{out_dir}'.")

def cmd_info(args):
    theme_path = Path(args.file)
    if not theme_path.exists():
        print(f"[Error] Theme file '{theme_path}' does not exist.")
        return

    with open(theme_path, "rb") as f:
        data = f.read()

    marker = b"__BLOB__\n"
    marker_pos = data.find(marker)
    if marker_pos == -1:
        marker = b"__BLOB__\r\n"
        marker_pos = data.find(marker)

    if marker_pos == -1:
        text = data.decode("utf-8", errors="replace")
        if "proctex.canvas" in text or "background.create" in text or theme_path.suffix == ".eat":
            print("=" * 60)
            print(f"Eatscript Procedural Theme: {theme_path.name} ({len(data):,} bytes)")
            print("Mode: Pure CPU Procedural Synthesis (Zero Heavy Bitmaps)")
            print("=" * 60)
            print(text.strip())
            print("=" * 60)
            return
        print("[Error] No __BLOB__ marker found in .eats file.")
        return

    recipe_part = data[:marker_pos].decode("utf-8", errors="replace")
    blob_start = marker_pos + len(marker)
    blob_bytes = data[blob_start:]

    print("=" * 60)
    print(f"Eatscript Background Theme: {theme_path.name} ({len(data):,} bytes)")
    print("=" * 60)
    print(recipe_part.strip())
    print("-" * 60)

    if len(blob_bytes) < EBKG_HEADER_SIZE:
        print(f"[Error] Blob size ({len(blob_bytes)} bytes) is smaller than EBKG header ({EBKG_HEADER_SIZE} bytes).")
        return

    magic, ver, c_off, c_sz, n_off, n_sz, o_off, o_sz = struct.unpack_from(EBKG_HEADER_STRUCT, blob_bytes, 0)
    print(f"Blob Magic      : {magic.decode('ascii', errors='replace')}")
    print(f"Blob Version    : {ver}")

    def inspect_img(name, offset, size):
        img_data = blob_bytes[offset:offset+size]
        try:
            im = Image.open(io.BytesIO(img_data))
            w, h = im.size
            fmt = im.format
            mode = im.mode
            print(f"  {name:<14}: offset {offset:<8} size {size:<8,} bytes ({fmt}, {w}x{h}, {mode})")
        except Exception as e:
            print(f"  {name:<14}: offset {offset:<8} size {size:<8,} bytes (decode failed: {e})")

    inspect_img("Color", c_off, c_sz)
    inspect_img("Normal", n_off, n_sz)
    inspect_img("ORM (Packed)", o_off, o_sz)
    print("=" * 60)

def cmd_pack_procedural(args):
    eat_path = Path(args.script)
    if not eat_path.exists():
        print(f"[Error] Procedural script '{eat_path}' does not exist.")
        return

    with open(eat_path, "r", encoding="utf-8") as f:
        script_code = f.read()

    out_path = Path(args.output) if args.output else eat_path.with_suffix(".eats")
    name = args.name or eat_path.stem.replace("_", " ").title()

    recipe_header = f"""# SolEngine Procedural Background Theme Package
# Synthesized at runtime via Eatscript proctex API (Zero heavy PNG textures)

sol.set_theme({{
    id: "{eat_path.stem}",
    name: "{name}",
    author: "{args.author}",
    description: "{args.description or 'Dynamic procedural PBR surface'}",
    version: 1,
    procedural: true
}})

# --- Procedural Synthesis Script ---
{script_code}
"""
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(recipe_header)

    print(f"[SolEngine] Successfully packaged procedural theme: {out_path} ({len(recipe_header):,} bytes)")

def cmd_unpack(args):
    theme_path = Path(args.file)
    out_dir = Path(args.out_dir) if args.out_dir else theme_path.with_suffix("")
    out_dir.mkdir(parents=True, exist_ok=True)

    with open(theme_path, "rb") as f:
        data = f.read()

    marker = b"__BLOB__\n"
    marker_pos = data.find(marker)
    if marker_pos == -1:
        marker = b"__BLOB__\r\n"
        marker_pos = data.find(marker)

    if marker_pos == -1:
        print("[Error] No __BLOB__ marker found.")
        return

    recipe_part = data[:marker_pos].decode("utf-8", errors="replace")
    with open(out_dir / "recipe.eats", "w", encoding="utf-8") as f:
        f.write(recipe_part)

    blob_start = marker_pos + len(marker)
    blob_bytes = data[blob_start:]
    magic, ver, c_off, c_sz, n_off, n_sz, o_off, o_sz = struct.unpack_from(EBKG_HEADER_STRUCT, blob_bytes, 0)

    with open(out_dir / "Color.jpg", "wb") as f:
        f.write(blob_bytes[c_off:c_off+c_sz])
    with open(out_dir / "Normal.jpg", "wb") as f:
        f.write(blob_bytes[n_off:n_off+n_sz])
    with open(out_dir / "ORM.jpg", "wb") as f:
        f.write(blob_bytes[o_off:o_off+o_sz])

    print(f"[SolEngine] Successfully unpacked '{theme_path.name}' into '{out_dir}/'.")

def main():
    parser = argparse.ArgumentParser(description="SolEngine Background Theme Pipeline")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # Pack command
    pack_p = subparsers.add_parser("pack", help="Pack an AmbientCG material into a .eats theme")
    pack_p.add_argument("-m", "--material", required=True, help="Material ID (e.g. Wood049)")
    pack_p.add_argument("-t", "--textures-dir", default="textures", help="Directory with loose textures")
    pack_p.add_argument("-o", "--output", help="Output .eats path")
    pack_p.add_argument("--name", help="Display name for the theme")
    pack_p.add_argument("--uv-scale", type=float, help="UV tiling scale")
    pack_p.add_argument("--normal-strength", type=float, help="Normal map relief strength")
    pack_p.add_argument("--specular-intensity", type=float, help="Specular reflection intensity")
    pack_p.add_argument("--ambient-level", type=float, help="Ambient light floor")
    pack_p.add_argument("--diffuse-level", type=float, help="Diffuse light scaling")
    pack_p.add_argument("--tint-r", type=float, help="Red tint multiplier")
    pack_p.add_argument("--tint-g", type=float, help="Green tint multiplier")
    pack_p.add_argument("--tint-b", type=float, help="Blue tint multiplier")
    pack_p.add_argument("--description", help="Theme description")
    pack_p.add_argument("--author", default="AmbientCG / CC0", help="Author attribution")
    pack_p.add_argument("-q", "--quality", type=int, default=92, help="JPEG quality for ORM")

    # Batch pack command
    batch_p = subparsers.add_parser("batch-pack", help="Batch pack all materials in textures/ into themes/")
    batch_p.add_argument("-t", "--textures-dir", default="textures", help="Directory with loose textures")
    batch_p.add_argument("-o", "--out-dir", default="themes", help="Output themes directory")
    batch_p.add_argument("-q", "--quality", type=int, default=92, help="JPEG quality for ORM")

    # Info command
    info_p = subparsers.add_parser("info", help="Inspect .eats background theme header & payload")
    info_p.add_argument("file", help="Path to .eats theme file")

    # Pack procedural command
    proc_p = subparsers.add_parser("pack-procedural", help="Pack an .eat script into a lightweight .eats theme")
    proc_p.add_argument("-s", "--script", required=True, help="Path to .eat procedural texture script")
    proc_p.add_argument("-o", "--output", help="Output .eats path")
    proc_p.add_argument("--name", help="Display name for the theme")
    proc_p.add_argument("--author", default="SolEngine / Eatscript", help="Author attribution")
    proc_p.add_argument("--description", help="Theme description")

    # Unpack command
    unpack_p = subparsers.add_parser("unpack", help="Unpack .eats theme to loose images and recipe")
    unpack_p.add_argument("file", help="Path to .eats theme file")
    unpack_p.add_argument("-o", "--out-dir", help="Output directory")

    args = parser.parse_args()
    if args.command == "pack":
        cmd_pack(args)
    elif args.command == "batch-pack":
        cmd_batch_pack(args)
    elif args.command == "info":
        cmd_info(args)
    elif args.command == "pack-procedural":
        cmd_pack_procedural(args)
    elif args.command == "unpack":
        cmd_unpack(args)

if __name__ == "__main__":
    main()

