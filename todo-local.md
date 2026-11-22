# Local to-do list
- In higher Wear options like Played and Vintage, cards have an obvious border or padded area where Wear is more prevalent. Can we vary the size of that area so some cards don't have it at all but still exhibit typical signs of Wear?
- Wear option adds a border. Can we have the wear simply applied based on the contents of the card.
- reflective card surfaces: https://www.reddit.com/r/soloboardgaming/comments/1or7puk/scoundrel_what_an_addictive_game/
- card style support for Spanish cards, hanafuda and other card variants
- Foley engine: ensure all game sounds are available via api and theme them across existing audio presets
- Scoundrel stats still render with Seed and other overlayed info in bottom center of display
- environmental shadows: subtractive blend mode
- Commit to solengine.org. Ensure the game doesn't get stuck with browser cache issues. Ensure PWA works.
- Icons. We really do need proper icons.
- logo needs to be vector for pwa.
- eatsdither; dither shader + procedural library for easy reuse across eatsbits apps
- change icon and clockwork theme still to low poly (should save a bit of gpu/cpu)
- c header style apis from solengine
- should we get solitaireengine.org instead? should we forego solengine entirely and instead refactor as solitaireengine since youtube channel is youtube.com/solitaireengine? Should SolEngine exist in its own Organization on GitHub?
- do animated cogwheels present a performance issue? Can we animate coghweels in our gui without affecting performance?

## Language and Accessibility

## Fastest, simplest base configuration
We want this engine to load blazing fast for the first time. New users wouldn't see the advanced visuals but they would be treated with a super fast startup. We need to build the defaults for fast startup but still with elegant minimalism.

Needed:
- minimal, simple base deck like https://www.vecteezy.com/vector-art/35358244-minimalist-design-deck-of-poker-playing-cards-full-deck
- suitable minimalist background

We're going with clockwork theme for the engine. So we'll want core assets following that theme.