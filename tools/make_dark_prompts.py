import json
import os
import sys

sys.path.insert(0, 'tools')
from deck_pipeline import CARD_NAMES, SUIT_SYMBOLS, RANK_LABELS

dark_prompts = []
for idx, name in enumerate(CARD_NAMES):
    parts = name.split('_')
    suit = parts[0]
    rank_key = '_'.join(parts[1:]) if len(parts) > 1 else parts[0]
    rank_label = RANK_LABELS.get(rank_key, rank_key.upper())
    suit_sym = SUIT_SYMBOLS.get(suit, suit)
    
    light_ref = f'c:/git/solengine/assets/deck_cards/baroque_light/png/{name}.png'
    noir_ref = 'c:/git/solengine/assets/deck_cards/baroque_noir/png/hearts_01_ace.png'
    
    if suit in ('hearts', 'diamonds') or name == 'joker_red':
        suit_req = (
            f"PROMINENT RED GLOW REQUIREMENT (CRITICAL FOR SOLITAIRE PLAYABILITY): "
            f"This is a RED card ({name}). The corner rank '{rank_label}', corner {suit_sym} pip, "
            f"and all primary suit symbols and artwork MUST feature an intensely prominent, radiant glowing ruby crimson bloom and neon halo effect (#FF0033 / #E60026) "
            f"that radiates and glows vividly against the dark slate background. Polished silver and rose-gold metallic bevels. "
            f"The red bloom must be unmistakably bright, bold, and high-contrast."
        )
    else:
        suit_req = (
            f"ANTIQUE CHISELED SILVER & OBSIDIAN REQUIREMENT: "
            f"This is a BLACK card ({name}). The corner rank '{rank_label}', corner {suit_sym} pip, "
            f"and all suit symbols and artwork MUST be sculpted in gleaming chiseled antique silver (#E2E8F0) "
            f"and dark oxidized pewter with lustrous metallic specular highlights and deep obsidian black accents (#111111)."
        )
        
    prompt = (
        f"An authentic standard playing card: {rank_label} of {suit.capitalize()} in Baroque Dark style. "
        f"ART & COMPOSITION: Match the exact artwork, acanthus filigree border, central artwork/figures/pips, and corner indices layout of the first reference image ({name} in Baroque Light). "
        f"BACKGROUND & AESTHETIC: Transform the background and palette to match the dark archaic aesthetic and surface inconsistencies of the second reference image (Baroque Noir): deep charcoal slate cardstock with subtle organic paper tooth, mottled patina, and chiaroscuro rim lighting. "
        f"{suit_req} "
        f"Flat 2D digital playing card face graphic texture blitted directly edge-to-edge on a 5:7 canvas. "
        f"STRICT PROHIBITIONS: NO external drop shadows, NO border margins, NO padding, NO tabletop surface, NO background surface, NO photograph of a card. The 4 edges of the image ARE the physical perimeter of the card."
    )
    
    dark_prompts.append({
        'index': idx,
        'card_name': name,
        'light_ref': light_ref,
        'noir_ref': noir_ref,
        'prompt': prompt
    })

with open('assets/baroque_dark_prompts.json', 'w', encoding='utf-8') as f:
    json.dump(dark_prompts, f, indent=2)

spades = [p for p in dark_prompts if p['card_name'].startswith('spades') and p['card_name'] != 'spades_01_ace']
hearts = [p for p in dark_prompts if p['card_name'].startswith('hearts') and p['card_name'] != 'hearts_01_ace']
clubs = [p for p in dark_prompts if p['card_name'].startswith('clubs')]
diamonds = [p for p in dark_prompts if p['card_name'].startswith('diamonds')]
jokers = [p for p in dark_prompts if p['card_name'].startswith('joker')]

with open('assets/baroque_dark_spades.json', 'w', encoding='utf-8') as f: json.dump(spades, f, indent=2)
with open('assets/baroque_dark_hearts.json', 'w', encoding='utf-8') as f: json.dump(hearts, f, indent=2)
with open('assets/baroque_dark_clubs.json', 'w', encoding='utf-8') as f: json.dump(clubs, f, indent=2)
with open('assets/baroque_dark_diamonds.json', 'w', encoding='utf-8') as f: json.dump(diamonds, f, indent=2)
with open('assets/baroque_dark_jokers.json', 'w', encoding='utf-8') as f: json.dump(jokers, f, indent=2)

print(f"Total: {len(dark_prompts)} prompts generated.")
print(f"Spades pending: {len(spades)}, Hearts pending: {len(hearts)}, Clubs pending: {len(clubs)}, Diamonds pending: {len(diamonds)}, Jokers pending: {len(jokers)}")

