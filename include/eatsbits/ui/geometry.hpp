#pragma once

#include <algorithm>
#include <vector>
#include <cmath>
#include <string_view>
#include <cctype>

namespace eatsbits::ui {

inline bool stringEqualsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

/**
 * 2D Point structure with basic arithmetic operations.
 */
struct Point {
    float x{0.0f};
    float y{0.0f};

    constexpr Point() noexcept = default;
    constexpr Point(float x_, float y_) noexcept : x(x_), y(y_) {}

    [[nodiscard]] constexpr Point operator+(const Point& other) const noexcept {
        return {x + other.x, y + other.y};
    }
    [[nodiscard]] constexpr Point operator-(const Point& other) const noexcept {
        return {x - other.x, y - other.y};
    }
    [[nodiscard]] constexpr Point operator*(float scalar) const noexcept {
        return {x * scalar, y * scalar};
    }
    [[nodiscard]] float distanceTo(const Point& other) const noexcept {
        float dx = x - other.x;
        float dy = y - other.y;
        return std::sqrt(dx * dx + dy * dy);
    }
};

using Point2D = Point;

/**
 * Fundamental 2D Rectangle structure providing a single source of truth for
 * layout calculation, bounding box geometry, slicing, and hit-testing.
 */
struct Rect {
    float x{0.0f};
    float y{0.0f};
    float w{0.0f};
    float h{0.0f};

    constexpr Rect() noexcept = default;
    constexpr Rect(float x_, float y_, float w_, float h_) noexcept
        : x(x_), y(y_), w(w_), h(h_) {}

    [[nodiscard]] constexpr float left() const noexcept { return x; }
    [[nodiscard]] constexpr float top() const noexcept { return y; }
    [[nodiscard]] constexpr float right() const noexcept { return x + w; }
    [[nodiscard]] constexpr float bottom() const noexcept { return y + h; }
    [[nodiscard]] constexpr Point center() const noexcept { return {x + w * 0.5f, y + h * 0.5f}; }

    [[nodiscard]] constexpr bool isEmpty() const noexcept { return w <= 0.0f || h <= 0.0f; }

    [[nodiscard]] constexpr bool contains(float px, float py) const noexcept {
        return px >= x && px <= (x + w) && py >= y && py <= (y + h);
    }

    [[nodiscard]] constexpr bool contains(const Point& pt) const noexcept {
        return contains(pt.x, pt.y);
    }

    [[nodiscard]] constexpr bool intersects(const Rect& other) const noexcept {
        return x < other.right() && right() > other.x &&
               y < other.bottom() && bottom() > other.y;
    }

    [[nodiscard]] constexpr Rect inset(float pad) const noexcept {
        return inset(pad, pad);
    }

    [[nodiscard]] constexpr Rect inset(float dx, float dy) const noexcept {
        float nw = std::max(0.0f, w - 2.0f * dx);
        float nh = std::max(0.0f, h - 2.0f * dy);
        return {x + dx, y + dy, nw, nh};
    }

    Rect cutLeft(float amount) noexcept {
        float sliceW = std::clamp(amount, 0.0f, w);
        Rect slice(x, y, sliceW, h);
        x += sliceW;
        w -= sliceW;
        return slice;
    }

    Rect cutRight(float amount) noexcept {
        float sliceW = std::clamp(amount, 0.0f, w);
        w -= sliceW;
        return Rect(x + w, y, sliceW, h);
    }

    Rect cutTop(float amount) noexcept {
        float sliceH = std::clamp(amount, 0.0f, h);
        Rect slice(x, y, w, sliceH);
        y += sliceH;
        h -= sliceH;
        return slice;
    }

    Rect cutBottom(float amount) noexcept {
        float sliceH = std::clamp(amount, 0.0f, h);
        h -= sliceH;
        return Rect(x, y + h, w, sliceH);
    }

    [[nodiscard]] std::vector<Rect> splitHorizontal(size_t n, float gap = 0.0f) const {
        if (n == 0) return {};
        std::vector<Rect> result;
        result.reserve(n);
        float totalGap = gap * static_cast<float>(n - 1);
        float cellW = std::max(0.0f, (w - totalGap) / static_cast<float>(n));
        for (size_t i = 0; i < n; ++i) {
            result.emplace_back(x + static_cast<float>(i) * (cellW + gap), y, cellW, h);
        }
        return result;
    }

    [[nodiscard]] std::vector<Rect> splitVertical(size_t n, float gap = 0.0f) const {
        if (n == 0) return {};
        std::vector<Rect> result;
        result.reserve(n);
        float totalGap = gap * static_cast<float>(n - 1);
        float cellH = std::max(0.0f, (h - totalGap) / static_cast<float>(n));
        for (size_t i = 0; i < n; ++i) {
            result.emplace_back(x, y + static_cast<float>(i) * (cellH + gap), w, cellH);
        }
        return result;
    }
};

using Rect2D = Rect;

/**
 * Sequential layout helper for advancing rows and columns without manual arithmetic.
 */
class LayoutBox {
public:
    enum class Direction {
        Horizontal,
        Vertical
    };

    explicit LayoutBox(Rect bounds, Direction dir = Direction::Vertical, float defaultGap = 6.0f) noexcept
        : remaining_(bounds), direction_(dir), gap_(defaultGap) {}

    Rect next(float size) noexcept {
        if (direction_ == Direction::Horizontal) {
            Rect item = remaining_.cutLeft(size);
            remaining_.cutLeft(gap_);
            return item;
        } else {
            Rect item = remaining_.cutTop(size);
            remaining_.cutTop(gap_);
            return item;
        }
    }

    [[nodiscard]] const Rect& remaining() const noexcept { return remaining_; }

private:
    Rect remaining_;
    Direction direction_{Direction::Vertical};
    float gap_{6.0f};
};

struct ShadowOffset {
    float dx{0.0f};
    float dy{0.0f};
    float blur{0.0f};
    float opacity{0.45f};
};

struct LightSource2D {
    float x{640.0f};
    float y{-180.0f};
    float z{500.0f};
    float intensity{0.85f};

    constexpr LightSource2D() noexcept = default;
    constexpr LightSource2D(float x_, float y_, float z_, float intensity_ = 0.85f) noexcept
        : x(x_), y(y_), z(z_), intensity(intensity_) {}

    [[nodiscard]] ShadowOffset computeShadowOffset(float elementX, float elementY,
                                                  float elevation, float baseOpacity = 0.45f) const noexcept {
        float effectiveZ = std::max(1.0f, z);
        float hRatio = elevation / effectiveZ;
        float dx = (elementX - x) * hRatio;
        float dy = (elementY - y) * hRatio;
        float blur = elevation * 0.40f;
        float opacity = std::clamp(baseOpacity * intensity, 0.0f, 1.0f);
        return {dx, dy, blur, opacity};
    }

    [[nodiscard]] float getLightAngle(float elementX, float elementY) const noexcept {
        return std::atan2(y - elementY, x - elementX);
    }

    [[nodiscard]] Point getLightDirection(float elementX, float elementY) const noexcept {
        float dx = x - elementX;
        float dy = y - elementY;
        float len = std::hypot(dx, dy);
        if (len < 0.0001f) return {0.0f, -1.0f};
        return {dx / len, dy / len};
    }
};

} // namespace eatsbits::ui
