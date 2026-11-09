#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <algorithm>

namespace eatsbits::tui {

enum class TextAttr : uint8_t {
    None         = 0,
    Bold         = 1 << 0,
    Dim          = 1 << 1,
    Underline    = 1 << 2,
    Reverse      = 1 << 3,
    Blink        = 1 << 4,
    Continuation = 1 << 5 // Non-rendering continuation cell (for multi-cell ligatures/wide chars)
};

inline constexpr TextAttr operator|(TextAttr a, TextAttr b) {
    return static_cast<TextAttr>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline constexpr TextAttr operator&(TextAttr a, TextAttr b) {
    return static_cast<TextAttr>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline constexpr TextAttr operator~(TextAttr a) {
    return static_cast<TextAttr>(~static_cast<uint8_t>(a));
}

struct Color {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};
    bool isDefault{false};

    constexpr Color() : r(0), g(0), b(0), a(255), isDefault(true) {}
    constexpr Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
        : r(red), g(green), b(blue), a(alpha), isDefault(false) {}

    static constexpr Color DefaultFg() {
        Color c;
        c.isDefault = true;
        c.r = 240; c.g = 240; c.b = 240;
        return c;
    }

    static constexpr Color DefaultBg() {
        Color c;
        c.isDefault = true;
        c.r = 18; c.g = 18; c.b = 20;
        return c;
    }

    static constexpr Color FromRgb(uint8_t r, uint8_t g, uint8_t b) {
        return Color(r, g, b);
    }

    static constexpr Color FromHex(uint32_t hex) {
        return Color(
            static_cast<uint8_t>((hex >> 16) & 0xFF),
            static_cast<uint8_t>((hex >> 8) & 0xFF),
            static_cast<uint8_t>(hex & 0xFF)
        );
    }

    bool operator==(const Color& other) const {
        if (isDefault && other.isDefault) return true;
        return isDefault == other.isDefault && r == other.r && g == other.g && b == other.b && a == other.a;
    }

    bool operator!=(const Color& other) const {
        return !(*this == other);
    }

    Color blend(const Color& other, float t) const {
        t = std::clamp(t, 0.0f, 1.0f);
        return Color(
            static_cast<uint8_t>(r + (other.r - r) * t),
            static_cast<uint8_t>(g + (other.g - g) * t),
            static_cast<uint8_t>(b + (other.b - b) * t)
        );
    }

    // Curated DAW color palette
    static constexpr Color Black()       { return FromHex(0x101012); }
    static constexpr Color DarkGray()    { return FromHex(0x222328); }
    static constexpr Color Gray()        { return FromHex(0x555864); }
    static constexpr Color LightGray()   { return FromHex(0x9Ea2B0); }
    static constexpr Color White()       { return FromHex(0xF5F6FA); }
    static constexpr Color AcidAmber()   { return FromHex(0xFF7700); }
    static constexpr Color AcidYellow()  { return FromHex(0xFFD600); }
    static constexpr Color CyberCyan()   { return FromHex(0x00E5FF); }
    static constexpr Color NeonGreen()   { return FromHex(0x00E676); }
    static constexpr Color RetroPurple() { return FromHex(0xD500F9); }
    static constexpr Color DangerRed()   { return FromHex(0xFF1744); }
    static constexpr Color MeterGreen()  { return FromHex(0x2ECC71); }
    static constexpr Color MeterYellow() { return FromHex(0xF1C40F); }
    static constexpr Color MeterRed()    { return FromHex(0xE74C3C); }
};

struct Cell {
    char32_t codepoint{' '};
    Color fg{Color::DefaultFg()};
    Color bg{Color::DefaultBg()};
    uint8_t attrs{0};

    bool operator==(const Cell& other) const {
        return codepoint == other.codepoint &&
               attrs == other.attrs &&
               fg == other.fg &&
               bg == other.bg;
    }

    bool operator!=(const Cell& other) const {
        return !(*this == other);
    }
};

struct Point {
    int x{0};
    int y{0};
};

struct Size {
    int width{0};
    int height{0};
};

struct Rect {
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    bool contains(int px, int py) const {
        return px >= x && px < (x + width) && py >= y && py < (y + height);
    }
};

} // namespace eatsbits::tui
