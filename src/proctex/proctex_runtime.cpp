#include "eatstextures/proctex_runtime.hpp"
#include "eatstextures/png_io.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace eatstextures {

namespace {

constexpr int kFieldHandleBase = 1;
constexpr int kImageHandleBase = 100000;

using eatsbits::eatscript::HostFunction;
using eatsbits::eatscript::Value;
using eatsbits::eatscript::resolveArg;

std::vector<ColorStop> parseStopsFromValue(const Value& val) {
    std::vector<ColorStop> stops;
    if (!val.isList()) return stops;
    for (const auto& item : val.listVal) {
        if (item.isList() && item.listVal.size() >= 2) {
            ColorStop cs;
            cs.pos = item.listVal[0].asFloat(0.0f);
            cs.color = parseHexColor(item.listVal[1].asString());
            stops.push_back(cs);
        } else if (item.isDict()) {
            ColorStop cs;
            cs.pos = item.get("pos", item.get("t", Value(0.0))).asFloat(0.0f);
            cs.color = parseHexColor(item.get("color", item.get("col", Value("#808080"))).asString());
            stops.push_back(cs);
        }
    }
    return stops;
}

} // namespace

ProcTexRuntime::ProcTexRuntime(uint32_t defaultWidth, uint32_t defaultHeight, uint32_t defaultSeed)
    : width_(defaultWidth), height_(defaultHeight), seed_(defaultSeed) {
    registerBindings();
}

int ProcTexRuntime::allocField(FloatField&& f) {
    fields_.push_back(std::move(f));
    return kFieldHandleBase + static_cast<int>(fields_.size()) - 1;
}

int ProcTexRuntime::allocImage(ColorImage&& img) {
    images_.push_back(std::move(img));
    return kImageHandleBase + static_cast<int>(images_.size()) - 1;
}

const FloatField* ProcTexRuntime::getField(int handle) const {
    const int idx = handle - kFieldHandleBase;
    if (idx >= 0 && static_cast<size_t>(idx) < fields_.size()) {
        return &fields_[static_cast<size_t>(idx)];
    }
    return nullptr;
}

const ColorImage* ProcTexRuntime::getImage(int handle) const {
    const int idx = handle - kImageHandleBase;
    if (idx >= 0 && static_cast<size_t>(idx) < images_.size()) {
        return &images_[static_cast<size_t>(idx)];
    }
    return nullptr;
}

bool ProcTexRuntime::executeScriptFile(const std::string& filePath) {
    std::ifstream in(filePath);
    if (!in) {
        lastError_ = "Could not open script file: " + filePath;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return executeScriptSource(ss.str());
}

bool ProcTexRuntime::executeScriptSource(const std::string& sourceCode) {
    fields_.clear();
    images_.clear();
    finalImage_ = ColorImage();
    pbrBundle_ = ProcTexPbrBundle{};
    lastError_.clear();

    evaluator_.evaluateSource(sourceCode);
    if (!evaluator_.getLastError().empty()) {
        lastError_ = evaluator_.getLastError();
        return false;
    }

    // If no explicit tex.output(...) was called, fallback to last created ColorImage or FloatField
    if (finalImage_.pixels.empty()) {
        if (!images_.empty()) {
            finalImage_ = images_.back();
        } else if (!fields_.empty()) {
            finalImage_ = floatFieldToGrayscaleImage(fields_.back());
        } else {
            lastError_ = "Script produced no procedural fields or output images.";
            return false;
        }
    }
    return true;
}

void ProcTexRuntime::registerBindings() {
    evaluator_.setVariable("nil", Value());
    evaluator_.setVariable("None", Value());

    std::map<std::string, Value> texMethods;

    auto bindMethod = [&](const std::string& name, HostFunction fn) {
        texMethods[name] = Value(fn);
        evaluator_.registerMemberFunction("proctex", name, fn);
        evaluator_.registerMemberFunction("tex", name, fn);
        evaluator_.registerFunction("tex_" + name, fn);
    };

    // 1. constant(value=0.5)
    bindMethod("constant", [this](const auto& args, const auto& kwargs) -> Value {
        const float val = resolveArg(args, kwargs, 0, "value", Value(0.5)).asFloat(0.5f);
        return Value(allocField(FloatField(width_, height_, std::clamp(val, 0.0f, 1.0f))));
    });

    // 2. fbm(scale_x=8.0, scale_y=8.0, octaves=5, persistence=0.5, lacunarity=2.0, mode="perlin", seed_offset=0)
    bindMethod("fbm", [this](const auto& args, const auto& kwargs) -> Value {
        const float scaleX = resolveArg(args, kwargs, 0, "scale_x", resolveArg(args, kwargs, 0, "scale", Value(8.0))).asFloat(8.0f);
        const float scaleY = resolveArg(args, kwargs, 1, "scale_y", Value(scaleX)).asFloat(scaleX);
        const int octaves = resolveArg(args, kwargs, 2, "octaves", Value(5)).asInt(5);
        const float persistence = resolveArg(args, kwargs, 3, "persistence", resolveArg(args, kwargs, 3, "gain", Value(0.5))).asFloat(0.5f);
        const float lacunarity = resolveArg(args, kwargs, 4, "lacunarity", Value(2.0)).asFloat(2.0f);
        const std::string mode = resolveArg(args, kwargs, 5, "mode", resolveArg(args, kwargs, 5, "noise_type", Value("perlin"))).asString();
        const uint32_t seedOff = static_cast<uint32_t>(resolveArg(args, kwargs, 6, "seed_offset", Value(0)).asInt(0));
        return Value(allocField(generateFbm(width_, height_, scaleX, scaleY, octaves, persistence, lacunarity, mode, seed_ + seedOff)));
    });

    // 3. worley(scale_x=8.0, scale_y=8.0, jitter=0.85, mode="f1", metric="euclidean", seed_offset=0)
    bindMethod("worley", [this](const auto& args, const auto& kwargs) -> Value {
        const float scaleX = resolveArg(args, kwargs, 0, "scale_x", resolveArg(args, kwargs, 0, "scale", Value(8.0))).asFloat(8.0f);
        const float scaleY = resolveArg(args, kwargs, 1, "scale_y", Value(scaleX)).asFloat(scaleX);
        const float jitter = resolveArg(args, kwargs, 2, "jitter", Value(0.85)).asFloat(0.85f);
        const std::string mode = resolveArg(args, kwargs, 3, "mode", Value("f1")).asString();
        const std::string metric = resolveArg(args, kwargs, 4, "metric", Value("euclidean")).asString();
        const uint32_t seedOff = static_cast<uint32_t>(resolveArg(args, kwargs, 5, "seed_offset", Value(0)).asInt(0));
        return Value(allocField(generateWorley(width_, height_, scaleX, scaleY, jitter, mode, metric, seed_ + seedOff)));
    });

    // 4. gabor(scale_x=12.0, scale_y=12.0, angle_deg=45.0, bandwidth=1.5, seed_offset=0)
    bindMethod("gabor", [this](const auto& args, const auto& kwargs) -> Value {
        const float scaleX = resolveArg(args, kwargs, 0, "scale_x", resolveArg(args, kwargs, 0, "scale", Value(12.0))).asFloat(12.0f);
        const float scaleY = resolveArg(args, kwargs, 1, "scale_y", Value(scaleX)).asFloat(scaleX);
        const float angleDeg = resolveArg(args, kwargs, 2, "angle_deg", Value(45.0)).asFloat(45.0f);
        const float bandwidth = resolveArg(args, kwargs, 3, "bandwidth", Value(1.5)).asFloat(1.5f);
        const uint32_t seedOff = static_cast<uint32_t>(resolveArg(args, kwargs, 4, "seed_offset", Value(0)).asInt(0));
        return Value(allocField(generateGabor(width_, height_, scaleX, scaleY, angleDeg, bandwidth, seed_ + seedOff)));
    });

    // 5. wood_grain(rings=16.0, turbulence=0.35, ring_sharpness=0.5, pore_density=0.25, fiber_strength=0.18, angle_deg=90.0, seed_offset=0, cell_id=nil, center_u=0.5, center_v=-1.5)
    bindMethod("wood_grain", [this](const auto& args, const auto& kwargs) -> Value {
        const float rings = resolveArg(args, kwargs, 0, "rings", resolveArg(args, kwargs, 0, "ring_frequency", Value(16.0))).asFloat(16.0f);
        const float turb = resolveArg(args, kwargs, 1, "turbulence", Value(0.35)).asFloat(0.35f);
        const float sharp = resolveArg(args, kwargs, 2, "ring_sharpness", Value(0.5)).asFloat(0.5f);
        const float pores = resolveArg(args, kwargs, 3, "pore_density", Value(0.25)).asFloat(0.25f);
        const float fibers = resolveArg(args, kwargs, 4, "fiber_strength", Value(0.18)).asFloat(0.18f);
        const float angle = resolveArg(args, kwargs, 5, "angle_deg", Value(90.0)).asFloat(90.0f);
        const uint32_t seedOff = static_cast<uint32_t>(resolveArg(args, kwargs, 6, "seed_offset", Value(0)).asInt(0));
        const Value cellVal = resolveArg(args, kwargs, 7, "cell_id", Value());
        const FloatField* cellField = cellVal.isNumber() ? getField(cellVal.asInt(0)) : nullptr;
        const float centerU = resolveArg(args, kwargs, 8, "center_u", resolveArg(args, kwargs, 8, "center_x", Value(-1.5))).asFloat(-1.5f);
        const float centerV = resolveArg(args, kwargs, 9, "center_v", resolveArg(args, kwargs, 9, "center_y", Value(0.5))).asFloat(0.5f);
        return Value(allocField(generateWoodGrain(width_, height_, rings, turb, sharp, pores, fibers, angle, seed_ + seedOff, cellField, centerU, centerV)));
    });

    // 6. brick_grid(cols=4, rows=8, mortar_width=0.06, bevel_width=0.08, row_offset=0.5, height_variance=0.25, slope_variance=0.15, seed_offset=0, mode="height")
    bindMethod("brick_grid", [this](const auto& args, const auto& kwargs) -> Value {
        const int cols = resolveArg(args, kwargs, 0, "cols", Value(4)).asInt(4);
        const int rows = resolveArg(args, kwargs, 1, "rows", Value(8)).asInt(8);
        const float mortar = resolveArg(args, kwargs, 2, "mortar_width", Value(0.06)).asFloat(0.06f);
        const float bevel = resolveArg(args, kwargs, 3, "bevel_width", Value(0.08)).asFloat(0.08f);
        const float offset = resolveArg(args, kwargs, 4, "row_offset", Value(0.5)).asFloat(0.5f);
        const float hVar = resolveArg(args, kwargs, 5, "height_variance", Value(0.25)).asFloat(0.25f);
        const float sVar = resolveArg(args, kwargs, 6, "slope_variance", Value(0.15)).asFloat(0.15f);
        const uint32_t seedOff = static_cast<uint32_t>(resolveArg(args, kwargs, 7, "seed_offset", Value(0)).asInt(0));
        const std::string mode = resolveArg(args, kwargs, 8, "mode", Value("height")).asString();
        return Value(allocField(generateBrickGrid(width_, height_, cols, rows, mortar, bevel, offset, hVar, sVar, seed_ + seedOff, mode)));
    });

    // 7. weave(scale_x=16, scale_y=16, pattern="plain", thread_width=0.78, slub=0.18, seed_offset=0, mode="height")
    bindMethod("weave", [this](const auto& args, const auto& kwargs) -> Value {
        const int sx = resolveArg(args, kwargs, 0, "scale_x", resolveArg(args, kwargs, 0, "scale", Value(16))).asInt(16);
        const int sy = resolveArg(args, kwargs, 1, "scale_y", Value(sx)).asInt(sx);
        const std::string pat = resolveArg(args, kwargs, 2, "pattern", Value("plain")).asString();
        const float tw = resolveArg(args, kwargs, 3, "thread_width", Value(0.78)).asFloat(0.78f);
        const float slub = resolveArg(args, kwargs, 4, "slub", Value(0.18)).asFloat(0.18f);
        const uint32_t seedOff = static_cast<uint32_t>(resolveArg(args, kwargs, 5, "seed_offset", Value(0)).asInt(0));
        const std::string mode = resolveArg(args, kwargs, 6, "mode", Value("height")).asString();
        return Value(allocField(generateWeave(width_, height_, sx, sy, pat, tw, slub, seed_ + seedOff, mode)));
    });

    // 8. scratches(count=120, length=0.25, width=0.004, angle_deg=0.0, angle_spread=180.0, seed_offset=0)
    bindMethod("scratches", [this](const auto& args, const auto& kwargs) -> Value {
        const int count = resolveArg(args, kwargs, 0, "count", Value(120)).asInt(120);
        const float len = resolveArg(args, kwargs, 1, "length", Value(0.25)).asFloat(0.25f);
        const float wNorm = resolveArg(args, kwargs, 2, "width", Value(0.004)).asFloat(0.004f);
        const float ang = resolveArg(args, kwargs, 3, "angle_deg", Value(0.0)).asFloat(0.0f);
        const float spread = resolveArg(args, kwargs, 4, "angle_spread", Value(180.0)).asFloat(180.0f);
        const uint32_t seedOff = static_cast<uint32_t>(resolveArg(args, kwargs, 5, "seed_offset", Value(0)).asInt(0));
        return Value(allocField(generateScratches(width_, height_, count, len, wNorm, ang, spread, seed_ + seedOff)));
    });

    // 9. warp(field, warp_x, warp_y, strength=0.08)
    bindMethod("warp", [this](const auto& args, const auto& kwargs) -> Value {
        const int fId = resolveArg(args, kwargs, 0, "field", Value(0)).asInt(0);
        const int wxId = resolveArg(args, kwargs, 1, "warp_x", Value(0)).asInt(0);
        const int wyId = resolveArg(args, kwargs, 2, "warp_y", Value(wxId)).asInt(wxId);
        const float str = resolveArg(args, kwargs, 3, "strength", Value(0.08)).asFloat(0.08f);
        const FloatField* f = getField(fId);
        const FloatField* wx = getField(wxId);
        const FloatField* wy = getField(wyId);
        if (!f || !wx || !wy) return Value(fId);
        return Value(allocField(opWarp(*f, *wx, *wy, str)));
    });

    // 10. directional_warp(field, intensity_field, angle_deg=45.0, strength=0.08)
    bindMethod("directional_warp", [this](const auto& args, const auto& kwargs) -> Value {
        const int fId = resolveArg(args, kwargs, 0, "field", Value(0)).asInt(0);
        const int mId = resolveArg(args, kwargs, 1, "intensity_field", Value(0)).asInt(0);
        const float ang = resolveArg(args, kwargs, 2, "angle_deg", Value(45.0)).asFloat(45.0f);
        const float str = resolveArg(args, kwargs, 3, "strength", Value(0.08)).asFloat(0.08f);
        const FloatField* f = getField(fId);
        const FloatField* m = getField(mId);
        if (!f || !m) return Value(fId);
        return Value(allocField(opDirectionalWarp(*f, *m, ang, str)));
    });

    // 11. slope_blur(field, slope_field, strength=0.03, samples=8, mode="min")
    bindMethod("slope_blur", [this](const auto& args, const auto& kwargs) -> Value {
        const int fId = resolveArg(args, kwargs, 0, "field", Value(0)).asInt(0);
        const int sId = resolveArg(args, kwargs, 1, "slope_field", Value(0)).asInt(0);
        const float str = resolveArg(args, kwargs, 2, "strength", Value(0.03)).asFloat(0.03f);
        const int samples = resolveArg(args, kwargs, 3, "samples", Value(8)).asInt(8);
        const std::string mode = resolveArg(args, kwargs, 4, "mode", Value("min")).asString();
        const FloatField* f = getField(fId);
        const FloatField* s = getField(sId);
        if (!f || !s) return Value(fId);
        return Value(allocField(opSlopeBlur(*f, *s, str, samples, mode)));
    });

    // 12. blur(field, radius=2.0)
    bindMethod("blur", [this](const auto& args, const auto& kwargs) -> Value {
        const int fId = resolveArg(args, kwargs, 0, "field", Value(0)).asInt(0);
        const float rad = resolveArg(args, kwargs, 1, "radius", Value(2.0)).asFloat(2.0f);
        const FloatField* f = getField(fId);
        if (!f) return Value(fId);
        return Value(allocField(opBlur(*f, rad)));
    });

    // 13. bevel(field, width=0.04)
    bindMethod("bevel", [this](const auto& args, const auto& kwargs) -> Value {
        const int fId = resolveArg(args, kwargs, 0, "field", Value(0)).asInt(0);
        const float wNorm = resolveArg(args, kwargs, 1, "width", Value(0.04)).asFloat(0.04f);
        const FloatField* f = getField(fId);
        if (!f) return Value(fId);
        return Value(allocField(opBevel(*f, wNorm)));
    });

    // 14. remap(field, in_min=0.0, in_max=1.0, out_min=0.0, out_max=1.0)
    bindMethod("remap", [this](const auto& args, const auto& kwargs) -> Value {
        const int fId = resolveArg(args, kwargs, 0, "field", Value(0)).asInt(0);
        const float inMin = resolveArg(args, kwargs, 1, "in_min", Value(0.0)).asFloat(0.0f);
        const float inMax = resolveArg(args, kwargs, 2, "in_max", Value(1.0)).asFloat(1.0f);
        const float outMin = resolveArg(args, kwargs, 3, "out_min", Value(0.0)).asFloat(0.0f);
        const float outMax = resolveArg(args, kwargs, 4, "out_max", Value(1.0)).asFloat(1.0f);
        const FloatField* f = getField(fId);
        if (!f) return Value(fId);
        return Value(allocField(opRemap(*f, inMin, inMax, outMin, outMax)));
    });

    // 14b. histogram_scan(field, position=0.5, contrast=0.5)
    bindMethod("histogram_scan", [this](const auto& args, const auto& kwargs) -> Value {
        const int fId = resolveArg(args, kwargs, 0, "field", Value(0)).asInt(0);
        const float pos = resolveArg(args, kwargs, 1, "position", Value(0.5)).asFloat(0.5f);
        const float contrast = resolveArg(args, kwargs, 2, "contrast", Value(0.5)).asFloat(0.5f);
        const FloatField* f = getField(fId);
        if (!f) return Value(fId);
        return Value(allocField(opHistogramScan(*f, pos, contrast)));
    });

    // 14c. highpass(field, radius=8.0, strength=1.0)
    bindMethod("highpass", [this](const auto& args, const auto& kwargs) -> Value {
        const int fId = resolveArg(args, kwargs, 0, "field", Value(0)).asInt(0);
        const float rad = resolveArg(args, kwargs, 1, "radius", Value(8.0)).asFloat(8.0f);
        const float str = resolveArg(args, kwargs, 2, "strength", Value(1.0)).asFloat(1.0f);
        const FloatField* f = getField(fId);
        if (!f) return Value(fId);
        return Value(allocField(opHighpass(*f, rad, str)));
    });

    // 14d. make_seamless(field_or_img, blend_width=0.15)
    bindMethod("make_seamless", [this](const auto& args, const auto& kwargs) -> Value {
        const int id = resolveArg(args, kwargs, 0, "target", Value(0)).asInt(0);
        const float bw = resolveArg(args, kwargs, 1, "blend_width", Value(0.15)).asFloat(0.15f);
        if (const FloatField* f = getField(id)) {
            return Value(allocField(opMakeSeamless(*f, bw)));
        } else if (const ColorImage* img = getImage(id)) {
            return Value(allocImage(opMakeSeamlessColor(*img, bw)));
        }
        return Value(id);
    });

    // 15. smoothstep(field, edge0=0.2, edge1=0.8)
    bindMethod("smoothstep", [this](const auto& args, const auto& kwargs) -> Value {
        const int fId = resolveArg(args, kwargs, 0, "field", Value(0)).asInt(0);
        const float e0 = resolveArg(args, kwargs, 1, "edge0", Value(0.2)).asFloat(0.2f);
        const float e1 = resolveArg(args, kwargs, 2, "edge1", Value(0.8)).asFloat(0.8f);
        const FloatField* f = getField(fId);
        if (!f) return Value(fId);
        return Value(allocField(opSmoothstep(*f, e0, e1)));
    });

    // 16. pow / invert / normalize
    bindMethod("pow", [this](const auto& args, const auto& kwargs) -> Value {
        const int fId = resolveArg(args, kwargs, 0, "field", Value(0)).asInt(0);
        const float exp = resolveArg(args, kwargs, 1, "exponent", Value(1.5)).asFloat(1.5f);
        const FloatField* f = getField(fId);
        if (!f) return Value(fId);
        return Value(allocField(opPow(*f, exp)));
    });

    bindMethod("invert", [this](const auto& args, const auto& kwargs) -> Value {
        const int fId = resolveArg(args, kwargs, 0, "field", Value(0)).asInt(0);
        const FloatField* f = getField(fId);
        if (!f) return Value(fId);
        return Value(allocField(opInvert(*f)));
    });

    bindMethod("normalize", [this](const auto& args, const auto& kwargs) -> Value {
        const int fId = resolveArg(args, kwargs, 0, "field", Value(0)).asInt(0);
        const FloatField* f = getField(fId);
        if (!f) return Value(fId);
        return Value(allocField(opNormalize(*f)));
    });

    // 17. blend(field_a, field_b, mode="mix", weight=0.5)
    bindMethod("blend", [this](const auto& args, const auto& kwargs) -> Value {
        const int aId = resolveArg(args, kwargs, 0, "field_a", Value(0)).asInt(0);
        const int bId = resolveArg(args, kwargs, 1, "field_b", Value(0)).asInt(0);
        const std::string mode = resolveArg(args, kwargs, 2, "mode", Value("mix")).asString();
        const float weight = resolveArg(args, kwargs, 3, "weight", Value(0.5)).asFloat(0.5f);
        const FloatField* a = getField(aId);
        const FloatField* b = getField(bId);
        if (!a || !b) return Value(aId);
        return Value(allocField(opBlendFields(*a, *b, mode, weight)));
    });

    // 18. height_blend(height_a, height_b, offset=0.0, contrast=0.25)
    bindMethod("height_blend", [this](const auto& args, const auto& kwargs) -> Value {
        const int aId = resolveArg(args, kwargs, 0, "height_a", Value(0)).asInt(0);
        const int bId = resolveArg(args, kwargs, 1, "height_b", Value(0)).asInt(0);
        const float offset = resolveArg(args, kwargs, 2, "offset", Value(0.0)).asFloat(0.0f);
        const float contrast = resolveArg(args, kwargs, 3, "contrast", Value(0.25)).asFloat(0.25f);
        const FloatField* a = getField(aId);
        const FloatField* b = getField(bId);
        if (!a || !b) return Value(aId);
        return Value(allocField(opHeightBlend(*a, *b, offset, contrast)));
    });

    // 19. curvature(height_field, strength=2.0, radius=1)
    bindMethod("curvature", [this](const auto& args, const auto& kwargs) -> Value {
        const int hId = resolveArg(args, kwargs, 0, "height_field", Value(0)).asInt(0);
        const float str = resolveArg(args, kwargs, 1, "strength", Value(2.0)).asFloat(2.0f);
        const int rad = resolveArg(args, kwargs, 2, "radius", Value(1)).asInt(1);
        const FloatField* h = getField(hId);
        if (!h) return Value(hId);
        return Value(allocField(computeCurvature(*h, str, rad)));
    });

    // 20. ambient_occlusion(height_field, radius=8, intensity=1.5)
    bindMethod("ambient_occlusion", [this](const auto& args, const auto& kwargs) -> Value {
        const int hId = resolveArg(args, kwargs, 0, "height_field", Value(0)).asInt(0);
        const int rad = resolveArg(args, kwargs, 1, "radius", Value(8)).asInt(8);
        const float intensity = resolveArg(args, kwargs, 2, "intensity", Value(1.5)).asFloat(1.5f);
        const FloatField* h = getField(hId);
        if (!h) return Value(hId);
        return Value(allocField(computeAmbientOcclusion(*h, rad, intensity)));
    });

    // 21. gradient_map(field, stops)
    bindMethod("gradient_map", [this](const auto& args, const auto& kwargs) -> Value {
        const int fId = resolveArg(args, kwargs, 0, "field", Value(0)).asInt(0);
        const Value stopsVal = resolveArg(args, kwargs, 1, "stops", Value());
        const FloatField* f = getField(fId);
        if (!f) return Value(0);
        return Value(allocImage(applyGradientMap(*f, parseStopsFromValue(stopsVal))));
    });
    // Alias color_ramp for convenience
    bindMethod("color_ramp", texMethods["gradient_map"].hostFn);

    // 22. solid_color(hex_color="#ffffff")
    bindMethod("solid_color", [this](const auto& args, const auto& kwargs) -> Value {
        const std::string hex = resolveArg(args, kwargs, 0, "hex_color", Value("#ffffff")).asString();
        return Value(allocImage(ColorImage(width_, height_, parseHexColor(hex))));
    });

    // 22b. resynthesize(path, jitter=0.006, detail=0.22, tile_scale=1.0, seed=42)
    bindMethod("resynthesize", [this](const auto& args, const auto& kwargs) -> Value {
        const std::string rawPath = resolveArg(args, kwargs, 0, "path", Value("")).asString();
        const float jitter = resolveArg(args, kwargs, 1, "jitter", Value(0.006)).asFloat(0.006f);
        const float detail = resolveArg(args, kwargs, 2, "detail", Value(0.22)).asFloat(0.22f);
        const float tileScale = resolveArg(args, kwargs, 3, "tile_scale", Value(1.0)).asFloat(1.0f);
        const uint32_t s = static_cast<uint32_t>(resolveArg(args, kwargs, 4, "seed", Value(static_cast<double>(seed_))).asInt(static_cast<int>(seed_)));

        if (rawPath.empty()) return Value(0);

        std::error_code ec;
        std::string resolved;
        const std::string candidates[] = {
            rawPath,
            "themes/" + rawPath,
            "../themes/" + rawPath,
            "../../themes/" + rawPath,
            "../../../themes/" + rawPath,
            "assets/" + rawPath,
            "../assets/" + rawPath
        };
        for (const auto& c : candidates) {
            if (std::filesystem::exists(c, ec) && std::filesystem::is_regular_file(c, ec)) {
                resolved = c;
                break;
            }
        }
        if (resolved.empty()) {
            return Value(0);
        }

        std::vector<uint8_t> rgba;
        uint32_t sw = 0, sh = 0;
        if (!readImageRGBA(resolved, rgba, sw, sh)) {
            return Value(0);
        }
        return Value(allocImage(resynthesizeFromRgba(rgba, sw, sh, width_, height_, jitter, detail, tileScale, s)));
    });

    // 23. color_blend(img_a, img_b, mode="mix", mask=nil, weight=0.5)
    bindMethod("color_blend", [this](const auto& args, const auto& kwargs) -> Value {
        const int aId = resolveArg(args, kwargs, 0, "img_a", Value(0)).asInt(0);
        const int bId = resolveArg(args, kwargs, 1, "img_b", Value(0)).asInt(0);
        const std::string mode = resolveArg(args, kwargs, 2, "mode", Value("mix")).asString();
        const Value maskVal = resolveArg(args, kwargs, 3, "mask", Value());
        const float weight = resolveArg(args, kwargs, 4, "weight", Value(0.5)).asFloat(0.5f);

        const ColorImage* a = getImage(aId);
        const ColorImage* b = getImage(bId);
        if (!a || !b) return Value(aId);
        const FloatField* mask = maskVal.isNumber() ? getField(maskVal.asInt(0)) : nullptr;
        return Value(allocImage(blendColorImages(*a, *b, mode, mask, weight)));
    });

    // 24. adjust_color(img, brightness=0.0, contrast=1.0, saturation=1.0, gamma=1.0)
    bindMethod("adjust_color", [this](const auto& args, const auto& kwargs) -> Value {
        const int imgId = resolveArg(args, kwargs, 0, "img", Value(0)).asInt(0);
        const float br = resolveArg(args, kwargs, 1, "brightness", Value(0.0)).asFloat(0.0f);
        const float co = resolveArg(args, kwargs, 2, "contrast", Value(1.0)).asFloat(1.0f);
        const float sa = resolveArg(args, kwargs, 3, "saturation", Value(1.0)).asFloat(1.0f);
        const float ga = resolveArg(args, kwargs, 4, "gamma", Value(1.0)).asFloat(1.0f);
        const ColorImage* img = getImage(imgId);
        if (!img) return Value(imgId);
        return Value(allocImage(adjustColorImage(*img, br, co, sa, ga)));
    });

    // 25. splatter_specks(img, count=80, color="#e07080", radius=1.5, opacity=0.5, seed_offset=0)
    bindMethod("splatter_specks", [this](const auto& args, const auto& kwargs) -> Value {
        const int imgId = resolveArg(args, kwargs, 0, "img", Value(0)).asInt(0);
        const int count = resolveArg(args, kwargs, 1, "count", Value(80)).asInt(80);
        const std::string hex = resolveArg(args, kwargs, 2, "color", Value("#e07080")).asString();
        const float rad = resolveArg(args, kwargs, 3, "radius", Value(1.5)).asFloat(1.5f);
        const float op = resolveArg(args, kwargs, 4, "opacity", Value(0.5)).asFloat(0.5f);
        const uint32_t seedOff = static_cast<uint32_t>(resolveArg(args, kwargs, 5, "seed_offset", Value(0)).asInt(0));
        const ColorImage* img = getImage(imgId);
        if (!img) return Value(imgId);
        return Value(allocImage(splatterSpecks(*img, count, parseHexColor(hex), rad, op, seed_ + seedOff)));
    });

    // 26. shade_pbr(albedo, height_field, roughness=0.5, metallic=0.0, normal_strength=3.5, ao_intensity=1.0, light_azimuth_deg=135.0, light_elevation_deg=45.0, light_intensity=1.15, ambient=0.38)
    bindMethod("shade_pbr", [this](const auto& args, const auto& kwargs) -> Value {
        const int albId = resolveArg(args, kwargs, 0, "albedo", Value(0)).asInt(0);
        const int hId = resolveArg(args, kwargs, 1, "height_field", resolveArg(args, kwargs, 1, "height", Value(0))).asInt(0);
        const Value roughVal = resolveArg(args, kwargs, 2, "roughness", Value(0.5));
        const float metallic = resolveArg(args, kwargs, 3, "metallic", Value(0.0)).asFloat(0.0f);
        const float normStr = resolveArg(args, kwargs, 4, "normal_strength", Value(3.5)).asFloat(3.5f);
        const float aoInt = resolveArg(args, kwargs, 5, "ao_intensity", Value(1.0)).asFloat(1.0f);
        const float azDeg = resolveArg(args, kwargs, 6, "light_azimuth_deg", Value(135.0)).asFloat(135.0f);
        const float elDeg = resolveArg(args, kwargs, 7, "light_elevation_deg", Value(45.0)).asFloat(45.0f);
        const float lightInt = resolveArg(args, kwargs, 8, "light_intensity", Value(1.15)).asFloat(1.15f);
        const float ambient = resolveArg(args, kwargs, 9, "ambient", Value(0.38)).asFloat(0.38f);

        const ColorImage* alb = getImage(albId);
        const FloatField* h = getField(hId);
        if (!alb) return Value(albId);
        FloatField fallbackH(width_, height_, 0.5f);
        const FloatField& activeH = h ? *h : fallbackH;

        const FloatField* roughField = nullptr;
        float roughScalar = 0.5f;
        if (roughVal.isNumber()) {
            const int maybeHandle = roughVal.asInt(-1);
            if (maybeHandle >= kFieldHandleBase && getField(maybeHandle) != nullptr && roughVal.asNumber() >= 1.0 &&
                static_cast<double>(maybeHandle) == roughVal.asNumber() && (maybeHandle > 1 || fields_.size() == 1)) {
                roughField = getField(maybeHandle);
            } else {
                roughScalar = roughVal.asFloat(0.5f);
            }
        }

        // Also populate PBR bundle automatically whenever shade_pbr is called
        pbrBundle_.valid = true;
        pbrBundle_.albedo = *alb;
        pbrBundle_.height = floatFieldToGrayscaleImage(activeH);
        pbrBundle_.normal = computeNormalMap(activeH, normStr);
        pbrBundle_.ao = floatFieldToGrayscaleImage(computeAmbientOcclusion(activeH, 6, std::max(0.5f, aoInt)));
        pbrBundle_.roughness = roughField ? floatFieldToGrayscaleImage(*roughField)
                                          : ColorImage(width_, height_, {roughScalar, roughScalar, roughScalar, 1.0f});

        return Value(allocImage(shadePBR(*alb, activeH, roughField, roughScalar, metallic, normStr, aoInt, azDeg, elDeg, lightInt, ambient)));
    });

    // 27. pixel_art(img, target_w=32, target_h=32, palette="endesga32", dither="bayer4x4", dither_strength=0.35, upscale_to_canvas=true)
    bindMethod("pixel_art", [this](const auto& args, const auto& kwargs) -> Value {
        const int imgId = resolveArg(args, kwargs, 0, "img", Value(0)).asInt(0);
        const uint32_t tw = static_cast<uint32_t>(std::max(4, resolveArg(args, kwargs, 1, "target_w", Value(32)).asInt(32)));
        const uint32_t th = static_cast<uint32_t>(std::max(4, resolveArg(args, kwargs, 2, "target_h", Value(static_cast<int>(tw))).asInt(static_cast<int>(tw))));
        const Value palVal = resolveArg(args, kwargs, 3, "palette", Value("endesga32"));
        const std::string dither = resolveArg(args, kwargs, 4, "dither", Value("bayer4x4")).asString();
        const float ditherStr = resolveArg(args, kwargs, 5, "dither_strength", Value(0.35)).asFloat(0.35f);
        const bool upscale = resolveArg(args, kwargs, 6, "upscale_to_canvas", Value(true)).asBoolean(true);

        const ColorImage* img = getImage(imgId);
        if (!img) return Value(imgId);

        std::vector<RGBAf> pal;
        if (palVal.isString()) {
            pal = getBuiltinPalette(palVal.asString());
        } else if (palVal.isList()) {
            for (const auto& c : palVal.listVal) {
                pal.push_back(parseHexColor(c.asString()));
            }
        }
        return Value(allocImage(quantizePixelArt(*img, tw, th, pal, dither, ditherStr, upscale)));
    });

    // 28. output(color_img_or_field)
    bindMethod("output", [this](const auto& args, const auto& kwargs) -> Value {
        const int handle = resolveArg(args, kwargs, 0, "image", Value(0)).asInt(0);
        if (const ColorImage* img = getImage(handle)) {
            finalImage_ = *img;
            return Value(handle);
        }
        if (const FloatField* f = getField(handle)) {
            finalImage_ = floatFieldToGrayscaleImage(*f);
            return Value(handle);
        }
        return Value(0);
    });

    // 29. output_pbr(albedo_img, height_field, roughness=0.5, normal_strength=3.5)
    bindMethod("output_pbr", [this](const auto& args, const auto& kwargs) -> Value {
        const int albId = resolveArg(args, kwargs, 0, "albedo", Value(0)).asInt(0);
        const int hId = resolveArg(args, kwargs, 1, "height_field", Value(0)).asInt(0);
        const Value roughVal = resolveArg(args, kwargs, 2, "roughness", Value(0.5));
        const float normStr = resolveArg(args, kwargs, 3, "normal_strength", Value(3.5)).asFloat(3.5f);

        const ColorImage* alb = getImage(albId);
        const FloatField* h = getField(hId);
        if (alb && h) {
            pbrBundle_.valid = true;
            pbrBundle_.albedo = *alb;
            pbrBundle_.height = floatFieldToGrayscaleImage(*h);
            pbrBundle_.normal = computeNormalMap(*h, normStr);
            pbrBundle_.ao = floatFieldToGrayscaleImage(computeAmbientOcclusion(*h, 6, 1.2f));
            const FloatField* rf = roughVal.isNumber() ? getField(roughVal.asInt(-1)) : nullptr;
            if (rf && roughVal.asInt(-1) > 1) {
                pbrBundle_.roughness = floatFieldToGrayscaleImage(*rf);
            } else {
                const float r = roughVal.asFloat(0.5f);
                pbrBundle_.roughness = ColorImage(width_, height_, {r, r, r, 1.0f});
            }
        }
        return Value(true);
    });

    // proctex.canvas(width=512, height=512, seed=42, seamless=true)
    HostFunction canvasFn = [this, texMethods](const auto& args, const auto& kwargs) -> Value {
        const uint32_t w = static_cast<uint32_t>(std::clamp(resolveArg(args, kwargs, 0, "width", Value(512)).asInt(512), 8, 4096));
        const uint32_t h = static_cast<uint32_t>(std::clamp(resolveArg(args, kwargs, 1, "height", Value(static_cast<int>(w))).asInt(static_cast<int>(w)), 8, 4096));
        const uint32_t s = static_cast<uint32_t>(resolveArg(args, kwargs, 2, "seed", Value(42)).asInt(42));
        seamless_ = resolveArg(args, kwargs, 3, "seamless", Value(true)).asBoolean(true);

        width_ = (overrideWidth_ > 0) ? overrideWidth_ : w;
        height_ = (overrideHeight_ > 0) ? overrideHeight_ : h;
        seed_ = hasOverrideSeed_ ? overrideSeed_ : s;
        fields_.clear();
        images_.clear();
        return Value(texMethods);
    };
    evaluator_.registerMemberFunction("proctex", "canvas", canvasFn);

    // ========================================================================
    // SolEngine Declarative `sol.*` Theme/Package Compatibility Layer
    // ========================================================================
    std::map<std::string, Value> solMethods;
    HostFunction solNoopFn = [](const auto&, const auto&) -> Value { return Value(true); };
    solMethods["set_theme"] = Value(solNoopFn);
    solMethods["create_theme"] = Value(solNoopFn);
    solMethods["set_deck"] = Value(solNoopFn);
    solMethods["set_background"] = Value(solNoopFn);
    solMethods["set_corner_radius"] = Value(solNoopFn);
    evaluator_.setVariable("sol", Value(solMethods));
    evaluator_.registerMemberFunction("sol", "set_theme", solNoopFn);
    evaluator_.registerMemberFunction("sol", "create_theme", solNoopFn);
    evaluator_.registerMemberFunction("sol", "set_deck", solNoopFn);
    evaluator_.registerMemberFunction("sol", "set_background", solNoopFn);
    evaluator_.registerMemberFunction("sol", "set_corner_radius", solNoopFn);
    evaluator_.registerFunction("set_theme", solNoopFn);
    evaluator_.registerFunction("create_theme", solNoopFn);
    evaluator_.registerFunction("set_deck", solNoopFn);
    evaluator_.registerFunction("set_background", solNoopFn);
    evaluator_.registerFunction("set_corner_radius", solNoopFn);

    // ========================================================================
    // SolEngine Declarative `background.*` Pipeline Compatibility Layer
    // ========================================================================
    std::map<std::string, Value> bgMethods;
    bgMethods["add_layer"] = Value([this](const auto& args, const auto& kwargs) -> Value {
        const std::string type = resolveArg(args, kwargs, 0, "type", Value("simplex_fractal")).asString();
        const Value cfg = resolveArg(args, kwargs, 1, "config", Value());
        FloatField layer(width_, height_, 0.5f);

        if (type == "wood_grain") {
            const float rf = cfg.get("ring_frequency", Value(20.0)).asFloat(20.0f);
            const float tb = cfg.get("turbulence", Value(0.35)).asFloat(0.35f);
            const float pd = cfg.get("pore_density", Value(0.22)).asFloat(0.22f);
            const float ang = cfg.get("anisotropy_angle", Value(90.0)).asFloat(90.0f);
            layer = generateWoodGrain(width_, height_, rf, tb, 0.55f, pd, 0.2f, ang, seed_);
        } else if (type == "worley_cellular") {
            const float cs = std::max(4.0f, cfg.get("cell_size", Value(64.0)).asFloat(64.0f));
            const float jit = cfg.get("jitter", Value(0.82)).asFloat(0.82f);
            layer = generateWorley(width_, height_, static_cast<float>(width_) / cs, static_cast<float>(height_) / cs, jit, "cracks", "euclidean", seed_);
        } else if (type == "tile_grid") {
            const float tw = std::max(8.0f, cfg.get("tile_w", Value(128.0)).asFloat(128.0f));
            const float th = std::max(8.0f, cfg.get("tile_h", Value(64.0)).asFloat(64.0f));
            const std::string pat = cfg.get("pattern", Value("subway")).asString();
            const float rowOff = (pat == "subway") ? 0.5f : 0.0f;
            layer = generateBrickGrid(width_, height_,
                                      std::max(1, static_cast<int>(width_ / tw)),
                                      std::max(1, static_cast<int>(height_ / th)),
                                      0.05f, 0.06f, rowOff, 0.1f, 0.08f, seed_);
        } else if (type == "woven_fabric" || type == "stipple_felt") {
            const float step = std::max(2.0f, cfg.get("yarn_step", cfg.get("fiber_scale", Value(6.0))).asFloat(6.0f));
            const std::string wt = cfg.get("weave_type", Value("plain")).asString();
            std::string pat = "plain";
            if (wt.find("basket") != std::string::npos) pat = "basket";
            else if (wt.find("denim") != std::string::npos) pat = "twill";
            else if (wt.find("jersey") != std::string::npos) pat = "knit";
            layer = generateWeave(width_, height_,
                                  std::max(4, static_cast<int>(width_ / step)),
                                  std::max(4, static_cast<int>(height_ / step)),
                                  pat, 0.80f, 0.20f, seed_);
        } else {
            const int oct = cfg.get("octaves", Value(4)).asInt(4);
            const float freq = cfg.get("frequency", Value(0.03)).asFloat(0.03f);
            const float scale = std::clamp(freq * static_cast<float>(width_), 2.0f, 64.0f);
            layer = generateFbm(width_, height_, scale, scale, oct, 0.5f, 2.0f, "perlin", seed_ + 77u);
        }

        const std::string blendMode = cfg.get("blend_mode", Value("mix")).asString();
        const float intensity = cfg.get("intensity", Value(1.0)).asFloat(1.0f);
        if (bgAccumHeight_.data.empty()) {
            bgAccumHeight_ = std::move(layer);
        } else {
            bgAccumHeight_ = opBlendFields(bgAccumHeight_, layer, blendMode, intensity);
        }
        return Value(true);
    });

    bgMethods["set_color_ramp"] = Value([this](const auto& args, const auto& kwargs) -> Value {
        bgColorRamp_ = parseStopsFromValue(resolveArg(args, kwargs, 0, "stops", Value()));
        return Value(true);
    });

    bgMethods["set_lighting"] = Value([this](const auto& args, const auto& kwargs) -> Value {
        bgHasLighting_ = true;
        bgAmbient_ = resolveArg(args, kwargs, 0, "ambient", Value(0.55)).asFloat(0.55f);
        bgDiffuse_ = resolveArg(args, kwargs, 1, "diffuse", Value(0.45)).asFloat(0.45f);
        bgLightAngle_ = resolveArg(args, kwargs, 2, "light_angle_deg", Value(135.0)).asFloat(135.0f);
        bgElevation_ = resolveArg(args, kwargs, 3, "elevation", Value(0.65)).asFloat(0.65f);
        return Value(true);
    });

    bgMethods["add_vignette"] = Value([this](const auto& args, const auto& kwargs) -> Value {
        bgHasVignette_ = true;
        bgVigRadius_ = resolveArg(args, kwargs, 0, "radius", Value(0.85)).asFloat(0.85f);
        bgVigFalloff_ = resolveArg(args, kwargs, 1, "falloff", Value(0.55)).asFloat(0.55f);
        bgVigDarkness_ = resolveArg(args, kwargs, 2, "darkness", Value(0.35)).asFloat(0.35f);
        return Value(true);
    });

    bgMethods["bake"] = Value([this](const auto&, const auto&) -> Value {
        if (bgAccumHeight_.data.empty()) {
            bgAccumHeight_ = generateFbm(width_, height_, 8.0f, 8.0f, 4, 0.5f, 2.0f, "perlin", seed_);
        }
        ColorImage albedo = applyGradientMap(bgAccumHeight_, bgColorRamp_);
        if (bgHasLighting_) {
            albedo = shadePBR(albedo, bgAccumHeight_, nullptr, 0.45f, 0.0f, 3.0f, 1.0f,
                              bgLightAngle_, bgElevation_ * 65.0f, bgDiffuse_ * 1.8f, bgAmbient_);
        }
        finalImage_ = std::move(albedo);
        return Value(true);
    });

    evaluator_.registerMemberFunction("background", "create",
        [this, bgMethods](const auto& args, const auto& kwargs) -> Value {
            const uint32_t w = static_cast<uint32_t>(std::clamp(resolveArg(args, kwargs, 1, "width", Value(512)).asInt(512), 8, 4096));
            const uint32_t h = static_cast<uint32_t>(std::clamp(resolveArg(args, kwargs, 2, "height", Value(512)).asInt(512), 8, 4096));
            width_ = (overrideWidth_ > 0) ? overrideWidth_ : w;
            height_ = (overrideHeight_ > 0) ? overrideHeight_ : h;
            bgAccumHeight_ = FloatField();
            bgColorRamp_.clear();
            bgHasLighting_ = false;
            bgHasVignette_ = false;
            return Value(bgMethods);
        });
}

} // namespace eatstextures
