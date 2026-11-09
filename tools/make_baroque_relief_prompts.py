#!/usr/bin/env python3
"""
tools/make_baroque_relief_prompts.py
====================================
Generates comprehensive Gemini multimodal image-to-image prompts for all 54 cards
in the new 'Baroque Relief' deck, anchoring to:
1. Style Reference Anchor: assets/deck_cards/baroque_relief/png/spades_01_ace.png
2. Layout Anchors: assets/deck_cards/classic/png/{card}.png & assets/deck_cards/baroque_light/png/{card}.png
3. Solitaire Red/Black Contrast Rules (CRITICAL FOR PLAYABILITY)
4. Anti-Shadow Full Bleed 5:7 Rules
"""

import os
import sys
import json

sys.path.insert(0, 'tools')
from deck_pipeline import CARD_NAMES, SUIT_SYMBOLS, RANK_LABELS

style_anchor = "c:/git/solengine/assets/deck_cards/baroque_relief/png/spades_01_ace.png"
relief_prompts = []

for idx, name in enumerate(CARD_NAMES):
    parts = name.split('_')
    suit = parts[0]
    rank_key = '_'.join(parts[1:]) if len(parts) > 1 else parts[0]
    rank_label = RANK_LABELS.get(rank_key, rank_key.upper())
    suit_sym = SUIT_SYMBOLS.get(suit, suit)
    
    classic_ref = f"c:/git/solengine/assets/deck_cards/classic/png/{name}.png"
    light_ref = f"c:/git/solengine/assets/deck_cards/baroque_light/png/{name}.png"
    is_court = any(k in name for k in ('jack', 'queen', 'king'))
    is_ace = '01_ace' in name
    
    if suit in ('hearts', 'diamonds') or name == 'joker_red':
        suit_rules = (
            f"VIVID RED SUIT REQUIREMENT (CRITICAL FOR SOLITAIRE PLAYABILITY): "
            f"This is a RED card ({rank_label} of {suit.capitalize()}). The corner rank '{rank_label}', "
            f"corner {suit_sym} pip, and all central suit symbols and artwork MUST radiate with a vivid, "
            f"luminous glowing ruby crimson bloom (#FF0033 / #E60026) with polished rose-gold and antique ivory bevels. "
            f"CORNER PIP SPECIFICATION: Corner {suit_sym} pips must feature a warm antique gold-ish/ivory outer sculpted relief bezel "
            f"surrounding a luminous, rich ruby crimson interior (#c8102e), ensuring instantaneous red suit recognition during gameplay."
        )
    else:
        suit_rules = (
            f"CARVED ANTIQUE IVORY & OBSIDIAN REQUIREMENT: "
            f"This is a BLACK card ({rank_label} of {suit.capitalize()}). The corner rank '{rank_label}', "
            f"corner {suit_sym} pip, and all primary suit symbols and filigree MUST be sculpted in high-relief "
            f"antique bone/ivory (#E8E2D2) and chiseled silver/patinated pewter with lustrous highlights and deep "
            f"obsidian black accents (#111111) on dark charcoal slate. "
            f"CORNER PIP SPECIFICATION (CRITICAL FOR BLACK SUIT RECOGNITION): The corner {suit_sym} pips in the open top-left and inverted "
            f"bottom-right corners MUST have an antique warm gold/ivory outer sculpted relief bezel/frame with a SOLID DEEP OBSIDIAN BLACK "
            f"INTERIOR (#0a0a0a / #111111). The interior must be solid black (not white, grey, or pale silver) so players instantly recognize "
            f"the card as a black {suit.capitalize()} suit."
        )

    if is_ace:
        composition = (
            f"An authentic standard playing card: {rank_label} of {suit.capitalize()} in Baroque Relief style. "
            f"Elaborate high-relief ornamental acanthus leaf scrollwork, volutes, and floral flourishes frame a grand central "
            f"sculpted {suit.capitalize()} emblem cartouche. Symmetrical architectural balance, matching the style reference anchor."
        )
    elif is_court:
        figure = rank_key.replace('_', ' ').capitalize()
        composition = (
            f"An authentic standard playing card: {figure} of {suit.capitalize()} in Baroque Relief style. "
            f"Central composition features a classical double-ended or single majestic Baroque sculpted bust/portrait of the {figure} "
            f"carved in exquisite ivory and chiseled metallic relief within an ornate architectural acanthus cartouche, matching the "
            f"compositional layout of the reference layout image."
        )
    elif name.startswith('joker'):
        composition = (
            f"An authentic standard playing card: {rank_label} in Baroque Relief style. "
            f"Features an elaborate carved Venetian Baroque jester mask and ornate crest in high-relief sculpted ivory and ornamental filigree."
        )
    else:
        composition = (
            f"An authentic standard playing card: {rank_label} of {suit.capitalize()} in Baroque Relief style. "
            f"The {rank_label} {suit.capitalize()} pips are sculpted as high-relief ornate emblems arranged according to the standard "
            f"playing card pip layout of the layout reference image, framed by delicate carved acanthus scrollwork."
        )

    prompt = (
        f"{composition} "
        f"STYLE REFERENCE CONDITIONING: Match the exact artistic style, high-relief carved ivory and chiseled metallic texture, "
        f"chiaroscuro rim lighting, and organic dark charcoal slate cardstock of the style reference anchor ({style_anchor}). "
        f"{suit_rules} "
        f"Corner Indices: Clear, functional, legible rank '{rank_label}' and suit symbol '{suit_sym}' in the top-left and inverted "
        f"in the bottom-right corners, carved in relief inside the card frame. "
        f"DIMENSIONS & BLEED: Flat 2D digital playing card face graphic texture blitted directly edge-to-edge on a 5:7 canvas. "
        f"STRICT PROHIBITIONS: NO external drop shadows, NO border margins, NO padding, NO tabletop surface, NO background surface, "
        f"NO photo of a card, NO 3D tilt. The 4 edges of the image ARE the physical perimeter of the card."
    )

    relief_prompts.append({
        'index': idx,
        'card_name': name,
        'suit': suit,
        'rank': rank_label,
        'classic_ref': classic_ref,
        'light_ref': light_ref,
        'style_anchor': style_anchor,
        'prompt': prompt
    })

os.makedirs('assets', exist_ok=True)
manifest_path = 'assets/baroque_relief_prompts.json'
with open(manifest_path, 'w', encoding='utf-8') as f:
    json.dump(relief_prompts, f, indent=2)

print(f"Generated {len(relief_prompts)} prompts in {manifest_path}")

court = [p for p in relief_prompts if any(k in p['card_name'] for k in ('jack', 'queen', 'king', '01_ace'))]
with open('assets/baroque_relief_court_and_aces.json', 'w', encoding='utf-8') as f:
    json.dump(court, f, indent=2)

print(f"Court & Aces subset: {len(court)} prompts saved to assets/baroque_relief_court_and_aces.json")

