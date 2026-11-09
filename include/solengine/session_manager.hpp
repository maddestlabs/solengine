#ifndef SOLENGINE_SESSION_MANAGER_HPP
#define SOLENGINE_SESSION_MANAGER_HPP

#include "solengine/solitaire_types.hpp"
#include <string>

namespace solengine {

class SolitaireBoard;

class SessionManager {
public:
    static SessionManager& instance();

    bool hasSavedSession() const;
    bool loadSession(SolitaireBoard& board);
    bool saveSession(const SolitaireBoard& board);
    void clearSession();

    [[nodiscard]] std::string getSessionFilePath() const;

private:
    SessionManager() = default;
};

} // namespace solengine

#endif // SOLENGINE_SESSION_MANAGER_HPP

