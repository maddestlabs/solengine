# Local to-do list
- reflective card surfaces: https://www.reddit.com/r/soloboardgaming/comments/1or7puk/scoundrel_what_an_addictive_game/
- card style support for Spanish cards, hanafuda and other card variants
- Scoundrel stats still render with Seed and other overlayed info in bottom center of display
- Scoundrel stats renders as overlay so animations go over it.
- Scoundrel: dungeon ambience
- environmental shadows: subtractive blend mode
- eatsdither; dither shader + procedural library for easy reuse across eatsbits apps
- c header style apis from solengine
- Fairytale Solitaire and similiar videogame board mechanics where some plays unlock other parts of the board

- Unsocial Score. Similar to badges in Steam and other platforms, Unsocial Scoring tracks player's use of the engine. Deck config, background choices, etc. Players earn points from being unsocial. Is this worth considering?

- IMPOSSIBLE: But can we support webgl? We've built entirely around WebGPU, how much effort would it take to support webgl so older ipads, for example, could run our engine?

## Language and Accessibility

## Fastest, simplest base configuration
We want this engine to load blazing fast for the first time. New users wouldn't see the advanced visuals but they would be treated with a super fast startup. We need to build the defaults for fast startup but still with elegant minimalism.

Needed:
- minimal, simple base deck like https://www.vecteezy.com/vector-art/35358244-minimalist-design-deck-of-poker-playing-cards-full-deck
- suitable minimalist background

We're going with clockwork theme for the engine. So we'll want core assets following that theme.