#include "eatsbits/ui/glyph/ebf_font.hpp"
#if defined(EATS_ENABLE_FREETYPE)
#include "eatsbits/ui/glyph/ebf_converter.hpp"
#endif
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace eatsbits::ui::glyph {

namespace {

// Helper to read unaligned little-endian integers from a bytecode buffer
inline int16_t readInt16(const uint8_t* ptr) noexcept {
    return static_cast<int16_t>(ptr[0] | (static_cast<uint16_t>(ptr[1]) << 8));
}

inline uint16_t readUint16(const uint8_t* ptr) noexcept {
    return static_cast<uint16_t>(ptr[0] | (static_cast<uint16_t>(ptr[1]) << 8));
}

inline uint32_t readUint32(const uint8_t* ptr) noexcept {
    return static_cast<uint32_t>(ptr[0]) |
           (static_cast<uint32_t>(ptr[1]) << 8) |
           (static_cast<uint32_t>(ptr[2]) << 16) |
           (static_cast<uint32_t>(ptr[3]) << 24);
}

// UTF-8 decoder helper
inline uint32_t decodeUtf8(const char*& ptr, const char* end) noexcept {
    if (ptr >= end) return 0;
    uint8_t c0 = static_cast<uint8_t>(*ptr++);
    if (c0 < 0x80) return c0;
    if ((c0 & 0xE0) == 0xC0) {
        if (ptr >= end) return 0;
        uint8_t c1 = static_cast<uint8_t>(*ptr++);
        return ((c0 & 0x1F) << 6) | (c1 & 0x3F);
    }
    if ((c0 & 0xF0) == 0xE0) {
        if (ptr + 1 >= end) return 0;
        uint8_t c1 = static_cast<uint8_t>(*ptr++);
        uint8_t c2 = static_cast<uint8_t>(*ptr++);
        return ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
    }
    if ((c0 & 0xF8) == 0xF0) {
        if (ptr + 2 >= end) return 0;
        uint8_t c1 = static_cast<uint8_t>(*ptr++);
        uint8_t c2 = static_cast<uint8_t>(*ptr++);
        uint8_t c3 = static_cast<uint8_t>(*ptr++);
        return ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    }
    return 0;
}

} // anonymous namespace

EbfFont::EbfFont() = default;

EbfFont::EbfFont(EbfFont&&) noexcept = default;
EbfFont& EbfFont::operator=(EbfFont&&) noexcept = default;

bool EbfFont::loadFromFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size < static_cast<std::streamsize>(sizeof(EbfHeader))) return false;

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) return false;

    return loadFromMemory(buffer.data(), buffer.size());
}

bool EbfFont::loadFromMemory(const uint8_t* data, size_t size) {
    if (!data || size < sizeof(EbfHeader)) return false;
    parseFromBuffer(data, size);
    return valid_;
}

bool EbfFont::loadFromTtfFile(const std::string& ttfPath) {
#if defined(EATS_ENABLE_FREETYPE)
    return EbfConverter::loadTtfIntoFont(ttfPath, *this);
#else
    (void)ttfPath;
    return false;
#endif
}

bool EbfFont::loadFromTtfMemory(const uint8_t* data, size_t size) {
#if defined(EATS_ENABLE_FREETYPE)
    return EbfConverter::loadTtfMemoryIntoFont(data, size, *this);
#else
    (void)data;
    (void)size;
    return false;
#endif
}

void EbfFont::parseFromBuffer(const uint8_t* data, size_t size) {
    valid_ = false;
    glyphs_.clear();
    commandData_.clear();
    kerningPairs_.clear();

    if (size < sizeof(EbfHeader)) return;
    std::memcpy(&header_, data, sizeof(EbfHeader));

    if (header_.magic[0] != 'E' || header_.magic[1] != 'B' ||
        header_.magic[2] != 'F' || header_.magic[3] != '1') {
        return;
    }

    if (header_.unitsPerEm == 0) header_.unitsPerEm = 1000;

    size_t offset = sizeof(EbfHeader);
    size_t glyphTableSize = static_cast<size_t>(header_.glyphCount) * sizeof(EbfGlyphRecord);
    if (offset + glyphTableSize > size) return;

    glyphs_.resize(header_.glyphCount);
    std::memcpy(glyphs_.data(), data + offset, glyphTableSize);
    offset += glyphTableSize;

    // Remaining bytes are bytecode and optional kerning table
    if (offset < size) {
        size_t cmdSize = size - offset;
        commandData_.resize(cmdSize);
        std::memcpy(commandData_.data(), data + offset, cmdSize);
    }

    // Sort glyphs by codepoint for fast binary searching
    std::sort(glyphs_.begin(), glyphs_.end(), [](const EbfGlyphRecord& a, const EbfGlyphRecord& b) {
        return a.codepoint < b.codepoint;
    });

    valid_ = true;
}

bool EbfFont::saveToFile(const std::string& filePath) const {
    if (!valid_) return false;
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    file.write(reinterpret_cast<const char*>(&header_), sizeof(EbfHeader));
    if (!glyphs_.empty()) {
        file.write(reinterpret_cast<const char*>(glyphs_.data()), glyphs_.size() * sizeof(EbfGlyphRecord));
    }
    if (!commandData_.empty()) {
        file.write(reinterpret_cast<const char*>(commandData_.data()), commandData_.size());
    }
    return file.good();
}

float EbfFont::getAscender(float fontSize) const noexcept {
    float u = (header_.unitsPerEm > 0) ? static_cast<float>(header_.unitsPerEm) : 1000.0f;
    return (static_cast<float>(header_.ascender) / u) * fontSize;
}

float EbfFont::getDescender(float fontSize) const noexcept {
    float u = (header_.unitsPerEm > 0) ? static_cast<float>(header_.unitsPerEm) : 1000.0f;
    return (static_cast<float>(header_.descender) / u) * fontSize;
}

float EbfFont::getLineHeight(float fontSize) const noexcept {
    float u = (header_.unitsPerEm > 0) ? static_cast<float>(header_.unitsPerEm) : 1000.0f;
    float h = static_cast<float>(header_.ascender - header_.descender + header_.lineGap);
    return (h / u) * fontSize;
}

const EbfGlyphRecord* EbfFont::findGlyph(uint32_t codepoint) const noexcept {
    if (glyphs_.empty()) return nullptr;
    auto it = std::lower_bound(glyphs_.begin(), glyphs_.end(), codepoint,
                               [](const EbfGlyphRecord& rec, uint32_t cp) {
                                   return rec.codepoint < cp;
                               });
    if (it != glyphs_.end() && it->codepoint == codepoint) {
        return &(*it);
    }
    return nullptr;
}

float EbfFont::getKerning(uint32_t leftCp, uint32_t rightCp, float fontSize) const noexcept {
    if (kerningPairs_.empty()) return 0.0f;
    float u = (header_.unitsPerEm > 0) ? static_cast<float>(header_.unitsPerEm) : 1000.0f;
    for (const auto& kp : kerningPairs_) {
        if (kp.leftCodepoint == leftCp && kp.rightCodepoint == rightCp) {
            return (static_cast<float>(kp.kerningAmount) / u) * fontSize;
        }
    }
    return 0.0f;
}

float EbfFont::measureText(std::string_view text, float fontSize) const {
    if (text.empty() || !valid_) return 0.0f;

    float u = (header_.unitsPerEm > 0) ? static_cast<float>(header_.unitsPerEm) : 1000.0f;
    float scale = fontSize / u;

    float maxW = 0.0f;
    float curW = 0.0f;
    const char* ptr = text.data();
    const char* end = ptr + text.size();
    uint32_t prevCp = 0;

    while (ptr < end) {
        uint32_t cp = decodeUtf8(ptr, end);
        if (cp == '\n') {
            if (curW > maxW) maxW = curW;
            curW = 0.0f;
            prevCp = 0;
            continue;
        }
        if (cp == '\r') continue;
        if (cp == '\t') {
            float tabW = 4.0f * (static_cast<float>(header_.maxAdvance) * 0.6f * scale);
            curW = (std::floor(curW / tabW) + 1.0f) * tabW;
            prevCp = 0;
            continue;
        }

        const auto* g = findGlyph(cp);
        if (!g) g = findGlyph('?');

        float adv = g ? static_cast<float>(g->advanceX) * scale : (static_cast<float>(header_.maxAdvance) * scale * 0.5f);
        if (prevCp != 0) {
            adv += getKerning(prevCp, cp, fontSize);
        }
        curW += adv;
        prevCp = cp;
    }

    return std::max(maxW, curW);
}

EbfTextMetrics EbfFont::measureTextDetailed(std::string_view text, float fontSize) const {
    EbfTextMetrics m{};
    if (text.empty() || !valid_) return m;

    m.ascender = getAscender(fontSize);
    m.descender = getDescender(fontSize);
    m.width = measureText(text, fontSize);

    // Compute line count
    size_t lines = 1;
    for (char c : text) {
        if (c == '\n') lines++;
    }
    m.height = static_cast<float>(lines) * getLineHeight(fontSize);
    return m;
}

bool EbfFont::renderGlyph(BatchRenderer2D& r, uint32_t codepoint, float x, float baselineY,
                          float fontSize, float red, float green, float blue, float alpha) const {
    const auto* g = findGlyph(codepoint);
    if (!g) {
        g = findGlyph('?');
        if (!g) return false;
    }

    if (g->commandLength == 0 || g->commandOffset >= commandData_.size()) {
        return true; // Space or non-rendering glyph
    }

    float u = (header_.unitsPerEm > 0) ? static_cast<float>(header_.unitsPerEm) : 1000.0f;
    float scale = fontSize / u;

    // Optical stroke thickness: calibrated for outline contours vs. skeleton strokes
    bool isOutlineFont = (header_.flags & 2) != 0 || (g->flags & 2) != 0;
    float stemW = isOutlineFont ? std::max(1.0f, std::min(1.5f, fontSize * 0.075f))
                                : std::max(1.15f, std::round(fontSize * 0.115f));
    float barH  = std::max(1.05f, std::round(fontSize * 0.088f));

    const uint8_t* p = commandData_.data() + g->commandOffset;
    const uint8_t* pEnd = p + g->commandLength;

    // Fast check: if glyph contains solid triangle geometry, do not stroke perimeter boundaries
    // to prevent artificial bolding, choked counters, and blurred small text.
    bool hasTriangles = false;
    for (const uint8_t* scan = p; scan < pEnd; ++scan) {
        if (*scan == static_cast<uint8_t>(EbfOpcode::Triangle)) {
            hasTriangles = true;
            break;
        }
    }

    float lastX = x;
    float lastY = baselineY;

    while (p < pEnd) {
        auto op = static_cast<EbfOpcode>(*p++);
        switch (op) {
            case EbfOpcode::End:
                return true;

            case EbfOpcode::MoveTo: {
                if (p + 4 > pEnd) return false;
                int16_t fx = readInt16(p); p += 2;
                int16_t fy = readInt16(p); p += 2;
                lastX = x + static_cast<float>(fx) * scale;
                lastY = baselineY - static_cast<float>(fy) * scale;
                break;
            }

            case EbfOpcode::LineTo: {
                if (p + 4 > pEnd) return false;
                int16_t fx = readInt16(p); p += 2;
                int16_t fy = readInt16(p); p += 2;
                float nx = x + static_cast<float>(fx) * scale;
                float ny = baselineY - static_cast<float>(fy) * scale;
                if (!hasTriangles) {
                    r.drawLine(lastX, lastY, nx, ny, red, green, blue, alpha, stemW);
                }
                lastX = nx;
                lastY = ny;
                break;
            }

            case EbfOpcode::QuadTo: {
                if (p + 8 > pEnd) return false;
                int16_t fcx = readInt16(p); p += 2;
                int16_t fcy = readInt16(p); p += 2;
                int16_t fx  = readInt16(p); p += 2;
                int16_t fy  = readInt16(p); p += 2;
                float cx = x + static_cast<float>(fcx) * scale;
                float cy = baselineY - static_cast<float>(fcy) * scale;
                float nx = x + static_cast<float>(fx) * scale;
                float ny = baselineY - static_cast<float>(fy) * scale;

                if (!hasTriangles) {
                    // 4-step quad curve subdivision
                    float px = lastX, py = lastY;
                    for (int step = 1; step <= 4; ++step) {
                        float t = static_cast<float>(step) * 0.25f;
                        float invT = 1.0f - t;
                        float qx = invT * invT * lastX + 2.0f * invT * t * cx + t * t * nx;
                        float qy = invT * invT * lastY + 2.0f * invT * t * cy + t * t * ny;
                        r.drawLine(px, py, qx, qy, red, green, blue, alpha, stemW);
                        px = qx;
                        py = qy;
                    }
                }
                lastX = nx;
                lastY = ny;
                break;
            }

            case EbfOpcode::CubicTo: {
                if (p + 12 > pEnd) return false;
                int16_t fc1x = readInt16(p); p += 2;
                int16_t fc1y = readInt16(p); p += 2;
                int16_t fc2x = readInt16(p); p += 2;
                int16_t fc2y = readInt16(p); p += 2;
                int16_t fx   = readInt16(p); p += 2;
                int16_t fy   = readInt16(p); p += 2;
                float c1x = x + static_cast<float>(fc1x) * scale;
                float c1y = baselineY - static_cast<float>(fc1y) * scale;
                float c2x = x + static_cast<float>(fc2x) * scale;
                float c2y = baselineY - static_cast<float>(fc2y) * scale;
                float nx  = x + static_cast<float>(fx) * scale;
                float ny  = baselineY - static_cast<float>(fy) * scale;

                if (!hasTriangles) {
                    float px = lastX, py = lastY;
                    for (int step = 1; step <= 6; ++step) {
                        float t = static_cast<float>(step) / 6.0f;
                        float invT = 1.0f - t;
                        float bx = invT * invT * invT * lastX +
                                   3.0f * invT * invT * t * c1x +
                                   3.0f * invT * t * t * c2x +
                                   t * t * t * nx;
                        float by = invT * invT * invT * lastY +
                                   3.0f * invT * invT * t * c1y +
                                   3.0f * invT * t * t * c2y +
                                   t * t * t * ny;
                        r.drawLine(px, py, bx, by, red, green, blue, alpha, stemW);
                        px = bx;
                        py = by;
                    }
                }
                lastX = nx;
                lastY = ny;
                break;
            }

            case EbfOpcode::Close:
                break;

            case EbfOpcode::Rect: {
                if (p + 8 > pEnd) return false;
                int16_t fx = readInt16(p); p += 2;
                int16_t fy = readInt16(p); p += 2;
                int16_t fw = readInt16(p); p += 2;
                int16_t fh = readInt16(p); p += 2;
                float rx = x + static_cast<float>(fx) * scale;
                float ry = baselineY - static_cast<float>(fy + fh) * scale;
                float rw = static_cast<float>(fw) * scale;
                float rh = static_cast<float>(fh) * scale;
                r.drawRect(rx, ry, rw, rh, red, green, blue, alpha);
                break;
            }

            case EbfOpcode::ChamferRect: {
                if (p + 10 > pEnd) return false;
                int16_t fx = readInt16(p); p += 2;
                int16_t fy = readInt16(p); p += 2;
                int16_t fw = readInt16(p); p += 2;
                int16_t fh = readInt16(p); p += 2;
                int16_t fc = readInt16(p); p += 2;

                float x0 = static_cast<float>(fx);
                float x1 = static_cast<float>(fx + fw);
                float y0 = static_cast<float>(fy);
                float y1 = static_cast<float>(fy + fh);
                float c  = static_cast<float>(fc);

                auto toScreenX = [&](float fxVal) { return x + fxVal * scale; };
                auto toScreenY = [&](float fyVal) { return baselineY - fyVal * scale; };

                // Horizontal bars
                float px0 = toScreenX(x0 + c);
                float px1 = toScreenX(x1 - c);
                r.drawRect(px0, toScreenY(y1) - barH * 0.5f, px1 - px0, barH, red, green, blue, alpha);
                r.drawRect(px0, toScreenY(y0) - barH * 0.5f, px1 - px0, barH, red, green, blue, alpha);

                // Vertical stems
                float py0 = toScreenY(y1 - c);
                float py1 = toScreenY(y0 + c);
                r.drawRect(toScreenX(x0) - stemW * 0.5f, py0, stemW, py1 - py0, red, green, blue, alpha);
                r.drawRect(toScreenX(x1) - stemW * 0.5f, py0, stemW, py1 - py0, red, green, blue, alpha);

                // Chamfer diagonals
                r.drawLine(toScreenX(x0), toScreenY(y1 - c), toScreenX(x0 + c), toScreenY(y1), red, green, blue, alpha, stemW);
                r.drawLine(toScreenX(x1 - c), toScreenY(y1), toScreenX(x1), toScreenY(y1 - c), red, green, blue, alpha, stemW);
                r.drawLine(toScreenX(x1), toScreenY(y0 + c), toScreenX(x1 - c), toScreenY(y0), red, green, blue, alpha, stemW);
                r.drawLine(toScreenX(x0 + c), toScreenY(y0), toScreenX(x0), toScreenY(y0 + c), red, green, blue, alpha, stemW);
                break;
            }

            case EbfOpcode::Triangle: {
                if (p + 12 > pEnd) return false;
                int16_t x0 = readInt16(p); p += 2;
                int16_t y0 = readInt16(p); p += 2;
                int16_t x1 = readInt16(p); p += 2;
                int16_t y1 = readInt16(p); p += 2;
                int16_t x2 = readInt16(p); p += 2;
                int16_t y2 = readInt16(p); p += 2;
                r.drawTriangle(x + static_cast<float>(x0) * scale, baselineY - static_cast<float>(y0) * scale,
                               x + static_cast<float>(x1) * scale, baselineY - static_cast<float>(y1) * scale,
                               x + static_cast<float>(x2) * scale, baselineY - static_cast<float>(y2) * scale,
                               red, green, blue, alpha);
                break;
            }

            case EbfOpcode::VStem: {
                if (p + 6 > pEnd) return false;
                int16_t fx  = readInt16(p); p += 2;
                int16_t fy0 = readInt16(p); p += 2;
                int16_t fy1 = readInt16(p); p += 2;
                float sx = x + static_cast<float>(fx) * scale;
                float sy0 = baselineY - static_cast<float>(fy1) * scale;
                float sy1 = baselineY - static_cast<float>(fy0) * scale;
                r.drawRect(sx - stemW * 0.5f, sy0, stemW, std::max(1.0f, sy1 - sy0), red, green, blue, alpha);
                break;
            }

            case EbfOpcode::HBar: {
                if (p + 6 > pEnd) return false;
                int16_t fx0 = readInt16(p); p += 2;
                int16_t fx1 = readInt16(p); p += 2;
                int16_t fy  = readInt16(p); p += 2;
                float sx0 = x + static_cast<float>(fx0) * scale;
                float sx1 = x + static_cast<float>(fx1) * scale;
                float sy = baselineY - static_cast<float>(fy) * scale;
                r.drawRect(sx0, sy - barH * 0.5f, std::max(1.0f, sx1 - sx0), barH, red, green, blue, alpha);
                break;
            }

            case EbfOpcode::Diag: {
                if (p + 8 > pEnd) return false;
                int16_t fx0 = readInt16(p); p += 2;
                int16_t fy0 = readInt16(p); p += 2;
                int16_t fx1 = readInt16(p); p += 2;
                int16_t fy1 = readInt16(p); p += 2;
                float sx0 = x + static_cast<float>(fx0) * scale;
                float sy0 = baselineY - static_cast<float>(fy0) * scale;
                float sx1 = x + static_cast<float>(fx1) * scale;
                float sy1 = baselineY - static_cast<float>(fy1) * scale;
                r.drawLine(sx0, sy0, sx1, sy1, red, green, blue, alpha, stemW);
                break;
            }
        }
    }

    return true;
}

void EbfFont::drawText(BatchRenderer2D& r, float x, float y, float fontSize,
                       std::string_view text,
                       float red, float green, float blue, float alpha) const {
    if (text.empty() || !valid_) return;

    float u = (header_.unitsPerEm > 0) ? static_cast<float>(header_.unitsPerEm) : 1000.0f;
    float scale = fontSize / u;
    float asc = getAscender(fontSize);
    float lineH = getLineHeight(fontSize);

    float curX = x;
    float baselineY = y + asc;

    const char* ptr = text.data();
    const char* end = ptr + text.size();
    uint32_t prevCp = 0;

    while (ptr < end) {
        uint32_t cp = decodeUtf8(ptr, end);
        if (cp == '\n') {
            curX = x;
            baselineY += lineH;
            prevCp = 0;
            continue;
        }
        if (cp == '\r') continue;
        if (cp == '\t') {
            float tabW = 4.0f * (static_cast<float>(header_.maxAdvance) * 0.6f * scale);
            curX = x + (std::floor((curX - x) / tabW) + 1.0f) * tabW;
            prevCp = 0;
            continue;
        }

        const auto* g = findGlyph(cp);
        if (!g) g = findGlyph('?');

        if (prevCp != 0) {
            curX += getKerning(prevCp, cp, fontSize);
        }

        renderGlyph(r, cp, curX, baselineY, fontSize, red, green, blue, alpha);

        float adv = g ? static_cast<float>(g->advanceX) * scale : (static_cast<float>(header_.maxAdvance) * scale * 0.5f);
        curX += adv;
        prevCp = cp;
    }
}

// ============================================================================
// Built-in Embedded Oswald Condensed Grotesque Vector Font
// ============================================================================

namespace {

class EbfBuilder {
public:
    EbfHeader header{};
    std::vector<EbfGlyphRecord> glyphs;
    std::vector<uint8_t> commands;

    EbfBuilder(uint16_t units = 1000, int16_t asc = 800, int16_t desc = -200, int16_t gap = 100) {
        header.unitsPerEm = units;
        header.ascender = asc;
        header.descender = desc;
        header.lineGap = gap;
        header.capHeight = 720;
        header.xHeight = 520;
        header.flags = 2; // Condensed
    }

    void addGlyph(uint32_t cp, int16_t adv, int16_t bx, int16_t by, uint16_t w, uint16_t h,
                  const std::vector<uint8_t>& cmd) {
        EbfGlyphRecord rec{};
        rec.codepoint = cp;
        rec.advanceX = adv;
        rec.bearingX = bx;
        rec.bearingY = by;
        rec.width = w;
        rec.height = h;
        rec.commandOffset = static_cast<uint32_t>(commands.size());
        rec.commandLength = static_cast<uint16_t>(cmd.size());
        glyphs.push_back(rec);
        commands.insert(commands.end(), cmd.begin(), cmd.end());
    }

    // Command generation helpers
    static void pushInt16(std::vector<uint8_t>& out, int16_t val) {
        out.push_back(static_cast<uint8_t>(val & 0xFF));
        out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    }

    static void addVStem(std::vector<uint8_t>& out, int16_t x, int16_t y0, int16_t y1) {
        out.push_back(static_cast<uint8_t>(EbfOpcode::VStem));
        pushInt16(out, x);
        pushInt16(out, y0);
        pushInt16(out, y1);
    }

    static void addHBar(std::vector<uint8_t>& out, int16_t x0, int16_t x1, int16_t y) {
        out.push_back(static_cast<uint8_t>(EbfOpcode::HBar));
        pushInt16(out, x0);
        pushInt16(out, x1);
        pushInt16(out, y);
    }

    static void addDiag(std::vector<uint8_t>& out, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
        out.push_back(static_cast<uint8_t>(EbfOpcode::Diag));
        pushInt16(out, x0);
        pushInt16(out, y0);
        pushInt16(out, x1);
        pushInt16(out, y1);
    }

    static void addChamferRect(std::vector<uint8_t>& out, int16_t x, int16_t y, int16_t w, int16_t h, int16_t c) {
        out.push_back(static_cast<uint8_t>(EbfOpcode::ChamferRect));
        pushInt16(out, x);
        pushInt16(out, y);
        pushInt16(out, w);
        pushInt16(out, h);
        pushInt16(out, c);
    }

    static void addRect(std::vector<uint8_t>& out, int16_t x, int16_t y, int16_t w, int16_t h) {
        out.push_back(static_cast<uint8_t>(EbfOpcode::Rect));
        pushInt16(out, x);
        pushInt16(out, y);
        pushInt16(out, w);
        pushInt16(out, h);
    }

    std::vector<uint8_t> buildBinary() {
        header.glyphCount = static_cast<uint16_t>(glyphs.size());
        int16_t maxAdv = 0;
        for (const auto& g : glyphs) {
            if (g.advanceX > maxAdv) maxAdv = g.advanceX;
        }
        header.maxAdvance = maxAdv;

        std::vector<uint8_t> bin;
        bin.resize(sizeof(EbfHeader) + glyphs.size() * sizeof(EbfGlyphRecord) + commands.size());
        std::memcpy(bin.data(), &header, sizeof(EbfHeader));
        size_t off = sizeof(EbfHeader);
        std::memcpy(bin.data() + off, glyphs.data(), glyphs.size() * sizeof(EbfGlyphRecord));
        off += glyphs.size() * sizeof(EbfGlyphRecord);
        if (!commands.empty()) {
            std::memcpy(bin.data() + off, commands.data(), commands.size());
        }
        return bin;
    }
};

std::unique_ptr<EbfFont> createOswaldCondensedFont() {
    EbfBuilder b(1000, 800, -200, 100);

    // Space
    b.addGlyph(' ', 280, 0, 0, 0, 0, {});

    // Numbers (0-9) - Tabular 500 adv
    {
        // '0'
        std::vector<uint8_t> cmd;
        EbfBuilder::addChamferRect(cmd, 60, 0, 380, 720, 110);
        EbfBuilder::addDiag(cmd, 130, 600, 370, 120);
        b.addGlyph('0', 500, 60, 720, 380, 720, cmd);

        // '1'
        cmd.clear();
        EbfBuilder::addDiag(cmd, 120, 540, 260, 720);
        EbfBuilder::addVStem(cmd, 260, 0, 720);
        EbfBuilder::addHBar(cmd, 100, 400, 0);
        b.addGlyph('1', 500, 100, 720, 300, 720, cmd);

        // '2'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 160, 340, 720);
        EbfBuilder::addDiag(cmd, 80, 620, 160, 720);
        EbfBuilder::addVStem(cmd, 420, 440, 620);
        EbfBuilder::addDiag(cmd, 340, 720, 420, 620);
        EbfBuilder::addDiag(cmd, 420, 440, 80, 0);
        EbfBuilder::addHBar(cmd, 80, 420, 0);
        b.addGlyph('2', 500, 80, 720, 340, 720, cmd);

        // '3'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 80, 420, 720);
        EbfBuilder::addDiag(cmd, 420, 720, 240, 380);
        EbfBuilder::addHBar(cmd, 240, 350, 380);
        EbfBuilder::addVStem(cmd, 420, 90, 320);
        EbfBuilder::addDiag(cmd, 350, 380, 420, 320);
        EbfBuilder::addDiag(cmd, 420, 90, 330, 0);
        EbfBuilder::addHBar(cmd, 110, 330, 0);
        EbfBuilder::addDiag(cmd, 110, 0, 80, 70);
        b.addGlyph('3', 500, 80, 720, 340, 720, cmd);

        // '4'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 360, 0, 720);
        EbfBuilder::addDiag(cmd, 360, 720, 70, 240);
        EbfBuilder::addHBar(cmd, 70, 430, 240);
        b.addGlyph('4', 500, 70, 720, 360, 720, cmd);

        // '5'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 80, 420, 720);
        EbfBuilder::addVStem(cmd, 80, 380, 720);
        EbfBuilder::addHBar(cmd, 80, 340, 380);
        EbfBuilder::addVStem(cmd, 420, 90, 320);
        EbfBuilder::addDiag(cmd, 340, 380, 420, 320);
        EbfBuilder::addDiag(cmd, 420, 90, 330, 0);
        EbfBuilder::addHBar(cmd, 110, 330, 0);
        EbfBuilder::addDiag(cmd, 110, 0, 80, 70);
        b.addGlyph('5', 500, 80, 720, 340, 720, cmd);

        // '6'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 80, 110, 610);
        EbfBuilder::addDiag(cmd, 190, 720, 80, 610);
        EbfBuilder::addHBar(cmd, 190, 400, 720);
        EbfBuilder::addChamferRect(cmd, 80, 0, 340, 400, 90);
        b.addGlyph('6', 500, 80, 720, 340, 720, cmd);

        // '7'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 80, 420, 720);
        EbfBuilder::addDiag(cmd, 420, 720, 180, 0);
        EbfBuilder::addHBar(cmd, 170, 330, 380);
        b.addGlyph('7', 500, 80, 720, 340, 720, cmd);

        // '8'
        cmd.clear();
        EbfBuilder::addChamferRect(cmd, 90, 360, 320, 360, 80);
        EbfBuilder::addChamferRect(cmd, 80, 0, 340, 380, 90);
        b.addGlyph('8', 500, 80, 720, 340, 720, cmd);

        // '9'
        cmd.clear();
        EbfBuilder::addChamferRect(cmd, 80, 320, 340, 400, 90);
        EbfBuilder::addVStem(cmd, 420, 110, 610);
        EbfBuilder::addDiag(cmd, 420, 110, 310, 0);
        EbfBuilder::addHBar(cmd, 100, 310, 0);
        b.addGlyph('9', 500, 80, 720, 340, 720, cmd);
    }

    // Uppercase Letters (A-Z) - Oswald condensed proportions
    {
        // 'A'
        std::vector<uint8_t> cmd;
        EbfBuilder::addDiag(cmd, 60, 0, 210, 720);
        EbfBuilder::addDiag(cmd, 460, 0, 310, 720);
        EbfBuilder::addHBar(cmd, 210, 310, 720);
        EbfBuilder::addHBar(cmd, 140, 380, 260);
        b.addGlyph('A', 520, 60, 720, 400, 720, cmd);

        // 'B'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 720);
        EbfBuilder::addChamferRect(cmd, 70, 360, 360, 360, 80);
        EbfBuilder::addChamferRect(cmd, 70, 0, 380, 380, 80);
        b.addGlyph('B', 510, 70, 720, 380, 720, cmd);

        // 'C'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 180, 450, 720);
        EbfBuilder::addVStem(cmd, 70, 110, 610);
        EbfBuilder::addDiag(cmd, 70, 610, 180, 720);
        EbfBuilder::addDiag(cmd, 70, 110, 180, 0);
        EbfBuilder::addHBar(cmd, 180, 450, 0);
        EbfBuilder::addVStem(cmd, 450, 570, 720);
        EbfBuilder::addVStem(cmd, 450, 0, 150);
        b.addGlyph('C', 510, 70, 720, 380, 720, cmd);

        // 'D'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 720);
        EbfBuilder::addChamferRect(cmd, 70, 0, 400, 720, 120);
        b.addGlyph('D', 530, 70, 720, 400, 720, cmd);

        // 'E'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 720);
        EbfBuilder::addHBar(cmd, 70, 430, 720);
        EbfBuilder::addHBar(cmd, 70, 370, 380);
        EbfBuilder::addHBar(cmd, 70, 430, 0);
        b.addGlyph('E', 470, 70, 720, 360, 720, cmd);

        // 'F'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 720);
        EbfBuilder::addHBar(cmd, 70, 420, 720);
        EbfBuilder::addHBar(cmd, 70, 360, 380);
        b.addGlyph('F', 450, 70, 720, 350, 720, cmd);

        // 'G'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 180, 450, 720);
        EbfBuilder::addVStem(cmd, 70, 110, 610);
        EbfBuilder::addDiag(cmd, 70, 610, 180, 720);
        EbfBuilder::addDiag(cmd, 70, 110, 180, 0);
        EbfBuilder::addHBar(cmd, 180, 450, 0);
        EbfBuilder::addVStem(cmd, 450, 0, 360);
        EbfBuilder::addHBar(cmd, 270, 450, 360);
        b.addGlyph('G', 530, 70, 720, 380, 720, cmd);

        // 'H'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 720);
        EbfBuilder::addVStem(cmd, 440, 0, 720);
        EbfBuilder::addHBar(cmd, 70, 440, 360);
        b.addGlyph('H', 510, 70, 720, 370, 720, cmd);

        // 'I'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 120, 0, 720);
        b.addGlyph('I', 240, 120, 720, 0, 720, cmd);

        // 'J'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 310, 80, 720);
        EbfBuilder::addDiag(cmd, 310, 80, 230, 0);
        EbfBuilder::addHBar(cmd, 120, 230, 0);
        EbfBuilder::addDiag(cmd, 120, 0, 70, 80);
        b.addGlyph('J', 380, 70, 720, 240, 720, cmd);

        // 'K'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 720);
        EbfBuilder::addDiag(cmd, 70, 340, 430, 720);
        EbfBuilder::addDiag(cmd, 230, 480, 440, 0);
        b.addGlyph('K', 490, 70, 720, 370, 720, cmd);

        // 'L'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 720);
        EbfBuilder::addHBar(cmd, 70, 390, 0);
        b.addGlyph('L', 420, 70, 720, 320, 720, cmd);

        // 'M'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 720);
        EbfBuilder::addVStem(cmd, 570, 0, 720);
        EbfBuilder::addDiag(cmd, 70, 720, 320, 0);
        EbfBuilder::addDiag(cmd, 570, 720, 320, 0);
        b.addGlyph('M', 640, 70, 720, 500, 720, cmd);

        // 'N'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 720);
        EbfBuilder::addVStem(cmd, 450, 0, 720);
        EbfBuilder::addDiag(cmd, 70, 720, 450, 0);
        b.addGlyph('N', 520, 70, 720, 380, 720, cmd);

        // 'O'
        cmd.clear();
        EbfBuilder::addChamferRect(cmd, 60, 0, 400, 720, 110);
        b.addGlyph('O', 520, 60, 720, 400, 720, cmd);

        // 'P'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 720);
        EbfBuilder::addChamferRect(cmd, 70, 320, 370, 400, 90);
        b.addGlyph('P', 490, 70, 720, 370, 720, cmd);

        // 'Q'
        cmd.clear();
        EbfBuilder::addChamferRect(cmd, 60, 0, 400, 720, 110);
        EbfBuilder::addDiag(cmd, 290, 160, 470, -70);
        b.addGlyph('Q', 520, 60, 720, 410, 790, cmd);

        // 'R'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 720);
        EbfBuilder::addChamferRect(cmd, 70, 320, 360, 400, 90);
        EbfBuilder::addDiag(cmd, 250, 320, 450, 0);
        b.addGlyph('R', 500, 70, 720, 380, 720, cmd);

        // 'S'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 180, 430, 720);
        EbfBuilder::addVStem(cmd, 70, 430, 610);
        EbfBuilder::addDiag(cmd, 70, 610, 180, 720);
        EbfBuilder::addDiag(cmd, 70, 430, 430, 310);
        EbfBuilder::addVStem(cmd, 430, 110, 310);
        EbfBuilder::addDiag(cmd, 430, 110, 320, 0);
        EbfBuilder::addHBar(cmd, 120, 320, 0);
        b.addGlyph('S', 480, 70, 720, 360, 720, cmd);

        // 'T'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 20, 440, 720);
        EbfBuilder::addVStem(cmd, 230, 0, 720);
        b.addGlyph('T', 460, 20, 720, 420, 720, cmd);

        // 'U'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 110, 720);
        EbfBuilder::addVStem(cmd, 430, 110, 720);
        EbfBuilder::addDiag(cmd, 70, 110, 180, 0);
        EbfBuilder::addDiag(cmd, 430, 110, 320, 0);
        EbfBuilder::addHBar(cmd, 180, 320, 0);
        b.addGlyph('U', 500, 70, 720, 360, 720, cmd);

        // 'V'
        cmd.clear();
        EbfBuilder::addDiag(cmd, 60, 720, 240, 0);
        EbfBuilder::addDiag(cmd, 420, 720, 240, 0);
        b.addGlyph('V', 480, 60, 720, 360, 720, cmd);

        // 'W'
        cmd.clear();
        EbfBuilder::addDiag(cmd, 50, 720, 180, 0);
        EbfBuilder::addDiag(cmd, 180, 0, 330, 460);
        EbfBuilder::addDiag(cmd, 330, 460, 480, 0);
        EbfBuilder::addDiag(cmd, 480, 0, 610, 720);
        b.addGlyph('W', 660, 50, 720, 560, 720, cmd);

        // 'X'
        cmd.clear();
        EbfBuilder::addDiag(cmd, 70, 720, 430, 0);
        EbfBuilder::addDiag(cmd, 430, 720, 70, 0);
        b.addGlyph('X', 500, 70, 720, 360, 720, cmd);

        // 'Y'
        cmd.clear();
        EbfBuilder::addDiag(cmd, 60, 720, 230, 380);
        EbfBuilder::addDiag(cmd, 400, 720, 230, 380);
        EbfBuilder::addVStem(cmd, 230, 0, 380);
        b.addGlyph('Y', 460, 60, 720, 340, 720, cmd);

        // 'Z'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 60, 420, 720);
        EbfBuilder::addDiag(cmd, 420, 720, 60, 0);
        EbfBuilder::addHBar(cmd, 60, 420, 0);
        b.addGlyph('Z', 480, 60, 720, 360, 720, cmd);
    }

    // Lowercase Letters (a-z) - Oswald condensed proportions
    {
        // 'a'
        std::vector<uint8_t> cmd;
        EbfBuilder::addVStem(cmd, 380, 0, 520);
        EbfBuilder::addChamferRect(cmd, 60, 0, 320, 380, 80);
        EbfBuilder::addHBar(cmd, 140, 380, 520);
        b.addGlyph('a', 440, 60, 520, 320, 520, cmd);

        // 'b'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 720);
        EbfBuilder::addChamferRect(cmd, 70, 0, 350, 520, 90);
        b.addGlyph('b', 470, 70, 720, 350, 720, cmd);

        // 'c'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 160, 380, 520);
        EbfBuilder::addVStem(cmd, 70, 90, 430);
        EbfBuilder::addDiag(cmd, 70, 430, 160, 520);
        EbfBuilder::addDiag(cmd, 70, 90, 160, 0);
        EbfBuilder::addHBar(cmd, 160, 380, 0);
        b.addGlyph('c', 430, 70, 520, 310, 520, cmd);

        // 'd'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 380, 0, 720);
        EbfBuilder::addChamferRect(cmd, 70, 0, 310, 520, 90);
        b.addGlyph('d', 470, 70, 720, 310, 720, cmd);

        // 'e'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 70, 370, 260);
        EbfBuilder::addVStem(cmd, 370, 260, 430);
        EbfBuilder::addDiag(cmd, 370, 430, 290, 520);
        EbfBuilder::addHBar(cmd, 160, 290, 520);
        EbfBuilder::addDiag(cmd, 70, 430, 160, 520);
        EbfBuilder::addVStem(cmd, 70, 90, 430);
        EbfBuilder::addDiag(cmd, 70, 90, 160, 0);
        EbfBuilder::addHBar(cmd, 160, 380, 0);
        b.addGlyph('e', 440, 70, 520, 310, 520, cmd);

        // 'f'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 40, 260, 520);
        EbfBuilder::addVStem(cmd, 140, 0, 630);
        EbfBuilder::addDiag(cmd, 140, 630, 230, 720);
        EbfBuilder::addHBar(cmd, 230, 290, 720);
        b.addGlyph('f', 310, 40, 720, 250, 720, cmd);

        // 'g'
        cmd.clear();
        EbfBuilder::addChamferRect(cmd, 70, 0, 340, 520, 90);
        EbfBuilder::addVStem(cmd, 410, -110, 520);
        EbfBuilder::addDiag(cmd, 410, -110, 320, -200);
        EbfBuilder::addHBar(cmd, 110, 320, -200);
        b.addGlyph('g', 470, 70, 520, 340, 720, cmd);

        // 'h'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 720);
        EbfBuilder::addHBar(cmd, 160, 330, 520);
        EbfBuilder::addDiag(cmd, 70, 430, 160, 520);
        EbfBuilder::addDiag(cmd, 330, 520, 400, 430);
        EbfBuilder::addVStem(cmd, 400, 0, 430);
        b.addGlyph('h', 470, 70, 720, 330, 720, cmd);

        // 'i'
        cmd.clear();
        EbfBuilder::addRect(cmd, 75, 620, 70, 90); // Square dot
        EbfBuilder::addVStem(cmd, 110, 0, 520);
        b.addGlyph('i', 220, 75, 710, 70, 710, cmd);

        // 'j'
        cmd.clear();
        EbfBuilder::addRect(cmd, 105, 620, 70, 90); // Square dot
        EbfBuilder::addVStem(cmd, 140, -100, 520);
        EbfBuilder::addDiag(cmd, 140, -100, 60, -200);
        EbfBuilder::addHBar(cmd, 20, 60, -200);
        b.addGlyph('j', 240, 20, 710, 120, 910, cmd);

        // 'k'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 720);
        EbfBuilder::addDiag(cmd, 70, 240, 360, 520);
        EbfBuilder::addDiag(cmd, 180, 330, 370, 0);
        b.addGlyph('k', 430, 70, 720, 300, 720, cmd);

        // 'l'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 110, 0, 720);
        b.addGlyph('l', 220, 110, 720, 0, 720, cmd);

        // 'm'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 60, 0, 520);
        EbfBuilder::addHBar(cmd, 60, 300, 520);
        EbfBuilder::addVStem(cmd, 300, 0, 520);
        EbfBuilder::addHBar(cmd, 300, 540, 520);
        EbfBuilder::addVStem(cmd, 540, 0, 520);
        b.addGlyph('m', 620, 60, 520, 480, 520, cmd);

        // 'n'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 520);
        EbfBuilder::addHBar(cmd, 160, 330, 520);
        EbfBuilder::addDiag(cmd, 70, 430, 160, 520);
        EbfBuilder::addDiag(cmd, 330, 520, 400, 430);
        EbfBuilder::addVStem(cmd, 400, 0, 430);
        b.addGlyph('n', 470, 70, 520, 330, 520, cmd);

        // 'o'
        cmd.clear();
        EbfBuilder::addChamferRect(cmd, 60, 0, 340, 520, 90);
        b.addGlyph('o', 460, 60, 520, 340, 520, cmd);

        // 'p'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, -200, 520);
        EbfBuilder::addChamferRect(cmd, 70, 0, 340, 520, 90);
        b.addGlyph('p', 470, 70, 520, 340, 720, cmd);

        // 'q'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 370, -200, 520);
        EbfBuilder::addChamferRect(cmd, 70, 0, 300, 520, 90);
        b.addGlyph('q', 470, 70, 520, 300, 720, cmd);

        // 'r'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 0, 520);
        EbfBuilder::addHBar(cmd, 160, 290, 520);
        EbfBuilder::addDiag(cmd, 70, 430, 160, 520);
        b.addGlyph('r', 340, 70, 520, 220, 520, cmd);

        // 's'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 140, 330, 520);
        EbfBuilder::addVStem(cmd, 70, 310, 440);
        EbfBuilder::addDiag(cmd, 70, 440, 140, 520);
        EbfBuilder::addDiag(cmd, 70, 310, 340, 210);
        EbfBuilder::addVStem(cmd, 340, 80, 210);
        EbfBuilder::addDiag(cmd, 340, 80, 270, 0);
        EbfBuilder::addHBar(cmd, 100, 270, 0);
        b.addGlyph('s', 410, 70, 520, 270, 520, cmd);

        // 't'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 40, 280, 520);
        EbfBuilder::addVStem(cmd, 140, 80, 660);
        EbfBuilder::addDiag(cmd, 140, 80, 210, 0);
        EbfBuilder::addHBar(cmd, 210, 280, 0);
        b.addGlyph('t', 320, 40, 660, 240, 660, cmd);

        // 'u'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 70, 90, 520);
        EbfBuilder::addVStem(cmd, 380, 0, 520);
        EbfBuilder::addDiag(cmd, 70, 90, 160, 0);
        EbfBuilder::addHBar(cmd, 160, 380, 0);
        b.addGlyph('u', 460, 70, 520, 310, 520, cmd);

        // 'v'
        cmd.clear();
        EbfBuilder::addDiag(cmd, 50, 520, 210, 0);
        EbfBuilder::addDiag(cmd, 370, 520, 210, 0);
        b.addGlyph('v', 420, 50, 520, 320, 520, cmd);

        // 'w'
        cmd.clear();
        EbfBuilder::addDiag(cmd, 40, 520, 150, 0);
        EbfBuilder::addDiag(cmd, 150, 0, 290, 360);
        EbfBuilder::addDiag(cmd, 290, 360, 430, 0);
        EbfBuilder::addDiag(cmd, 430, 0, 540, 520);
        b.addGlyph('w', 580, 40, 520, 500, 520, cmd);

        // 'x'
        cmd.clear();
        EbfBuilder::addDiag(cmd, 60, 520, 350, 0);
        EbfBuilder::addDiag(cmd, 350, 520, 60, 0);
        b.addGlyph('x', 410, 60, 520, 290, 520, cmd);

        // 'y'
        cmd.clear();
        EbfBuilder::addDiag(cmd, 50, 520, 190, 160);
        EbfBuilder::addDiag(cmd, 350, 520, 90, -200);
        b.addGlyph('y', 400, 50, 520, 300, 720, cmd);

        // 'z'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 50, 350, 520);
        EbfBuilder::addDiag(cmd, 350, 520, 50, 0);
        EbfBuilder::addHBar(cmd, 50, 350, 0);
        b.addGlyph('z', 400, 50, 520, 300, 520, cmd);
    }

    // Punctuation & Common Symbols
    {
        // '.'
        std::vector<uint8_t> cmd;
        EbfBuilder::addRect(cmd, 90, 0, 80, 80);
        b.addGlyph('.', 260, 90, 80, 80, 80, cmd);

        // ','
        cmd.clear();
        EbfBuilder::addRect(cmd, 90, 0, 80, 80);
        EbfBuilder::addDiag(cmd, 130, 40, 70, -70);
        b.addGlyph(',', 260, 70, 80, 80, 150, cmd);

        // ':'
        cmd.clear();
        EbfBuilder::addRect(cmd, 90, 80, 80, 80);
        EbfBuilder::addRect(cmd, 90, 360, 80, 80);
        b.addGlyph(':', 260, 90, 440, 80, 360, cmd);

        // ';'
        cmd.clear();
        EbfBuilder::addRect(cmd, 90, 360, 80, 80);
        EbfBuilder::addRect(cmd, 90, 0, 80, 80);
        EbfBuilder::addDiag(cmd, 130, 40, 70, -70);
        b.addGlyph(';', 260, 70, 440, 80, 510, cmd);

        // '!'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 130, 200, 720);
        EbfBuilder::addRect(cmd, 90, 0, 80, 80);
        b.addGlyph('!', 260, 90, 720, 80, 720, cmd);

        // '?'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 140, 290, 720);
        EbfBuilder::addVStem(cmd, 70, 530, 640);
        EbfBuilder::addDiag(cmd, 70, 640, 140, 720);
        EbfBuilder::addVStem(cmd, 350, 430, 640);
        EbfBuilder::addDiag(cmd, 290, 720, 350, 640);
        EbfBuilder::addDiag(cmd, 350, 430, 210, 280);
        EbfBuilder::addVStem(cmd, 210, 190, 280);
        EbfBuilder::addRect(cmd, 170, 0, 80, 80);
        b.addGlyph('?', 420, 70, 720, 280, 720, cmd);

        // '-'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 60, 280, 320);
        b.addGlyph('-', 340, 60, 320, 220, 0, cmd);

        // '+'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 60, 380, 360);
        EbfBuilder::addVStem(cmd, 220, 200, 520);
        b.addGlyph('+', 440, 60, 520, 320, 320, cmd);

        // '='
        cmd.clear();
        EbfBuilder::addHBar(cmd, 60, 380, 440);
        EbfBuilder::addHBar(cmd, 60, 380, 280);
        b.addGlyph('=', 440, 60, 440, 320, 160, cmd);

        // '('
        cmd.clear();
        EbfBuilder::addDiag(cmd, 210, 720, 100, 540);
        EbfBuilder::addVStem(cmd, 100, 180, 540);
        EbfBuilder::addDiag(cmd, 100, 180, 210, 0);
        b.addGlyph('(', 280, 100, 720, 110, 720, cmd);

        // ')'
        cmd.clear();
        EbfBuilder::addDiag(cmd, 90, 720, 200, 540);
        EbfBuilder::addVStem(cmd, 200, 180, 540);
        EbfBuilder::addDiag(cmd, 200, 180, 90, 0);
        b.addGlyph(')', 280, 90, 720, 110, 720, cmd);

        // '['
        cmd.clear();
        EbfBuilder::addVStem(cmd, 100, 0, 720);
        EbfBuilder::addHBar(cmd, 100, 220, 720);
        EbfBuilder::addHBar(cmd, 100, 220, 0);
        b.addGlyph('[', 280, 100, 720, 120, 720, cmd);

        // ']'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 180, 0, 720);
        EbfBuilder::addHBar(cmd, 60, 180, 720);
        EbfBuilder::addHBar(cmd, 60, 180, 0);
        b.addGlyph(']', 280, 60, 720, 120, 720, cmd);

        // '{'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 130, 210, 720);
        EbfBuilder::addVStem(cmd, 130, 430, 720);
        EbfBuilder::addDiag(cmd, 130, 430, 70, 360);
        EbfBuilder::addDiag(cmd, 70, 360, 130, 290);
        EbfBuilder::addVStem(cmd, 130, 0, 290);
        EbfBuilder::addHBar(cmd, 130, 210, 0);
        b.addGlyph('{', 280, 70, 720, 140, 720, cmd);

        // '}'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 70, 150, 720);
        EbfBuilder::addVStem(cmd, 150, 430, 720);
        EbfBuilder::addDiag(cmd, 150, 430, 210, 360);
        EbfBuilder::addDiag(cmd, 210, 360, 150, 290);
        EbfBuilder::addVStem(cmd, 150, 0, 290);
        EbfBuilder::addHBar(cmd, 70, 150, 0);
        b.addGlyph('}', 280, 70, 720, 140, 720, cmd);

        // '/'
        cmd.clear();
        EbfBuilder::addDiag(cmd, 50, 0, 310, 720);
        b.addGlyph('/', 360, 50, 720, 260, 720, cmd);

        // '\'
        cmd.clear();
        EbfBuilder::addDiag(cmd, 50, 720, 310, 0);
        b.addGlyph('\\', 360, 50, 720, 260, 720, cmd);

        // '|'
        cmd.clear();
        EbfBuilder::addVStem(cmd, 110, -100, 750);
        b.addGlyph('|', 220, 110, 750, 0, 850, cmd);

        // '_'
        cmd.clear();
        EbfBuilder::addHBar(cmd, 20, 460, -100);
        b.addGlyph('_', 480, 20, -100, 440, 0, cmd);

        // '<'
        cmd.clear();
        EbfBuilder::addDiag(cmd, 330, 580, 70, 360);
        EbfBuilder::addDiag(cmd, 70, 360, 330, 140);
        b.addGlyph('<', 400, 70, 580, 260, 440, cmd);

        // '>'
        cmd.clear();
        EbfBuilder::addDiag(cmd, 70, 580, 330, 360);
        EbfBuilder::addDiag(cmd, 330, 360, 70, 140);
        b.addGlyph('>', 400, 70, 580, 260, 440, cmd);
    }

    std::vector<uint8_t> bin = b.buildBinary();
    auto font = std::make_unique<EbfFont>();
    font->loadFromMemory(bin.data(), bin.size());
    return font;
}

#include "eatsbits/ui/glyph/oswald_embedded_data.hpp"
#include "eatsbits/ui/glyph/sharetech_embedded_data.hpp"

} // anonymous namespace

const EbfFont& EbfFont::getEmbeddedOswaldCondensed() {
    static const std::unique_ptr<EbfFont> s_oswald = []() {
        auto f = std::make_unique<EbfFont>();
        if (!f->loadFromMemory(kOswaldEmbeddedData, kOswaldEmbeddedSize)) {
            return createOswaldCondensedFont();
        }
        return f;
    }();
    return *s_oswald;
}

const EbfFont& EbfFont::getEmbeddedShareTechMono() {
    static const std::unique_ptr<EbfFont> s_shareTech = []() {
        auto f = std::make_unique<EbfFont>();
        if (!f->loadFromMemory(kShareTechEmbeddedData, kShareTechEmbeddedSize)) {
            return createOswaldCondensedFont();
        }
        return f;
    }();
    return *s_shareTech;
}

} // namespace eatsbits::ui::glyph
