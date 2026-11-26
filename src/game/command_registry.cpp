#include "solengine/command_registry.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace solengine {

namespace {

std::string toLowerCopy(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

std::string trimCopy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

std::vector<std::string> splitTokens(const std::string& s) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) {
        tokens.push_back(tok);
    }
    return tokens;
}

// Returns >0 if needle matches haystack (exact prefix > word boundary > substring > fuzzy subsequence).
int scoreFieldMatch(const std::string& haystackLower, const std::string& needleLower) {
    if (needleLower.empty() || haystackLower.empty()) return 0;

    if (haystackLower == needleLower) {
        return 120;
    }
    if (haystackLower.rfind(needleLower, 0) == 0) {
        return 95;
    }

    const size_t pos = haystackLower.find(needleLower);
    if (pos != std::string::npos) {
        // Word boundary bonus
        const bool wordBoundary = (pos == 0) ||
            !std::isalnum(static_cast<unsigned char>(haystackLower[pos - 1]));
        return wordBoundary ? 80 : 60;
    }

    // Fuzzy subsequence match (e.g. "nsd" -> "new solvable deal")
    size_t hIdx = 0;
    int consecutive = 0;
    int boundaryHits = 0;
    for (size_t nIdx = 0; nIdx < needleLower.size(); ++nIdx) {
        const char nc = needleLower[nIdx];
        size_t found = haystackLower.find(nc, hIdx);
        if (found == std::string::npos) {
            return 0;
        }
        if (found == hIdx && nIdx > 0) {
            ++consecutive;
        }
        if (found == 0 || !std::isalnum(static_cast<unsigned char>(haystackLower[found - 1]))) {
            ++boundaryHits;
        }
        hIdx = found + 1;
    }

    return 20 + std::min(25, boundaryHits * 6 + consecutive * 3);
}

int scoreCommandAgainstText(const CommandEntry& cmd, const std::string& queryLower) {
    const std::string titleL = toLowerCopy(cmd.title);
    const std::string subL = toLowerCopy(cmd.subtitle);
    const std::string idL = toLowerCopy(cmd.id);
    const std::string kwL = toLowerCopy(cmd.keywords);
    const std::string catL = toLowerCopy(commandCategoryName(cmd.category));

    const auto tokens = splitTokens(queryLower);
    if (tokens.empty()) return 1;

    int totalScore = 0;
    for (const auto& tok : tokens) {
        int bestTok = 0;
        bestTok = std::max(bestTok, scoreFieldMatch(titleL, tok) * 3);
        bestTok = std::max(bestTok, scoreFieldMatch(idL, tok) * 2);
        bestTok = std::max(bestTok, scoreFieldMatch(kwL, tok) * 2);
        bestTok = std::max(bestTok, scoreFieldMatch(subL, tok));
        bestTok = std::max(bestTok, scoreFieldMatch(catL, tok));
        if (bestTok <= 0) {
            return 0; // All tokens must match some field
        }
        totalScore += bestTok;
    }

    // Bonus for full phrase match in title
    const int phraseScore = scoreFieldMatch(titleL, queryLower);
    if (phraseScore > 0) {
        totalScore += phraseScore * 2;
    }

    return totalScore;
}

} // namespace

const char* commandCategoryName(CommandCategory cat) noexcept {
    switch (cat) {
        case CommandCategory::All:        return "ALL";
        case CommandCategory::Play:       return "PLAY";
        case CommandCategory::Game:       return "GAMES";
        case CommandCategory::Appearance: return "APPEARANCE";
        case CommandCategory::Settings:   return "SETTINGS";
        case CommandCategory::View:       return "VIEW";
        default:                          return "ALL";
    }
}

void CommandRegistry::clear() {
    commands_.clear();
}

void CommandRegistry::registerCommand(CommandEntry entry) {
    if (entry.id.empty()) return;
    for (auto& existing : commands_) {
        if (existing.id == entry.id) {
            existing = std::move(entry);
            return;
        }
    }
    commands_.push_back(std::move(entry));
}

const CommandEntry* CommandRegistry::findById(const std::string& id) const {
    for (const auto& cmd : commands_) {
        if (cmd.id == id) return &cmd;
    }
    return nullptr;
}

bool CommandRegistry::executeById(const std::string& id, const std::string& arg) {
    const CommandEntry* cmd = findById(id);
    if (!cmd || !cmd->execute) return false;
    recordUsage(id);
    cmd->execute(arg);
    return true;
}

void CommandRegistry::recordUsage(const std::string& id) {
    if (id.empty()) return;
    auto it = std::find(recentIds_.begin(), recentIds_.end(), id);
    if (it != recentIds_.end()) {
        recentIds_.erase(it);
    }
    recentIds_.insert(recentIds_.begin(), id);
    constexpr size_t kMaxRecent = 16;
    if (recentIds_.size() > kMaxRecent) {
        recentIds_.resize(kMaxRecent);
    }
}

std::vector<CommandMatch> CommandRegistry::search(
    const std::string& query,
    CommandCategory category
) const {
    const std::string trimmed = trimCopy(query);
    const std::string queryLower = toLowerCopy(trimmed);

    std::vector<CommandMatch> results;
    results.reserve(commands_.size());

    auto recencyBonus = [this](const std::string& id) -> int {
        for (size_t i = 0; i < recentIds_.size(); ++i) {
            if (recentIds_[i] == id) {
                return static_cast<int>(recentIds_.size() - i) * 3;
            }
        }
        return 0;
    };

    for (const auto& cmd : commands_) {
        if (category != CommandCategory::All && cmd.category != category) {
            continue;
        }

        if (queryLower.empty()) {
            CommandMatch m;
            m.command = &cmd;
            m.score = 1 + recencyBonus(cmd.id);
            results.push_back(std::move(m));
            continue;
        }

        int bestScore = scoreCommandAgainstText(cmd, queryLower);
        std::string extractedArg;

        // If the command accepts an inline argument (e.g. "seed 2026"), try splitting
        // on the first whitespace so the prefix matches the command and the rest is passed as arg.
        if (cmd.acceptsArgument) {
            const size_t spacePos = trimmed.find(' ');
            if (spacePos != std::string::npos) {
                const std::string headLower = toLowerCopy(trimCopy(trimmed.substr(0, spacePos)));
                const std::string tail = trimCopy(trimmed.substr(spacePos + 1));
                if (!headLower.empty() && !tail.empty()) {
                    const int headScore = scoreCommandAgainstText(cmd, headLower);
                    if (headScore > 0 && headScore + 40 > bestScore) {
                        bestScore = headScore + 40;
                        extractedArg = tail;
                    }
                }
            }
        }

        if (bestScore > 0) {
            CommandMatch m;
            m.command = &cmd;
            m.score = bestScore + recencyBonus(cmd.id);
            m.extractedArg = std::move(extractedArg);
            results.push_back(std::move(m));
        }
    }

    if (!queryLower.empty()) {
        std::stable_sort(results.begin(), results.end(), [](const CommandMatch& a, const CommandMatch& b) {
            return a.score > b.score;
        });
    } else if (!recentIds_.empty()) {
        // Keep recently used items at the top when browsing with an empty query
        std::stable_sort(results.begin(), results.end(), [](const CommandMatch& a, const CommandMatch& b) {
            return a.score > b.score;
        });
    }

    return results;
}

} // namespace solengine
