#ifndef SOLENGINE_COMMAND_REGISTRY_HPP
#define SOLENGINE_COMMAND_REGISTRY_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace solengine {

enum class CommandCategory : uint8_t {
    All = 0,
    Play,
    Game,
    Appearance,
    Settings,
    View,
    Count
};

[[nodiscard]] const char* commandCategoryName(CommandCategory cat) noexcept;

struct CommandEntry {
    std::string id;
    std::string title;
    std::string subtitle;
    std::string shortcut;
    std::string keywords;
    CommandCategory category{CommandCategory::Play};
    bool acceptsArgument{false};
    std::function<std::string()> valueProvider;
    std::function<void(const std::string& arg)> execute;
};

struct CommandMatch {
    const CommandEntry* command{nullptr};
    int score{0};
    std::string extractedArg;
};

/**
 * Unified typed command & setting registry for SolEngine.
 * Powers the Command Palette, discoverable hotkeys, and future CLI/Eatscript bindings
 * from a single source of truth.
 */
class CommandRegistry {
public:
    CommandRegistry() = default;
    ~CommandRegistry() = default;

    void clear();
    void registerCommand(CommandEntry entry);

    [[nodiscard]] const CommandEntry* findById(const std::string& id) const;
    bool executeById(const std::string& id, const std::string& arg = "");

    void recordUsage(const std::string& id);

    [[nodiscard]] const std::vector<CommandEntry>& commands() const noexcept { return commands_; }
    [[nodiscard]] std::vector<CommandMatch> search(
        const std::string& query,
        CommandCategory category = CommandCategory::All
    ) const;

private:
    std::vector<CommandEntry> commands_;
    std::vector<std::string> recentIds_;
};

} // namespace solengine

#endif // SOLENGINE_COMMAND_REGISTRY_HPP
