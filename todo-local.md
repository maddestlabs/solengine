# Local to-do list

- Help section. Can we build a help system around game content? Can we feasibly derive an adequate help system based on the game's .eats content?
- PyScol and PySolFC are not comprehensive. We need to ensure our core is decoupled sufficiently to handle lots of edge cases and potential future extension. https://gemini.google.com/app/9c08ea849afe92e1
- our own prior wood texture: https://www.shadertoy.com/view/lfKGWR
- and our chrome texture: https://www.shadertoy.com/view/XfVGDR
- procedural foley resynthesis & variance pipeline (see FOLEY_RESYNTHESIS_PIPELINE.md) for authentic card foley (deal-series.wav) and Eatsbits DAW integration
- foley enhancement: enviroments with presets for rain, fireplace crackle, campfire, woodlands, riverbed, simple whitenoise
- try more marble countertop styles
- fix auto-play (solver). It fails on freecell games where user has played and has easy pathway to win.
- [x] Settings > Theme tab: Show card back staggered at angle in back of rendered deck card so user can still see card back in the tab.
- [x] Wear implementation uses just a bit too much vertical noise at bottom and top edges. Also, card backs look exactly the same in contrast to the unique wear aesthetic on individual card faces. Can we peformantly apply randomized aesthetic per each card back as well?
- [x] Clockwork theme: remove cog wheels from bottom of dialogs (both settings and seed dialogs). Remove all colors except grayscale and gold. Remove cog wheel animations except for SolEngine icon. Mouse-hover in SolEngine icon should use white as highlight color instead of blue.