#ifndef EATSTEXTURES_PROCTEX_RUNTIME_HPP
#define EATSTEXTURES_PROCTEX_RUNTIME_HPP

#include "eatstextures/field_ops.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace eatstextures {

struct ProcTexPbrBundle {
    bool valid{false};
    ColorImage albedo;
    ColorImage normal;
    ColorImage roughness;
    ColorImage height;
    ColorImage ao;
};

class ProcTexRuntime {
public:
    ProcTexRuntime(uint32_t defaultWidth = 512, uint32_t defaultHeight = 512, uint32_t defaultSeed = 42);

    /**
     * Evaluates an Eatscript procedural texture file or source string.
     * Returns true if execution succeeded and produced a rendered output image.
     */
    bool executeScriptFile(const std::string& filePath);
    bool executeScriptSource(const std::string& sourceCode);

    [[nodiscard]] const ColorImage& outputImage() const noexcept { return finalImage_; }
    [[nodiscard]] const ProcTexPbrBundle& pbrBundle() const noexcept { return pbrBundle_; }
    [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }
    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }
    [[nodiscard]] uint32_t seed() const noexcept { return seed_; }

    void setOverrideDimensions(uint32_t w, uint32_t h) noexcept {
        overrideWidth_ = w;
        overrideHeight_ = h;
    }

    void setOverrideSeed(uint32_t s, bool enabled = true) noexcept {
        overrideSeed_ = s;
        hasOverrideSeed_ = enabled;
    }

private:
    void registerBindings();
    int allocField(FloatField&& f);
    int allocImage(ColorImage&& img);
    const FloatField* getField(int handle) const;
    const ColorImage* getImage(int handle) const;

    uint32_t width_{512};
    uint32_t height_{512};
    uint32_t seed_{42};
    uint32_t overrideWidth_{0};
    uint32_t overrideHeight_{0};
    uint32_t overrideSeed_{0};
    bool hasOverrideSeed_{false};
    bool seamless_{true};

    std::vector<FloatField> fields_;
    std::vector<ColorImage> images_;

    ColorImage finalImage_;
    ProcTexPbrBundle pbrBundle_;
    std::string lastError_;

    // Support for SolEngine declarative `background.*` scripts
    FloatField bgAccumHeight_;
    std::vector<ColorStop> bgColorRamp_;
    bool bgHasLighting_{false};
    float bgAmbient_{0.55f};
    float bgDiffuse_{0.45f};
    float bgLightAngle_{135.0f};
    float bgElevation_{0.65f};
    bool bgHasVignette_{false};
    float bgVigRadius_{0.85f};
    float bgVigFalloff_{0.55f};
    float bgVigDarkness_{0.35f};

    eatsbits::eatscript::Evaluator evaluator_;
};

} // namespace eatstextures

#endif // EATSTEXTURES_PROCTEX_RUNTIME_HPP
