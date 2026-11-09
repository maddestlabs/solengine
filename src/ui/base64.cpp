#include "eatsbits/ui/base64.hpp"
#include <algorithm>
#include <array>
#include <cstring>

namespace eatsbits::ui {

namespace {

constexpr std::array<int8_t, 256> buildDecodeTable() {
    std::array<int8_t, 256> table{};
    for (size_t i = 0; i < 256; ++i) {
        table[i] = -1;
    }
    for (int i = 0; i < 26; ++i) {
        table[static_cast<size_t>('A' + i)] = static_cast<int8_t>(i);
        table[static_cast<size_t>('a' + i)] = static_cast<int8_t>(i + 26);
    }
    for (int i = 0; i < 10; ++i) {
        table[static_cast<size_t>('0' + i)] = static_cast<int8_t>(i + 52);
    }
    // Standard Base64
    table[static_cast<size_t>('+')] = 62;
    table[static_cast<size_t>('/')] = 63;
    // URL-safe variants
    table[static_cast<size_t>('-')] = 62;
    table[static_cast<size_t>('_')] = 63;
    return table;
}

const auto s_decodeTable = buildDecodeTable();

const char s_encodeChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

std::vector<uint8_t> base64Decode(std::string_view input) {
    std::vector<uint8_t> out;
    if (input.empty()) return out;

    out.reserve((input.size() * 3) / 4);

    uint32_t val = 0;
    int bits = 0;

    for (char c : input) {
        if (c == '=') {
            break; // Padding reached
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            continue; // Skip whitespace
        }
        int8_t d = s_decodeTable[static_cast<uint8_t>(c)];
        if (d < 0) {
            continue; // Skip unrecognized non-base64 characters
        }

        val = (val << 6) | static_cast<uint32_t>(d);
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
        }
    }

    return out;
}

std::string base64Encode(const void* data, size_t len) {
    if (!data || len == 0) return "";

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t b0 = bytes[i];
        uint32_t b1 = (i + 1 < len) ? bytes[i + 1] : 0;
        uint32_t b2 = (i + 2 < len) ? bytes[i + 2] : 0;

        uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(s_encodeChars[(triple >> 18) & 0x3F]);
        out.push_back(s_encodeChars[(triple >> 12) & 0x3F]);

        if (i + 1 < len) {
            out.push_back(s_encodeChars[(triple >> 6) & 0x3F]);
        } else {
            out.push_back('=');
        }

        if (i + 2 < len) {
            out.push_back(s_encodeChars[triple & 0x3F]);
        } else {
            out.push_back('=');
        }
    }

    return out;
}

std::optional<DataUriInfo> parseDataUri(std::string_view uri) {
    if (uri.empty()) return std::nullopt;

    // Check for "data:" prefix
    if (uri.rfind("data:", 0) == 0) {
        size_t commaPos = uri.find(',');
        if (commaPos == std::string_view::npos) {
            return std::nullopt;
        }

        std::string_view meta = uri.substr(5, commaPos - 5);
        std::string_view payload = uri.substr(commaPos + 1);

        DataUriInfo info;
        size_t semiPos = meta.find(';');
        if (semiPos != std::string_view::npos) {
            info.mimeType = std::string(meta.substr(0, semiPos));
            std::string_view enc = meta.substr(semiPos + 1);
            info.isBase64 = (enc.find("base64") != std::string_view::npos);
        } else {
            info.mimeType = meta.empty() ? "text/plain" : std::string(meta);
            info.isBase64 = false;
        }

        if (info.isBase64) {
            info.data = base64Decode(payload);
        } else {
            info.data.assign(payload.begin(), payload.end());
        }

        return info;
    }

    // Fallback: If not "data:", treat as raw Base64 payload
    auto bytes = base64Decode(uri);
    if (!bytes.empty()) {
        DataUriInfo info;
        info.mimeType = "application/octet-stream";
        info.isBase64 = true;
        info.data = std::move(bytes);
        return info;
    }

    return std::nullopt;
}

std::string makeDataUri(std::string_view mimeType, const void* data, size_t len) {
    std::string out = "data:";
    out.append(mimeType.empty() ? "application/octet-stream" : mimeType);
    out.append(";base64,");
    out.append(base64Encode(data, len));
    return out;
}

std::string generateEmbeddedAssetDeclaration(std::string_view varName,
                                             std::string_view mimeType,
                                             const void* data,
                                             size_t len) {
    std::string out = "const ";
    out.append(varName.empty() ? "EMBEDDED_ASSET" : varName);
    out.append(" = \"");
    out.append(makeDataUri(mimeType, data, len));
    out.append("\";\n");
    return out;
}

} // namespace eatsbits::ui
