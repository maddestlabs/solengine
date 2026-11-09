#!/usr/bin/env python3
"""
SolEngine Complete Deck Asset & Pipeline Tool
==============================================
Provides a full end-to-end automated workflow:
1. 'export-base': Slices the CC0 master SVG and binary cache into 54 standalone
   individual card SVGs and PNGs with clear names and a manifest.
2. 'generate-prompts': Generates specialized Gemini image-to-image prompts for all
   54 cards for any custom theme (e.g. Victorian Steampunk, Eldritch, Cyberpunk).
3. 'generate-ai': Calls Google GenAI (gemini-2.5-flash-image / gemini-3-pro-image)
   via API to generate card illustrations, then vectorizes them with vtracer.
4. 'assemble': Takes standalone card vector SVGs and assembles them with
   mathematical precision into the master 9x6 grid SVG.
5. 'auto-deck': End-to-end automation: generates AI cards -> vectorizes -> assembles
   ready-to-play deck in decks/<theme>_svg.svg.
"""

import os
import sys
import json
import time
import struct
import argparse
import re
import concurrent.futures
from PIL import Image

try:
    import vtracer
    HAS_VTRACER = True
except ImportError:
    HAS_VTRACER = False

try:
    from google import genai
    from google.genai import types
    HAS_GENAI = True
except ImportError:
    HAS_GENAI = False

CARD_NAMES = [
    # Spades (0..12)
    "spades_01_ace", "spades_02", "spades_03", "spades_04", "spades_05",
    "spades_06", "spades_07", "spades_08", "spades_09", "spades_10",
    "spades_11_jack", "spades_12_queen", "spades_13_king",
    # Hearts (13..25)
    "hearts_01_ace", "hearts_02", "hearts_03", "hearts_04", "hearts_05",
    "hearts_06", "hearts_07", "hearts_08", "hearts_09", "hearts_10",
    "hearts_11_jack", "hearts_12_queen", "hearts_13_king",
    # Clubs (26..38)
    "clubs_01_ace", "clubs_02", "clubs_03", "clubs_04", "clubs_05",
    "clubs_06", "clubs_07", "clubs_08", "clubs_09", "clubs_10",
    "clubs_11_jack", "clubs_12_queen", "clubs_13_king",
    # Diamonds (39..51)
    "diamonds_01_ace", "diamonds_02", "diamonds_03", "diamonds_04", "diamonds_05",
    "diamonds_06", "diamonds_07", "diamonds_08", "diamonds_09", "diamonds_10",
    "diamonds_11_jack", "diamonds_12_queen", "diamonds_13_king",
    # Jokers (52, 53)
    "joker_black", "joker_red"
]

COURT_CARDS = [
    "spades_11_jack", "spades_12_queen", "spades_13_king",
    "hearts_11_jack", "hearts_12_queen", "hearts_13_king",
    "clubs_11_jack", "clubs_12_queen", "clubs_13_king",
    "diamonds_11_jack", "diamonds_12_queen", "diamonds_13_king"
]

def export_base(args):
    svg_path = args.svg or "decks/classic_svg.svg"
    bin_path = args.bin or "decks/classic_svg.bin"
    out_dir = args.out or "assets/deck_cards/classic"
    os.makedirs(out_dir, exist_ok=True)
    png_dir = os.path.join(out_dir, "png")
    svg_dir = os.path.join(out_dir, "svg")
    os.makedirs(png_dir, exist_ok=True)
    os.makedirs(svg_dir, exist_ok=True)

    print(f"[SolEngine] Slicing base deck into standalone card assets in {out_dir}...")

    # 1. Export high-res PNGs from binary cache if available
    if os.path.exists(bin_path):
        with open(bin_path, "rb") as fp:
            header = fp.read(16)
            magic, w, h, count = struct.unpack("<IIII", header)
            if magic == 0x4344434B:
                print(f"[SolEngine] Found binary cache: {w}x{h}, {count} textures.")
                byte_count = w * h * 4
                for i in range(count):
                    raw_bytes = fp.read(byte_count)
                    if len(raw_bytes) != byte_count:
                        break
                    img = Image.frombytes("RGBA", (w, h), raw_bytes)
                    name = CARD_NAMES[i] if i < len(CARD_NAMES) else f"card_{i:02d}"
                    png_file = os.path.join(png_dir, f"{name}.png")
                    img.save(png_file, "PNG")
                print(f"[SolEngine] Exported {count} reference PNGs to {png_dir}")

    # 2. Extract standalone SVGs from master SVG
    if os.path.exists(svg_path):
        with open(svg_path, "r", encoding="utf-8") as f:
            content = f.read()

        pattern = r'<g transform="matrix\(0\.24,0,0,0\.24,([^,]+),([^)]+)\)">(.*?)(?=<g transform="matrix\(0\.24|\Z)'
        matches = re.findall(pattern, content, re.DOTALL)
        print(f"[SolEngine] Found {len(matches)} card vector cells in master SVG.")

        manifest = []
        for idx, (x_str, y_str, card_body) in enumerate(matches):
            name = CARD_NAMES[idx] if idx < len(CARD_NAMES) else f"card_{idx:02d}"
            col = int(idx % 9)
            row = int(idx // 9)

            clean_body = card_body.strip()
            if clean_body.endswith("</svg>"):
                clean_body = clean_body[:-6].strip()

            standalone_svg = (
                f'<?xml version="1.0" encoding="UTF-8" standalone="no"?>\n'
                f'<svg width="750" height="1050" viewBox="0 0 750 1050" version="1.1" '
                f'xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">\n'
                f'    <g transform="matrix(0.24,0,0,0.24,375,525)">\n'
                f'        <!-- Standalone Card: {name} (Cell {idx}) -->\n'
                f'        {clean_body}\n'
                f'</svg>\n'
            )

            svg_file = os.path.join(svg_dir, f"{name}.svg")
            with open(svg_file, "w", encoding="utf-8") as sf:
                sf.write(standalone_svg)

            manifest.append({
                "index": idx,
                "name": name,
                "grid_col": col,
                "grid_row": row,
                "svg_file": f"svg/{name}.svg",
                "png_file": f"png/{name}.png"
            })

        manifest_path = os.path.join(out_dir, "manifest.json")
        with open(manifest_path, "w", encoding="utf-8") as mf:
            json.dump(manifest, mf, indent=2)

        print(f"[SolEngine] Exported {len(manifest)} standalone SVGs and manifest to {out_dir}")

SUIT_SYMBOLS = {
    "spades": "♠ spade",
    "hearts": "♥ heart",
    "clubs": "♣ club",
    "diamonds": "♦ diamond"
}

RANK_LABELS = {
    "01_ace": "A", "02": "2", "03": "3", "04": "4", "05": "5",
    "06": "6", "07": "7", "08": "8", "09": "9", "10": "10",
    "11_jack": "J", "12_queen": "Q", "13_king": "K"
}

def get_card_prompt(name, theme):
    parts = name.split("_")
    suit = parts[0]
    rank_key = "_".join(parts[1:]) if len(parts) > 1 else parts[0]

    rank_label = RANK_LABELS.get(rank_key, rank_key.upper())
    suit_sym = SUIT_SYMBOLS.get(suit, suit)
    theme_lower = theme.lower()

    indices_instruction = ""
    if suit in SUIT_SYMBOLS:
        indices_instruction = (
            f"Corner indices: Bold, prominent rank '{rank_label}' and {suit_sym} pip in the top-left corner, "
            f"and matching 180-degree inverted rank '{rank_label}' and {suit_sym} pip in the bottom-right corner. "
        )

    if "anime" in theme_lower or "manga" in theme_lower:
        # Specialized vibrant anime/manga prompt builder
        if "king" in name:
            subject = (
                f"Charismatic anime King and emperor of {suit.capitalize()} with sharp expressive eyes, stylish flowing hair, "
                f"regal commander armor with cape, holding a glowing anime broadsword or royal scepter with {suit_sym} crest, "
                f"majestic anime aura"
            )
        elif "queen" in name:
            subject = (
                f"Gorgeous anime Queen and sorceress of {suit.capitalize()} with large sparkling anime eyes, long flowing twin-tail or braided hair, "
                f"ornate magical princess gown with {suit_sym} accents and glowing ribbons, celestial sparkle aura"
            )
        elif "jack" in name:
            subject = (
                f"Dashing youthful anime hero knight or rogue of {suit.capitalize()} with spiky anime hair, confident smirk, "
                f"stylish anime fantasy adventurer jacket and gauntlets, wielding a sleek rapier or kunai dagger with {suit_sym} charm"
            )
        elif "ace" in name:
            subject = (
                f"Monumental glowing anime summoning circle and monumental {suit.capitalize()} crest, luminous geometric magic runes, "
                f"crystalline {suit_sym} emblem, symmetrical celestial anime energy flourish"
            )
        elif "joker" in name:
            subject = (
                f"Mischievous anime trickster / kitsune jester with fox ears or stylized cat mask, colorful twin-tone hair, playful grin, "
                f"juggling floating glowing poker cards and elemental spirit wisps"
            )
        else:
            subject = (
                f"Standard playing card layout with {rank_label} of {suit.capitalize()} featuring crisp stylized anime geometric pips ({suit_sym}), "
                f"sleek modern anime energy border accents, clean vibrant card aesthetic"
            )

        if suit in ("hearts", "diamonds"):
            color_rule = (
                f"VIVID RED SUIT COLOR REQUIREMENT (CRITICAL FOR SOLITAIRE PLAYABILITY): "
                f"This is a RED suit ({suit.capitalize()}). The corner rank '{rank_label}', corner {suit_sym} pip, "
                f"and all primary suit symbols MUST be bright, vivid radiant ruby crimson (#E60033 / #FF1744) with crisp white or gold highlights. "
                f"Do NOT make the symbols black or dark monochrome. The red must be unmistakable, bold, and high-contrast. "
            )
        elif suit in ("spades", "clubs"):
            color_rule = (
                f"BLACK SUIT COLOR REQUIREMENT: This is a BLACK suit ({suit.capitalize()}). "
                f"The corner rank '{rank_label}', corner {suit_sym} pip, and primary suit symbols MUST be deep solid obsidian midnight-black (#111111 / #0F172A) "
                f"with sleek silver or sapphire accents. "
            )
        else:
            color_rule = "Vibrant multi-color anime palette with distinct contrast. "

        art_style = (
            f"Vibrant high-quality modern Japanese anime and manga illustration style, crisp clean cel-shaded outlines, "
            f"bold solid color fills, smooth cel shading, dynamic anime lighting, clean ivory cardstock background, "
            f"no blur, no photographic realism, no woodblock texture, edge-to-edge card layout."
        )

    elif "cyberpunk" in theme_lower or "synthwave" in theme_lower or "neon" in theme_lower:
        # Specialized vibrant Cyberpunk / Synthwave prompt builder
        if "king" in name:
            subject = (
                f"Futuristic Cyberpunk corporate megacorp executive / tech syndicate warlord King of {suit.capitalize()} "
                f"with glowing neural cyberware implants, chrome cybernetic augments, high-collar LED trench coat, "
                f"holding a holographic glowing cyber-blade or data pad with {suit_sym} insignia, neon-lit high-tech aesthetic"
            )
        elif "queen" in name:
            subject = (
                f"High-tech Cyberpunk netrunner queen / cyber-enchantress of {suit.capitalize()} with neon-streaked hair, "
                f"glowing ocular visor implants, sleek carbon-fiber armor dress with luminous fiber-optic circuit traces "
                f"and floating holographic {suit_sym} interfaces"
            )
        elif "jack" in name:
            subject = (
                f"Streetwise Cyberpunk mercenary / street samurai rogue of {suit.capitalize()} with cybernetic arm, "
                f"holographic augmented reality HUD eyewear, neon jacket, wielding twin thermal monoblades or smart weapon with {suit_sym} decal"
            )
        elif "ace" in name:
            subject = (
                f"Monumental glowing holographic {suit.capitalize()} ace crest, neon vector wireframe grid, glowing laser circuitry, "
                f"crystalline cyber {suit_sym} core, high-tech isometric cybernetic flourish"
            )
        elif "joker" in name:
            subject = (
                f"Wild Cyberpunk renegade glitch hacker / neon court jester in techwear with AR hologram joker mask, "
                f"holographic glowing tarot cards, electrical arc sparks, and neon chromatic glitch accents"
            )
        else:
            subject = (
                f"Standard playing card layout with {rank_label} of {suit.capitalize()} featuring glowing high-tech neon laser pips ({suit_sym}), "
                f"sleek tech-bracket cyber border accents, clean dark obsidian card aesthetic"
            )

        if suit in ("hearts", "diamonds"):
            color_rule = (
                f"VIVID RED SUIT COLOR REQUIREMENT (CRITICAL FOR SOLITAIRE PLAYABILITY): "
                f"This is a RED suit ({suit.capitalize()}). The corner rank '{rank_label}', corner {suit_sym} pip, "
                f"and all primary suit symbols MUST be ultra-vivid radiant laser neon magenta-red (#FF0055 / #FF007F) with glowing hot-pink highlights. "
                f"Do NOT make the symbols blue, cyan, or dark monochrome. The red must be unmistakable, bold, and high-contrast. "
            )
        elif suit in ("spades", "clubs"):
            color_rule = (
                f"ELECTRIC CYAN SUIT COLOR REQUIREMENT (HIGH-CONTRAST PLAYABILITY): "
                f"This is a BLACK suit ({suit.capitalize()}). The corner rank '{rank_label}', corner {suit_sym} pip, "
                f"and primary suit symbols MUST be brilliant electric cyber-cyan (#00F0FF / #00D2FF) with sleek chrome and neon-blue circuit traces "
                f"against deep obsidian dark cardstock (#0A0E17). "
            )
        else:
            color_rule = "Vibrant electric neon palette with high contrast against dark cardstock. "

        art_style = (
            f"Crisp graphic Cyberpunk vector illustration, bold clean outlines, vibrant solid neon laser colors, sleek cel-shading, "
            f"high-contrast lighting against dark obsidian tech cardstock, no blur, no realistic photo noise, edge-to-edge card layout."
        )

    elif ("noir" in theme_lower or "dark" in theme_lower or "gothic" in theme_lower or (("baroque" in theme_lower or "filigree" in theme_lower) and "alabaster" not in theme_lower and "ivory" not in theme_lower and "light" not in theme_lower)):
        # Specialized dark gothic baroque / ornate filigree relief prompt builder
        if "king" in name:
            subject = (
                f"Regal Baroque Monarch King of {suit.capitalize()} with sculpted antique silver crown, noble velvet and damask robes with elaborate silver embroidery, "
                f"holding an ornate baroque ceremonial scepter or sword with {suit_sym} heraldic crest, encased in a dimensional acanthus scrollwork frame"
            )
        elif "queen" in name:
            subject = (
                f"Majestic Baroque Queen and Empress of {suit.capitalize()} wearing an intricate chiseled silver tiara and baroque pearls, ornate lace ruff, "
                f"holding an engraved baroque {suit_sym} mirror or talisman, framed in layered 3D rococo floral filigree"
            )
        elif "jack" in name:
            subject = (
                f"Dashing young Baroque Jack knight of {suit.capitalize()} in embossed antique silver plate armor with baroque scroll engravings, "
                f"ornate collar, holding an engraved dueling rapier with {suit_sym} pommel, surrounded by baroque leaf flourishes"
            )
        elif "ace" in name:
            subject = (
                f"Monumental central ornate {suit.capitalize()} ace emblem with intricate 3D interior floral filigree and leaf scrollwork, "
                f"encased in an opulent baroque acanthus cartouche and grand sculpted antique silver flourish frame"
            )
        elif "joker" in name:
            subject = (
                f"Mysterious Baroque masquerade jester in an ornate sculpted silver plague doctor or Venetian carnival mask, "
                f"holding a silver scepter with bells and juggling floating miniature ornate cards, surrounded by baroque scrollwork"
            )
        else:
            subject = (
                f"Standard playing card layout with {rank_label} of {suit.capitalize()} featuring sculpted 3D relief {suit_sym} pips "
                f"adorned with delicate interior filigree and ornate baroque leaf accents, surrounded by a symmetrical rococo acanthus border frame"
            )

        if suit in ("hearts", "diamonds"):
            color_rule = (
                f"VIVID CRIMSON RED SUIT COLOR REQUIREMENT (CRITICAL FOR SOLITAIRE PLAYABILITY): "
                f"This is a RED suit ({suit.capitalize()}). The corner rank '{rank_label}', corner {suit_sym} pip, "
                f"and primary suit symbols MUST be intensely prominent, radiant ruby crimson enamel (#FF0033 / #E60026) with a luminous light bloom / radiant neon halo effect glowing vividly against the dark slate background with polished silver and rose-gold bevels. "
                f"Do NOT make the symbols black or monochrome pewter. The red bloom must be unmistakable, bold, and high-contrast against the dark charcoal cardstock. "
            )
        elif suit in ("spades", "clubs"):
            color_rule = (
                f"BLACK SUIT COLOR REQUIREMENT: This is a BLACK suit ({suit.capitalize()}). "
                f"The corner rank '{rank_label}', corner {suit_sym} pip, and primary suit symbols MUST be deep solid obsidian black (#111111) "
                f"and chiseled antique silver / dark pewter (#A0A5AA / #333333) with high-contrast metallic highlights against the matte slate background. "
            )
        else:
            color_rule = "Ornate antique silver and dark pewter palette with dramatic chiaroscuro contrast against dark cardstock. "

        art_style = (
            f"Flat 2D digital playing card face graphic texture blitted directly edge-to-edge on a 5:7 canvas. "
            f"Archaic dark slate and charcoal obsidian cardstock with authentic subtle surface grain inconsistencies, organic paper tooth, and mottled dark patina of the original Baroque Noir background. "
            f"Bas-relief sculpted chiseled antique silver, oxidized pewter, and gunmetal filigree, dimensional acanthus scrollwork and rococo flourishes, "
            f"prominent radiant ruby crimson bloom on all red elements, dramatic chiaroscuro rim lighting, impeccable symmetry, crisp edges, no blur, standard 5:7 portrait playing card proportions. "
            f"STRICT PROHIBITIONS: NO external drop shadows, NO border margins, NO padding, NO tabletop surface, NO background surface, NO photograph of a card. The 4 edges of the image ARE the physical perimeter of the card."


        )

    elif "alabaster" in theme_lower or "ivory" in theme_lower or "porcelain" in theme_lower or "white" in theme_lower or "light" in theme_lower:
        # Specialized light ivory/alabaster cardstock with dark metal relief and glowing ruby bloom
        if "king" in name:
            subject = (
                f"Regal Baroque Monarch King of {suit.capitalize()} sculpted in high-relief dark oxidized pewter and blackened steel plate armor, "
                f"noble robes, holding an ornate ceremonial scepter or sword with {suit_sym} heraldic crest, framed in an acanthus scrollwork cartouche"
            )
        elif "queen" in name:
            subject = (
                f"Majestic Baroque Queen of {suit.capitalize()} sculpted in chiseled dark metal and antique silver, wearing an intricate tiara, lace ruff, "
                f"holding an engraved baroque {suit_sym} mirror or talisman, framed in layered 3D rococo floral filigree"
            )
        elif "jack" in name:
            subject = (
                f"Dashing young Baroque Jack knight of {suit.capitalize()} in embossed dark pewter plate armor with baroque scroll engravings, "
                f"holding an engraved dueling rapier with {suit_sym} pommel, surrounded by baroque leaf flourishes"
            )
        elif "ace" in name:
            subject = (
                f"Monumental central ornate {suit.capitalize()} ace emblem sculpted in 3D dark pewter and blackened steel with interior floral filigree, "
                f"encased in an opulent baroque acanthus cartouche and grand sculpted dark metal flourish frame"
            )
        elif "joker" in name:
            subject = (
                f"Mysterious Baroque masquerade jester in an ornate sculpted dark silver and blackened steel Venetian mask, "
                f"holding a silver scepter with bells and juggling miniature ornate cards, surrounded by baroque scrollwork"
            )
        else:
            subject = (
                f"Standard playing card layout with {rank_label} of {suit.capitalize()} featuring sculpted 3D relief {suit_sym} pips "
                f"adorned with delicate interior filigree and ornate baroque leaf accents, surrounded by a slender rococo acanthus border frame"
            )

        if suit in ("hearts", "diamonds"):
            color_rule = (
                f"VIVID GLOWING RUBY CRIMSON SUIT REQUIREMENT WITH BLOOM (SOLITAIRE READABILITY): "
                f"This is a RED suit ({suit.capitalize()}). All primary suit symbols, pips, and corner rank '{rank_label}' indices "
                f"MUST feature a radiant, luminous glowing ruby crimson enamel (#FF0033 / #E60026) with an ethereal light bloom / neon halo effect "
                f"set inside the dark sculpted metal. The glowing red must be intensely bright, unmistakably vibrant, and distinct from black suits. "
            )
        elif suit in ("spades", "clubs"):
            color_rule = (
                f"DARK SCULPTED METAL SUIT REQUIREMENT: This is a BLACK suit ({suit.capitalize()}). "
                f"The corner rank '{rank_label}', corner {suit_sym} pip, and primary suit symbols MUST be deep solid obsidian black (#111111) "
                f"and sculpted antique dark pewter / blackened damascene steel (#2A2D34) with sharp metallic highlights. "
            )
        else:
            color_rule = "Dark sculpted metal with selective radiant crimson glow against ivory cardstock. "

        art_style = (
            f"Flat 2D digital playing card face graphic texture blitted directly edge-to-edge on a 5:7 canvas. "
            f"Bas-relief sculpted dark metallic artwork on warm antique ivory / alabaster porcelain cardstock background (#FAF8F5), "
            f"crisp sharp silhouettes, radiant bloom glow on red elements, impeccable symmetry, no blur, standard 5:7 portrait playing card proportions. "
            f"STRICT PROHIBITIONS: NO external drop shadows, NO border margins, NO padding, NO tabletop surface, NO background surface, NO photograph of a card. The 4 edges of the image ARE the physical perimeter of the card."
        )

    else:
        # Default / Victorian Steampunk prompt builder
        if "king" in name:
            subject = f"Monarch King of {suit.capitalize()} wearing noble royal {theme} attire, holding a ceremonial scepter, clockwork gears or arcane details"
        elif "queen" in name:
            subject = f"Queen of {suit.capitalize()} in noble royal {theme} gown, diadems, gears or sigils, intricate filigree"
        elif "jack" in name:
            subject = f"Young Jack knight or navigator of {suit.capitalize()} with {theme} gadgets, goggles or weapons, and ornate collar"
        elif "ace" in name:
            subject = f"Monumental central ornate {suit.capitalize()} ace crest with {theme} mechanical details, flourish and symmetry"
        elif "joker" in name:
            subject = f"Court jester / mechanist in wild {theme} attire juggling gears, steam valves or arcane sparks"
        else:
            subject = f"Standard playing card layout with {rank_label} of {suit.capitalize()} with {suit_sym} pips and vintage engraved {theme} accents"

        color_rule = ""
        if suit in ("hearts", "diamonds"):
            color_rule = (
                f"VIVID RED SUIT COLOR REQUIREMENT (CRITICAL FOR SOLITAIRE PLAYABILITY): "
                f"This is a RED suit ({suit.capitalize()}). The corner rank '{rank_label}', corner {suit_sym} pip, "
                f"and all primary suit symbols MUST be bright, vivid crimson red (#c8102e / #d01828). "
                f"Do NOT make the symbols black, brown, brass, gold, or dark monochrome. "
                f"The red must be unmistakable, bold, and high-contrast against the antique parchment. "
            )
        elif suit in ("spades", "clubs"):
            color_rule = (
                f"BLACK SUIT COLOR REQUIREMENT: This is a BLACK suit ({suit.capitalize()}). "
                f"The corner rank '{rank_label}', corner {suit_sym} pip, and primary suit symbols MUST be deep solid charcoal-black (#111111). "
                f"Accented with antique copper and brass metallic details. "
            )

        art_style = (
            f"Crisp graphic woodblock engraving and copperplate linework, bold clean outlines, solid colors, "
            f"on clean antique parchment cardstock background, no external drop shadows, edge-to-edge card layout."
        )

    return (
        f"A authentic standard playing card: {rank_label} of {suit.capitalize()} in {theme} style. "
        f"Framing & Layout: Maintain standard playing card proportions (5:7 portrait aspect ratio) matching the reference card. "
        f"{indices_instruction}"
        f"{color_rule}"
        f"Central artwork: {subject}. "
        f"Art style: {art_style}"
    )

def generate_prompts(args):
    theme = args.theme or "Victorian Steampunk"
    out_file = args.out or f"assets/{theme.lower().replace(' ', '_')}_prompts.json"
    os.makedirs(os.path.dirname(out_file), exist_ok=True)

    print(f"[SolEngine] Generating Gemini prompt suite for '{theme}'...")
    prompts = []
    for idx, name in enumerate(CARD_NAMES):
        prompt_text = get_card_prompt(name, theme)
        prompt_entry = {
            "index": idx,
            "card_name": name,
            "source_reference": f"png/{name}.png",
            "gemini_prompt": prompt_text
        }
        prompts.append(prompt_entry)

    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(prompts, f, indent=2)

    print(f"[SolEngine] Generated {len(prompts)} prompts in {out_file}")

def _generate_single_card(card_name, client, model_name, theme, png_dir, svg_dir, args, idx, total_cards):
    png_out = os.path.join(png_dir, f"{card_name}.png")
    svg_out = os.path.join(svg_dir, f"{card_name}.svg")

    if os.path.exists(svg_out) and not getattr(args, "force", False):
        print(f"[{idx+1}/{total_cards}] {card_name} already exists ({os.path.getsize(svg_out)} bytes), skipping.")
        return True

    prompt_text = get_card_prompt(card_name, theme)

    # Load reference PNG if available for image-to-image conditioning
    ref_png = os.path.join("assets/deck_cards/classic/png", f"{card_name}.png")
    contents = []
    if os.path.exists(ref_png):
        try:
            ref_img = Image.open(ref_png)
            contents.append(ref_img)
        except Exception as e:
            print(f"  -> Note: could not load reference image {ref_png}: {e}")

    style_ref_path = getattr(args, "style_ref", None)
    if style_ref_path and os.path.exists(style_ref_path):
        try:
            style_img = Image.open(style_ref_path)
            contents.append(style_img)
            prompt_text = f"Style Reference: Match the artistic style, line weight, and {theme} ornamentation of the second image (while strictly adhering to the suit color requirement below). " + prompt_text
        except Exception as e:
            print(f"  -> Note: could not load style reference image {style_ref_path}: {e}")

    contents.append(prompt_text)

    print(f"[{idx+1}/{total_cards}] Requesting: {card_name} ...")
    max_retries = 3
    for attempt in range(max_retries):
        try:
            response = client.models.generate_content(
                model=model_name,
                contents=contents
            )

            img_bytes = None
            if response.candidates and response.candidates[0].content.parts:
                for part in response.candidates[0].content.parts:
                    if part.inline_data and part.inline_data.data:
                        img_bytes = part.inline_data.data
                        break

            if img_bytes:
                with open(png_out, "wb") as pf:
                    pf.write(img_bytes)
                print(f"  -> Saved raw PNG for {card_name} ({len(img_bytes)} bytes)")

                # Resize to standard 750x1050 card dimensions and quantize colors
                raw_img = Image.open(png_out).convert("RGB")
                resized_img = raw_img.resize((750, 1050), Image.Resampling.LANCZOS)
                quantized = resized_img.quantize(colors=16, method=Image.Quantize.FASTOCTREE).convert("RGB")
                proc_png = os.path.join(png_dir, f"{card_name}_proc.png")
                quantized.save(proc_png, "PNG")

                # Vectorize to clean SVG via vtracer
                vtracer.convert_image_to_svg_py(
                    proc_png,
                    svg_out,
                    colormode="color",
                    filter_speckle=16,
                    corner_threshold=65,
                    length_threshold=6.0,
                    max_iterations=10,
                    splice_threshold=45
                )
                print(f"  -> Vectorized {card_name} SVG ({os.path.getsize(svg_out)} bytes)")
                return True
            else:
                print(f"  -> No image data returned for {card_name} (attempt {attempt+1}/{max_retries})")
                if attempt < max_retries - 1:
                    time.sleep(3)

        except Exception as e:
            print(f"  -> Error generating {card_name} (attempt {attempt+1}/{max_retries}): {e}")
            if attempt < max_retries - 1:
                wait_time = (attempt + 1) * 5
                print(f"     Backing off for {wait_time}s before retry...")
                time.sleep(wait_time)

    return False

def generate_ai_cards(args):
    if not HAS_GENAI:
        print("Error: 'google-genai' package is required. Install with: pip install google-genai")
        sys.exit(1)
    if not HAS_VTRACER:
        print("Error: 'vtracer' package is required. Install with: pip install vtracer")
        sys.exit(1)

    api_key = args.api_key or os.environ.get("GEMINI_API_KEY") or os.environ.get("GOOGLE_API_KEY")
    if not api_key:
        print("Error: No API key provided. Set GEMINI_API_KEY environment variable or pass --api-key.")
        sys.exit(1)

    client = genai.Client(api_key=api_key)
    theme = args.theme or "Victorian Steampunk"
    theme_slug = theme.lower().replace(" ", "_")
    out_dir = args.out or f"assets/deck_cards/{theme_slug}"
    png_dir = os.path.join(out_dir, "png")
    svg_dir = os.path.join(out_dir, "svg")
    os.makedirs(png_dir, exist_ok=True)
    os.makedirs(svg_dir, exist_ok=True)

    cards_to_gen = []
    if args.cards == "all":
        cards_to_gen = CARD_NAMES
    elif args.cards == "court":
        cards_to_gen = COURT_CARDS
    elif args.cards == "aces":
        cards_to_gen = [c for c in CARD_NAMES if "ace" in c]
    elif args.cards == "red":
        cards_to_gen = [c for c in CARD_NAMES if c.startswith("hearts_") or c.startswith("diamonds_")]
    elif args.cards == "black":
        cards_to_gen = [c for c in CARD_NAMES if c.startswith("spades_") or c.startswith("clubs_")]
    elif args.cards in ("hearts", "diamonds", "spades", "clubs"):
        cards_to_gen = [c for c in CARD_NAMES if c.startswith(f"{args.cards}_")]
    else:
        cards_to_gen = [c.strip() for c in args.cards.split(",") if c.strip() in CARD_NAMES]

    model_name = args.model or "gemini-3-pro-image"
    workers = getattr(args, "workers", 1) or 1
    print(f"[SolEngine] Generating {len(cards_to_gen)} AI cards with Google GenAI ({model_name}, workers: {workers})...")
    print(f"[SolEngine] Output: {out_dir}")

    if workers > 1:
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
            futures = {
                executor.submit(_generate_single_card, card_name, client, model_name, theme, png_dir, svg_dir, args, idx, len(cards_to_gen)): card_name
                for idx, card_name in enumerate(cards_to_gen)
            }
            for future in concurrent.futures.as_completed(futures):
                name = futures[future]
                try:
                    future.result()
                except Exception as exc:
                    print(f"  -> Exception in worker for {name}: {exc}")
    else:
        for idx, card_name in enumerate(cards_to_gen):
            _generate_single_card(card_name, client, model_name, theme, png_dir, svg_dir, args, idx, len(cards_to_gen))
            if idx + 1 < len(cards_to_gen):
                time.sleep(args.delay)

    print(f"[SolEngine] AI Generation & Vectorization completed in {out_dir}!")

def assemble_deck(args):
    cards_dir = args.cards_dir
    out_svg = args.out or "decks/custom_assembled_svg.svg"
    base_svg = getattr(args, "base", None) or "decks/classic_svg.svg"

    print(f"[SolEngine] Assembling master 9x6 deck SVG from {cards_dir} -> {out_svg} (base: {base_svg})...")

    if not os.path.exists(base_svg):
        print(f"Error: Base SVG '{base_svg}' does not exist.")
        sys.exit(1)

    with open(base_svg, "r", encoding="utf-8") as f:
        base_content = f.read()

    pattern = r'<g\s+transform="matrix\(0\.24,0,0,0\.24,([0-9.]+),([0-9.]+)\)">'
    matches = list(re.finditer(pattern, base_content))
    if len(matches) != 54:
        print(f"Error: Expected 54 card groups in base SVG '{base_svg}', found {len(matches)}")
        sys.exit(1)

    header = base_content[:matches[0].start()]
    footer_start = base_content.rfind("</svg>")
    footer = base_content[footer_start:]

    assembled_cards = []
    custom_count = 0

    for i in range(54):
        name = CARD_NAMES[i]
        col = i % 9
        row = i // 9
        cell_x = col * 750
        cell_y = row * 1050

        # Candidate filenames in cards_dir
        candidates = [f"{name}.svg"]
        if i == 52:
            candidates.extend(["joker.svg", "card_back.svg"])
        elif i == 53:
            candidates.extend(["joker_2.svg", "joker_red.svg"])

        custom_card_path = None
        for cand in candidates:
            p = os.path.join(cards_dir, cand)
            if os.path.exists(p):
                custom_card_path = p
                break

        start = matches[i].start()
        end = matches[i+1].start() if i+1 < len(matches) else footer_start

        if custom_card_path:
            with open(custom_card_path, "r", encoding="utf-8") as cf:
                content = cf.read()
            body_match = re.search(r'<svg[^>]*>(.*?)</svg>', content, re.DOTALL)
            if body_match:
                card_body = body_match.group(1).strip()
            else:
                card_body = content.strip()

            custom_block = (
                f'    <!-- Custom Card {i:02d}: {name} (Col {col}, Row {row}) -->\n'
                f'    <g id="card_{name}" transform="translate({cell_x}, {cell_y})">\n'
                f'        {card_body}\n'
                f'    </g>\n'
            )
            assembled_cards.append(custom_block)
            custom_count += 1
            print(f"  [+] Replaced card {i:02d} ({name}) with {os.path.basename(custom_card_path)}")
        else:
            assembled_cards.append(base_content[start:end])

    full_svg = header + "".join(assembled_cards) + footer
    os.makedirs(os.path.dirname(out_svg), exist_ok=True)
    with open(out_svg, "w", encoding="utf-8") as f:
        f.write(full_svg)

    # Invalidate stale binary cache so SolEngine will re-rasterize
    bin_path = out_svg
    if bin_path.endswith(".svg"):
        bin_path = bin_path[:-4] + ".bin"
    else:
        bin_path += ".bin"

    if os.path.exists(bin_path):
        os.remove(bin_path)
        print(f"[SolEngine] Removed stale binary cache: {bin_path}")

    print(f"[SolEngine] Successfully assembled master SVG: {out_svg} ({custom_count}/54 custom cards, {os.path.getsize(out_svg)} bytes)")

def get_card_crop_box(img_path):
    """
    Automatically detects the card's outer frame within an AI-generated image
    and computes a standard 5:7 playing card crop box centered on the card,
    removing surrounding margins, tabletop, or padding.
    """
    try:
        import cv2
        import numpy as np
        img = cv2.imread(img_path)
        if img is None:
            return None
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        edges = cv2.Canny(gray, 50, 150)
        h, w = gray.shape

        left = 30 + int(np.argmax(edges[:, 30:110].sum(axis=0)))
        right = w - 110 + int(np.argmax(edges[:, w-110:w-30].sum(axis=0)))
        top = 80 + int(np.argmax(edges[80:180, :].sum(axis=1)))
        bottom = h - 180 + int(np.argmax(edges[h-180:h-80, :].sum(axis=1)))

        card_w = right - left
        card_h = bottom - top
        if card_w < w * 0.5 or card_h < h * 0.5:
            return (0, 0, w, h)

        cx = (left + right) / 2.0
        cy = (top + bottom) / 2.0

        margin_x = int(card_w * 0.025)
        margin_y = int(card_h * 0.025)

        target_ratio = 5.0 / 7.0
        eff_w = card_w + 2 * margin_x
        eff_h = card_h + 2 * margin_y

        if eff_w / eff_h < target_ratio:
            crop_h = eff_h
            crop_w = int(round(crop_h * target_ratio))
        else:
            crop_w = eff_w
            crop_h = int(round(crop_w / target_ratio))

        x0 = int(round(cx - crop_w / 2.0))
        x1 = x0 + crop_w
        y0 = int(round(cy - crop_h / 2.0))
        y1 = y0 + crop_h

        if x0 < 0:
            x1 -= x0; x0 = 0
        if x1 > w:
            x0 -= (x1 - w); x1 = w
        if y0 < 0:
            y1 -= y0; y0 = 0
        if y1 > h:
            y0 -= (y1 - h); y1 = h

        return (max(0, x0), max(0, y0), min(w, x1), min(h, y1))
    except Exception as e:
        print(f"  Warning: crop detection failed for {img_path}: {e}")
        return None


def crop_cards_deck(args):
    import shutil, glob
    cards_dir = args.cards_dir
    target_w = getattr(args, "target_width", 360)
    target_h = getattr(args, "target_height", 504)
    backup_dir = getattr(args, "backup_dir", None)
    if not backup_dir:
        backup_dir = os.path.join(cards_dir, "..", "raw")
    backup_dir = os.path.normpath(backup_dir)
    os.makedirs(backup_dir, exist_ok=True)

    print(f"[SolEngine] Cropping cards in {cards_dir} to {target_w}x{target_h} (5:7 ratio)...")
    print(f"  Backing up original uncropped PNGs to {backup_dir}...")

    png_files = glob.glob(os.path.join(cards_dir, "*.png"))
    processed = 0
    for p in png_files:
        fname = os.path.basename(p)
        raw_backup = os.path.join(backup_dir, fname)
        if not os.path.exists(raw_backup):
            shutil.copy2(p, raw_backup)

        box = get_card_crop_box(raw_backup)
        img = Image.open(raw_backup).convert("RGBA")
        if box:
            img = img.crop(box)
        img = img.resize((target_w, target_h), Image.Resampling.LANCZOS)
        img.save(p, "PNG")
        processed += 1

    print(f"[SolEngine] Successfully cropped and updated {processed} card PNGs in {cards_dir}!")


def assemble_bitmap_deck(args):
    png_dir = args.cards_dir
    out_png = args.out or "decks/steampunk.png"
    target_w = getattr(args, "card_width", 360)
    target_h = getattr(args, "card_height", 504)

    cols = 9
    rows = 6
    sheet_w = cols * target_w
    sheet_h = rows * target_h

    print(f"[SolEngine] Assembling 9x6 master bitmap deck sheet ({sheet_w}x{sheet_h}) from {png_dir} -> {out_png}...")
    master_img = Image.new("RGBA", (sheet_w, sheet_h), (255, 255, 255, 255))

    pasted = 0
    for idx, name in enumerate(CARD_NAMES):
        col = idx % 9
        row = idx // 9
        x = col * target_w
        y = row * target_h

        card_path = os.path.join(png_dir, f"{name}.png")
        if not os.path.exists(card_path):
            if name == "joker_black":
                card_path = os.path.join(png_dir, "joker.png")
            elif name == "joker_red":
                card_path = os.path.join(png_dir, "joker_2.png")

        fallback_dir = getattr(args, "fallback_dir", None)
        if not os.path.exists(card_path) and fallback_dir and os.path.exists(fallback_dir):
            fb_path = os.path.join(fallback_dir, f"{name}.png")
            if not os.path.exists(fb_path):
                if name == "joker_black":
                    fb_path = os.path.join(fallback_dir, "joker.png")
                elif name == "joker_red":
                    fb_path = os.path.join(fallback_dir, "joker_2.png")
            if os.path.exists(fb_path):
                card_path = fb_path

        if os.path.exists(card_path):
            card_img = Image.open(card_path).convert("RGBA")
            if getattr(args, "auto_crop", False):
                box = get_card_crop_box(card_path)
                if box:
                    card_img = card_img.crop(box)
            if card_img.size != (target_w, target_h):
                card_img = card_img.resize((target_w, target_h), Image.Resampling.LANCZOS)
            master_img.paste(card_img, (x, y))
            pasted += 1
        else:
            print(f"  Warning: missing PNG for card {idx:02d} ({name})")

    os.makedirs(os.path.dirname(out_png), exist_ok=True)
    master_img.save(out_png, "PNG", optimize=True)
    file_size_mb = os.path.getsize(out_png) / (1024 * 1024)
    print(f"[SolEngine] Successfully assembled bitmap deck: {out_png} ({pasted}/54 cards, {file_size_mb:.2f} MB)")

    # Invalidate binary cache if present
    bin_path = out_png
    dot_pos = bin_path.rfind('.')
    if dot_pos != -1:
        bin_path = bin_path[:dot_pos] + ".bin"
    else:
        bin_path += ".bin"

    if os.path.exists(bin_path):
        os.remove(bin_path)
        print(f"[SolEngine] Removed stale binary cache: {bin_path}")

def auto_deck(args):
    theme_slug = args.theme.lower().replace(" ", "_")
    cards_out = os.path.join("assets/deck_cards", theme_slug)
    master_svg_out = args.out or f"decks/{theme_slug}_svg.svg"

    # Step 1: Generate AI cards & vectorize
    args.out = cards_out
    generate_ai_cards(args)

    # Step 2: Assemble master 9x6 SVG
    class AsmArgs:
        cards_dir = os.path.join(cards_out, "svg")
        out = master_svg_out
        base = getattr(args, "base", "decks/classic_svg.svg")

    assemble_deck(AsmArgs())
    print(f"[SolEngine] Full automated deck generation complete: {master_svg_out}")

def main():
    parser = argparse.ArgumentParser(description="SolEngine Complete Deck Asset & Pipeline Tool")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # export-base
    p_export = subparsers.add_parser("export-base", help="Slice base deck into 54 standalone SVGs and PNGs")
    p_export.add_argument("--svg", default="decks/classic_svg.svg", help="Path to master classic SVG")
    p_export.add_argument("--bin", default="decks/classic_svg.bin", help="Path to binary cache")
    p_export.add_argument("--out", default="assets/deck_cards/classic", help="Output directory")

    # generate-prompts
    p_prompt = subparsers.add_parser("generate-prompts", help="Generate Gemini prompts for custom cards")
    p_prompt.add_argument("--theme", default="Victorian Steampunk", help="Theme name")
    p_prompt.add_argument("--out", default=None, help="Output JSON path")

    # generate-ai
    p_ai = subparsers.add_parser("generate-ai", help="Generate AI card artwork via Google GenAI + vtracer")
    p_ai.add_argument("--theme", default="Victorian Steampunk", help="Theme name")
    p_ai.add_argument("--cards", default="court", help="'all', 'court', 'aces', or comma-separated card names")
    p_ai.add_argument("--api-key", default=None, help="Google Gemini API Key")
    p_ai.add_argument("--model", default="gemini-3-pro-image", help="GenAI model ID (e.g. gemini-3-pro-image, gemini-3.1-flash-image)")
    p_ai.add_argument("--delay", type=float, default=2.0, help="Delay in seconds between requests")
    p_ai.add_argument("--workers", type=int, default=1, help="Concurrent worker threads for generation (e.g. 2-4)")
    p_ai.add_argument("--style-ref", default=None, help="Optional style reference image path (for multi-image style transfer)")
    p_ai.add_argument("--force", action="store_true", help="Force overwrite of existing card SVGs")
    p_ai.add_argument("--out", default=None, help="Output card assets directory")

    # assemble
    p_asm = subparsers.add_parser("assemble", help="Assemble 54 standalone SVGs into master 9x6 SVG")
    p_asm.add_argument("--cards-dir", required=True, help="Directory containing individual card .svg files")
    p_asm.add_argument("--base", default="decks/classic_svg.svg", help="Path to base template master SVG (e.g. classic, hardwood, eldritch)")
    p_asm.add_argument("--out", default="decks/custom_svg.svg", help="Output master SVG path")

    # assemble-bitmap
    p_asm_bmp = subparsers.add_parser("assemble-bitmap", help="Assemble 54 standalone PNGs into master 9x6 PNG sheet")
    p_asm_bmp.add_argument("--cards-dir", required=True, help="Directory containing individual card .png files")
    p_asm_bmp.add_argument("--out", default="decks/steampunk.png", help="Output master PNG path")
    p_asm_bmp.add_argument("--card-width", type=int, default=360, help="Per-card width in pixels (default 360)")
    p_asm_bmp.add_argument("--card-height", type=int, default=504, help="Per-card height in pixels (default 504)")
    p_asm_bmp.add_argument("--fallback-dir", default=None, help="Directory containing fallback card PNGs (e.g. assets/deck_cards/classic/png)")
    p_asm_bmp.add_argument("--auto-crop", action="store_true", default=False, help="Automatically crop outer margins/padding to 5:7 aspect ratio")

    # crop-cards
    p_crop = subparsers.add_parser("crop-cards", help="Batch crop cards to 5:7 aspect ratio, removing outer padding")
    p_crop.add_argument("--cards-dir", required=True, help="Directory containing card PNGs to crop in place")
    p_crop.add_argument("--backup-dir", default=None, help="Directory to back up original uncropped PNGs")
    p_crop.add_argument("--target-width", type=int, default=360)
    p_crop.add_argument("--target-height", type=int, default=504)

    # package-eats
    p_pkg = subparsers.add_parser("package-eats", help="Package SVG/PNG deck into compressed .eats deck recipe")
    p_pkg.add_argument("--input", required=True, help="Input SVG or PNG file path")
    p_pkg.add_argument("--out", required=True, help="Output .eats file path")
    p_pkg.add_argument("--name", default="Custom Deck", help="Deck display name")
    p_pkg.add_argument("--theme-id", default="custom", help="Theme ID identifier")

    # auto-deck (all in one)
    p_auto = subparsers.add_parser("auto-deck", help="End-to-end: GenAI cards -> vectorize -> assemble")
    p_auto.add_argument("--theme", default="Victorian Steampunk", help="Theme name")
    p_auto.add_argument("--cards", default="court", help="'all', 'court', 'aces', or comma-separated card names")
    p_auto.add_argument("--api-key", default=None, help="Google Gemini API Key")
    p_auto.add_argument("--model", default="gemini-3-pro-image", help="GenAI model ID (e.g. gemini-3-pro-image, gemini-3.1-flash-image)")
    p_auto.add_argument("--delay", type=float, default=2.0, help="Delay in seconds between requests")
    p_auto.add_argument("--workers", type=int, default=1, help="Concurrent worker threads for generation (e.g. 2-4)")
    p_auto.add_argument("--style-ref", default=None, help="Optional style reference image path (for multi-image style transfer)")
    p_auto.add_argument("--base", default="decks/classic_svg.svg", help="Path to base template master SVG")
    p_auto.add_argument("--out", default=None, help="Output master SVG path")

    args = parser.parse_args()
    if args.command == "export-base":
        export_base(args)
    elif args.command == "generate-prompts":
        generate_prompts(args)
    elif args.command == "generate-ai":
        generate_ai_cards(args)
    elif args.command == "assemble":
        assemble_deck(args)
    elif args.command == "assemble-bitmap":
        assemble_bitmap_deck(args)
    elif args.command == "crop-cards":
        crop_cards_deck(args)
    elif args.command == "package-eats":
        import zlib, base64
        in_path = args.input
        out_path = args.out
        with open(in_path, "rb") as f:
            data = f.read()
        if in_path.endswith(".svg"):
            comp = zlib.compress(data, level=9)
            b64 = base64.b64encode(comp).decode("ascii")
            fmt = "svgz"
            desc = "Lossless Deflate-compressed SVG vector master sheet."
        else:
            b64 = base64.b64encode(data).decode("ascii")
            fmt = "png"
            desc = "High-resolution raster master sheet embedded in Eatscript."
        content = f'''# SolEngine Deck Recipe — {args.name}
name        = "{args.name}"
theme_id    = "{args.theme_id}"
format      = "{fmt}"
card_w      = 360
card_h      = 504
author      = "SolEngine"
description = "{desc}"

deck_blob = """{b64}"""
'''
        with open(out_path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"[SolEngine] Packaged {in_path} ({len(data):,} bytes) -> {out_path} ({os.path.getsize(out_path):,} bytes)")
    elif args.command == "auto-deck":
        auto_deck(args)

if __name__ == "__main__":
    main()
