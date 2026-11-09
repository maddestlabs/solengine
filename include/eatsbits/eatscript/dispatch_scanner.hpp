#ifndef EATS_DISPATCH_SCANNER_HPP
#define EATS_DISPATCH_SCANNER_HPP

#include <string>
#include <map>
#include <vector>
#include "api_manifest.hpp"

namespace eatsbits::eatscript {

struct Program;

enum class NativeDispatchTarget {
    None,
    TB303,
    PolySynth,
    SID,
    DX7,
    SNES,
    YM2612,
    DrumKit808,
    DrumKit909,
    GrandPiano,
    UprightBass,
    SpanishGuitar,
    SteelGuitar,
    Convolver,
    StereoDelay
};

struct ExtractedParam {
    std::string name;
    float minVal{0.0f};
    float maxVal{1.0f};
    float defaultVal{0.5f};
};

/**
 * Metadata harvested from Eatscript header directives (# @id, # @engine, etc.).
 */
struct ScriptMetadata {
    std::string id;          // e.g. "eats_303"
    std::string name;        // e.g. "Eatsbits 303 Bassline"
    std::string vendor{"Maddest Labs"};
    std::string version{"1.0.0"};
    std::string description;
    std::string category{"instrument"}; // "instrument" or "audio-effect"
    std::string engine;      // e.g. "tb303"
};

/**
 * Parameter description for CLAP export and reflection.
 */
struct ClapExportParam {
    uint32_t id{0};
    std::string name;
    std::string module{"General"};
    double minValue{0.0};
    double maxValue{1.0};
    double defaultValue{0.0};
    uint32_t flags{0x20}; // CLAP_PARAM_IS_AUTOMATABLE (1 << 5)
};

/**
 * Complete scan result including metadata, resolved dependencies, and parameters.
 */
struct DependencyScanResult {
    ScriptMetadata metadata;
    NativeDispatchTarget dispatchTarget{NativeDispatchTarget::None};
    DependencySet dependencies;
    std::vector<ExtractedParam> parameters;
    std::vector<std::string> referencedSymbols;

    [[nodiscard]] std::vector<ClapExportParam> generateClapParamDefs() const;
};

/**
 * High-performance AST-driven tree-shaking and dependency scanner for Eatscript.
 *
 * Scans script AST for `eat.*` API calls and `# @directives`, resolves exact C++
 * headers and sources from ApiManifest, and generates parameter descriptions for CLAP.
 */
class DependencyScanner {
public:
    /// Fully scans source and AST to extract metadata, dependencies, parameters, and dispatch target.
    [[nodiscard]] static DependencyScanResult scan(const std::string& scriptSource);

    /// Scans metadata directives (# @id, # @engine, etc.) from source comments.
    static void scanDirectives(const std::string& scriptSource, DependencyScanResult& result);

    /// Recursively walks parsed Eatscript AST to harvest referenced API modules and parameters.
    static void scanAST(const Program& program, DependencyScanResult& result);

    // --- Backward Compatibility for NativeDispatchScanner ---
    [[nodiscard]] static NativeDispatchTarget detectTarget(const std::string& scriptSource) noexcept;
    [[nodiscard]] static const char* targetToString(NativeDispatchTarget target) noexcept;
    [[nodiscard]] static std::map<std::string, ExtractedParam> extractParameters(const std::string& scriptSource);
    [[nodiscard]] static std::map<std::string, float> extractParamsFromInit(const std::string& scriptSource);
};

using NativeDispatchScanner = DependencyScanner;

} // namespace eatsbits::eatscript

#endif // EATS_DISPATCH_SCANNER_HPP
