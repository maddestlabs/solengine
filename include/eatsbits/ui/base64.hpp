#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <optional>

namespace eatsbits::ui {

/**
 * @brief Structure representing parsed Data URI contents (RFC 2397).
 */
struct DataUriInfo {
    std::string mimeType;
    bool isBase64{true};
    std::vector<uint8_t> data;
};

/**
 * @brief High-performance Base64 decode using a 256-entry lookup table.
 * Strips whitespace, supports standard Base64 (+/) and URL-safe Base64 (-_),
 * and correctly handles padded (=) or unpadded payloads.
 */
std::vector<uint8_t> base64Decode(std::string_view input);

/**
 * @brief High-performance Base64 encode converting raw binary data to Base64 ASCII.
 */
std::string base64Encode(const void* data, size_t len);

inline std::string base64Encode(const std::vector<uint8_t>& bytes) {
    return base64Encode(bytes.data(), bytes.size());
}

/**
 * @brief Parses an RFC 2397 Data URI (e.g. "data:image/png;base64,iVBORw0KG...")
 * or decodes raw Base64 payload if "data:" prefix is omitted.
 */
std::optional<DataUriInfo> parseDataUri(std::string_view uri);

/**
 * @brief Formats raw data into an RFC 2397 Data URI string.
 * Example: makeDataUri("image/png", ptr, size) -> "data:image/png;base64,..."
 */
std::string makeDataUri(std::string_view mimeType, const void* data, size_t len);

inline std::string makeDataUri(std::string_view mimeType, const std::vector<uint8_t>& bytes) {
    return makeDataUri(mimeType, bytes.data(), bytes.size());
}

/**
 * @brief Generates an idiomatic Eatscript embedded asset declaration.
 * Example: "const KNOB_SKIN = \"data:image/png;base64,...\";\n"
 */
std::string generateEmbeddedAssetDeclaration(std::string_view varName,
                                             std::string_view mimeType,
                                             const void* data,
                                             size_t len);

} // namespace eatsbits::ui
