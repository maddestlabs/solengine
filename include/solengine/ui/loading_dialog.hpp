#ifndef SOLENGINE_LOADING_DIALOG_HPP
#define SOLENGINE_LOADING_DIALOG_HPP

#include "eatsbits/ui/batch_renderer_2d.hpp"
#include <string>

namespace solengine {

struct ThemePalette;

/**
 * Centered modal loading overlay with backdrop darkening, animated card/suit indicator,
 * real-time stage progress bar, and user-cancellation support ([Cancel] button / Esc key).
 * 
 * Used during procedural deck generation, card wear/stock conditioning,
 * and procedural PBR table surface baking to keep the rendering loop responsive.
 */
class LoadingDialog {
public:
    LoadingDialog();
    ~LoadingDialog() = default;

    void open(
        const std::string& title = "LOADING...",
        const std::string& initialStatus = "",
        bool cancellable = true
    );
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return isOpen_; }

    void setProgress(float progress) noexcept;
    [[nodiscard]] float progress() const noexcept { return progress_; }

    void setTitle(const std::string& title);
    void setStatus(const std::string& status);

    [[nodiscard]] bool isCancelRequested() const noexcept { return cancelRequested_; }
    void requestCancel() noexcept { cancelRequested_ = true; }
    void clearCancelRequest() noexcept { cancelRequested_ = false; }
    [[nodiscard]] bool isCancellable() const noexcept { return cancellable_; }

    void render(
        eatsbits::ui::BatchRenderer2D& r,
        float screenW,
        float screenH,
        float mouseX,
        float mouseY,
        const ThemePalette& pal,
        double currentTime
    );

    // Input handlers (returns true if event was absorbed by the modal)
    bool handleMouseButton(
        int button,
        int action,
        float mouseX,
        float mouseY
    );

    bool handleKey(
        int key,
        int action,
        int mods
    );

private:
    bool isOpen_{false};
    bool cancellable_{true};
    bool cancelRequested_{false};
    float progress_{0.0f};
    std::string title_{"PREPARING PROCEDURAL ASSETS"};
    std::string status_{"Synthesizing..."};

    // Cached dialog bounds for layout & hit-testing
    float dialogX_{0.0f};
    float dialogY_{0.0f};
    float dialogW_{520.0f};
    float dialogH_{240.0f};

    // Cancel button bounds
    float cancelX_{0.0f};
    float cancelY_{0.0f};
    float cancelW_{160.0f};
    float cancelH_{32.0f};
};

} // namespace solengine

#endif // SOLENGINE_LOADING_DIALOG_HPP

