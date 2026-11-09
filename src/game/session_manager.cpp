#include "solengine/session_manager.hpp"
#include "solengine/solitaire_board.hpp"
#include "solengine/json_parser.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace solengine {

SessionManager& SessionManager::instance() {
    static SessionManager sInstance;
    return sInstance;
}

std::string SessionManager::getSessionFilePath() const {
#if defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    if (appData && *appData) {
        std::filesystem::path p(appData);
        std::filesystem::path newP = p / "SolEngine" / "session.json";
        std::filesystem::path oldP = p / "SolEnshaded" / "session.json";
        if (!std::filesystem::exists(newP) && std::filesystem::exists(oldP)) {
            return oldP.string();
        }
        return newP.string();
    }
#elif !defined(__EMSCRIPTEN__)
    const char* home = std::getenv("HOME");
    if (home && *home) {
        std::filesystem::path p(home);
        std::filesystem::path newP = p / ".config" / "solengine" / "session.json";
        std::filesystem::path oldP = p / ".config" / "solenshaded" / "session.json";
        if (!std::filesystem::exists(newP) && std::filesystem::exists(oldP)) {
            return oldP.string();
        }
        return newP.string();
    }
#endif
    return "session.json";
}

bool SessionManager::hasSavedSession() const {
#if defined(__EMSCRIPTEN__)
    int exists = MAIN_THREAD_EM_ASM_INT({
        try {
            var s = localStorage.getItem("solengine_session") || localStorage.getItem("solenshaded_session");
            return (s && s.length > 10) ? 1 : 0;
        } catch (e) {
            return 0;
        }
    });
    return exists != 0;
#else
    std::string path = getSessionFilePath();
    return std::filesystem::exists(path);
#endif
}

void SessionManager::clearSession() {
#if defined(__EMSCRIPTEN__)
    MAIN_THREAD_EM_ASM({
        try {
            localStorage.removeItem("solengine_session"); localStorage.removeItem("solenshaded_session");
        } catch (e) {}
    });
#else
    std::string path = getSessionFilePath();
    try {
        if (std::filesystem::exists(path)) {
            std::filesystem::remove(path);
        }
    } catch (...) {}
#endif
}

bool SessionManager::saveSession(const SolitaireBoard& board) {
    if (board.isVictory()) {
        clearSession();
        return true;
    }

    json::Object root;
    root["version"] = 1;
    root["seed"] = static_cast<double>(board.seed());
    root["rngState"] = static_cast<double>(board.rngState());
    root["variant"] = static_cast<int>(board.variant());
    root["score"] = static_cast<double>(board.score());
    root["moveCount"] = static_cast<double>(board.moveCount());
    root["elapsedSeconds"] = static_cast<double>(board.elapsedSeconds());

    // Serialize piles
    json::Array pilesArr;
    for (const auto& pile : board.piles()) {
        json::Object pObj;
        pObj["type"] = static_cast<int>(pile.type);
        pObj["index"] = static_cast<int>(pile.index);

        json::Array cardsArr;
        for (const auto& c : pile.cards) {
            json::Object cObj;
            cObj["id"] = static_cast<int>(c.id);
            cObj["suit"] = static_cast<int>(c.suit);
            cObj["rank"] = static_cast<int>(c.rank);
            cObj["faceUp"] = c.faceUp;
            cardsArr.push_back(json::Value{cObj});
        }
        pObj["cards"] = cardsArr;
        pilesArr.push_back(json::Value{pObj});
    }
    root["piles"] = pilesArr;

    // Serialize undo stack
    json::Array undoArr;
    for (const auto& cmd : board.undoStack()) {
        json::Object cmdObj;
        cmdObj["fromPile"] = static_cast<int>(cmd.fromPile);
        cmdObj["toPile"] = static_cast<int>(cmd.toPile);
        cmdObj["cardCount"] = static_cast<int>(cmd.cardCount);
        cmdObj["flippedSourceTop"] = cmd.flippedSourceTop;
        cmdObj["recycledWaste"] = cmd.recycledWaste;
        cmdObj["scoreDelta"] = static_cast<double>(cmd.scoreDelta);
        undoArr.push_back(json::Value{cmdObj});
    }
    root["undoStack"] = undoArr;

    // Serialize redo stack
    json::Array redoArr;
    for (const auto& cmd : board.redoStack()) {
        json::Object cmdObj;
        cmdObj["fromPile"] = static_cast<int>(cmd.fromPile);
        cmdObj["toPile"] = static_cast<int>(cmd.toPile);
        cmdObj["cardCount"] = static_cast<int>(cmd.cardCount);
        cmdObj["flippedSourceTop"] = cmd.flippedSourceTop;
        cmdObj["recycledWaste"] = cmd.recycledWaste;
        cmdObj["scoreDelta"] = static_cast<double>(cmd.scoreDelta);
        redoArr.push_back(json::Value{cmdObj});
    }
    root["redoStack"] = redoArr;

    std::string jsonStr = json::stringify(json::Value{root});

#if defined(__EMSCRIPTEN__)
    MAIN_THREAD_EM_ASM({
        try {
            localStorage.setItem("solengine_session", UTF8ToString($0));
        } catch (e) {
            console.error("Failed to save session to localStorage:", e);
        }
    }, jsonStr.c_str());
    return true;
#else
    std::string path = getSessionFilePath();
    try {
        std::filesystem::path p(path);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
        std::ofstream ofs(path);
        if (ofs.is_open()) {
            ofs << jsonStr;
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[SolEngine] Failed to write session to " << path << ": " << e.what() << "\n";
    }
    return false;
#endif
}

bool SessionManager::loadSession(SolitaireBoard& board) {
    std::string content;
#if defined(__EMSCRIPTEN__)
    char* jsVal = reinterpret_cast<char*>(MAIN_THREAD_EM_ASM_PTR({
        try {
            var s = localStorage.getItem("solengine_session") || localStorage.getItem("solenshaded_session");
            if (!s) return 0;
            var len = lengthBytesUTF8(s) + 1;
            var buf = _malloc(len);
            stringToUTF8(s, buf, len);
            return buf;
        } catch (e) {
            return 0;
        }
    }));
    if (jsVal) {
        content = jsVal;
        free(jsVal);
    }
#else
    std::string path = getSessionFilePath();
    try {
        std::ifstream ifs(path);
        if (ifs.is_open()) {
            std::stringstream ss;
            ss << ifs.rdbuf();
            content = ss.str();
        }
    } catch (...) {}
#endif

    if (content.empty()) return false;

    try {
        json::Value root = json::parse(content);
        if (!root.isObject() || !root.contains("piles") || !root["piles"].isArray()) {
            return false;
        }

        uint32_t seed = root["seed"].asUInt(2026u);
        uint32_t rngState = root["rngState"].asUInt(seed);
        GameVariant variant = static_cast<GameVariant>(std::clamp(root["variant"].asInt(0), 0, 7));
        int32_t score = root["score"].asInt(0);
        uint32_t moveCount = root["moveCount"].asUInt(0u);
        float elapsedSeconds = root["elapsedSeconds"].asFloat(0.0f);

        std::vector<Pile> piles;
        const auto& pilesArr = root["piles"].asArray();
        piles.reserve(pilesArr.size());

        for (const auto& pVal : pilesArr) {
            if (!pVal.isObject()) continue;
            Pile pile{};
            pile.type = static_cast<PileType>(pVal["type"].asInt(3));
            pile.index = static_cast<uint8_t>(pVal["index"].asInt(0));

            if (pVal.contains("cards") && pVal["cards"].isArray()) {
                const auto& cardsArr = pVal["cards"].asArray();
                pile.cards.reserve(cardsArr.size());
                for (const auto& cVal : cardsArr) {
                    if (!cVal.isObject()) continue;
                    Card c{};
                    c.id = static_cast<uint8_t>(cVal["id"].asInt(0));
                    c.suit = static_cast<Suit>(cVal["suit"].asInt(0));
                    c.rank = static_cast<Rank>(cVal["rank"].asInt(1));
                    c.faceUp = cVal["faceUp"].asBool(false);
                    pile.cards.push_back(c);
                }
            }
            piles.push_back(std::move(pile));
        }

        std::vector<MoveCommand> undoStack;
        if (root.contains("undoStack") && root["undoStack"].isArray()) {
            const auto& arr = root["undoStack"].asArray();
            for (const auto& v : arr) {
                if (!v.isObject()) continue;
                MoveCommand cmd{};
                cmd.fromPile = static_cast<uint8_t>(v["fromPile"].asInt(0));
                cmd.toPile = static_cast<uint8_t>(v["toPile"].asInt(0));
                cmd.cardCount = static_cast<uint8_t>(v["cardCount"].asInt(1));
                cmd.flippedSourceTop = v["flippedSourceTop"].asBool(false);
                cmd.recycledWaste = v["recycledWaste"].asBool(false);
                cmd.scoreDelta = v["scoreDelta"].asInt(0);
                undoStack.push_back(cmd);
            }
        }

        std::vector<MoveCommand> redoStack;
        if (root.contains("redoStack") && root["redoStack"].isArray()) {
            const auto& arr = root["redoStack"].asArray();
            for (const auto& v : arr) {
                if (!v.isObject()) continue;
                MoveCommand cmd{};
                cmd.fromPile = static_cast<uint8_t>(v["fromPile"].asInt(0));
                cmd.toPile = static_cast<uint8_t>(v["toPile"].asInt(0));
                cmd.cardCount = static_cast<uint8_t>(v["cardCount"].asInt(1));
                cmd.flippedSourceTop = v["flippedSourceTop"].asBool(false);
                cmd.recycledWaste = v["recycledWaste"].asBool(false);
                cmd.scoreDelta = v["scoreDelta"].asInt(0);
                redoStack.push_back(cmd);
            }
        }

        bool ok = board.restoreSessionState(seed, rngState, variant, score, moveCount, elapsedSeconds, piles, undoStack, redoStack);
        if (ok) {
            std::cout << "[SolEngine] Successfully restored in-progress game session (Seed #" << seed << ", Moves: " << moveCount << ").\n";
        }
        return ok;
    } catch (const std::exception& e) {
        std::cerr << "[SolEngine] Error loading session: " << e.what() << "\n";
        return false;
    }
}

} // namespace solengine

