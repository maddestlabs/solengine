# Local to-do list

- New tab for Environment/Scene? Themes should probably be split into: Deck, Scene.
- lighting, god rays and environmental shadows: https://gemini.google.com/app/36ba178a3fb44eb0
- In higher Wear options like Played and Vintage, cards have an obvious border or padded area where Wear is more prevalent. Can we vary the size of that area so some cards don't have it at all but still exhibit typical signs of Wear?
- Auto solver continues from where user left off so history is included in solver.
- Sandy dunes, sandy beach, wet sand textures
- Help sidebar additions: Games list, New solvable game
- reflective card surfaces: https://www.reddit.com/r/soloboardgaming/comments/1or7puk/scoundrel_what_an_addictive_game/
- card style support for Spanish cards, hanafuda and other card variants
- Foley engine: ensure all game sounds are available via api and theme them across existing audio presets
- Wear option adds a border. Can we have the wear simply applied based on the contents of the card.
- if settings menu is open, mouse activity in settings is passing through to playfield.

## Fastest, simplest base configuration
We want this engine to load blazing fast for the first time. New users wouldn't see the advanced visuals but they would be treated with a super fast startup. We need to build the defaults for fast startup but still with elegant minimalism.

Needed:
- minimal, simple base deck like https://www.vecteezy.com/vector-art/35358244-minimalist-design-deck-of-poker-playing-cards-full-deck
- suitable minimalist background

We're going with clockwork theme for the engine. So we'll want core assets following that theme.