#include "solengine/game_definition.hpp"
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
    tp.id = "tripeaks";
    tp.name = "TriPeaks";
    tp.family = "Golf";
    tp.category = "GOLF";
    tp.description = "Fast-paced peak elimination. Stack cards +-1 rank onto foundation waste; streak bonuses.";
    tp.decks = 1;
    tp.tableaus = 7;
    tp.foundations = 1;
    tp.freecells = 0;
    tp.tableauDealCounts = {4, 4, 4, 4, 4, 4, 4};
    tp.tableauAllFaceUp = false;
    tp.foundationStarterCard = true;
    tp.stockDealMode = "deal_to_foundation";
    tp.stockDealCount = 1;
    tp.nativeVariant = GameVariant::Golf;
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

void GameRegistry::initialize(const std::string& gamesDir) {
    if (initialized_) return;
    registerBuiltins();
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
        outDef.id = toLowerStr(outDef.name);
        std::replace(outDef.id.begin(), outDef.id.end(), ' ', '_');
        outDef.family = kv.count("family") ? kv["family"] : outDef.name;
        outDef.category = kv.count("category") ? kv["category"] : outDef.family;
        outDef.layout = kv.count("layout") ? kv["layout"] : "";
        outDef.description = kv.count("description") ? kv["description"] : "";
        outDef.decks = static_cast<uint8_t>(kv.count("decks") ? std::clamp(std::stoi(kv["decks"]), 1, 4) : 1);
        outDef.tableaus = static_cast<uint8_t>(kv.count("tableaus") ? std::clamp(std::stoi(kv["tableaus"]), 1, 12) : 7);
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
    } else {
        outDef.name = ev.getVariable("name").asString();
        outDef.id = toLowerStr(outDef.name);
        std::replace(outDef.id.begin(), outDef.id.end(), ' ', '_');

        outDef.family = ev.hasVariable("family") ? ev.getVariable("family").asString() : outDef.name;
        outDef.category = ev.hasVariable("category") ? ev.getVariable("category").asString() : outDef.family;
        outDef.layout = ev.hasVariable("layout") ? ev.getVariable("layout").asString() : "";
        outDef.description = ev.hasVariable("description") ? ev.getVariable("description").asString() : "";
        outDef.decks = static_cast<uint8_t>(std::clamp(ev.getVariable("decks").asInt(1), 1, 4));
        outDef.tableaus = static_cast<uint8_t>(std::clamp(ev.getVariable("tableaus").asInt(7), 1, 12));
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
        } else if (outDef.id == "tripeaks") {
            outDef.layout = "tripeaks";
        } else {
            outDef.layout = "columns";
        }
    }
    if (nameLower.find("golf") != std::string::npos || famLower.find("golf") != std::string::npos) {
        outDef.nativeVariant = GameVariant::Golf;
    } else if (nameLower.find("spider") != std::string::npos || famLower.find("spider") != std::string::npos) {
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

void GameRegistry::registerGame(const SolitaireGameDef& def) {
    for (auto& g : games_) {
        if (g.id == def.id || g.name == def.name) {
            g = def;
            return;
        }
    }
    games_.push_back(def);
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

std::vector<SolitaireGameDef> GameRegistry::search(const std::string& query, const std::string& category) const {
    std::vector<SolitaireGameDef> results;
    std::string q = toLowerStr(query);
    std::string cat = toLowerStr(category);

    for (const auto& g : games_) {
        if (cat == "favorites") {
            if (!SettingsManager::instance().isFavorite(g.id)) continue;
        } else if (cat != "all" && !cat.empty()) {
            std::string gCat = toLowerStr(g.category);
            std::string gFam = toLowerStr(g.family);
            if (gCat != cat && gFam != cat) continue;
        }
        if (q.empty()) {
            results.push_back(g);
            continue;
        }
        if (toLowerStr(g.name).find(q) != std::string::npos ||
            toLowerStr(g.family).find(q) != std::string::npos ||
            toLowerStr(g.description).find(q) != std::string::npos) {
            results.push_back(g);
        }
    }
    return results;
}

std::vector<std::string> GameRegistry::categories() const {
    std::vector<std::string> cats;
    cats.push_back("ALL");
    cats.push_back("FAVORITES");
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
