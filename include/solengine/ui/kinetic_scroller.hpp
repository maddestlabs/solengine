#ifndef SOLENGINE_KINETIC_SCROLLER_HPP
#define SOLENGINE_KINETIC_SCROLLER_HPP

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace solengine {

/**
 * Reusable iOS-style kinetic scroll controller for SolEngine UI lists and panels.
 *
 * Features:
 *  - Timestamped velocity ring buffer (immune to frame-rate jitter & mobile-web event batching)
 *  - Authentic iOS UIScrollView exponential momentum deceleration
 *  - Apple rubber-band overscroll resistance while dragging past edges
 *  - Critically damped spring-back at top/bottom bounds (zero oscillation)
 *  - Tap-to-stop ("catch") semantics so tapping a coasting list halts it without clicking a row
 *  - Optional horizontal-vs-vertical axis lock (for panels containing horizontal sliders)
 *  - Smooth mouse-wheel easing and auto-fading, overscroll-compressing scrollbar geometry
 */
class KineticScroller {
public:
    KineticScroller() = default;

    /// Reset all scroll, momentum, and gesture state (e.g. on dialog open or filter change).
    void reset(float initialOffset = 0.0f) noexcept {
        offset_ = std::max(0.0f, initialOffset);
        velocity_ = 0.0f;
        wheelSmoothing_ = false;
        wheelTarget_ = offset_;
        pointerDown_ = false;
        isDragging_ = false;
        caughtCoast_ = false;
        axisRejected_ = false;
        sampleCount_ = 0;
        sampleHead_ = 0;
        indicatorAlpha_ = 0.0f;
        indicatorHoldTimer_ = 0.0f;
    }

    /// Update scrollable content height and viewport height (call each layout/render).
    /// Does NOT hard-clamp active overscroll so rubber-banding and spring-back render smoothly.
    void setBounds(float contentHeight, float viewHeight) noexcept {
        viewHeight_ = std::max(1.0f, viewHeight);
        contentHeight_ = std::max(0.0f, contentHeight);
        maxOffset_ = std::max(0.0f, contentHeight_ - viewHeight_);

        if (maxOffset_ <= 0.5f && !pointerDown_) {
            if (std::abs(offset_) < 0.5f && std::abs(velocity_) < 1.0f) {
                offset_ = 0.0f;
                velocity_ = 0.0f;
            }
            wheelSmoothing_ = false;
            wheelTarget_ = 0.0f;
        } else if (wheelSmoothing_) {
            wheelTarget_ = std::clamp(wheelTarget_, 0.0f, maxOffset_);
        }
    }

    /// Directly set scroll offset (e.g. scrollbar thumb drag or keyboard jump).
    void setOffsetImmediate(float y) noexcept {
        offset_ = std::clamp(y, 0.0f, maxOffset_);
        velocity_ = 0.0f;
        wheelSmoothing_ = false;
        wheelTarget_ = offset_;
        wakeIndicator();
    }

    /// Programmatically scroll to a target offset (smooth or immediate).
    void scrollTo(float targetY, bool animate = true) noexcept {
        const float clamped = std::clamp(targetY, 0.0f, maxOffset_);
        if (!animate) {
            setOffsetImmediate(clamped);
            return;
        }
        velocity_ = 0.0f;
        wheelTarget_ = clamped;
        wheelSmoothing_ = true;
        wakeIndicator();
    }

    /// Ensure an item range [itemTop, itemBottom] is visible inside [0, visibleH].
    void ensureRangeVisible(float itemTop, float itemBottom, float visibleH, bool animate = true) noexcept {
        const float current = wheelSmoothing_ ? wheelTarget_ : std::clamp(offset_, 0.0f, maxOffset_);
        float target = current;
        if (itemTop < current) {
            target = itemTop;
        } else if (itemBottom > current + visibleH) {
            target = itemBottom - visibleH;
        }
        target = std::clamp(target, 0.0f, maxOffset_);
        if (std::abs(target - current) > 0.5f) {
            scrollTo(target, animate);
        }
    }

    /// Apply a mouse-wheel / trackpad scroll delta (positive yoffset = scroll up, matching GLFW).
    void handleWheel(float yoffset, float pixelsPerTick = 42.0f) noexcept {
        if (maxOffset_ <= 0.5f) return;
        const float base = wheelSmoothing_ ? wheelTarget_ : std::clamp(offset_, 0.0f, maxOffset_);
        wheelTarget_ = std::clamp(base - yoffset * pixelsPerTick, 0.0f, maxOffset_);
        wheelSmoothing_ = true;
        velocity_ = 0.0f;
        wakeIndicator();
    }

    /// Begin pointer/touch press inside the scrollable viewport.
    /// Returns true if this press "caught" (stopped) an active coasting scroll.
    bool beginDrag(float pointerX, float pointerY) noexcept {
        pointerDown_ = true;
        isDragging_ = false;
        axisRejected_ = false;
        wheelSmoothing_ = false;
        dragStartX_ = pointerX;
        dragStartY_ = pointerY;
        dragLastY_ = pointerY;

        // If the list is actively coasting, arrest momentum and flag as caught so release won't click an item.
        caughtCoast_ = (std::abs(velocity_) > kCatchVelocityThreshold);
        velocity_ = 0.0f;

        // Convert any active rubber-banded visual offset back into linear drag space so grabbing mid-bounce is seamless.
        initialLinearOffset_ = visualToLinear(offset_);

        sampleCount_ = 0;
        sampleHead_ = 0;
        pushSample(pointerY);
        return caughtCoast_;
    }

    /// Overload when X coordinate is not needed.
    bool beginDrag(float pointerY) noexcept {
        return beginDrag(0.0f, pointerY);
    }

    /// Update pointer/touch movement while pressed.
    /// If allowAxisLock is true and horizontal movement clearly dominates before the drag threshold,
    /// the scroller yields so horizontal controls (like sliders) can own the gesture.
    /// Returns true if actively scroll-dragging.
    bool moveDrag(float pointerX, float pointerY, bool allowAxisLock = false) noexcept {
        if (!pointerDown_ || axisRejected_) return false;
        if (maxOffset_ <= 0.5f) return false;

        const float dx = pointerX - dragStartX_;
        const float dy = pointerY - dragStartY_;

        if (!isDragging_) {
            if (allowAxisLock && std::abs(dx) > kDragThreshold && std::abs(dx) > std::abs(dy) * 1.15f) {
                axisRejected_ = true;
                return false;
            }
            if (std::abs(dy) > kDragThreshold) {
                isDragging_ = true;
                // Re-anchor drag start slightly to avoid a sudden jump equal to kDragThreshold.
                dragStartY_ = pointerY - (dy > 0.0f ? kDragThreshold : -kDragThreshold);
            }
        }

        if (isDragging_) {
            const float effDy = pointerY - dragStartY_;
            const float linear = initialLinearOffset_ - effDy;
            offset_ = linearToVisual(linear);
            dragLastY_ = pointerY;
            pushSample(pointerY);
            wakeIndicator();
        }

        return isDragging_;
    }

    bool moveDrag(float pointerY) noexcept {
        return moveDrag(0.0f, pointerY, false);
    }

    /// End pointer/touch press. Launches momentum coasting if dragged.
    /// Returns true if the gesture should be treated as a scroll (i.e. suppress item click/tap).
    bool endDrag() noexcept {
        if (!pointerDown_) return false;
        const bool suppressClick = isDragging_ || caughtCoast_;

        if (isDragging_) {
            velocity_ = computeReleaseVelocity();
            wakeIndicator();
        }

        pointerDown_ = false;
        isDragging_ = false;
        caughtCoast_ = false;
        axisRejected_ = false;
        return suppressClick;
    }

    /// Cancel an in-progress drag without launching momentum (e.g. pinch-zoom took over).
    void cancelDrag() noexcept {
        pointerDown_ = false;
        isDragging_ = false;
        caughtCoast_ = false;
        axisRejected_ = false;
        sampleCount_ = 0;
    }

    /// Advance scroll inertia, rubber-band spring-back, wheel smoothing, and indicator fade.
    void update(float dt) noexcept {
        const float safeDt = std::clamp(dt, 0.0f, 0.05f);
        if (safeDt <= 0.0f) return;

        bool activeMotion = false;

        if (pointerDown_ && isDragging_) {
            activeMotion = true;
        } else if (wheelSmoothing_) {
            const float alpha = 1.0f - std::exp(-kWheelEaseRate * safeDt);
            offset_ += (wheelTarget_ - offset_) * alpha;
            if (std::abs(wheelTarget_ - offset_) < 0.35f) {
                offset_ = wheelTarget_;
                wheelSmoothing_ = false;
            } else {
                activeMotion = true;
            }
        } else {
            // Free coasting or out-of-bounds spring-back
            const bool belowTop = (offset_ < 0.0f);
            const bool pastBottom = (offset_ > maxOffset_);

            if (belowTop || pastBottom) {
                const float edge = belowTop ? 0.0f : maxOffset_;
                // Critically damped spring back to nearest edge (zero oscillation)
                const float x = offset_ - edge;
                const float w = kBounceOmega;
                const float e = std::exp(-w * safeDt);
                const float newX = (x + (velocity_ + w * x) * safeDt) * e;
                const float newV = (velocity_ - (velocity_ + w * x) * w * safeDt) * e;
                offset_ = edge + newX;
                velocity_ = newV;

                if (std::abs(offset_ - edge) < 0.35f && std::abs(velocity_) < 8.0f) {
                    offset_ = edge;
                    velocity_ = 0.0f;
                } else {
                    activeMotion = true;
                }
            } else if (std::abs(velocity_) > 0.0f) {
                // Exact exponential deceleration (iOS UIScrollView feel)
                const float decay = std::exp(-kDecelRate * safeDt);
                offset_ += velocity_ * (1.0f - decay) / kDecelRate;
                velocity_ *= decay;

                // If momentum just carried us past an edge, damp excess outward velocity slightly
                // so fast flicks produce a tight, natural iOS bounce (~40-75px peak).
                if (offset_ < 0.0f || offset_ > maxOffset_) {
                    velocity_ *= 0.62f;
                }

                if (std::abs(velocity_) < kStopVelocityThreshold &&
                    offset_ >= 0.0f && offset_ <= maxOffset_) {
                    velocity_ = 0.0f;
                } else {
                    activeMotion = true;
                }
            }
        }

        // Scrollbar indicator fade state
        if (activeMotion) {
            wakeIndicator();
            indicatorAlpha_ = std::min(1.0f, indicatorAlpha_ + safeDt * 12.0f);
        } else {
            if (indicatorHoldTimer_ > 0.0f) {
                indicatorHoldTimer_ = std::max(0.0f, indicatorHoldTimer_ - safeDt);
            } else if (indicatorAlpha_ > 0.0f) {
                indicatorAlpha_ = std::max(0.0f, indicatorAlpha_ - safeDt * 2.6f);
            }
        }
    }

    /// Compute iOS-style scrollbar thumb geometry (compresses smoothly during overscroll).
    void computeThumb(float trackY, float trackH, float& outThumbY, float& outThumbH) const noexcept {
        const float safeTrackH = std::max(20.0f, trackH);
        const float denomContent = std::max(viewHeight_, contentHeight_);
        const float visibleRatio = std::clamp(viewHeight_ / std::max(1.0f, denomContent), 0.12f, 1.0f);
        float thumbH = std::max(24.0f, safeTrackH * visibleRatio);
        float thumbY = trackY;

        if (maxOffset_ > 0.5f) {
            if (offset_ < 0.0f) {
                const float over = -offset_;
                thumbH = std::max(12.0f, thumbH - over * 0.45f);
                thumbY = trackY;
            } else if (offset_ > maxOffset_) {
                const float over = offset_ - maxOffset_;
                thumbH = std::max(12.0f, thumbH - over * 0.45f);
                thumbY = trackY + safeTrackH - thumbH;
            } else {
                const float frac = offset_ / maxOffset_;
                thumbY = trackY + frac * (safeTrackH - thumbH);
            }
        }

        outThumbY = thumbY;
        outThumbH = thumbH;
    }

    /// Current visual scroll offset (may be < 0 or > maxOffset during rubber-band bounce).
    [[nodiscard]] float offset() const noexcept { return offset_; }
    /// Current scroll offset clamped to [0, maxOffset].
    [[nodiscard]] float clampedOffset() const noexcept { return std::clamp(offset_, 0.0f, maxOffset_); }
    [[nodiscard]] float maxOffset() const noexcept { return maxOffset_; }
    [[nodiscard]] float velocity() const noexcept { return velocity_; }
    [[nodiscard]] bool isPointerDown() const noexcept { return pointerDown_; }
    [[nodiscard]] bool isDragging() const noexcept { return isDragging_; }
    [[nodiscard]] bool isAnimating() const noexcept {
        return wheelSmoothing_ || std::abs(velocity_) > 0.0f || offset_ < 0.0f || offset_ > maxOffset_;
    }
    /// Indicator opacity in [0, 1] (1 while scrolling, fades smoothly to 0 after rest).
    [[nodiscard]] float indicatorAlpha() const noexcept { return indicatorAlpha_; }

private:
    static constexpr float kDragThreshold = 7.0f;            // Pointer slop (px) before drag engages
    static constexpr float kCatchVelocityThreshold = 55.0f;  // Coast speed (px/s) that a tap catches
    static constexpr float kStopVelocityThreshold = 8.0f;    // Stop coasting below this speed (px/s)
    static constexpr float kDecelRate = 3.1f;                // iOS-like exponential decay (~0.997/ms)
    static constexpr float kBounceOmega = 14.0f;             // Critically damped spring stiffness (rad/s)
    static constexpr float kWheelEaseRate = 20.0f;           // Smooth desktop wheel catch-up rate
    static constexpr float kRubberBandCoeff = 0.55f;         // Apple UIScrollView rubber-band constant
    static constexpr float kMaxReleaseVelocity = 4200.0f;    // Safety clamp (px/s)

    using Clock = std::chrono::steady_clock;
    struct Sample {
        float y{0.0f};
        Clock::time_point t{};
    };
    static constexpr size_t kMaxSamples = 12;

    void wakeIndicator() noexcept {
        indicatorHoldTimer_ = 0.85f;
        if (indicatorAlpha_ < 0.85f) indicatorAlpha_ = 0.85f;
    }

    [[nodiscard]] float rubberBandDim() const noexcept {
        return std::max(140.0f, viewHeight_);
    }

    [[nodiscard]] float linearToVisual(float linear) const noexcept {
        const float dim = rubberBandDim();
        if (linear < 0.0f) {
            const float d = -linear;
            const float rb = (1.0f - 1.0f / (d * kRubberBandCoeff / dim + 1.0f)) * dim;
            return -rb;
        }
        if (linear > maxOffset_) {
            const float d = linear - maxOffset_;
            const float rb = (1.0f - 1.0f / (d * kRubberBandCoeff / dim + 1.0f)) * dim;
            return maxOffset_ + rb;
        }
        return linear;
    }

    [[nodiscard]] float visualToLinear(float visual) const noexcept {
        const float dim = rubberBandDim();
        if (visual < 0.0f) {
            const float b = std::min(-visual, dim * 0.95f);
            const float d = (b * dim) / (kRubberBandCoeff * std::max(1.0f, dim - b));
            return -d;
        }
        if (visual > maxOffset_) {
            const float b = std::min(visual - maxOffset_, dim * 0.95f);
            const float d = (b * dim) / (kRubberBandCoeff * std::max(1.0f, dim - b));
            return maxOffset_ + d;
        }
        return visual;
    }

    void pushSample(float y) noexcept {
        samples_[sampleHead_] = Sample{y, Clock::now()};
        sampleHead_ = (sampleHead_ + 1) % kMaxSamples;
        if (sampleCount_ < kMaxSamples) ++sampleCount_;
    }

    [[nodiscard]] float computeReleaseVelocity() const noexcept {
        if (sampleCount_ < 2) return 0.0f;

        const auto now = Clock::now();
        const size_t latestIdx = (sampleHead_ + kMaxSamples - 1) % kMaxSamples;
        const Sample& latest = samples_[latestIdx];

        // If the finger stopped moving for >85ms before lifting, do not fling.
        const float staleSec = std::chrono::duration<float>(now - latest.t).count();
        if (staleSec > 0.085f) return 0.0f;

        // Walk back up to ~100ms to get a clean, jitter-free average release velocity.
        Sample oldest = latest;
        for (size_t i = 1; i < sampleCount_; ++i) {
            const size_t idx = (sampleHead_ + kMaxSamples - 1 - i) % kMaxSamples;
            const Sample& s = samples_[idx];
            const float ageSec = std::chrono::duration<float>(latest.t - s.t).count();
            if (ageSec > 0.11f) break;
            oldest = s;
        }

        const float dtSec = std::chrono::duration<float>(latest.t - oldest.t).count();
        if (dtSec < 0.004f) return 0.0f;

        // Finger moving UP (y decreases) -> positive scroll velocity (offset increases).
        const float vel = -(latest.y - oldest.y) / dtSec;
        if (std::abs(vel) < 35.0f) return 0.0f;
        return std::clamp(vel, -kMaxReleaseVelocity, kMaxReleaseVelocity);
    }

    float offset_{0.0f};
    float maxOffset_{0.0f};
    float viewHeight_{300.0f};
    float contentHeight_{0.0f};
    float velocity_{0.0f};

    bool wheelSmoothing_{false};
    float wheelTarget_{0.0f};

    bool pointerDown_{false};
    bool isDragging_{false};
    bool caughtCoast_{false};
    bool axisRejected_{false};
    float dragStartX_{0.0f};
    float dragStartY_{0.0f};
    float dragLastY_{0.0f};
    float initialLinearOffset_{0.0f};

    std::array<Sample, kMaxSamples> samples_{};
    size_t sampleHead_{0};
    size_t sampleCount_{0};

    float indicatorAlpha_{0.0f};
    float indicatorHoldTimer_{0.0f};
};

} // namespace solengine

#endif // SOLENGINE_KINETIC_SCROLLER_HPP
