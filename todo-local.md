# Local to-do list
- card style support for Spanish cards, hanafuda and other card variants
- Scoundrel, ensure ending works. User can continue after gameover.
- Scoundrel stats still render with Seed and other overlayed info in bottom center of display
- environmental shadows: subtractive blend mode
- eatsdither; dither shader + procedural library for easy reuse across eatsbits apps
- c header style apis from solengine
- Fairytale Solitaire and similiar videogame board mechanics where some plays unlock other parts of the board
- PWA logo, not working?
- LowPoly War should become default deck. We'll name it as SolEngine, overwriting the existing deck. The existing procedural deck could then become SolEngine Classic. We'll maintain our procedural decks and totally remove the curren SolEngine Classic that bundles binary directly (we should gain a little runtime size from this).
- lowpoly memory deck needs distinct pictures. Currently reuses lowpoly dark so court cards are hard to distinguish.

- Unsocial Score. Similar to badges in Steam and other platforms, Unsocial Scoring tracks player's use of the engine. Deck config, background choices, etc. Players earn points from being unsocial. Is this worth considering?

- scrolling lists. Standard mobile scrolling functionality in lists.

- IMPOSSIBLE: But can we support webgl? We've built entirely around WebGPU, how much effort would it take to support webgl so older ipads, for example, could run our engine?

## Presets

Settings presets need refinement and a full dialog similar to game library.

## Language and Accessibility

## Fastest, simplest base configuration
We want this engine to load blazing fast for the first time. New users wouldn't see the advanced visuals but they would be treated with a super fast startup. We need to build the defaults for fast startup but still with elegant minimalism.

Needed:
- minimal, simple base deck like https://www.vecteezy.com/vector-art/35358244-minimalist-design-deck-of-poker-playing-cards-full-deck
- suitable minimalist background

We're going with clockwork theme for the engine. So we'll want core assets following that theme.