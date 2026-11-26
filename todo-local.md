# Local to-do list
- reflective card surfaces: https://www.reddit.com/r/soloboardgaming/comments/1or7puk/scoundrel_what_an_addictive_game/
- card style support for Spanish cards, hanafuda and other card variants
- Scoundrel stats still render with Seed and other overlayed info in bottom center of display
- Scoundrel stats renders as overlay so animations go over it.
- Scoundrel: dungeon ambience
- environmental shadows: subtractive blend mode
- eatsdither; dither shader + procedural library for easy reuse across eatsbits apps
- c header style apis from solengine

- Auto-play toolbar: add settings: outline toggle, backgrond fade toggle, particles toggle.

## Language and Accessibility

## Fastest, simplest base configuration
We want this engine to load blazing fast for the first time. New users wouldn't see the advanced visuals but they would be treated with a super fast startup. We need to build the defaults for fast startup but still with elegant minimalism.

Needed:
- minimal, simple base deck like https://www.vecteezy.com/vector-art/35358244-minimalist-design-deck-of-poker-playing-cards-full-deck
- suitable minimalist background

We're going with clockwork theme for the engine. So we'll want core assets following that theme.