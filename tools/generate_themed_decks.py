#!/usr/bin/env python3
"""
SolEngine Themed SVG Deck Generator
Transforms the base CC0 Wikimedia playing card deck into stylized vector themes:
1. Cyberpunk / Synthwave (Neon Cyan & Magenta, dark obsidian cardstock, tech borders)
2. Eldritch Gothic (Tarnished gold & blood crimson, blackened parchment, arcane borders)
3. Victorian Hardwood (Antique woodcut ink & oxblood, warm ivory parchment, classic borders)
"""

import os
import re
import sys

def create_themed_deck(src_path, dst_path, theme):
    print(f"[SolEngine] Generating {theme['name']} -> {dst_path}...")
    with open(src_path, "r", encoding="utf-8") as f:
        content = f.read()

    # 1. Replace card base background color
    # The original background path has style="fill:white;"
    card_bg = theme["card_bg"]
    content = content.replace('style="fill:white;"', f'style="fill:{card_bg};"')

    # 2. Adjust red suits (Hearts & Diamonds)
    # Original is rgb(204,0,0)
    red_fill = theme["red_fill"]
    content = content.replace('fill:rgb(204,0,0)', f'fill:{red_fill}')
    content = content.replace('stroke:rgb(204,0,0)', f'stroke:{red_fill}')

    # 3. Adjust Court Card colors:
    # Original gold: rgb(255,207,64)
    # Original court blue: rgb(6,25,116)
    court_gold = theme["court_gold"]
    court_blue = theme["court_blue"]
    content = content.replace('rgb(255,207,64)', court_gold)
    content = content.replace('rgb(6,25,116)', court_blue)

    # 4. Set black suits (Spades & Clubs) colors on their container groups
    # In classic_svg, spade/club groups are <g id="spades..." ...> or <g id="clubs..." ...>
    # nanoSVG inherits fill from <g> when children don't specify fill.
    black_fill = theme["black_fill"]
    
    # We update <g id="spades..." and <g id="clubs..." to include fill="{black_fill}"
    def replace_suit_group(m):
        return f'{m.group(1)} fill="{black_fill}" '

    content = re.sub(r'(<g\s+id="(?:spades|clubs)[^"]*")\s+', replace_suit_group, content)

    # Also handle near-black colors if any are hardcoded in strokes/fills
    for near_black in ['rgb(0,0,1)', 'rgb(0,0,3)', 'rgb(0,0,4)', 'rgb(0,1,0)', 'rgb(0,2,0)', 'rgb(1,0,0)', 'rgb(2,0,0)', 'rgb(3,0,0)']:
        content = content.replace(near_black, black_fill)

    # 5. Inject decorative border overlay into each of the 54 card cells
    border_svg = theme.get("border_svg", "")
    if border_svg:
        # Each card background is followed by </g>. We can insert our border decorative elements right after the card base path.
        target_card_path = f'style="fill:{card_bg};"/>'
        replacement = f'{target_card_path}\n{border_svg}'
        content = content.replace(target_card_path, replacement)

    # Ensure output directory exists and write
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    with open(dst_path, "w", encoding="utf-8") as f:
        f.write(content)

    print(f"[SolEngine] Successfully generated {dst_path} ({os.path.getsize(dst_path)} bytes)")


# Border definitions designed in the 750x1050 coordinate space of each card cell:
CYBERPUNK_BORDER = """
                <!-- Cyberpunk Neon Dual Inset Border & Corner Brackets -->
                <path d="M 40,40 L 710,40 L 710,1010 L 40,1010 Z" style="fill:none;stroke:#00f0ff;stroke-width:3;stroke-opacity:0.65;"/>
                <path d="M 52,52 L 698,52 L 698,998 L 52,998 Z" style="fill:none;stroke:#ff007f;stroke-width:1.5;stroke-opacity:0.4;"/>
                <!-- Corner Tech Brackets -->
                <path d="M 32,70 L 32,32 L 70,32" style="fill:none;stroke:#00f0ff;stroke-width:5;"/>
                <path d="M 680,32 L 718,32 L 718,70" style="fill:none;stroke:#00f0ff;stroke-width:5;"/>
                <path d="M 32,980 L 32,1018 L 70,1018" style="fill:none;stroke:#00f0ff;stroke-width:5;"/>
                <path d="M 680,1018 L 718,1018 L 718,980" style="fill:none;stroke:#00f0ff;stroke-width:5;"/>
"""

ELDRITCH_BORDER = """
                <!-- Eldritch Tarnished Gold Filigree & Runic Brackets -->
                <path d="M 42,42 L 708,42 L 708,1008 L 42,1008 Z" style="fill:none;stroke:#d4af37;stroke-width:2.5;stroke-opacity:0.8;"/>
                <path d="M 52,52 L 698,52 L 698,998 L 52,998 Z" style="fill:none;stroke:#8c7028;stroke-width:1.2;stroke-dasharray:6,4;"/>
                <!-- Corner Diamonds -->
                <polygon points="42,42 49,35 56,42 49,49" style="fill:#d4af37;"/>
                <polygon points="708,42 701,35 694,42 701,49" style="fill:#d4af37;"/>
                <polygon points="42,1008 49,1001 56,1008 49,1015" style="fill:#d4af37;"/>
                <polygon points="708,1008 701,1001 694,1008 701,1015" style="fill:#d4af37;"/>
"""

HARDWOOD_BORDER = """
                <!-- Victorian Hardwood Woodcut Double Rule -->
                <path d="M 36,36 L 714,36 L 714,1014 L 36,1014 Z" style="fill:none;stroke:#2c2017;stroke-width:3.5;"/>
                <path d="M 46,46 L 704,46 L 704,1004 L 46,1004 Z" style="fill:none;stroke:#5c4432;stroke-width:1.5;"/>
                <!-- Corner Ornaments -->
                <circle cx="46" cy="46" r="4" style="fill:#8b261e;"/>
                <circle cx="704" cy="46" r="4" style="fill:#8b261e;"/>
                <circle cx="46" cy="1004" r="4" style="fill:#8b261e;"/>
                <circle cx="704" cy="1004" r="4" style="fill:#8b261e;"/>
"""

ANIME_BORDER = """
                <!-- Modern Anime Geometric Framing & Radiant Corner Stars -->
                <path d="M 36,36 L 714,36 L 714,1014 L 36,1014 Z" style="fill:none;stroke:#2b3a55;stroke-width:2.5;stroke-opacity:0.85;"/>
                <path d="M 46,46 L 704,46 L 704,1004 L 46,1004 Z" style="fill:none;stroke:#3a86ff;stroke-width:1.2;stroke-opacity:0.5;"/>
                <!-- Anime Corner Star Sparks -->
                <polygon points="46,40 48,44 52,46 48,48 46,52 44,48 40,46 44,44" style="fill:#ffb703;"/>
                <polygon points="704,40 706,44 710,46 706,48 704,52 702,48 698,46 702,44" style="fill:#ffb703;"/>
                <polygon points="46,1000 48,1004 52,1006 48,1008 46,1012 44,1008 40,1006 44,1004" style="fill:#ffb703;"/>
                <polygon points="704,1000 706,1004 710,1006 706,1008 704,1012 702,1008 698,1006 702,1004" style="fill:#ffb703;"/>
"""

THEMES = [
    {
        "name": "Cyberpunk Neon",
        "file": "decks/cyberpunk_svg.svg",
        "card_bg": "#0c1017",
        "black_fill": "#00f0ff",     # Electric cyan for Spades/Clubs
        "red_fill": "#ff007f",       # Hot laser magenta for Hearts/Diamonds
        "court_gold": "#ffe600",     # Neon cyber yellow
        "court_blue": "#7b2cbf",     # Synthwave ultraviolet
        "border_svg": CYBERPUNK_BORDER
    },
    {
        "name": "Eldritch Gothic",
        "file": "decks/eldritch_svg.svg",
        "card_bg": "#141210",
        "black_fill": "#d4af37",     # Tarnished gold for Spades/Clubs
        "red_fill": "#a31621",       # Blood crimson for Hearts/Diamonds
        "court_gold": "#d4af37",     # Antique gold
        "court_blue": "#211a2c",     # Void deep purple
        "border_svg": ELDRITCH_BORDER
    },
    {
        "name": "Victorian Hardwood",
        "file": "decks/hardwood_svg.svg",
        "card_bg": "#fcf8ee",
        "black_fill": "#1c1916",     # Woodcut carbon black
        "red_fill": "#9e2a2b",       # Antique oxblood
        "court_gold": "#c8963e",     # Brass gold
        "court_blue": "#1d3557",     # Classic Prussian navy
        "border_svg": HARDWOOD_BORDER
    },
    {
        "name": "Anime Celestia",
        "file": "decks/anime_svg.svg",
        "card_bg": "#f8faff",
        "black_fill": "#161b26",     # Obsidian midnight for Spades/Clubs
        "red_fill": "#e60033",       # Radiant anime ruby crimson for Hearts/Diamonds
        "court_gold": "#ffb703",     # Luminous anime gold
        "court_blue": "#3a86ff",     # Royal anime azure
        "border_svg": ANIME_BORDER
    }
]

def main():
    src_path = "decks/classic_svg.svg"
    if not os.path.exists(src_path):
        print(f"Error: {src_path} not found.")
        sys.exit(1)

    for theme in THEMES:
        create_themed_deck(src_path, theme["file"], theme)

if __name__ == "__main__":
    main()
