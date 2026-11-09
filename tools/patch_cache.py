with open('src/render/card_texture_cache.cpp', 'r', encoding='utf-8') as f:
    c = f.read()

c_norm = c.replace('\r\n', '\n')

target1 = """    } else if (eatsFilePath.find("classic") != std::string::npos) {
        currentTheme_ = DeckThemeId::ClassicSvg;
    } else {"""

replacement1 = """    } else if (eatsFilePath.find("classic") != std::string::npos) {
        currentTheme_ = DeckThemeId::ClassicSvg;
    } else if (eatsFilePath.find("baroque_light") != std::string::npos || eatsFilePath.find("baroque_alabaster") != std::string::npos) {
        currentTheme_ = DeckThemeId::BaroqueLight;
    } else if (eatsFilePath.find("baroque_dark") != std::string::npos) {
        currentTheme_ = DeckThemeId::BaroqueDark;
    } else if (eatsFilePath.find("baroque_noir") != std::string::npos || eatsFilePath.find("baroque") != std::string::npos) {
        currentTheme_ = DeckThemeId::BaroqueNoir;
    } else {"""

if target1 in c_norm:
    c_norm = c_norm.replace(target1, replacement1, 1)
    print("Match 1 replaced successfully")
else:
    print("Match 1 failed")

target2 = """    } else if (theme == DeckThemeId::Custom) {
        const std::string candidateEats[] = {
            "decks/baroque_noir.eats",
            "/decks/baroque_noir.eats",
            "/user/baroque_noir.eats",
            "decks/custom.eats",
            "/decks/custom.eats",
            "/user/custom.eats",
            "../decks/baroque_noir.eats",
            "../../decks/baroque_noir.eats",
            "../decks/custom.eats",
            "../../decks/custom.eats"
        };
        for (const auto& path : candidateEats) {
            if (loadEatsDeck(path, cardW, cardH)) return true;
        }
        const std::string candidateBitmaps[] = {
            "decks/baroque_noir.png",
            "decks/custom.png",
            "../decks/baroque_noir.png",
            "../../decks/baroque_noir.png",
            "../decks/custom.png",
            "../../decks/custom.png"
        };
        for (const auto& path : candidateBitmaps) {
            if (loadBitmapDeck(path, cardW, cardH)) return true;
        }"""

replacement2 = """    } else if (theme == DeckThemeId::BaroqueNoir) {
        currentTheme_ = DeckThemeId::BaroqueNoir;
        const std::string candidateEats[] = {
            "decks/baroque_noir.eats",
            "/decks/baroque_noir.eats",
            "/user/baroque_noir.eats",
            "../decks/baroque_noir.eats",
            "../../decks/baroque_noir.eats"
        };
        for (const auto& path : candidateEats) {
            if (loadEatsDeck(path, cardW, cardH)) return true;
        }
        const std::string candidateBitmaps[] = {
            "decks/baroque_noir.png",
            "../decks/baroque_noir.png",
            "../../decks/baroque_noir.png"
        };
        for (const auto& path : candidateBitmaps) {
            if (loadBitmapDeck(path, cardW, cardH)) return true;
        }
    } else if (theme == DeckThemeId::BaroqueLight) {
        currentTheme_ = DeckThemeId::BaroqueLight;
        const std::string candidateEats[] = {
            "decks/baroque_light.eats",
            "decks/baroque_alabaster.eats",
            "/decks/baroque_light.eats",
            "/decks/baroque_alabaster.eats",
            "/user/baroque_light.eats",
            "../decks/baroque_light.eats",
            "../../decks/baroque_light.eats"
        };
        for (const auto& path : candidateEats) {
            if (loadEatsDeck(path, cardW, cardH)) return true;
        }
        const std::string candidateBitmaps[] = {
            "decks/baroque_light.png",
            "decks/baroque_alabaster.png",
            "../decks/baroque_light.png",
            "../../decks/baroque_light.png"
        };
        for (const auto& path : candidateBitmaps) {
            if (loadBitmapDeck(path, cardW, cardH)) return true;
        }
    } else if (theme == DeckThemeId::BaroqueDark) {
        currentTheme_ = DeckThemeId::BaroqueDark;
        const std::string candidateEats[] = {
            "decks/baroque_dark.eats",
            "/decks/baroque_dark.eats",
            "/user/baroque_dark.eats",
            "../decks/baroque_dark.eats",
            "../../decks/baroque_dark.eats"
        };
        for (const auto& path : candidateEats) {
            if (loadEatsDeck(path, cardW, cardH)) return true;
        }
        const std::string candidateBitmaps[] = {
            "decks/baroque_dark.png",
            "../decks/baroque_dark.png",
            "../../decks/baroque_dark.png"
        };
        for (const auto& path : candidateBitmaps) {
            if (loadBitmapDeck(path, cardW, cardH)) return true;
        }
    } else if (theme == DeckThemeId::Custom) {
        const std::string candidateEats[] = {
            "decks/custom.eats",
            "/decks/custom.eats",
            "/user/custom.eats",
            "../decks/custom.eats",
            "../../decks/custom.eats"
        };
        for (const auto& path : candidateEats) {
            if (loadEatsDeck(path, cardW, cardH)) return true;
        }
        const std::string candidateBitmaps[] = {
            "decks/custom.png",
            "../decks/custom.png",
            "../../decks/custom.png"
        };
        for (const auto& path : candidateBitmaps) {
            if (loadBitmapDeck(path, cardW, cardH)) return true;
        }"""

if target2 in c_norm:
    c_norm = c_norm.replace(target2, replacement2, 1)
    print("Match 2 replaced successfully")
else:
    print("Match 2 failed")

with open('src/render/card_texture_cache.cpp', 'w', encoding='utf-8') as f:
    f.write(c_norm)

