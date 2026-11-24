#include "solengine/game_definition.hpp"
#include "solengine/eats_package_loader.hpp"
#include "solengine/settings_manager.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

namespace solengine {

namespace {
std::string toLowerStr(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string deriveGameId(const std::string& filePath, const std::string& name) {
    if (!filePath.empty()) {
        std::string stem = toLowerStr(std::filesystem::path(filePath).stem().string());
        if (!stem.empty()) return stem;
    }
    std::string id;
    id.reserve(name.size());
    for (char ch : name) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) {
            id += static_cast<char>(std::tolower(c));
        } else if (ch == ' ' || ch == '-' || ch == '_') {
            if (id.empty() || id.back() != '_') id += '_';
        }
    }
    while (!id.empty() && id.back() == '_') id.pop_back();
    return id;
}
} // namespace

GameRegistry& GameRegistry::instance() {
    static GameRegistry s_reg;
    return s_reg;
}

GameRegistry::GameRegistry() {
    registerBuiltins();
}

void GameRegistry::registerBuiltins() {
    games_.clear();

    // 1. Klondike Draw 1
    SolitaireGameDef k1{};
    k1.id = "klondike_draw_1";
    k1.name = "Klondike (Draw 1)";
    k1.family = "Klondike";
    k1.category = "POPULAR";
    k1.description = "The classic solitaire. Build down in alternating colors; single card draws to waste.";
    k1.decks = 1;
    k1.tableaus = 7;
    k1.foundations = 4;
    k1.freecells = 0;
    k1.tableauDealCounts = {1, 2, 3, 4, 5, 6, 7};
    k1.tableauAllFaceUp = false;
    k1.stockDealMode = "waste_draw_1";
    k1.stockDealCount = 1;
    k1.maxRedeals = 0;
    k1.nativeVariant = GameVariant::KlondikeDraw1;
    games_.push_back(k1);

    // 2. Klondike Draw 3
    SolitaireGameDef k3 = k1;
    k3.id = "klondike_draw_3";
    k3.name = "Klondike (Draw 3)";
    k3.description = "Tournament standard. Cards are drawn in packets of three with unlimited redeals.";
    k3.stockDealMode = "waste_draw_3";
    k3.stockDealCount = 3;
    k3.maxRedeals = -1;
    k3.nativeVariant = GameVariant::KlondikeDraw3;
    games_.push_back(k3);

    // 3. FreeCell
    SolitaireGameDef fc{};
    fc.id = "freecell";
    fc.name = "FreeCell";
    fc.family = "FreeCell";
    fc.category = "POPULAR";
    fc.description = "Open tableau game of skill. 4 reserve free cells; nearly every deal is winnable.";
    fc.decks = 1;
    fc.tableaus = 8;
    fc.foundations = 4;
    fc.freecells = 4;
    fc.tableauDealCounts = {7, 7, 7, 7, 6, 6, 6, 6};
    fc.tableauAllFaceUp = true;
    fc.stockDealMode = "none";
    fc.nativeVariant = GameVariant::FreeCell;
    games_.push_back(fc);

    // 4. Spider (1 Suit)
    SolitaireGameDef sp1{};
    sp1.id = "spider_1_suit";
    sp1.name = "Spider (1 Suit)";
    sp1.family = "Spider";
    sp1.category = "SPIDER";
    sp1.description = "Relaxed introductory Spider. All cards are Spades; assemble full King-to-Ace runs.";
    sp1.decks = 2;
    sp1.tableaus = 10;
    sp1.foundations = 8;
    sp1.freecells = 0;
    sp1.tableauDealCounts = {6, 6, 6, 6, 5, 5, 5, 5, 5, 5};
    sp1.tableauAllFaceUp = false;
    sp1.stockDealMode = "deal_to_all_columns";
    sp1.stockDealCount = 10;
    sp1.nativeVariant = GameVariant::Spider1Suit;
    games_.push_back(sp1);

    // 5. Spider (2 Suits)
    SolitaireGameDef sp2 = sp1;
    sp2.id = "spider_2_suits";
    sp2.name = "Spider (2 Suits)";
    sp2.description = "Strategic two-suit variant (Spades & Hearts). Stacking allowed across suits, lifting requires matching suit.";
    sp2.nativeVariant = GameVariant::Spider2Suits;
    games_.push_back(sp2);

    // 6. Spider (3 Suits)
    SolitaireGameDef sp3 = sp1;
    sp3.id = "spider_3_suits";
    sp3.name = "Spider (3 Suits)";
    sp3.description = "Advanced 3-suit configuration (Spades, Hearts, Diamonds).";
    sp3.nativeVariant = GameVariant::Spider3Suits;
    games_.push_back(sp3);

    // 7. Spider (4 Suits)
    SolitaireGameDef sp4 = sp1;
    sp4.id = "spider_4_suits";
    sp4.name = "Spider (4 Suits)";
    sp4.description = "The classic masterpiece. 104 cards across all 4 suits; rigorous planning required.";
    sp4.nativeVariant = GameVariant::Spider4Suits;
    games_.push_back(sp4);

    // 8. Golf
    SolitaireGameDef gf{};
    gf.id = "golf";
    gf.name = "Golf";
    gf.family = "Golf";
    gf.category = "POPULAR";
    gf.description = "Fast-paced elimination solitaire. Stack adjacent (+-1) ranks onto the foundation; Kings are dead ends.";
    gf.decks = 1;
    gf.tableaus = 7;
    gf.foundations = 1;
    gf.freecells = 0;
    gf.tableauDealCounts = {5, 5, 5, 5, 5, 5, 5};
    gf.tableauAllFaceUp = true;
    gf.foundationStarterCard = true;
    gf.stockDealMode = "deal_to_foundation";
    gf.stockDealCount = 1;
    gf.maxRedeals = 0;
    gf.nativeVariant = GameVariant::Golf;
    games_.push_back(gf);

    // 9. Baker's Game
    SolitaireGameDef bg{};
    bg.id = "bakers_game";
    bg.name = "Baker's Game";
    bg.family = "FreeCell";
    bg.category = "FREECELL";
    bg.description = "The historical predecessor to FreeCell. Strict same-suit tableau building; pure intellect.";
    bg.decks = 1;
    bg.tableaus = 8;
    bg.foundations = 4;
    bg.freecells = 4;
    bg.tableauDealCounts = {7, 7, 7, 7, 6, 6, 6, 6};
    bg.tableauAllFaceUp = true;
    bg.stockDealMode = "none";
    bg.nativeVariant = GameVariant::CustomScript;
    games_.push_back(bg);

    // 10. Spiderette
    SolitaireGameDef spd{};
    spd.id = "spiderette";
    spd.name = "Spiderette";
    spd.family = "Spider";
    spd.category = "SPIDER";
    spd.description = "Compact 52-card single-deck Spider. 7 tableaus; assemble complete King-to-Ace runs to score.";
    spd.decks = 1;
    spd.tableaus = 7;
    spd.foundations = 4;
    spd.freecells = 0;
    spd.tableauDealCounts = {1, 2, 3, 4, 5, 6, 7};
    spd.tableauAllFaceUp = false;
    spd.stockDealMode = "deal_to_all_columns";
    spd.stockDealCount = 7;
    spd.nativeVariant = GameVariant::Spider1Suit;
    games_.push_back(spd);

    // 11. TriPeaks
    SolitaireGameDef tp{};
    tp.id = "tri_peaks";
    tp.name = "TriPeaks";
    tp.family = "Golf";
    tp.category = "GOLF";
    tp.layout = "tripeaks";
    tp.description = "Fast-paced peak elimination. Stack cards +-1 rank onto foundation waste; streak bonuses.";
    tp.decks = 1;
    tp.tableaus = 28;
    tp.foundations = 1;
    tp.freecells = 0;
    tp.tableauDealCounts = std::vector<uint8_t>(28, 1);
    tp.tableauAllFaceUp = false;
    tp.foundationStarterCard = true;
    tp.stockDealMode = "deal_to_foundation";
    tp.stockDealCount = 1;
    tp.nativeVariant = GameVariant::CustomScript;
    games_.push_back(tp);

    // 12. Yukon
    SolitaireGameDef yk{};
    yk.id = "yukon";
    yk.name = "Yukon";
    yk.family = "Yukon";
    yk.category = "POPULAR";
    yk.description = "Beloved classic. Any face-up card stack can be moved regardless of sequence; Kings to spaces.";
    yk.decks = 1;
    yk.tableaus = 7;
    yk.foundations = 4;
    yk.freecells = 0;
    yk.tableauDealCounts = {1, 6, 7, 8, 9, 10, 11};
    yk.tableauAllFaceUp = false;
    yk.stockDealMode = "none";
    yk.nativeVariant = GameVariant::CustomScript;
    games_.push_back(yk);

    // 13. Russian Solitaire
    SolitaireGameDef rs{};
    rs.id = "russian_solitaire";
    rs.name = "Russian Solitaire";
    rs.family = "Yukon";
    rs.category = "POPULAR";
    rs.description = "High-mastery Yukon variant. Tableaux build down strictly in the same suit; any face-up stack can move.";
    rs.decks = 1;
    rs.tableaus = 7;
    rs.foundations = 4;
    rs.freecells = 0;
    rs.tableauDealCounts = {1, 6, 7, 8, 9, 10, 11};
    rs.tableauAllFaceUp = false;
    rs.stockDealMode = "none";
    rs.nativeVariant = GameVariant::CustomScript;
    games_.push_back(rs);

    // 14. Forty Thieves
    SolitaireGameDef ft{};
    ft.id = "forty_thieves";
    ft.name = "Forty Thieves";
    ft.family = "FortyThieves";
    ft.category = "POPULAR";
    ft.description = "Two-deck classic. 10 tableaus of 4 cards; build down in same suit; single card movement only.";
    ft.decks = 2;
    ft.tableaus = 10;
    ft.foundations = 8;
    ft.freecells = 0;
    ft.tableauDealCounts = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
    ft.tableauAllFaceUp = true;
    ft.stockDealMode = "waste_draw_1";
    ft.stockDealCount = 1;
    ft.nativeVariant = GameVariant::CustomScript;
    games_.push_back(ft);

    // 15. Easthaven
    SolitaireGameDef eh{};
    eh.id = "easthaven";
    eh.name = "Easthaven";
    eh.family = "Klondike";
    eh.category = "KLONDIKE";
    eh.description = "Klondike mechanics with Spider stock deals. Tap stock to deal one card to all 7 columns.";
    eh.decks = 1;
    eh.tableaus = 7;
    eh.foundations = 4;
    eh.freecells = 0;
    eh.tableauDealCounts = {3, 3, 3, 3, 3, 3, 3};
    eh.tableauAllFaceUp = false;
    eh.stockDealMode = "deal_to_all_columns";
    eh.stockDealCount = 7;
    eh.nativeVariant = GameVariant::KlondikeDraw1;
    games_.push_back(eh);

    // 16. Eight Off
    SolitaireGameDef eo{};
    eo.id = "eight_off";
    eo.name = "Eight Off";
    eo.family = "FreeCell";
    eo.category = "FREECELL";
    eo.description = "FreeCell ancestor with 8 reserve cells. Strict same-suit building; Kings to empty spaces.";
    eo.decks = 1;
    eo.tableaus = 8;
    eo.foundations = 4;
    eo.freecells = 8;
    eo.tableauDealCounts = {6, 6, 6, 6, 6, 6, 6, 6};
    eo.tableauAllFaceUp = true;
    eo.stockDealMode = "none";
    eo.nativeVariant = GameVariant::CustomScript;
    games_.push_back(eo);

    // 17. Seahaven Towers
    SolitaireGameDef st{};
    st.id = "seahaven_towers";
    st.name = "Seahaven Towers";
    st.family = "FreeCell";
    st.category = "FREECELL";
    st.description = "Acclaimed 10-column open solitaire. Build down in same suit; Kings to empty spaces.";
    st.decks = 1;
    st.tableaus = 10;
    st.foundations = 4;
    st.freecells = 4;
    st.tableauDealCounts = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
    st.tableauAllFaceUp = true;
    st.stockDealMode = "none";
    st.nativeVariant = GameVariant::CustomScript;
    games_.push_back(st);

    // 18. Pyramid
    SolitaireGameDef pyr{};
    pyr.id = "pyramid";
    pyr.name = "Pyramid";
    pyr.family = "Pyramid";
    pyr.category = "ADDING";
    pyr.layout = "pyramid";
    pyr.description = "Legendary pairing solitaire. Match exposed cards summing to 13 (A+Q, 2+J, etc.); Kings clear alone.";
    pyr.decks = 1;
    pyr.tableaus = 7;
    pyr.foundations = 1;
    pyr.freecells = 0;
    pyr.tableauDealCounts = {1, 2, 3, 4, 5, 6, 7};
    pyr.tableauAllFaceUp = true;
    pyr.stockDealMode = "waste_draw_1";
    pyr.stockDealCount = 1;
    pyr.maxRedeals = 2;
    pyr.nativeVariant = GameVariant::CustomScript;
    games_.push_back(pyr);

    // 19. Giza
    SolitaireGameDef gz{};
    gz.id = "giza";
    gz.name = "Giza";
    gz.family = "Pyramid";
    gz.category = "ADDING";
    gz.layout = "pyramid";
    gz.description = "Open Pyramid variant by Michael Keller. All 52 cards are face up; match pairs summing to 13.";
    gz.decks = 1;
    gz.tableaus = 7;
    gz.foundations = 1;
    gz.freecells = 8;
    gz.tableauDealCounts = {1, 2, 3, 4, 5, 6, 7};
    gz.tableauAllFaceUp = true;
    gz.stockDealMode = "none";
    gz.maxRedeals = 0;
    gz.nativeVariant = GameVariant::CustomScript;
    games_.push_back(gz);

    // 20. Canfield
    SolitaireGameDef can{};
    can.id = "canfield";
    can.name = "Canfield";
    can.family = "Canfield";
    can.category = "POPULAR";
    can.layout = "canfield";
    can.description = "Famous Saratoga casino classic. 13-card reserve, wrapping alternating cascades, and foundation starter card.";
    can.decks = 1;
    can.tableaus = 4;
    can.foundations = 4;
    can.freecells = 1;
    can.tableauDealCounts = {1, 1, 1, 1};
    can.tableauAllFaceUp = true;
    can.foundationStarterCard = true;
    can.stockDealMode = "waste_draw_3";
    can.stockDealCount = 3;
    can.maxRedeals = -1;
    can.nativeVariant = GameVariant::CustomScript;
    games_.push_back(can);

    // 21. Rainbow
    SolitaireGameDef rb = can;
    rb.id = "rainbow";
    rb.name = "Rainbow";
    rb.description = "Strict Canfield variant. 13-card reserve, same-suit building, and single-card draws to waste.";
    rb.stockDealMode = "waste_draw_1";
    rb.stockDealCount = 1;
    games_.push_back(rb);

    // 22. The Fan
    SolitaireGameDef fan{};
    fan.id = "the_fan";
    fan.name = "The Fan";
    fan.family = "Fan";
    fan.category = "FREECELL";
    fan.layout = "fan_grid";
    fan.description = "18 fans of cards. Build down in suit; Kings only can fill empty spaces; pure puzzle of skill.";
    fan.decks = 1;
    fan.tableaus = 18;
    fan.foundations = 4;
    fan.freecells = 0;
    fan.tableauDealCounts = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1};
    fan.tableauAllFaceUp = true;
    fan.stockDealMode = "none";
    fan.nativeVariant = GameVariant::CustomScript;
    games_.push_back(fan);

    // 23. La Belle Lucie
    SolitaireGameDef lbl = fan;
    lbl.id = "la_belle_lucie";
    lbl.name = "La Belle Lucie";
    lbl.description = "18 fans of cards. Build down in suit; only top cards can move; spaces cannot be filled; redeals allowed.";
    lbl.maxRedeals = 2;
    games_.push_back(lbl);
}

namespace {
void inferClassificationDefaults(SolitaireGameDef& entry);
}

void GameRegistry::initialize(const std::string& gamesDir) {
    if (initialized_) return;
    registerBuiltins();
    for (auto& g : games_) {
        inferClassificationDefaults(g);
    }
    scanDirectory(gamesDir);
    initialized_ = true;
}

void GameRegistry::scanDirectory(const std::string& gamesDir) {
    std::error_code ec;
    const std::vector<std::string> searchPaths = {
        gamesDir,
        "../" + gamesDir,
        "../../" + gamesDir,
        "../../../" + gamesDir,
        "./" + gamesDir,
        "/" + gamesDir
    };

    std::string targetDir;
    for (const auto& sp : searchPaths) {
        if (std::filesystem::exists(sp, ec) && std::filesystem::is_directory(sp, ec)) {
            targetDir = sp;
            break;
        }
    }
    if (targetDir.empty()) return;

    for (const auto& entry : std::filesystem::directory_iterator(targetDir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const auto ext = entry.path().extension().string();
        if (ext == ".eats") {
            EatsLoadResult pkgRes;
            if (EatsPackageLoader::loadFile(entry.path().string(), pkgRes) && !pkgRes.gameIds.empty()) {
                continue;
            }
            SolitaireGameDef def{};
            if (loadGameFile(entry.path().string(), def)) {
                registerGame(def);
            }
        }
    }
}

bool GameRegistry::loadGameFile(const std::string& filePath, SolitaireGameDef& outDef) {
    std::ifstream f(filePath);
    if (!f.is_open()) return false;
    std::stringstream buf;
    buf << f.rdbuf();
    const std::string source = buf.str();

    eatsbits::eatscript::Evaluator ev;
    auto noop = [](const std::vector<eatsbits::eatscript::Value>&, const std::map<std::string, eatsbits::eatscript::Value>&) {
        return eatsbits::eatscript::Value(true);
    };
    ev.registerFunction("doc_goal", noop);
    ev.registerFunction("doc_victory", noop);
    ev.registerFunction("doc_victory_anim", noop);
    ev.registerFunction("victory_clear_stages", noop);
    ev.registerFunction("victory_add_stage", noop);
    ev.registerFunction("doc_zone", noop);
    ev.registerFunction("doc_stat", noop);
    ev.registerFunction("doc_tag", noop);
    ev.registerFunction("doc_rule", noop);
    ev.registerFunction("doc_meta", noop);
    ev.registerFunction("doc_aliases", noop);
    ev.registerFunction("doc_video", noop);
    ev.evaluateSource(source);

    if (!ev.hasVariable("name")) {
        // Fallback line-by-line metadata scanner for robust parsing of all .eats formats
        std::istringstream stream(source);
        std::string line;
        std::map<std::string, std::string> kv;
        while (std::getline(stream, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos || line[start] == '#') continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(start, eq - start);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            std::string val = line.substr(eq + 1);
            size_t valStart = val.find_first_not_of(" \t\r\n");
            if (valStart != std::string::npos) val = val.substr(valStart);
            size_t hash = val.find('#');
            if (hash != std::string::npos) val = val.substr(0, hash);
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r' || val.back() == '\n')) val.pop_back();
            if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\''))) {
                val = val.substr(1, val.size() - 2);
            }
            kv[key] = val;
        }
        if (kv.find("name") == kv.end()) {
            return false;
        }
        outDef.name = kv["name"];
        outDef.id = deriveGameId(filePath, outDef.name);
        outDef.family = kv.count("family") ? kv["family"] : outDef.name;
        outDef.category = kv.count("category") ? kv["category"] : outDef.family;
        outDef.layout = kv.count("layout") ? kv["layout"] : "";
        outDef.description = kv.count("description") ? kv["description"] : "";
        outDef.decks = static_cast<uint8_t>(kv.count("decks") ? std::clamp(std::stoi(kv["decks"]), 1, 4) : 1);
        outDef.excludeRedRoyals = kv.count("exclude_red_royals") && (kv["exclude_red_royals"] == "True" || kv["exclude_red_royals"] == "true" || kv["exclude_red_royals"] == "1");
        outDef.deckCards = static_cast<uint16_t>(kv.count("deck_cards") ? std::clamp(std::stoi(kv["deck_cards"]), 0, 416) : (outDef.excludeRedRoyals ? outDef.decks * 44 : 0));
        outDef.suitCount = static_cast<uint8_t>(kv.count("suit_count") ? std::clamp(std::stoi(kv["suit_count"]), 1, 4) : 4);
        outDef.jokers = static_cast<uint8_t>(kv.count("jokers") ? std::clamp(std::stoi(kv["jokers"]), 0, 2) : 0);
        outDef.gridCols = static_cast<uint8_t>(kv.count("grid_cols") ? std::clamp(std::stoi(kv["grid_cols"]), 0, 20) : 0);
        if (kv.count("supports_solver")) {
            outDef.supportsSolver = (kv["supports_solver"] == "True" || kv["supports_solver"] == "true" || kv["supports_solver"] == "1");
        }
        if (kv.count("allow_undo")) {
            outDef.allowUndo = (kv["allow_undo"] == "True" || kv["allow_undo"] == "true" || kv["allow_undo"] == "1");
        }
        outDef.tableaus = static_cast<uint8_t>(kv.count("tableaus") ? std::clamp(std::stoi(kv["tableaus"]), 1, 104) : 7);
        outDef.foundations = static_cast<uint8_t>(kv.count("foundations") ? std::clamp(std::stoi(kv["foundations"]), 1, 8) : 4);
        outDef.freecells = static_cast<uint8_t>(kv.count("freecells") ? std::clamp(std::stoi(kv["freecells"]), 0, 8) : 0);
        outDef.tableauAllFaceUp = kv.count("tableau_all_face_up") && (kv["tableau_all_face_up"] == "True" || kv["tableau_all_face_up"] == "true" || kv["tableau_all_face_up"] == "1");
        outDef.foundationStarterCard = kv.count("foundation_starter_card") && (kv["foundation_starter_card"] == "True" || kv["foundation_starter_card"] == "true" || kv["foundation_starter_card"] == "1");
        outDef.stockDealMode = kv.count("stock_deal_mode") ? kv["stock_deal_mode"] : "waste_draw_1";
        outDef.stockDealCount = kv.count("stock_deal_count") ? std::stoi(kv["stock_deal_count"]) : 1;
        outDef.maxRedeals = kv.count("max_redeals") ? std::stoi(kv["max_redeals"]) : 0;
        outDef.tableauDealCounts.assign(outDef.tableaus, 5);
        outDef.filePath = filePath;
        outDef.isScripted = true;
        if (kv.count("default_deck")) outDef.defaultDeckSlug = kv["default_deck"];
        if (kv.count("default_theme")) outDef.defaultTableThemeSlug = kv["default_theme"];
        if (kv.count("default_postfx")) outDef.defaultPostFxId = kv["default_postfx"];
        if (kv.count("default_ambience")) outDef.defaultAmbienceId = kv["default_ambience"];
        if (ev.hasVariable("demo_seed")) {
            outDef.demoSeed = static_cast<uint32_t>(std::max(1, ev.getVariable("demo_seed").asInt(2026)));
        }
        if (ev.hasVariable("demo_seeds")) {
            auto ds = ev.getVariable("demo_seeds");
            if (ds.isList()) {
                for (const auto& v : ds.listVal) {
                    int s = v.asInt(0);
                    if (s > 0) outDef.demoSeeds.push_back(static_cast<uint32_t>(s));
                }
                if (!outDef.demoSeeds.empty() && !ev.hasVariable("demo_seed")) {
                    outDef.demoSeed = outDef.demoSeeds.front();
                }
            }
        }
        if (ev.hasVariable("alternate_names")) {
            auto alts = ev.getVariable("alternate_names");
            if (alts.isList()) {
                for (const auto& a : alts.listVal) {
                    if (a.isString() && !a.asString().empty()) {
                        outDef.alternateNames.push_back(a.asString());
                    }
                }
            }
        }
    } else {
        outDef.name = ev.getVariable("name").asString();
        outDef.id = deriveGameId(filePath, outDef.name);

        outDef.family = ev.hasVariable("family") ? ev.getVariable("family").asString() : outDef.name;
        outDef.category = ev.hasVariable("category") ? ev.getVariable("category").asString() : outDef.family;
        if (ev.hasVariable("mechanic")) outDef.mechanic = toLowerStr(ev.getVariable("mechanic").asString());
        if (ev.hasVariable("visibility")) outDef.visibility = toLowerStr(ev.getVariable("visibility").asString());
        if (ev.hasVariable("tags")) {
            auto tv = ev.getVariable("tags");
            if (tv.isList()) {
                for (const auto& t : tv.listVal) {
                    std::string ts = toLowerStr(t.asString());
                    if (!ts.empty()) outDef.tags.push_back(std::move(ts));
                }
            }
        }
        outDef.layout = ev.hasVariable("layout") ? ev.getVariable("layout").asString() : "";
        outDef.wrapRanks = ev.getVariable("wrap_ranks").asBoolean(false);
        outDef.kingDeadEnd = ev.getVariable("king_dead_end").asBoolean(false);
        if (ev.hasVariable("tableau_build")) outDef.tableauBuild = toLowerStr(ev.getVariable("tableau_build").asString());
        if (ev.hasVariable("empty_tableau")) outDef.emptyTableau = toLowerStr(ev.getVariable("empty_tableau").asString());
        if (ev.hasVariable("move_group")) outDef.moveGroup = toLowerStr(ev.getVariable("move_group").asString());
        if (ev.hasVariable("foundation_mode")) outDef.foundationMode = toLowerStr(ev.getVariable("foundation_mode").asString());
        if (ev.hasVariable("ribbon_rule")) outDef.ribbonRule = toLowerStr(ev.getVariable("ribbon_rule").asString());
        outDef.description = ev.hasVariable("description") ? ev.getVariable("description").asString() : "";
        outDef.decks = static_cast<uint8_t>(std::clamp(ev.getVariable("decks").asInt(1), 1, 4));
        outDef.excludeRedRoyals = ev.getVariable("exclude_red_royals").asBoolean(false);
        outDef.deckCards = static_cast<uint16_t>(std::clamp(ev.getVariable("deck_cards").asInt(outDef.excludeRedRoyals ? outDef.decks * 44 : 0), 0, 416));
        outDef.suitCount = static_cast<uint8_t>(std::clamp(ev.getVariable("suit_count").asInt(4), 1, 4));
        outDef.jokers = static_cast<uint8_t>(std::clamp(ev.getVariable("jokers").asInt(0), 0, 2));
        outDef.gridCols = static_cast<uint8_t>(std::clamp(ev.getVariable("grid_cols").asInt(0), 0, 20));
        if (ev.hasVariable("supports_solver")) {
            outDef.supportsSolver = ev.getVariable("supports_solver").asBoolean(true);
        }
        if (ev.hasVariable("prefer_curated_solvable")) {
            outDef.preferCuratedSolvable = ev.getVariable("prefer_curated_solvable").asBoolean(false);
        }
        if (ev.hasVariable("dynamic_layout")) {
            outDef.dynamicLayout = ev.getVariable("dynamic_layout").asBoolean(false);
        }
        if (ev.hasVariable("allow_undo")) {
            outDef.allowUndo = ev.getVariable("allow_undo").asBoolean(true);
        }
        if (ev.hasVariable("exclude_ranks")) {
            auto er = ev.getVariable("exclude_ranks");
            if (er.isList()) {
                for (const auto& v : er.listVal) {
                    int r = v.asInt(0);
                    if (r >= 1 && r <= 13) outDef.excludeRanks.push_back(static_cast<uint8_t>(r));
                }
            }
        }
        outDef.tableaus = static_cast<uint8_t>(std::clamp(ev.getVariable("tableaus").asInt(7), 1, 104));
        outDef.foundations = static_cast<uint8_t>(std::clamp(ev.getVariable("foundations").asInt(4), 1, 8));
        outDef.freecells = static_cast<uint8_t>(std::clamp(ev.getVariable("freecells").asInt(0), 0, 8));

        if (ev.hasVariable("tableau_deal_counts")) {
            auto val = ev.getVariable("tableau_deal_counts");
            if (val.isList()) {
                outDef.tableauDealCounts.clear();
                for (const auto& c : val.listVal) {
                    outDef.tableauDealCounts.push_back(static_cast<uint8_t>(c.asInt(5)));
                }
            }
        }
        if (outDef.tableauDealCounts.empty()) {
            outDef.tableauDealCounts.assign(outDef.tableaus, 5);
        }

        outDef.tableauAllFaceUp = ev.getVariable("tableau_all_face_up").asBoolean(false);
        outDef.foundationStarterCard = ev.getVariable("foundation_starter_card").asBoolean(false);
        outDef.stockDealMode = ev.getVariable("stock_deal_mode").asString();
        if (outDef.stockDealMode.empty()) outDef.stockDealMode = "waste_draw_1";
        outDef.stockDealCount = ev.getVariable("stock_deal_count").asInt(1);
        outDef.maxRedeals = ev.getVariable("max_redeals").asInt(0);
        outDef.filePath = filePath;
        outDef.isScripted = true;
        if (ev.hasVariable("default_deck")) outDef.defaultDeckSlug = ev.getVariable("default_deck").asString();
        if (ev.hasVariable("default_theme")) outDef.defaultTableThemeSlug = ev.getVariable("default_theme").asString();
        if (ev.hasVariable("default_postfx")) outDef.defaultPostFxId = ev.getVariable("default_postfx").asString();
        if (ev.hasVariable("default_ambience")) outDef.defaultAmbienceId = ev.getVariable("default_ambience").asString();
        if (ev.hasVariable("demo_seed")) {
            outDef.demoSeed = static_cast<uint32_t>(std::max(1, ev.getVariable("demo_seed").asInt(2026)));
        }
        if (ev.hasVariable("demo_seeds")) {
            auto ds = ev.getVariable("demo_seeds");
            if (ds.isList()) {
                for (const auto& v : ds.listVal) {
                    int s = v.asInt(0);
                    if (s > 0) outDef.demoSeeds.push_back(static_cast<uint32_t>(s));
                }
                if (!outDef.demoSeeds.empty() && !ev.hasVariable("demo_seed")) {
                    outDef.demoSeed = outDef.demoSeeds.front();
                }
            }
        }
        if (ev.hasVariable("alternate_names")) {
            auto alts = ev.getVariable("alternate_names");
            if (alts.isList()) {
                for (const auto& a : alts.listVal) {
                    if (a.isString() && !a.asString().empty()) {
                        outDef.alternateNames.push_back(a.asString());
                    }
                }
            }
        }
    }

    // Match to native variant if applicable
    std::string famLower = toLowerStr(outDef.family);
    std::string nameLower = toLowerStr(outDef.name);

    if (outDef.layout.empty() || outDef.layout == "columns") {
        if (famLower == "pyramid" || nameLower.find("pyramid") != std::string::npos || outDef.id == "giza") {
            outDef.layout = "pyramid";
        } else if (famLower == "canfield" || outDef.id == "canfield" || outDef.id == "rainbow") {
            outDef.layout = "canfield";
        } else if (famLower == "fan" || outDef.id == "the_fan" || outDef.id == "la_belle_lucie" || outDef.tableaus > 12) {
            outDef.layout = "fan_grid";
        } else if (famLower == "clock" || nameLower.find("clock") != std::string::npos) {
            outDef.layout = "radial_clock";
        } else if (outDef.id == "tri_peaks" || outDef.id == "tripeaks") {
            outDef.layout = "tripeaks";
        } else {
            outDef.layout = "columns";
        }
    }
    if (nameLower.find("golf") != std::string::npos || famLower.find("golf") != std::string::npos) {
        outDef.nativeVariant = GameVariant::Golf;
    } else if (nameLower.find("spider") != std::string::npos) {
        if (nameLower.find("1") != std::string::npos) outDef.nativeVariant = GameVariant::Spider1Suit;
        else if (nameLower.find("2") != std::string::npos) outDef.nativeVariant = GameVariant::Spider2Suits;
        else if (nameLower.find("3") != std::string::npos) outDef.nativeVariant = GameVariant::Spider3Suits;
        else outDef.nativeVariant = GameVariant::Spider4Suits;
    } else if (nameLower.find("freecell") != std::string::npos) {
        outDef.nativeVariant = GameVariant::FreeCell;
    } else if (nameLower.find("draw 3") != std::string::npos) {
        outDef.nativeVariant = GameVariant::KlondikeDraw3;
    } else if (nameLower.find("draw 1") != std::string::npos) {
        outDef.nativeVariant = GameVariant::KlondikeDraw1;
    } else {
        outDef.nativeVariant = GameVariant::CustomScript;
    }

    return true;
}

namespace {
void inferClassificationDefaults(SolitaireGameDef& entry) {
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string fam = lower(entry.family);
    const std::string cat = lower(entry.category);
    const std::string lay = lower(entry.layout);

    if (entry.mechanic.empty()) {
        if (lay == "pyramid" || fam == "pyramid" || cat == "adding") {
            entry.mechanic = "pairing";
        } else if (fam == "golf" || cat == "golf" || lay == "tripeaks" || lay == "radial_clock") {
            entry.mechanic = "clearing";
        } else if (fam == "memory" || cat == "matching") {
            entry.mechanic = "matching";
        } else if (fam == "scoundrel" || cat == "rpg") {
            entry.mechanic = "rpg";
        } else {
            entry.mechanic = "building";
        }
    }
    if (entry.visibility.empty()) {
        if (entry.mechanic == "matching" || entry.mechanic == "rpg") {
            entry.visibility = "closed";
        } else if (entry.tableauAllFaceUp && entry.stockDealMode == "none") {
            entry.visibility = "open";
        } else {
            entry.visibility = "half_open";
        }
    }
    if (entry.mechanic == "clearing" && !entry.wrapRanks && !entry.kingDeadEnd) {
        if (lay == "tripeaks" || lay == "radial_clock" || entry.id == "black_hole") {
            entry.wrapRanks = true;
        } else if (entry.id == "golf") {
            entry.kingDeadEnd = true;
        }
    }
    if (entry.foundationMode.empty()) {
        if (fam == "spider" || entry.id == "spiderette" || entry.id == "scorpion" || entry.id == "wasp") {
            entry.foundationMode = "completed_run";
        } else if (lay == "canfield" || fam == "canfield") {
            entry.foundationMode = "canfield_wrap";
        } else {
            entry.foundationMode = "suit_up";
        }
    }
    if (entry.tableauBuild.empty()) {
        if (entry.mechanic != "building") {
            entry.tableauBuild = "none";
        } else if (entry.foundationMode == "completed_run" && entry.id != "scorpion" && entry.id != "wasp") {
            entry.tableauBuild = "any_suit";
        } else if (entry.id == "bakers_dozen" || entry.id == "rainbow") {
            entry.tableauBuild = "any_suit";
        } else if (entry.id == "bakers_game" || entry.id == "eight_off" || entry.id == "seahaven_towers" ||
                   entry.id == "la_belle_lucie" || entry.id == "the_fan" || entry.id == "russian_solitaire" ||
                   fam == "fortythieves" || entry.id == "scorpion" || entry.id == "wasp") {
            entry.tableauBuild = "same_suit";
        } else {
            entry.tableauBuild = "alt_color";
        }
    }
    if (entry.emptyTableau.empty()) {
        if (entry.mechanic != "building") {
            entry.emptyTableau = "none";
        } else if (entry.id == "bakers_dozen" || entry.id == "la_belle_lucie") {
            entry.emptyTableau = "none";
        } else if (entry.id == "freecell" || entry.id == "bakers_game" || fam == "fortythieves" ||
                   entry.id == "wasp" || entry.id == "easthaven" ||
                   (entry.foundationMode == "completed_run" && entry.id != "scorpion")) {
            entry.emptyTableau = "any";
        } else {
            entry.emptyTableau = "king";
        }
    }
    if (entry.moveGroup.empty()) {
        if (entry.mechanic != "building") {
            entry.moveGroup = "single";
        } else if (entry.foundationMode == "canfield_wrap") {
            entry.moveGroup = "whole_column";
        } else if (fam == "yukon" || entry.id == "scorpion" || entry.id == "wasp") {
            entry.moveGroup = "any_face_up";
        } else if (fam == "fortythieves" || fam == "fan" || lay == "fan_grid" || entry.id == "bakers_dozen") {
            entry.moveGroup = "single";
        } else {
            entry.moveGroup = "ordered";
        }
    }
}
} // namespace

void GameRegistry::registerGame(const SolitaireGameDef& def) {
    SolitaireGameDef entry = def;
    inferClassificationDefaults(entry);
    for (const auto& s : series_) {
        for (size_t idx = 0; idx < s.gameIds.size(); ++idx) {
            if (s.gameIds[idx] == entry.id) {
                if (entry.seriesId.empty()) entry.seriesId = s.id;
                if (entry.seriesName.empty()) entry.seriesName = s.name;
                if (entry.seriesOrder == 0) entry.seriesOrder = static_cast<int32_t>(idx + 1);
                if (entry.defaultPresetId.empty() && !s.presetId.empty()) entry.defaultPresetId = s.presetId;
                break;
            }
        }
    }
    for (auto& g : games_) {
        if ((!entry.id.empty() && g.id == entry.id) || (entry.id.empty() && g.name == entry.name)) {
            if (entry.nativeVariant == GameVariant::CustomScript && g.nativeVariant != GameVariant::CustomScript) {
                entry.nativeVariant = g.nativeVariant;
            }
            g = entry;
            return;
        }
    }
    games_.push_back(std::move(entry));
}

void GameRegistry::registerSeries(const GameSeriesDef& series) {
    if (series.id.empty()) return;
    bool updated = false;
    for (auto& s : series_) {
        if (s.id == series.id) {
            s = series;
            updated = true;
            break;
        }
    }
    if (!updated) {
        series_.push_back(series);
    }
    for (size_t idx = 0; idx < series.gameIds.size(); ++idx) {
        for (auto& g : games_) {
            if (g.id == series.gameIds[idx]) {
                if (g.seriesId.empty()) g.seriesId = series.id;
                if (g.seriesName.empty()) g.seriesName = series.name;
                if (g.seriesOrder == 0) g.seriesOrder = static_cast<int32_t>(idx + 1);
                if (g.defaultPresetId.empty() && !series.presetId.empty()) g.defaultPresetId = series.presetId;
            }
        }
    }
}

const GameSeriesDef* GameRegistry::findSeriesById(const std::string& id) const noexcept {
    for (const auto& s : series_) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

const SolitaireGameDef* GameRegistry::findById(const std::string& id) const noexcept {
    for (const auto& g : games_) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

const SolitaireGameDef* GameRegistry::findByName(const std::string& name) const noexcept {
    for (const auto& g : games_) {
        if (g.name == name) return &g;
    }
    return nullptr;
}

const SolitaireGameDef* GameRegistry::findByVariant(GameVariant v) const noexcept {
    for (const auto& g : games_) {
        if (g.nativeVariant == v) return &g;
    }
    return games_.empty() ? nullptr : &games_[0];
}

std::vector<SolitaireGameDef> GameRegistry::search(
    const std::string& query,
    const std::string& category,
    const std::string& mechanic,
    const std::string& visibility
) const {
    std::vector<SolitaireGameDef> results;
    std::string q = toLowerStr(query);
    std::string cat = toLowerStr(category);
    std::string mech = toLowerStr(mechanic);
    std::string vis = toLowerStr(visibility);

    for (const auto& g : games_) {
        if (cat == "favorites") {
#if !defined(SOLENGINE_HEADLESS_CLI)
            if (!SettingsManager::instance().isFavorite(g.id)) continue;
#else
            continue;
#endif
        } else if (cat == "series") {
            if (g.seriesId.empty()) continue;
        } else if (cat != "all" && !cat.empty()) {
            std::string gCat = toLowerStr(g.category);
            std::string gFam = toLowerStr(g.family);
            std::string gSer = toLowerStr(g.seriesName);
            if (gCat != cat && gFam != cat && gSer != cat && toLowerStr(g.seriesId) != cat) continue;
        }
        if (mech != "all" && !mech.empty() && toLowerStr(g.mechanic) != mech) {
            continue;
        }
        if (vis != "all" && vis != "any" && !vis.empty() && toLowerStr(g.visibility) != vis) {
            continue;
        }
        if (q.empty()) {
            results.push_back(g);
            continue;
        }
        bool matched =
            toLowerStr(g.name).find(q) != std::string::npos ||
            toLowerStr(g.family).find(q) != std::string::npos ||
            toLowerStr(g.seriesName).find(q) != std::string::npos ||
            toLowerStr(g.mechanic).find(q) != std::string::npos ||
            toLowerStr(g.visibility).find(q) != std::string::npos ||
            toLowerStr(g.description).find(q) != std::string::npos;
        if (!matched) {
            for (const auto& alt : g.alternateNames) {
                if (toLowerStr(alt).find(q) != std::string::npos) { matched = true; break; }
            }
        }
        if (!matched) {
            for (const auto& tag : g.tags) {
                if (toLowerStr(tag).find(q) != std::string::npos) { matched = true; break; }
            }
        }
        if (matched) {
            results.push_back(g);
        }
    }
    return results;
}

std::vector<std::string> GameRegistry::categories() const {
    std::vector<std::string> cats;
    cats.push_back("ALL");
    cats.push_back("FAVORITES");
    if (!series_.empty()) {
        cats.push_back("SERIES");
    }
    cats.push_back("POPULAR");
    cats.push_back("SPIDER");
    cats.push_back("GOLF");
    cats.push_back("FREECELL");
    cats.push_back("KLONDIKE");
    cats.push_back("ADDING");

    for (const auto& g : games_) {
        std::string c = g.category;
        for (char& ch : c) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (std::find(cats.begin(), cats.end(), c) == cats.end()) {
            cats.push_back(c);
        }
    }
    return cats;
}

} // namespace solengine
