#include "solengine/game_definition.hpp"
#include "solengine/solitaire_board.hpp"
#include "solengine/solitaire_solver.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace fs = std::filesystem;
using namespace solengine;

namespace {

struct VerifyReport {
    uint32_t seed{0};
    bool solved{false};
    bool replayOk{false};
    bool victoryOk{false};
    uint32_t statesExplored{0};
    float solveTimeMs{0.0f};
    size_t totalMoves{0};
    size_t executedMoves{0};
    std::string errorDetail;

    [[nodiscard]] bool passed() const noexcept {
        return solved && replayOk && victoryOk;
    }
};

bool checkTableauFaceUpOrder(const SolitaireBoard& board, std::string& outErr) {
    const auto& piles = board.piles();
    for (size_t pIdx = 0; pIdx < piles.size(); ++pIdx) {
        const auto& p = piles[pIdx];
        if (p.type != PileType::Tableau) continue;
        bool seenFaceUp = false;
        for (size_t cIdx = 0; cIdx < p.cards.size(); ++cIdx) {
            if (p.cards[cIdx].faceUp) {
                seenFaceUp = true;
            } else if (seenFaceUp) {
                outErr = "Face-down card above face-up card in pile " +
                         std::to_string(pIdx) + " at card index " + std::to_string(cIdx);
                return false;
            }
        }
    }
    return true;
}

VerifyReport verifyGameSeed(
    const SolitaireGameDef& def,
    uint32_t seed,
    uint32_t maxStates = 0,
    float timeoutMs = 2500.0f
) {
    VerifyReport rep{};
    rep.seed = seed;
    const uint32_t effectiveMaxStates = (maxStates > 0) ? maxStates : ((def.decks >= 2) ? 65000u : 25000u);

    SolitaireBoard board;
    board.newDeal(seed, def);

    std::string orderErr;
    if (!checkTableauFaceUpOrder(board, orderErr)) {
        rep.errorDetail = "Initial deal anomaly: " + orderErr;
        return rep;
    }

    // Mirror AutoPlayController::start(): copy board before solving
    SolitaireBoard boardCopy = board;
    SolitaireSolver solver;
    SolveResult res = solver.solve(boardCopy, effectiveMaxStates, timeoutMs, nullptr);

    rep.statesExplored = res.statesExplored;
    rep.solveTimeMs = res.solveTimeMs;
    rep.totalMoves = res.moves.size();

    if (!res.solved) {
        rep.errorDetail = res.timedOut ? "Timed out / state cap reached" : "Unsolvable";
        return rep;
    }
    if (res.moves.empty() && !board.isVictory()) {
        rep.errorDetail = "Solver reported solved with 0 moves on non-victory board";
        return rep;
    }
    rep.solved = true;

    // Step-by-step replay mirroring AutoPlayController::stepForward()
    for (size_t i = 0; i < res.moves.size(); ++i) {
        const auto& m = res.moves[i];
        bool ok = false;
        if (m.action == SolverMove::Action::DrawStock) {
            ok = board.drawFromStock();
        } else if (m.action == SolverMove::Action::PairPyramid) {
            ok = board.executePyramidPair(m.fromPile, m.secondFromPile, m.toPile);
        } else if (m.action == SolverMove::Action::ClickCard) {
            ok = board.smartMove(m.fromPile, m.cardIndex);
        } else {
            ok = board.executeMove(m.fromPile, m.cardIndex, m.toPile, true);
        }

        if (!ok) {
            rep.errorDetail = "Move " + std::to_string(i + 1) + "/" +
                              std::to_string(res.moves.size()) + " rejected (" +
                              std::to_string(m.fromPile) + "[" + std::to_string(m.cardIndex) +
                              "]->" + std::to_string(m.toPile) + " " + m.notation + ")";
            return rep;
        }

        ++rep.executedMoves;

        if (!checkTableauFaceUpOrder(board, orderErr)) {
            rep.errorDetail = "After move " + std::to_string(i + 1) + ": " + orderErr;
            return rep;
        }

        if (board.isVictory()) {
            break;
        }
    }

    rep.replayOk = true;
    rep.victoryOk = board.isVictory();
    if (!rep.victoryOk) {
        rep.errorDetail = "Replay finished all " + std::to_string(rep.executedMoves) +
                          " moves without reaching victory";
    }
    return rep;
}

std::vector<SolitaireGameDef> loadAllEatsGames(const std::string& gamesDir) {
    std::vector<SolitaireGameDef> defs;
    if (!fs::exists(gamesDir)) {
        return defs;
    }

    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(gamesDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".eats") {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());

    GameRegistry& reg = GameRegistry::instance();
    for (const auto& p : paths) {
        SolitaireGameDef def{};
        if (reg.loadGameFile(p.string(), def)) {
            defs.push_back(std::move(def));
        } else {
            std::cerr << "[WARN] Failed to parse " << p.string() << "\n";
        }
    }
    return defs;
}

std::string trimLeft(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

bool startsWithKey(const std::string& line, const std::string& key) {
    std::string t = trimLeft(line);
    if (t.rfind(key, 0) != 0) return false;
    size_t pos = key.size();
    while (pos < t.size() && (t[pos] == ' ' || t[pos] == '\t')) ++pos;
    return pos < t.size() && t[pos] == '=';
}

bool writeSeedsToEatsFile(const std::string& filePath, const std::vector<uint32_t>& seeds) {
    if (seeds.empty() || filePath.empty()) return false;

    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    const bool useCrlf = (content.find("\r\n") != std::string::npos);
    const std::string nl = useCrlf ? "\r\n" : "\n";

    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= content.size()) {
        size_t end = content.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(content.substr(start));
            break;
        }
        std::string line = content.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        start = end + 1;
        if (start == content.size()) {
            lines.emplace_back("");
            break;
        }
    }

    std::ostringstream listOss;
    listOss << "demo_seeds = [";
    for (size_t i = 0; i < seeds.size(); ++i) {
        if (i > 0) listOss << ", ";
        listOss << seeds[i];
    }
    listOss << "]";
    const std::string demoSeedLine = "demo_seed = " + std::to_string(seeds.front());
    const std::string demoSeedsLine = listOss.str();

    // Remove existing demo_seeds line if present so we place it cleanly right after demo_seed
    std::vector<std::string> filtered;
    filtered.reserve(lines.size() + 2);
    for (const auto& l : lines) {
        if (startsWithKey(l, "demo_seeds")) continue;
        filtered.push_back(l);
    }

    bool replacedDemoSeed = false;
    std::vector<std::string> outLines;
    outLines.reserve(filtered.size() + 2);
    for (const auto& l : filtered) {
        if (!replacedDemoSeed && startsWithKey(l, "demo_seed")) {
            outLines.push_back(demoSeedLine);
            outLines.push_back(demoSeedsLine);
            replacedDemoSeed = true;
        } else {
            outLines.push_back(l);
        }
    }

    if (!replacedDemoSeed) {
        // Insert after max_redeals or before first function / section header
        size_t insertIdx = outLines.size();
        for (size_t i = 0; i < outLines.size(); ++i) {
            if (startsWithKey(outLines[i], "max_redeals")) {
                insertIdx = i + 1;
                break;
            }
            std::string t = trimLeft(outLines[i]);
            if (t.rfind("def ", 0) == 0 || t.rfind("doc_", 0) == 0) {
                insertIdx = i;
                break;
            }
        }
        outLines.insert(outLines.begin() + static_cast<std::ptrdiff_t>(insertIdx), demoSeedsLine);
        outLines.insert(outLines.begin() + static_cast<std::ptrdiff_t>(insertIdx), demoSeedLine);
    }

    std::ofstream out;
    for (int attempt = 0; attempt < 6; ++attempt) {
        out.open(filePath, std::ios::binary | std::ios::trunc);
        if (out.is_open()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (!out.is_open()) return false;
    for (size_t i = 0; i < outLines.size(); ++i) {
        out << outLines[i];
        if (i + 1 < outLines.size()) {
            out << nl;
        }
    }
    out.close();

    // Also mirror to web/games/<filename> if web/games directory exists
    fs::path srcPath(filePath);
    fs::path webDir = srcPath.parent_path().parent_path() / "web" / "games";
    if (fs::exists(webDir) && fs::is_directory(webDir)) {
        fs::path webFile = webDir / srcPath.filename();
        std::error_code ec;
        fs::copy_file(srcPath, webFile, fs::copy_options::overwrite_existing, ec);
    }

    return true;
}

int cmdAudit(const std::string& gamesDir, uint32_t maxStates, float timeoutMs) {
    auto defs = loadAllEatsGames(gamesDir);
    if (defs.empty()) {
        std::cerr << "No .eats games found in '" << gamesDir << "'\n";
        return 1;
    }

    std::cout << "Auditing " << defs.size() << " bundled games (maxStates=" << maxStates
              << ", timeoutMs=" << timeoutMs << ")...\n";
    std::cout << std::string(86, '-') << "\n";
    std::cout << std::left << std::setw(22) << "GAME ID"
              << std::setw(12) << "SEEDS OK"
              << std::setw(14) << "PRIMARY SEED"
              << std::setw(10) << "MOVES"
              << std::setw(10) << "STATES"
              << "STATUS\n";
    std::cout << std::string(86, '-') << "\n";

    int failedGames = 0;
    for (const auto& def : defs) {
        std::vector<uint32_t> toCheck = def.demoSeeds;
        if (def.demoSeed != 0 &&
            std::find(toCheck.begin(), toCheck.end(), def.demoSeed) == toCheck.end()) {
            toCheck.insert(toCheck.begin(), def.demoSeed);
        }

        size_t okCount = 0;
        VerifyReport primaryRep{};
        std::string firstFailMsg;

        for (size_t i = 0; i < toCheck.size(); ++i) {
            VerifyReport r = verifyGameSeed(def, toCheck[i], maxStates, timeoutMs);
            if (i == 0) primaryRep = r;
            if (r.passed()) {
                ++okCount;
            } else if (firstFailMsg.empty()) {
                firstFailMsg = "seed " + std::to_string(toCheck[i]) + ": " + r.errorDetail;
            }
        }

        const bool allOk = !toCheck.empty() && (okCount == toCheck.size());
        if (!allOk) ++failedGames;

        std::string ratioStr = std::to_string(okCount) + "/" + std::to_string(toCheck.size());
        std::cout << std::left << std::setw(22) << def.id
                  << std::setw(12) << ratioStr
                  << std::setw(14) << (toCheck.empty() ? 0u : toCheck.front())
                  << std::setw(10) << primaryRep.executedMoves
                  << std::setw(10) << primaryRep.statesExplored
                  << (allOk ? "PASS" : ("FAIL (" + firstFailMsg + ")")) << "\n";
    }
    std::cout << std::string(86, '-') << "\n";
    std::cout << "Result: " << (defs.size() - static_cast<size_t>(failedGames)) << "/"
              << defs.size() << " games passed all configured demo seeds.\n";
    return failedGames == 0 ? 0 : 2;
}

int cmdCheck(
    const std::string& gamesDir,
    const std::string& gameId,
    uint32_t seed,
    uint32_t maxStates,
    float timeoutMs
) {
    auto defs = loadAllEatsGames(gamesDir);
    const SolitaireGameDef* target = nullptr;
    for (const auto& d : defs) {
        if (d.id == gameId) {
            target = &d;
            break;
        }
    }
    if (!target) {
        std::cerr << "Unknown game id: '" << gameId << "'\n";
        return 1;
    }

    VerifyReport r = verifyGameSeed(*target, seed, maxStates, timeoutMs);
    std::cout << "Game:    " << target->name << " (" << target->id << ")\n"
              << "Seed:    " << seed << "\n"
              << "Solved:  " << (r.solved ? "yes" : "no") << "\n"
              << "Replay:  " << (r.replayOk ? "yes" : "no") << "\n"
              << "Victory: " << (r.victoryOk ? "yes" : "no") << "\n"
              << "Moves:   " << r.executedMoves << "/" << r.totalMoves << "\n"
              << "States:  " << r.statesExplored << " (" << std::fixed << std::setprecision(1)
              << r.solveTimeMs << " ms)\n";
    if (!r.passed()) {
        std::cout << "Detail:  " << r.errorDetail << "\n";
        return 2;
    }
    std::cout << "Status:  PASS\n";
    return 0;
}

std::vector<uint32_t> findSeedsForGame(
    const SolitaireGameDef& def,
    size_t targetCount,
    uint32_t startSeed,
    uint32_t maxAttempts,
    uint32_t maxStates,
    float timeoutMs,
    bool keepExisting
) {
    std::vector<uint32_t> accepted;
    accepted.reserve(targetCount);

    if (keepExisting) {
        std::vector<uint32_t> existing = def.demoSeeds;
        if (def.demoSeed != 0 &&
            std::find(existing.begin(), existing.end(), def.demoSeed) == existing.end()) {
            existing.insert(existing.begin(), def.demoSeed);
        }
        for (uint32_t s : existing) {
            if (accepted.size() >= targetCount) break;
            if (s == 0) continue;
            if (std::find(accepted.begin(), accepted.end(), s) != accepted.end()) continue;
            VerifyReport r = verifyGameSeed(def, s, maxStates, timeoutMs);
            if (r.passed()) {
                accepted.push_back(s);
            }
        }
    }

    if (accepted.size() >= targetCount) {
        return accepted;
    }

    std::atomic<bool> done{false};
    std::mutex mtx;

    const int64_t totalTries = static_cast<int64_t>(maxAttempts);
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4)
#endif
    for (int64_t offset = 0; offset < totalTries; ++offset) {
        if (done.load(std::memory_order_relaxed)) continue;

        uint32_t candidate = startSeed + static_cast<uint32_t>(offset);
        if (candidate == 0) continue;

        {
            std::lock_guard<std::mutex> lk(mtx);
            if (accepted.size() >= targetCount) {
                done.store(true, std::memory_order_relaxed);
                continue;
            }
            if (std::find(accepted.begin(), accepted.end(), candidate) != accepted.end()) {
                continue;
            }
        }

        // Fast scan followed by exact AutoPlayController budget verification
        const float scanTimeout = std::min(timeoutMs, 600.0f);
        VerifyReport scanRep = verifyGameSeed(def, candidate, maxStates, scanTimeout);
        if (!scanRep.passed()) continue;

        if (scanTimeout < timeoutMs) {
            VerifyReport fullRep = verifyGameSeed(def, candidate, maxStates, timeoutMs);
            if (!fullRep.passed()) continue;
        }

        {
            std::lock_guard<std::mutex> lk(mtx);
            if (accepted.size() < targetCount &&
                std::find(accepted.begin(), accepted.end(), candidate) == accepted.end()) {
                accepted.push_back(candidate);
                if (accepted.size() >= targetCount) {
                    done.store(true, std::memory_order_relaxed);
                }
            }
        }
    }

    std::sort(accepted.begin(), accepted.end());
    return accepted;
}

int cmdFind(
    const std::string& gamesDir,
    const std::string& targetGame,
    bool allGames,
    size_t count,
    uint32_t startSeed,
    uint32_t maxAttempts,
    uint32_t maxStates,
    float timeoutMs,
    bool writeFiles,
    bool keepExisting
) {
    auto defs = loadAllEatsGames(gamesDir);
    if (defs.empty()) {
        std::cerr << "No .eats games found in '" << gamesDir << "'\n";
        return 1;
    }

    int incomplete = 0;
    for (const auto& def : defs) {
        if (!allGames && def.id != targetGame) continue;

        auto t0 = std::chrono::high_resolution_clock::now();
        auto seeds = findSeedsForGame(def, count, startSeed, maxAttempts, maxStates, timeoutMs, keepExisting);
        auto t1 = std::chrono::high_resolution_clock::now();
        float sec = std::chrono::duration<float>(t1 - t0).count();

        std::cout << std::left << std::setw(22) << def.id
                  << " found " << seeds.size() << "/" << count
                  << " (" << std::fixed << std::setprecision(2) << sec << "s): [";
        for (size_t i = 0; i < seeds.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << seeds[i];
        }
        std::cout << "]";

        if (writeFiles && !seeds.empty()) {
            if (writeSeedsToEatsFile(def.filePath, seeds)) {
                std::cout << " -> written";
            } else {
                std::cout << " -> WRITE FAILED";
            }
        }
        std::cout << "\n";

        if (seeds.size() < count) {
            ++incomplete;
        }
    }

    return incomplete == 0 ? 0 : 2;
}

void printUsage() {
    std::cout
        << "SolEngine Headless Solver & Seed CLI (solcli)\n\n"
        << "Usage:\n"
        << "  solcli audit [--games-dir games] [--max-states 25000] [--timeout-ms 2500]\n"
        << "  solcli check <game_id> <seed> [--games-dir games] [--max-states 25000] [--timeout-ms 2500]\n"
        << "  solcli find <game_id|--all> [--count 5] [--start-seed 1] [--max-attempts 5000]\n"
        << "              [--max-states 25000] [--timeout-ms 2500] [--fresh] [--write]\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 0;
    }

    std::string sub = argv[1];
    std::string gamesDir = "games";
    uint32_t maxStates = 0; // 0 = match AutoPlayController (25k 1-deck / 65k 2-deck)
    float timeoutMs = 2500.0f;
    size_t count = 5;
    uint32_t startSeed = 1;
    uint32_t maxAttempts = 5000;
    bool writeFiles = false;
    bool keepExisting = true;
    bool allGames = false;

    std::vector<std::string> pos;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--games-dir" && i + 1 < argc) {
            gamesDir = argv[++i];
        } else if (a == "--max-states" && i + 1 < argc) {
            maxStates = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (a == "--timeout-ms" && i + 1 < argc) {
            timeoutMs = std::stof(argv[++i]);
        } else if (a == "--count" && i + 1 < argc) {
            count = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (a == "--start-seed" && i + 1 < argc) {
            startSeed = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (a == "--max-attempts" && i + 1 < argc) {
            maxAttempts = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (a == "--write") {
            writeFiles = true;
        } else if (a == "--fresh") {
            keepExisting = false;
        } else if (a == "--all") {
            allGames = true;
        } else if (a == "--help" || a == "-h") {
            printUsage();
            return 0;
        } else {
            pos.push_back(a);
        }
    }

    if (sub == "audit") {
        return cmdAudit(gamesDir, maxStates, timeoutMs);
    }
    if (sub == "check") {
        if (pos.size() < 2) {
            std::cerr << "Usage: solcli check <game_id> <seed>\n";
            return 1;
        }
        uint32_t seed = static_cast<uint32_t>(std::stoul(pos[1]));
        return cmdCheck(gamesDir, pos[0], seed, maxStates, timeoutMs);
    }
    if (sub == "find") {
        if (!allGames && pos.empty()) {
            std::cerr << "Usage: solcli find <game_id|--all> [options]\n";
            return 1;
        }
        std::string targetGame = pos.empty() ? "" : pos[0];
        return cmdFind(gamesDir, targetGame, allGames, count, startSeed, maxAttempts,
                       maxStates, timeoutMs, writeFiles, keepExisting);
    }

    printUsage();
    return 1;
}
