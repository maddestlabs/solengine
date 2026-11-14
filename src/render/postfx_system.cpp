#include "solengine/postfx_system.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include "eatsbits/eatscript/lexer.hpp"
#include "eatsbits/eatscript/parser.hpp"
#include "eatsbits/eatscript/wgsl_transpiler.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace solengine {

PostFxSystem& PostFxSystem::instance() {
    static PostFxSystem s_inst;
    return s_inst;
}

const std::vector<PostFxDef>& PostFxSystem::availableEffects() {
    if (!scanned_) {
        scanPostFx("postfx");
    }
    return effects_;
}

size_t PostFxSystem::findEffectIndex(const std::string& id) const {
    for (size_t i = 0; i < effects_.size(); ++i) {
        if (effects_[i].id == id) return i;
    }
    return 0;
}

const PostFxDef* PostFxSystem::getEffectDef(size_t index) const {
    if (index < effects_.size()) return &effects_[index];
    return nullptr;
}

const PostFxDef* PostFxSystem::getEffectById(const std::string& id) const {
    for (const auto& e : effects_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

void PostFxSystem::resolveParams(const std::string& effectId,
                                 const std::string& optionId,
                                 float outU[4],
                                 float& outParam1,
                                 float& outParam2) const {
    outU[0] = outU[1] = outU[2] = outU[3] = 0.0f;
    outParam1 = 0.0f;
    outParam2 = 0.0f;

    const PostFxDef* def = getEffectById(effectId);
    if (!def) return;

    outParam1 = def->defaultParam1;
    outParam2 = def->defaultParam2;

    if (!def->options.empty()) {
        const PostFxOptionChoice* chosen = &def->options[0];
        for (const auto& opt : def->options) {
            if (opt.id == optionId) {
                chosen = &opt;
                break;
            }
        }
        for (int i = 0; i < 4; ++i) outU[i] = chosen->u[i];
        if (chosen->param1 != 0.0f) outParam1 = chosen->param1;
        if (chosen->param2 != 0.0f) outParam2 = chosen->param2;
    }
}

void PostFxSystem::warpMouseCoords(const std::string& effectId,
                                   const std::string& optionId,
                                   float intensity,
                                   float width,
                                   float height,
                                   float& ioX,
                                   float& ioY) const {
    if (effectId.empty() || effectId == "none" || intensity <= 0.05f || width <= 1.0f || height <= 1.0f) {
        return;
    }
    const PostFxDef* def = getEffectById(effectId);
    if (!def || def->builtinFilter != "crt") {
        return;
    }

    float u[4]{};
    float p1 = 0.0f, p2 = 0.0f;
    resolveParams(effectId, optionId, u, p1, p2);
    const float curveStrength = (p1 > 0.0f) ? p1 : 0.95f;

    const float nu = ioX / width;
    const float nv = ioY / height;
    const float dx = nu - 0.5f;
    const float dy = nv - 0.5f;
    const float dist = std::hypot(dx, dy);
    const float warp = std::pow(dist, 5.0f) * curveStrength;

    const float wu = nu + dx * warp;
    const float wv = nv + dy * warp;

    const float frame = 20.0f / width;
    const float denom = std::max(1.0f - 2.0f * frame, 0.001f);
    const float su = (wu - frame) / denom;
    const float sv = (wv - frame) / denom;

    ioX = su * width;
    ioY = sv * height;
}

void PostFxSystem::scanPostFx(const std::string& dir) {
    effects_.clear();

    PostFxDef noneDef;
    noneDef.id = "none";
    noneDef.name = "None (Off)";
    noneDef.description = "No full-screen post-processing";
    effects_.push_back(std::move(noneDef));

    const std::vector<std::string> candidateDirs = {
        dir,
        "/" + dir,
        "../" + dir,
        "../../" + dir,
        "../../../" + dir,
        "assets/" + dir,
        "../assets/" + dir,
        "./" + dir
    };

    std::string resolvedDir;
    for (const auto& d : candidateDirs) {
        std::error_code ec;
        if (fs::exists(d, ec) && fs::is_directory(d, ec)) {
            resolvedDir = d;
            break;
        }
    }

    if (!resolvedDir.empty()) {
        std::vector<fs::path> files;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(resolvedDir, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".eats") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());

        for (const auto& fpath : files) {
            PostFxDef def;
            std::string err;
            if (loadEatsFile(fpath.string(), def, &err)) {
                effects_.push_back(std::move(def));
            } else if (!err.empty()) {
                std::cerr << "[PostFxSystem] Failed to load " << fpath.string() << ": " << err << "\n";
            }
        }
    }

    scanned_ = true;
}

bool PostFxSystem::loadEatsFile(const std::string& filePath, PostFxDef& outDef, std::string* errOut) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) {
        if (errOut) *errOut = "Cannot open file";
        return false;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string source = ss.str();

    outDef = PostFxDef{};
    outDef.filePath = filePath;
    outDef.id = fs::path(filePath).stem().string();
    outDef.name = outDef.id;

    eatsbits::eatscript::Evaluator eval;
    bool hasMeta = false;

    auto handleMeta = [&](const std::vector<eatsbits::eatscript::Value>& args,
                          const std::map<std::string, eatsbits::eatscript::Value>&) -> eatsbits::eatscript::Value {
        if (args.empty() || !args[0].isDict()) return eatsbits::eatscript::Value(false);
        const auto& dict = args[0].dictVal;
        auto getStr = [&](const char* key, const std::string& def = "") -> std::string {
            auto it = dict.find(key);
            return (it != dict.end() && it->second.isString()) ? it->second.asString() : def;
        };
        auto getFloat = [&](const char* key, float def = 0.0f) -> float {
            auto it = dict.find(key);
            return (it != dict.end() && it->second.isNumber()) ? it->second.asFloat(def) : def;
        };

        outDef.id = getStr("id", outDef.id);
        outDef.name = getStr("name", outDef.name);
        outDef.author = getStr("author");
        outDef.description = getStr("description");
        outDef.shaderFunc = getStr("shader");
        outDef.builtinFilter = getStr("filter");
        outDef.optionLabel = getStr("option_label", "Preset");
        outDef.gpuCost = getStr("gpu_cost", "Low");
        outDef.defaultParam1 = getFloat("param1", 0.0f);
        outDef.defaultParam2 = getFloat("param2", 0.0f);

        auto optIt = dict.find("options");
        if (optIt != dict.end() && optIt->second.isList()) {
            for (const auto& item : optIt->second.listVal) {
                if (!item.isDict()) continue;
                const auto& od = item.dictVal;
                PostFxOptionChoice ch;
                if (auto it = od.find("id"); it != od.end() && it->second.isString()) ch.id = it->second.asString();
                if (auto it = od.find("name"); it != od.end() && it->second.isString()) ch.name = it->second.asString();
                if (auto it = od.find("param1"); it != od.end() && it->second.isNumber()) ch.param1 = it->second.asFloat(0.0f);
                if (auto it = od.find("param2"); it != od.end() && it->second.isNumber()) ch.param2 = it->second.asFloat(0.0f);
                if (auto it = od.find("u"); it != od.end() && it->second.isList()) {
                    const auto& ul = it->second.listVal;
                    for (size_t i = 0; i < 4 && i < ul.size(); ++i) {
                        ch.u[i] = ul[i].asFloat(0.0f);
                    }
                }
                if (!ch.id.empty()) {
                    if (ch.name.empty()) ch.name = ch.id;
                    outDef.options.push_back(std::move(ch));
                }
            }
        }
        hasMeta = true;
        return eatsbits::eatscript::Value(true);
    };

    eval.registerMemberFunction("sol", "set_postfx", handleMeta);
    eval.registerFunction("set_postfx", handleMeta);

    // No-op ctx stubs in case script has on_post_process
    eval.registerMemberFunction("ctx", "apply_filter", [&](const auto& args, const auto&) {
        if (!args.empty() && args[0].isString() && outDef.builtinFilter.empty()) {
            outDef.builtinFilter = args[0].asString();
        }
        return eatsbits::eatscript::Value(true);
    });
    eval.registerMemberFunction("ctx", "apply_shader", [&](const auto& args, const auto&) {
        if (!args.empty() && args[0].isString() && outDef.shaderFunc.empty()) {
            outDef.shaderFunc = args[0].asString();
        }
        return eatsbits::eatscript::Value(true);
    });

    try {
        eatsbits::eatscript::Lexer lexer(source);
        auto tokens = lexer.tokenize();
        eatsbits::eatscript::Parser parser(std::move(tokens));
        auto program = parser.parse();
        if (program) eval.evaluate(*program);
    } catch (const std::exception& ex) {
        if (errOut) *errOut = ex.what();
        return false;
    }

    if (!hasMeta) {
        if (errOut) *errOut = "Missing sol.set_postfx({...}) metadata";
        return false;
    }

    if (!outDef.builtinFilter.empty()) {
        outDef.wgslSource = buildBuiltinFilterWgsl(outDef.builtinFilter);
        if (outDef.wgslSource.empty()) {
            if (errOut) *errOut = "Unknown built-in filter: " + outDef.builtinFilter;
            return false;
        }
        return true;
    }

    eatsbits::eatscript::WgslShaderTranspiler transpiler;
    std::string transpileErr;
    outDef.wgslSource = transpiler.transpilePostFxSource(source, outDef.shaderFunc, &transpileErr);
    if (outDef.wgslSource.empty()) {
        if (errOut) *errOut = transpileErr.empty() ? "Failed to transpile Eatscript shader to WGSL" : transpileErr;
        return false;
    }
    return true;
}

std::string PostFxSystem::buildBuiltinFilterWgsl(const std::string& filterName) {
    std::string pre = eatsbits::eatscript::WgslShaderTranspiler::buildPostFxPreamble();

    if (filterName == "tilt_shift") {
        return pre + R"(
@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    let base = sample_scene(in.uv);
    let focusY: f32 = select(0.50, ufx.param1, ufx.param1 > 0.0 && ufx.param1 <= 1.0);
    let focusHalfW: f32 = select(0.20, ufx.param2, ufx.param2 > 0.0 && ufx.param2 <= 1.0);
    let dist: f32 = abs(in.uv.y - focusY);
    let blurT: f32 = smoothstep(focusHalfW * 0.65, focusHalfW + 0.28, dist);

    if (blurT <= 0.005) {
        return base;
    }

    let radius: f32 = (1.5 + 7.5 * clamp(ufx.intensity, 0.0, 1.0)) * blurT;
    var acc: vec3<f32> = vec3<f32>(0.0);
    var wsum: f32 = 0.0;
    // 13-tap Poisson/disc blur scaled by blurT
    let offsets = array<vec2<f32>, 12>(
        vec2<f32>(-0.326, -0.405), vec2<f32>(-0.840, -0.073),
        vec2<f32>(-0.695,  0.457), vec2<f32>(-0.203,  0.620),
        vec2<f32>( 0.962, -0.194), vec2<f32>( 0.473, -0.480),
        vec2<f32>( 0.519,  0.767), vec2<f32>( 0.185, -0.893),
        vec2<f32>( 0.507,  0.064), vec2<f32>( 0.896,  0.412),
        vec2<f32>(-0.321, -0.932), vec2<f32>(-0.791, -0.597)
    );
    acc = acc + base.rgb;
    wsum = wsum + 1.0;
    let texel = vec2<f32>(ufx.texel_x, ufx.texel_y) * radius;
    for (var i: i32 = 0; i < 12; i = i + 1) {
        acc = acc + sample_scene(in.uv + offsets[i] * texel).rgb;
        wsum = wsum + 1.0;
    }
    var blurred = acc / wsum;
    // Slight miniature saturation boost in focused zone
    let luma = dot(blurred, vec3<f32>(0.299, 0.587, 0.114));
    let satBoost = 1.0 + 0.14 * (1.0 - blurT) * clamp(ufx.intensity, 0.0, 1.0);
    blurred = clamp(vec3<f32>(luma) + (blurred - vec3<f32>(luma)) * satBoost, vec3<f32>(0.0), vec3<f32>(1.0));
    return vec4<f32>(mix(base.rgb, blurred, clamp(ufx.intensity, 0.0, 1.0)), base.a);
}
)";
    }

    if (filterName == "dither" || filterName == "bayer") {
        return pre + R"(
@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    let base = sample_scene(in.uv);
    let levels: f32 = select(4.0, clamp(ufx.param1, 2.0, 32.0), ufx.param1 >= 2.0);
    let pxScale: f32 = select(1.0, clamp(ufx.param2, 1.0, 6.0), ufx.param2 >= 1.0);
    let px = floor(in.uv.x * ufx.width / pxScale);
    let py = floor(in.uv.y * ufx.height / pxScale);
    let threshold = bayer4x4(px, py) - 0.5;
    let spread = 1.0 / max(levels - 1.0, 1.0);
    let dithered = clamp(floor((base.rgb + vec3<f32>(threshold * spread)) * (levels - 1.0) + 0.5) / max(levels - 1.0, 1.0), vec3<f32>(0.0), vec3<f32>(1.0));
    return vec4<f32>(mix(base.rgb, dithered, clamp(ufx.intensity, 0.0, 1.0)), base.a);
}
)";
    }

    if (filterName == "crt") {
        return pre + R"(
fn crt_rnd(c: vec2<f32>) -> f32 {
    return fract(sin(dot(c, vec2<f32>(12.9898, 78.233))) * 43758.5453);
}

fn crt_hsl2rgb(c: vec3<f32>) -> vec3<f32> {
    let K = vec4<f32>(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    let p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, vec3<f32>(0.0), vec3<f32>(1.0)), c.y);
}

fn crt_rgb_distort(uv: vec2<f32>, offset: f32) -> vec3<f32> {
    let r = sample_scene(uv + vec2<f32>(offset, 0.0)).r;
    let g = sample_scene(uv).g;
    let b = sample_scene(uv - vec2<f32>(offset, 0.0)).b;
    return vec3<f32>(r, g, b);
}

fn crt_light_factor(uv: vec2<f32>, t: f32) -> f32 {
    let intensity: f32 = 1.15;
    let ambient: f32 = 0.38;
    let lightX: f32 = 0.5 + sin(t * 0.85) * 0.30;
    let lightPos = vec2<f32>(lightX, 0.2);
    let dist = length(uv - lightPos);
    let falloff = pow(clamp(1.0 - (dist / 1.5), 0.0, 1.0), 0.85);
    return mix(ambient, 1.0 + intensity, falloff);
}

fn crt_tex(uv_in: vec2<f32>, bSize: f32, bColor: vec3<f32>, isFrame: bool, fSize: f32) -> vec3<f32> {
    let t = ufx.time;
    let grilleLvl: f32 = 0.95;
    let grilleDensity: f32 = 800.0;
    let scanlineLvl: f32 = 0.84;
    let rgbOffset: f32 = 0.0009;
    let noiseLevel: f32 = 0.045;
    let glassTint: f32 = 0.14;
    let vignetteStart: f32 = 0.25;
    let vignetteLvl: f32 = 20.0;

    var color = vec3<f32>(0.0);
    let isBorder = (uv_in.x < bSize || uv_in.x > 1.0 - bSize ||
                    uv_in.y < bSize || uv_in.y > 1.0 - bSize);
    let screenUV = (uv_in - vec2<f32>(bSize, bSize)) / max(1.0 - 2.0 * bSize, 0.001);

    if (isBorder && bSize > 0.0) {
        if (screenUV.x < 0.0 || screenUV.x > 1.0 || screenUV.y < 0.0 || screenUV.y > 1.0) {
            color = bColor;
        } else {
            color = crt_rgb_distort(screenUV, rgbOffset);
        }
    } else {
        if (screenUV.x < 0.0 || screenUV.x > 1.0 || screenUV.y < 0.0 || screenUV.y > 1.0) {
            color = bColor;
        } else {
            color = crt_rgb_distort(screenUV, rgbOffset);
        }
    }

    // Aperture Grille
    var grillePattern = sin(uv_in.x * grilleDensity * 3.14159265);
    grillePattern = grilleLvl + (1.0 - grilleLvl) * grillePattern;
    color = color * (0.5 + 0.5 * grillePattern);

    // Scanlines
    let scanlinePattern = sin(uv_in.y * ufx.height * 3.14159265);
    color = color * (scanlineLvl + (1.0 - scanlineLvl) * scanlinePattern);

    // Subtle phosphor grain
    color = color + crt_rnd(uv_in + vec2<f32>(t, t * 0.7)) * noiseLevel * 0.5;

    // Glass tint gradient
    let gt = 0.5 + 0.5 * uv_in.y;
    color = color + crt_hsl2rgb(vec3<f32>(0.0, 0.3, gt)) * glassTint * 0.35;

    // Corner-hugging CRT Vignette: uv * (1 - uv.yx)
    var vUv = uv_in;
    if (isFrame) {
        vUv = (vUv - 0.5) * (1.0 / max(1.0 - fSize, 0.001)) + 0.5;
    }
    vUv = clamp(vUv, vec2<f32>(0.0), vec2<f32>(1.0));
    vUv = vUv * (1.0 - vUv.yx);
    color = color * pow(max(vUv.x * vUv.y * vignetteLvl, 0.0), vignetteStart);

    // Overhead light source
    let lightFactor = crt_light_factor(uv_in, t);
    color = color * vec3<f32>(1.0, 0.98, 0.95) * lightFactor;
    return color;
}

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    let base = sample_scene(in.uv);
    let center = vec2<f32>(0.5, 0.5);
    let distFromCenter = length(in.uv - center);
    let pxSize = vec2<f32>(ufx.texel_x, ufx.texel_y);

    let curveStrength: f32 = select(0.95, ufx.param1, ufx.param1 > 0.0);
    let curveDistance: f32 = 5.0;
    let uv = in.uv + (in.uv - center) * pow(distFromCenter, curveDistance) * curveStrength;

    let frameSize: f32 = 20.0;
    let frameHue: f32 = 0.025;
    let frameSat: f32 = 0.10;
    let frameLight: f32 = 0.025;
    let frameReflect: f32 = 0.55;
    let frameGrain: f32 = 0.15;
    let borderSize: f32 = 0.0;

    let bColor = vec3<f32>(0.0, 0.0, 0.0);
    let frame = frameSize * pxSize.x;
    let border = borderSize * pxSize.x;
    let suv = (uv - vec2<f32>(frame, frame)) / max(1.0 - 2.0 * frame, 0.001);

    let isFrame = (uv.x < frame || uv.x > (1.0 - frame) ||
                   uv.y < frame || uv.y > (1.0 - frame));

    var color = vec3<f32>(0.0);
    if (isFrame) {
        let nX = 100.0 * pxSize.x;
        let nY = 100.0 * pxSize.y;
        let distX = min(uv.x, 1.0 - uv.x);
        let distY = min(uv.y, 1.0 - uv.y);
        let minDist = min(distX, distY);
        let intensity = mix(frameLight, 0.0, clamp(minDist / max(nX, nY) * 4.0, 0.0, 1.0));

        let f = vec2<f32>(borderSize * pxSize.x, borderSize * pxSize.y);
        var reflectedUV = suv;
        if (reflectedUV.x < f.x) {
            reflectedUV.x = f.x - (reflectedUV.x - f.x);
        } else if (reflectedUV.x > 1.0 - f.x) {
            reflectedUV.x = 1.0 - f.x - (reflectedUV.x - (1.0 - f.x));
        }
        if (reflectedUV.y < f.y) {
            reflectedUV.y = f.y - (reflectedUV.y - f.y);
        } else if (reflectedUV.y > 1.0 - f.y) {
            reflectedUV.y = 1.0 - f.y - (reflectedUV.y - (1.0 - f.y));
        }

        var blurred = vec3<f32>(0.0);
        let blurStep = 2.5 * pxSize.x;
        for (var bx: i32 = -1; bx <= 1; bx = bx + 1) {
            for (var by: i32 = -1; by <= 1; by = by + 1) {
                let bPos = reflectedUV + vec2<f32>(f32(bx) * blurStep, f32(by) * blurStep);
                blurred = blurred + crt_tex(bPos, border, bColor, true, frame);
            }
        }
        blurred = blurred / 18.0;

        color = crt_hsl2rgb(vec3<f32>(frameHue, frameSat, intensity));
        color = color * (1.0 - frameGrain * crt_rnd(suv));
        color = color + blurred * frameReflect * 0.5;

        let lf = crt_light_factor(uv, ufx.time);
        color = color * vec3<f32>(1.0, 0.98, 0.95) * lf;
    } else {
        color = crt_tex(suv, border, bColor, false, frame);
    }

    return vec4<f32>(mix(base.rgb, clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), clamp(ufx.intensity, 0.0, 1.0)), base.a);
}
)";
    }

    if (filterName == "vignette") {
        return pre + R"(
@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    let base = sample_scene(in.uv);
    let d = length(in.uv - vec2<f32>(0.5, 0.5)) * 1.4142;
    let vig = 1.0 - smoothstep(0.45, 1.15, d) * 0.55;
    return vec4<f32>(mix(base.rgb, base.rgb * vig, clamp(ufx.intensity, 0.0, 1.0)), base.a);
}
)";
    }

    if (filterName == "realism") {
        return pre + R"(
fn real_rnd(c: vec2<f32>) -> f32 {
    return fract(sin(dot(c, vec2<f32>(12.9898, 78.233))) * 43758.5453);
}

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    let base = sample_scene(in.uv);

    // u0: saturation multiplier (e.g. 0.40..0.68, default 0.55)
    // u1: optical lens soften radius in texels (default 0.85)
    // u2: subtle filmic contrast (default 1.06)
    // u3: warm/cool balance (-0.04..+0.04)
    let sat: f32 = select(0.55, ufx.u0, ufx.u0 > 0.0);
    let blurPx: f32 = select(0.85, ufx.u1, ufx.u1 > 0.0);
    let contrast: f32 = select(1.06, ufx.u2, ufx.u2 > 0.0);
    let warmth: f32 = ufx.u3;

    // 1. Subtle 9-tap Gaussian optical lens soften (tones down razor-sharp digital edges)
    let step = vec2<f32>(ufx.texel_x, ufx.texel_y) * blurPx;
    var col = base.rgb * 0.25;
    col = col + sample_scene(in.uv + vec2<f32>( step.x,  0.0)).rgb * 0.125;
    col = col + sample_scene(in.uv + vec2<f32>(-step.x,  0.0)).rgb * 0.125;
    col = col + sample_scene(in.uv + vec2<f32>( 0.0,  step.y)).rgb * 0.125;
    col = col + sample_scene(in.uv + vec2<f32>( 0.0, -step.y)).rgb * 0.125;
    col = col + sample_scene(in.uv + vec2<f32>( step.x,  step.y)).rgb * 0.0625;
    col = col + sample_scene(in.uv + vec2<f32>(-step.x,  step.y)).rgb * 0.0625;
    col = col + sample_scene(in.uv + vec2<f32>( step.x, -step.y)).rgb * 0.0625;
    col = col + sample_scene(in.uv + vec2<f32>(-step.x, -step.y)).rgb * 0.0625;

    // 2. Desaturate vivid digital primaries toward natural real-world pigment tones
    let luma = dot(col, vec3<f32>(0.2126, 0.7152, 0.0722));
    col = mix(vec3<f32>(luma), col, sat);

    // 3. Soft filmic highlight roll-off & gentle midtone contrast
    col = (col / (vec3<f32>(1.0) + col * 0.14)) * 1.12;
    col = clamp((col - vec3<f32>(0.45)) * contrast + vec3<f32>(0.45), vec3<f32>(0.0), vec3<f32>(1.0));

    // 4. Subtle natural white-balance shift
    col.r = clamp(col.r + warmth, 0.0, 1.0);
    col.b = clamp(col.b - warmth, 0.0, 1.0);

    // 5. Barely-perceptible optical vignette & sensor micro-grain (breaks up flat CG gradients)
    let d = length((in.uv - vec2<f32>(0.5, 0.5)) * vec2<f32>(1.25, 1.25));
    let vig = 1.0 - smoothstep(0.55, 1.25, d) * 0.14;
    let grain = (real_rnd(in.uv * vec2<f32>(ufx.width, ufx.height) + vec2<f32>(ufx.time * 0.1)) - 0.5) * 0.012;
    col = clamp(col * vig + vec3<f32>(grain), vec3<f32>(0.0), vec3<f32>(1.0));

    return vec4<f32>(mix(base.rgb, col, clamp(ufx.intensity, 0.0, 1.0)), base.a);
}
)";
    }

    if (filterName == "bloom" || filterName == "glow") {
        return pre + R"(
@fragment
fn fs_main(in: VsOut) -> @location(0) vec4<f32> {
    let base = sample_scene(in.uv);
    let thresh: f32 = select(0.68, ufx.param1, ufx.param1 > 0.0);
    let texel = vec2<f32>(ufx.texel_x, ufx.texel_y) * 3.0;
    var glow = vec3<f32>(0.0);
    for (var dy: i32 = -2; dy <= 2; dy = dy + 1) {
        for (var dx: i32 = -2; dx <= 2; dx = dx + 1) {
            let s = sample_scene(in.uv + vec2<f32>(f32(dx), f32(dy)) * texel).rgb;
            let lum = dot(s, vec3<f32>(0.299, 0.587, 0.114));
            glow = glow + s * max(lum - thresh, 0.0);
        }
    }
    glow = glow / 12.0;
    let outRgb = clamp(base.rgb + glow * clamp(ufx.intensity, 0.0, 2.0), vec3<f32>(0.0), vec3<f32>(1.0));
    return vec4<f32>(outRgb, base.a);
}
)";
    }

    return "";
}

} // namespace solengine
