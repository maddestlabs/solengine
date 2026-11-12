#include "eatsbits/ui/batch_renderer_2d.hpp"
#include "eatsbits/ui/monospace_font_8x16.hpp"
#include "eatsbits/ui/base64.hpp"
#include "eatsbits/tui/types.hpp"
#include "stb_image.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <iostream>
#include <fstream>
#include <mutex>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>
#if defined(EATS_ENABLE_WEBGPU)
#include <webgpu/webgpu.h>
#include <webgpu/webgpu_glfw.h>
#endif
#elif defined(EATS_ENABLE_WEBGPU)
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#endif

namespace eatsbits::ui {

namespace {
    BatchRenderer2D* s_activeRenderer = nullptr;
    std::mutex s_activeRendererMutex;
}

void BatchRenderer2D::setActiveBatchRenderer(BatchRenderer2D* renderer) {
    std::lock_guard<std::mutex> lock(s_activeRendererMutex);
    s_activeRenderer = renderer;
}

BatchRenderer2D* BatchRenderer2D::getActiveBatchRenderer() {
    std::lock_guard<std::mutex> lock(s_activeRendererMutex);
    return s_activeRenderer;
}

// ============================================================================
// Filament Batch Backend (Modern GPU / Filament PBR Stream + Zero-OpenGL DIB Presenter)
// ============================================================================
class FilamentBatchBackend : public IBatchRenderBackend {
public:
    FilamentBatchBackend() = default;
    ~FilamentBatchBackend() override {
        shutdown();
    }

    bool initialize(void* windowHandle, uint32_t width, uint32_t height) override {
        viewportWidth_ = width;
        viewportHeight_ = height;
        atlasWidth_ = 1024;
        atlasHeight_ = 1024;
        fontAtlas_.assign(atlasWidth_ * atlasHeight_, 0);

        // Populate dedicated 128x256 monospace font atlas directly from kMonospaceFont8x16
        monospaceAtlas_.resize(128 * 256, 0);
        for (int c = 0; c < 256; ++c) {
            int glyphCol = c % 16;
            int glyphRow = c / 16;
            int originX = glyphCol * 8;
            int originY = glyphRow * 16;
            for (int y = 0; y < 16; ++y) {
                uint8_t rowByte = kMonospaceFont8x16[c * 16 + y];
                for (int x = 0; x < 8; ++x) {
                    uint8_t alpha = (rowByte & (1 << (7 - x))) ? 255 : 0;
                    monospaceAtlas_[(originY + y) * 128 + (originX + x)] = alpha;
                }
            }
        }

        if (width > 0 && height > 0) {
            framebuffer_.assign(static_cast<size_t>(width) * height, 0xFF14171E);
        }

#if defined(_WIN32)
        if (windowHandle) {
            HWND candHwnd = reinterpret_cast<HWND>(windowHandle);
            if (IsWindow(candHwnd)) {
                hwnd_ = candHwnd;
            } else {
                HWND win32 = glfwGetWin32Window(static_cast<GLFWwindow*>(windowHandle));
                if (win32 && IsWindow(win32)) {
                    hwnd_ = win32;
                }
            }
        }
#else
        (void)windowHandle;
#endif
        return true;
    }

    void shutdown() override {
        fontAtlas_.clear();
        monospaceAtlas_.clear();
        framebuffer_.clear();
#if defined(_WIN32)
        hwnd_ = nullptr;
#endif
    }

    void resize(uint32_t width, uint32_t height) override {
        if (width == 0 || height == 0) return;
        viewportWidth_ = width;
        viewportHeight_ = height;
        framebuffer_.assign(static_cast<size_t>(width) * height, 0xFF14171E);
    }

    void beginPass(float width, float height) override {
        uint32_t targetW = static_cast<uint32_t>(std::round(width));
        uint32_t targetH = static_cast<uint32_t>(std::round(height));
        if (targetW > 0 && targetH > 0) {
            viewportWidth_ = targetW;
            viewportHeight_ = targetH;
        }
        blendMode_ = BlendMode::Normal;
        scissorEnabled_ = false;
        size_t totalPixels = static_cast<size_t>(viewportWidth_) * viewportHeight_;
        if (framebuffer_.size() != totalPixels) {
            framebuffer_.assign(totalPixels, 0xFF14171E);
        } else {
            std::fill(framebuffer_.begin(), framebuffer_.end(), 0xFF14171E);
        }
    }

    void updateFontTexture(int x, int y, int w, int h, const unsigned char* data, int atlasW, int atlasH) override {
        (void)x; (void)y; (void)w; (void)h;
        atlasWidth_ = atlasW;
        atlasHeight_ = atlasH;
        size_t totalBytes = static_cast<size_t>(atlasW) * atlasH;
        if (fontAtlas_.size() != totalBytes) {
            fontAtlas_.resize(totalBytes, 0);
        }
        if (data && totalBytes > 0) {
            std::memcpy(fontAtlas_.data(), data, totalBytes);
        }
    }

    void renderBatch(const std::vector<Vertex2D>& vertices) override {
        totalVerticesRendered_ += vertices.size();
        if (vertices.size() < 3 || viewportWidth_ == 0 || viewportHeight_ == 0 || framebuffer_.empty()) return;

        const size_t triCount = vertices.size() / 3;
        for (size_t t = 0; t < triCount; ) {
            // 1. Analytical Rounded Rect / Circle quad (mode == 2)
            if (t + 1 < triCount && vertices[t * 3 + 0].mode == 2 && vertices[(t + 1) * 3 + 0].mode == 2) {
                const auto& v0 = vertices[t * 3 + 0];
                const auto& v1 = vertices[t * 3 + 1];
                const auto& v2 = vertices[t * 3 + 2];
                const auto& w2 = vertices[(t + 1) * 3 + 2];
                rasterizeRoundedRect(v0, v1, v2, w2, v0.u);
                t += 2;
                continue;
            }

            // 2. Check if triangles t and t+1 form an axis-aligned or convex quad
            if (t + 1 < triCount) {
                const auto& v0 = vertices[t * 3 + 0];
                const auto& v1 = vertices[t * 3 + 1];
                const auto& v2 = vertices[t * 3 + 2];
                const auto& w0 = vertices[(t + 1) * 3 + 0];
                const auto& w1 = vertices[(t + 1) * 3 + 1];
                const auto& w2 = vertices[(t + 1) * 3 + 2];

                // Fast path: standard quad emitted by drawRect / drawKnurledRing (solid mode == 0 or 5)
                if ((v0.mode == 0 || v0.mode == 5) && (w0.mode == 0 || w0.mode == 5) &&
                    std::abs(v0.x - w0.x) < 0.001f && std::abs(v0.y - w0.y) < 0.001f &&
                    std::abs(v2.x - w1.x) < 0.001f && std::abs(v2.y - w1.y) < 0.001f &&
                    v0.color == w0.color && v2.color == w1.color) {
                    rasterizeQuad(v0, v1, v2, w2);
                    t += 2;
                    continue;
                }

                // Generalized quad detection: any 2 shared vertices between solid coplanar triangles
                if ((v0.mode == 0 || v0.mode == 5) && (w0.mode == 0 || w0.mode == 5) &&
                    v0.color == v1.color && v0.color == v2.color &&
                    v0.color == w0.color && v0.color == w1.color && v0.color == w2.color) {
                    
                    const Vertex2D* vArr[3] = { &v0, &v1, &v2 };
                    const Vertex2D* wArr[3] = { &w0, &w1, &w2 };

                    int vMatch[3] = { -1, -1, -1 };
                    int matchCount = 0;
                    for (int vi = 0; vi < 3; ++vi) {
                        for (int wi = 0; wi < 3; ++wi) {
                            if (std::abs(vArr[vi]->x - wArr[wi]->x) < 0.001f &&
                                std::abs(vArr[vi]->y - wArr[wi]->y) < 0.001f) {
                                vMatch[vi] = wi;
                                ++matchCount;
                                break;
                            }
                        }
                    }

                    if (matchCount == 2) {
                        int vUnmatched = 0;
                        while (vUnmatched < 3 && vMatch[vUnmatched] != -1) ++vUnmatched;

                        bool wMatched[3] = { false, false, false };
                        for (int vi = 0; vi < 3; ++vi) {
                            if (vMatch[vi] != -1) wMatched[vMatch[vi]] = true;
                        }
                        int wUnmatched = 0;
                        while (wUnmatched < 3 && wMatched[wUnmatched]) ++wUnmatched;

                        int m0 = (vUnmatched + 1) % 3;
                        int m1 = (vUnmatched + 2) % 3;

                        const Vertex2D& q0 = *vArr[vUnmatched];
                        const Vertex2D& q1 = *vArr[m0];
                        const Vertex2D& q2 = *wArr[wUnmatched];
                        const Vertex2D& q3 = *vArr[m1];

                        float cp0 = (q1.x - q0.x) * (q2.y - q1.y) - (q1.y - q0.y) * (q2.x - q1.x);
                        float cp1 = (q2.x - q1.x) * (q3.y - q2.y) - (q2.y - q1.y) * (q3.x - q2.x);
                        float cp2 = (q3.x - q2.x) * (q0.y - q3.y) - (q3.y - q2.y) * (q0.x - q3.x);
                        float cp3 = (q0.x - q3.x) * (q1.y - q0.y) - (q0.y - q3.y) * (q1.x - q0.x);

                        bool allPos = (cp0 > 1e-4f && cp1 > 1e-4f && cp2 > 1e-4f && cp3 > 1e-4f);
                        bool allNeg = (cp0 < -1e-4f && cp1 < -1e-4f && cp2 < -1e-4f && cp3 < -1e-4f);

                        if (allPos || allNeg) {
                            rasterizeQuad(q0, q1, q2, q3);
                            t += 2;
                            continue;
                        }
                    }
                }
            }

            rasterizeTriangle(vertices[t * 3 + 0], vertices[t * 3 + 1], vertices[t * 3 + 2]);
            t += 1;
        }
    }

    void endPass() override {
#if defined(_WIN32)
        if (directPresent_ && hwnd_ && !framebuffer_.empty() && viewportWidth_ > 0 && viewportHeight_ > 0) {
            HDC hdc = GetDC(hwnd_);
            if (hdc) {
                RECT clientRect{};
                GetClientRect(hwnd_, &clientRect);
                int clientW = clientRect.right - clientRect.left;
                int clientH = clientRect.bottom - clientRect.top;

                BITMAPINFO bmi{};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = static_cast<LONG>(viewportWidth_);
                bmi.bmiHeader.biHeight = -static_cast<LONG>(viewportHeight_); // Top-down DIB
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;

                StretchDIBits(
                    hdc,
                    0, 0, clientW, clientH,
                    0, 0, viewportWidth_, viewportHeight_,
                    framebuffer_.data(),
                    &bmi,
                    DIB_RGB_COLORS,
                    SRCCOPY
                );
                ReleaseDC(hwnd_, hdc);
            }
        }
#endif
    }

    RenderBackendType getBackendType() const noexcept override {
        return RenderBackendType::Filament;
    }

    const std::vector<unsigned char>& getFontAtlas() const noexcept { return fontAtlas_; }
    const uint32_t* getFramebuffer() const noexcept override { return framebuffer_.data(); }
    uint32_t getFramebufferWidth() const noexcept override { return viewportWidth_; }
    uint32_t getFramebufferHeight() const noexcept override { return viewportHeight_; }
    void setDirectPresent(bool enable) noexcept override { directPresent_ = enable; }
    void setAntiAliasingMode(int mode) override { antiAliasingMode_ = std::clamp(mode, 0, 2); }
    void setBlendMode(BlendMode mode) override { blendMode_ = mode; }
    size_t getTotalVerticesRendered() const noexcept { return totalVerticesRendered_; }

    void snapshotFramebuffer(std::vector<uint32_t>& outPixels, uint32_t& outW, uint32_t& outH) const override {
        outW = viewportWidth_;
        outH = viewportHeight_;
        if (!framebuffer_.empty() && outW > 0 && outH > 0) {
            outPixels = framebuffer_;
        } else {
            outPixels.clear();
        }
    }

    void restoreFramebuffer(const uint32_t* srcPixels, uint32_t w, uint32_t h, float opacity = 1.0f) override {
        if (!srcPixels || w == 0 || h == 0 || framebuffer_.empty()) return;
        if (w == viewportWidth_ && h == viewportHeight_ && opacity >= 0.999f) {
            std::memcpy(framebuffer_.data(), srcPixels, framebuffer_.size() * sizeof(uint32_t));
        } else {
            renderRgba(0.0f, 0.0f, static_cast<float>(viewportWidth_), static_cast<float>(viewportHeight_),
                       reinterpret_cast<const uint8_t*>(srcPixels), static_cast<int>(w), static_cast<int>(h), opacity);
        }
    }

    void restoreFramebufferRegion(const uint32_t* srcPixels, uint32_t srcW, uint32_t srcH,
                                  float dstX, float dstY, float srcX, float srcY, float w, float h, float opacity = 1.0f) override {
        if (!srcPixels || srcW == 0 || srcH == 0 || framebuffer_.empty() || w <= 0.0f || h <= 0.0f) return;

        int dx = static_cast<int>(std::round(dstX));
        int dy = static_cast<int>(std::round(dstY));
        int sx = static_cast<int>(std::round(srcX));
        int sy = static_cast<int>(std::round(srcY));
        int rw = static_cast<int>(std::round(w));
        int rh = static_cast<int>(std::round(h));

        if (dx < 0) { rw += dx; sx -= dx; dx = 0; }
        if (dy < 0) { rh += dy; sy -= dy; dy = 0; }
        if (dx + rw > static_cast<int>(viewportWidth_)) rw = static_cast<int>(viewportWidth_) - dx;
        if (dy + rh > static_cast<int>(viewportHeight_)) rh = static_cast<int>(viewportHeight_) - dy;
        if (sx < 0 || sy < 0 || sx + rw > static_cast<int>(srcW) || sy + rh > static_cast<int>(srcH) || rw <= 0 || rh <= 0) return;

        if (opacity >= 0.999f) {
            for (int row = 0; row < rh; ++row) {
                std::memcpy(&framebuffer_[(dy + row) * viewportWidth_ + dx],
                            &srcPixels[(sy + row) * srcW + sx],
                            static_cast<size_t>(rw) * sizeof(uint32_t));
            }
        } else {
            renderRgba(dstX, dstY, w, h, reinterpret_cast<const uint8_t*>(srcPixels), static_cast<int>(srcW), static_cast<int>(srcH), opacity);
        }
    }

    void setScissorRect(float x, float y, float w, float h) override {
        scissorEnabled_ = true;
        scissor_ = Rect2D{x, y, w, h};
    }

    void clearScissorRect() override {
        scissorEnabled_ = false;
    }

    inline void getClipBounds(int& clipMinX, int& clipMaxX, int& clipMinY, int& clipMaxY) const noexcept {
        if (scissorEnabled_) {
            clipMinX = std::max(0, static_cast<int>(std::floor(scissor_.x)));
            clipMaxX = std::min(static_cast<int>(viewportWidth_) - 1, static_cast<int>(std::ceil(scissor_.x + scissor_.w)) - 1);
            clipMinY = std::max(0, static_cast<int>(std::floor(scissor_.y)));
            clipMaxY = std::min(static_cast<int>(viewportHeight_) - 1, static_cast<int>(std::ceil(scissor_.y + scissor_.h)) - 1);
        } else {
            clipMinX = 0;
            clipMaxX = static_cast<int>(viewportWidth_) - 1;
            clipMinY = 0;
            clipMaxY = static_cast<int>(viewportHeight_) - 1;
        }
    }

    void renderRgbaRegion(float x, float y, float w, float h,
                          const uint8_t* rgba, int imgW, int imgH,
                          float u0, float v0, float u1, float v1,
                          float opacity) override {
        if (!rgba || imgW <= 0 || imgH <= 0 || w <= 0.0f || h <= 0.0f || framebuffer_.empty() || opacity <= 0.001f) return;

        int clipMinX, clipMaxX, clipMinY, clipMaxY;
        getClipBounds(clipMinX, clipMaxX, clipMinY, clipMaxY);
        int minX = std::max(clipMinX, static_cast<int>(std::floor(x)));
        int maxX = std::min(clipMaxX, static_cast<int>(std::ceil(x + w)));
        int minY = std::max(clipMinY, static_cast<int>(std::floor(y)));
        int maxY = std::min(clipMaxY, static_cast<int>(std::ceil(y + h)));

        if (minX > maxX || minY > maxY) return;

        // 1:1 unscaled opaque scanline blit optimization (e.g. for cached faceplate backgrounds)
        if (std::abs(w - static_cast<float>(imgW)) < 0.01f &&
            std::abs(h - static_cast<float>(imgH)) < 0.01f &&
            std::abs(u0) < 0.001f && std::abs(v0) < 0.001f &&
            std::abs(u1 - 1.0f) < 0.001f && std::abs(v1 - 1.0f) < 0.001f &&
            opacity >= 0.999f) {
            int srcStartX = minX - static_cast<int>(std::floor(x));
            int copyW = maxX - minX + 1;
            if (copyW > 0) {
                for (int py = minY; py <= maxY; ++py) {
                    int srcY = py - static_cast<int>(std::floor(y));
                    if (srcY >= 0 && srcY < imgH) {
                        const uint32_t* srcRow = reinterpret_cast<const uint32_t*>(rgba) + (srcY * imgW + srcStartX);
                        uint32_t* dstRow = &framebuffer_[py * viewportWidth_ + minX];
                        std::memcpy(dstRow, srcRow, static_cast<size_t>(copyW) * sizeof(uint32_t));
                    }
                }
                return;
            }
        }

        const float invW = 1.0f / w;
        const float invH = 1.0f / h;
        const float du = u1 - u0;
        const float dv = v1 - v0;

        for (int py = minY; py <= maxY; ++py) {
            float normY = std::clamp((static_cast<float>(py) + 0.5f - y) * invH, 0.0f, 1.0f);
            float v = v0 + normY * dv;
            float fy = v * static_cast<float>(imgH) - 0.5f;
            int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, imgH - 1);
            int y1 = std::min(y0 + 1, imgH - 1);
            float wy = std::clamp(fy - std::floor(fy), 0.0f, 1.0f);

            uint32_t* dstPtr = &framebuffer_[py * viewportWidth_ + minX];
            for (int px = minX; px <= maxX; ++px, ++dstPtr) {
                float normX = std::clamp((static_cast<float>(px) + 0.5f - x) * invW, 0.0f, 1.0f);
                float u = u0 + normX * du;
                float fx = u * static_cast<float>(imgW) - 0.5f;
                int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, imgW - 1);
                int x1 = std::min(x0 + 1, imgW - 1);
                float wx = std::clamp(fx - std::floor(fx), 0.0f, 1.0f);

                const uint8_t* p00 = &rgba[(y0 * imgW + x0) * 4];
                const uint8_t* p10 = &rgba[(y0 * imgW + x1) * 4];
                const uint8_t* p01 = &rgba[(y1 * imgW + x0) * 4];
                const uint8_t* p11 = &rgba[(y1 * imgW + x1) * 4];

                float w00 = (1.0f - wx) * (1.0f - wy);
                float w10 = wx * (1.0f - wy);
                float w01 = (1.0f - wx) * wy;
                float w11 = wx * wy;

                float r = p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11;
                float g = p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11;
                float b = p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11;
                float a = (p00[3] * w00 + p10[3] * w10 + p01[3] * w01 + p11[3] * w11) * opacity;

                uint32_t ua = static_cast<uint32_t>(a + 0.5f);
                if (ua >= 255) {
                    *dstPtr = (0xFF << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
                } else if (ua > 0) {
                    uint32_t dst = *dstPtr;
                    uint32_t dr = (dst >> 16) & 0xFF;
                    uint32_t dg = (dst >> 8) & 0xFF;
                    uint32_t db = dst & 0xFF;
                    uint32_t invA = 255 - ua;
                    uint32_t ur = static_cast<uint32_t>(r);
                    uint32_t ug = static_cast<uint32_t>(g);
                    uint32_t ub = static_cast<uint32_t>(b);
                    *dstPtr = (0xFF << 24) | (((ur * ua + dr * invA) / 255) << 16)
                                           | (((ug * ua + dg * invA) / 255) << 8)
                                           | ((ub * ua + db * invA) / 255);
                }
            }
        }
    }

    void renderRgba(float x, float y, float w, float h, const uint8_t* rgba, int imgW, int imgH, float opacity) override {
        renderRgbaRegion(x, y, w, h, rgba, imgW, imgH, 0.0f, 0.0f, 1.0f, 1.0f, opacity);
    }

    void applyBackdropBlur(float /*radius*/ = 3.0f, float dimFactor = 0.50f) override {
        if (framebuffer_.empty() || viewportWidth_ == 0 || viewportHeight_ == 0) return;

        const uint32_t w = viewportWidth_;
        const uint32_t h = viewportHeight_;
        const uint32_t dsW = std::max(1u, w / 2);
        const uint32_t dsH = std::max(1u, h / 2);

        // 1. Downsample 2x with 2x2 box average into temp buffer
        std::vector<uint32_t> smallBuf(static_cast<size_t>(dsW) * dsH);
        for (uint32_t dy = 0; dy < dsH; ++dy) {
            uint32_t sy0 = dy * 2;
            uint32_t sy1 = std::min(sy0 + 1, h - 1);
            const uint32_t* row0 = &framebuffer_[static_cast<size_t>(sy0) * w];
            const uint32_t* row1 = &framebuffer_[static_cast<size_t>(sy1) * w];
            uint32_t* dstRow = &smallBuf[static_cast<size_t>(dy) * dsW];

            for (uint32_t dx = 0; dx < dsW; ++dx) {
                uint32_t sx0 = dx * 2;
                uint32_t sx1 = std::min(sx0 + 1, w - 1);

                uint32_t p00 = row0[sx0];
                uint32_t p01 = row0[sx1];
                uint32_t p10 = row1[sx0];
                uint32_t p11 = row1[sx1];

                // Average RGBA
                uint32_t r = (((p00 & 0xFF) + (p01 & 0xFF) + (p10 & 0xFF) + (p11 & 0xFF)) >> 2);
                uint32_t g = ((((p00 >> 8) & 0xFF) + ((p01 >> 8) & 0xFF) + ((p10 >> 8) & 0xFF) + ((p11 >> 8) & 0xFF)) >> 2);
                uint32_t b = ((((p00 >> 16) & 0xFF) + ((p01 >> 16) & 0xFF) + ((p10 >> 16) & 0xFF) + ((p11 >> 16) & 0xFF)) >> 2);
                dstRow[dx] = 0xFF000000 | (b << 16) | (g << 8) | r;
            }
        }

        // 2. Horizontal Blur pass (5-tap Gaussian)
        std::vector<uint32_t> tempBuf(smallBuf.size());
        for (uint32_t y = 0; y < dsH; ++y) {
            const uint32_t* srcRow = &smallBuf[static_cast<size_t>(y) * dsW];
            uint32_t* dstRow = &tempBuf[static_cast<size_t>(y) * dsW];
            for (uint32_t x = 0; x < dsW; ++x) {
                int xL2 = std::max(0, static_cast<int>(x) - 2);
                int xL1 = std::max(0, static_cast<int>(x) - 1);
                int xR1 = std::min(static_cast<int>(dsW) - 1, static_cast<int>(x) + 1);
                int xR2 = std::min(static_cast<int>(dsW) - 1, static_cast<int>(x) + 2);

                uint32_t c0 = srcRow[xL2];
                uint32_t c1 = srcRow[xL1];
                uint32_t c2 = srcRow[x];
                uint32_t c3 = srcRow[xR1];
                uint32_t c4 = srcRow[xR2];

                // Weights: 1, 2, 4, 2, 1 (sum = 10)
                uint32_t r = ((c0 & 0xFF) + 2 * (c1 & 0xFF) + 4 * (c2 & 0xFF) + 2 * (c3 & 0xFF) + (c4 & 0xFF)) / 10;
                uint32_t g = (((c0 >> 8) & 0xFF) + 2 * ((c1 >> 8) & 0xFF) + 4 * ((c2 >> 8) & 0xFF) + 2 * ((c3 >> 8) & 0xFF) + ((c4 >> 8) & 0xFF)) / 10;
                uint32_t b = (((c0 >> 16) & 0xFF) + 2 * ((c1 >> 16) & 0xFF) + 4 * ((c2 >> 16) & 0xFF) + 2 * ((c3 >> 16) & 0xFF) + ((c4 >> 16) & 0xFF)) / 10;
                dstRow[x] = 0xFF000000 | (b << 16) | (g << 8) | r;
            }
        }

        // 3. Vertical Blur pass
        for (uint32_t y = 0; y < dsH; ++y) {
            int yT2 = std::max(0, static_cast<int>(y) - 2);
            int yT1 = std::max(0, static_cast<int>(y) - 1);
            int yB1 = std::min(static_cast<int>(dsH) - 1, static_cast<int>(y) + 1);
            int yB2 = std::min(static_cast<int>(dsH) - 1, static_cast<int>(y) + 2);

            const uint32_t* rT2 = &tempBuf[static_cast<size_t>(yT2) * dsW];
            const uint32_t* rT1 = &tempBuf[static_cast<size_t>(yT1) * dsW];
            const uint32_t* rM  = &tempBuf[static_cast<size_t>(y) * dsW];
            const uint32_t* rB1 = &tempBuf[static_cast<size_t>(yB1) * dsW];
            const uint32_t* rB2 = &tempBuf[static_cast<size_t>(yB2) * dsW];
            uint32_t* dstRow = &smallBuf[static_cast<size_t>(y) * dsW];

            for (uint32_t x = 0; x < dsW; ++x) {
                uint32_t c0 = rT2[x];
                uint32_t c1 = rT1[x];
                uint32_t c2 = rM[x];
                uint32_t c3 = rB1[x];
                uint32_t c4 = rB2[x];

                uint32_t r = ((c0 & 0xFF) + 2 * (c1 & 0xFF) + 4 * (c2 & 0xFF) + 2 * (c3 & 0xFF) + (c4 & 0xFF)) / 10;
                uint32_t g = (((c0 >> 8) & 0xFF) + 2 * ((c1 >> 8) & 0xFF) + 4 * ((c2 >> 8) & 0xFF) + 2 * ((c3 >> 8) & 0xFF) + ((c4 >> 8) & 0xFF)) / 10;
                uint32_t b = (((c0 >> 16) & 0xFF) + 2 * ((c1 >> 16) & 0xFF) + 4 * ((c2 >> 16) & 0xFF) + 2 * ((c3 >> 16) & 0xFF) + ((c4 >> 16) & 0xFF)) / 10;
                dstRow[x] = 0xFF000000 | (b << 16) | (g << 8) | r;
            }
        }

        // 4. Bilinear upsample back to framebuffer_ with dimFactor
        float dim = std::clamp(dimFactor, 0.0f, 1.0f);
        for (uint32_t y = 0; y < h; ++y) {
            float srcY = (static_cast<float>(y) + 0.5f) * (static_cast<float>(dsH) / static_cast<float>(h)) - 0.5f;
            int y0 = std::clamp(static_cast<int>(std::floor(srcY)), 0, static_cast<int>(dsH) - 1);
            int y1 = std::clamp(y0 + 1, 0, static_cast<int>(dsH) - 1);
            float fy = std::clamp(srcY - static_cast<float>(y0), 0.0f, 1.0f);

            const uint32_t* row0 = &smallBuf[static_cast<size_t>(y0) * dsW];
            const uint32_t* row1 = &smallBuf[static_cast<size_t>(y1) * dsW];
            uint32_t* dstRow = &framebuffer_[static_cast<size_t>(y) * w];

            for (uint32_t x = 0; x < w; ++x) {
                float srcX = (static_cast<float>(x) + 0.5f) * (static_cast<float>(dsW) / static_cast<float>(w)) - 0.5f;
                int x0 = std::clamp(static_cast<int>(std::floor(srcX)), 0, static_cast<int>(dsW) - 1);
                int x1 = std::clamp(x0 + 1, 0, static_cast<int>(dsW) - 1);
                float fx = std::clamp(srcX - static_cast<float>(x0), 0.0f, 1.0f);

                uint32_t p00 = row0[x0];
                uint32_t p10 = row0[x1];
                uint32_t p01 = row1[x0];
                uint32_t p11 = row1[x1];

                float r0 = (p00 & 0xFF) * (1.0f - fx) + (p10 & 0xFF) * fx;
                float r1 = (p01 & 0xFF) * (1.0f - fx) + (p11 & 0xFF) * fx;
                float r = (r0 * (1.0f - fy) + r1 * fy) * dim;

                float g0 = ((p00 >> 8) & 0xFF) * (1.0f - fx) + ((p10 >> 8) & 0xFF) * fx;
                float g1 = ((p01 >> 8) & 0xFF) * (1.0f - fx) + ((p11 >> 8) & 0xFF) * fx;
                float g = (g0 * (1.0f - fy) + g1 * fy) * dim;

                float b0 = ((p00 >> 16) & 0xFF) * (1.0f - fx) + ((p10 >> 16) & 0xFF) * fx;
                float b1 = ((p01 >> 16) & 0xFF) * (1.0f - fx) + ((p11 >> 16) & 0xFF) * fx;
                float b = (b0 * (1.0f - fy) + b1 * fy) * dim;

                dstRow[x] = 0xFF000000 | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r);
            }
        }
    }

    void renderConvexPolygon(const Point2D* inputPts, size_t count, uint32_t col0, uint32_t col1,
                             Point2D gradP0, Point2D gradP1) override {
        if (!inputPts || count < 3 || count > 32) return;
        uint32_t a0 = (col0 >> 24) & 0xFF;
        uint32_t a1 = (col1 >> 24) & 0xFF;
        if (a0 == 0 && a1 == 0) return;

        float fMinX = inputPts[0].x, fMaxX = inputPts[0].x;
        float fMinY = inputPts[0].y, fMaxY = inputPts[0].y;
        for (size_t i = 1; i < count; ++i) {
            fMinX = std::min(fMinX, inputPts[i].x);
            fMaxX = std::max(fMaxX, inputPts[i].x);
            fMinY = std::min(fMinY, inputPts[i].y);
            fMaxY = std::max(fMaxY, inputPts[i].y);
        }

        int clipMinX, clipMaxX, clipMinY, clipMaxY;
        getClipBounds(clipMinX, clipMaxX, clipMinY, clipMaxY);
        int minX = std::max(clipMinX, static_cast<int>(std::floor(fMinX)));
        int maxX = std::min(clipMaxX, static_cast<int>(std::ceil(fMaxX)));
        int minY = std::max(clipMinY, static_cast<int>(std::floor(fMinY)));
        int maxY = std::min(clipMaxY, static_cast<int>(std::ceil(fMaxY)));

        if (minX > maxX || minY > maxY) return;

        // Check signed area to determine winding
        float area = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            size_t next = (i + 1) % count;
            area += (inputPts[i].x * inputPts[next].y - inputPts[next].x * inputPts[i].y);
        }
        if (std::abs(area) < 1e-4f) return;

        std::array<Point2D, 32> pts;
        if (area > 0.0f) {
            for (size_t i = 0; i < count; ++i) pts[i] = inputPts[i];
        } else {
            for (size_t i = 0; i < count; ++i) pts[i] = inputPts[count - 1 - i];
        }

        // Edge deltas
        std::array<float, 32> dx, dy, stepX;
        for (size_t i = 0; i < count; ++i) {
            size_t next = (i + 1) % count;
            dx[i] = pts[next].x - pts[i].x;
            dy[i] = pts[next].y - pts[i].y;
            stepX[i] = -dy[i];
        }

        const bool isGradient = (col0 != col1);
        const float c0_r = static_cast<float>(col0 & 0xFF);
        const float c0_g = static_cast<float>((col0 >> 8) & 0xFF);
        const float c0_b = static_cast<float>((col0 >> 16) & 0xFF);
        const float c0_a = static_cast<float>((col0 >> 24) & 0xFF);

        const float c1_r = static_cast<float>(col1 & 0xFF);
        const float c1_g = static_cast<float>((col1 >> 8) & 0xFF);
        const float c1_b = static_cast<float>((col1 >> 16) & 0xFF);
        const float c1_a = static_cast<float>((col1 >> 24) & 0xFF);

        float gDx = gradP1.x - gradP0.x;
        float gDy = gradP1.y - gradP0.y;
        float gLenSq = gDx * gDx + gDy * gDy;
        float gInvLenSq = (gLenSq > 1e-4f) ? (1.0f / gLenSq) : 0.0f;
        float gStepTx = gDx * gInvLenSq;

        const float startPx = static_cast<float>(minX) + 0.5f;

        if (antiAliasingMode_ == 0) {
            for (int y = minY; y <= maxY; ++y) {
                const float py = static_cast<float>(y) + 0.5f;
                std::array<float, 32> e;
                for (size_t i = 0; i < count; ++i) {
                    e[i] = dx[i] * (py - pts[i].y) - dy[i] * (startPx - pts[i].x);
                }

                float curT = ((startPx - gradP0.x) * gDx + (py - gradP0.y) * gDy) * gInvLenSq;
                uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + minX];

                for (int x = minX; x <= maxX; ++x, ++rowDst) {
                    bool inside = true;
                    for (size_t i = 0; i < count; ++i) {
                        if (e[i] < 0.0f) { inside = false; break; }
                    }
                    if (inside) {
                        if (!isGradient) {
                            *rowDst = blendPixel(*rowDst, static_cast<uint32_t>(c0_r),
                                                          static_cast<uint32_t>(c0_g),
                                                          static_cast<uint32_t>(c0_b),
                                                          static_cast<uint32_t>(c0_a));
                        } else {
                            float t = std::clamp(curT, 0.0f, 1.0f);
                            uint32_t ur = static_cast<uint32_t>(c0_r + (c1_r - c0_r) * t);
                            uint32_t ug = static_cast<uint32_t>(c0_g + (c1_g - c0_g) * t);
                            uint32_t ub = static_cast<uint32_t>(c0_b + (c1_b - c0_b) * t);
                            uint32_t ua = static_cast<uint32_t>(c0_a + (c1_a - c0_a) * t);
                            *rowDst = blendPixel(*rowDst, ur, ug, ub, ua);
                        }
                    }
                    for (size_t i = 0; i < count; ++i) e[i] += stepX[i];
                    curT += gStepTx;
                }
            }
            return;
        }

        // Subpixel Sampling: 2x Fast or 4x RGSS
        const int numSamples = (antiAliasingMode_ == 1) ? 2 : 4;
        const float ox[4] = { 0.375f, -0.125f,  0.125f, -0.375f };
        const float oy[4] = {-0.125f, -0.375f,  0.375f,  0.125f };

        std::array<std::array<float, 4>, 32> de;
        std::array<float, 32> minDe, maxDe;
        for (size_t i = 0; i < count; ++i) {
            for (int s = 0; s < numSamples; ++s) {
                de[i][s] = dx[i] * oy[s] - dy[i] * ox[s];
            }
            minDe[i] = de[i][0];
            maxDe[i] = de[i][0];
            for (int s = 1; s < numSamples; ++s) {
                minDe[i] = std::min(minDe[i], de[i][s]);
                maxDe[i] = std::max(maxDe[i], de[i][s]);
            }
        }

        for (int y = minY; y <= maxY; ++y) {
            const float py = static_cast<float>(y) + 0.5f;
            std::array<float, 32> e;
            for (size_t i = 0; i < count; ++i) {
                e[i] = dx[i] * (py - pts[i].y) - dy[i] * (startPx - pts[i].x);
            }

            float curT = ((startPx - gradP0.x) * gDx + (py - gradP0.y) * gDy) * gInvLenSq;
            uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + minX];

            for (int x = minX; x <= maxX; ++x, ++rowDst) {
                bool maybeInside = true;
                for (size_t i = 0; i < count; ++i) {
                    if (e[i] + maxDe[i] < 0.0f) { maybeInside = false; break; }
                }
                if (maybeInside) {
                    bool fullyInside = true;
                    for (size_t i = 0; i < count; ++i) {
                        if (e[i] + minDe[i] < 0.0f) { fullyInside = false; break; }
                    }

                    uint32_t mask = numSamples;
                    if (!fullyInside) {
                        mask = 0;
                        for (int s = 0; s < numSamples; ++s) {
                            bool sampleIn = true;
                            for (size_t i = 0; i < count; ++i) {
                                if (e[i] + de[i][s] < 0.0f) { sampleIn = false; break; }
                            }
                            if (sampleIn) ++mask;
                        }
                    }

                    if (mask > 0) {
                        if (!isGradient) {
                            uint32_t effA = (mask == static_cast<uint32_t>(numSamples))
                                ? static_cast<uint32_t>(c0_a)
                                : ((static_cast<uint32_t>(c0_a) * mask + (numSamples / 2)) / numSamples);
                            *rowDst = blendPixel(*rowDst, static_cast<uint32_t>(c0_r),
                                                          static_cast<uint32_t>(c0_g),
                                                          static_cast<uint32_t>(c0_b),
                                                          effA);
                        } else {
                            float t = std::clamp(curT, 0.0f, 1.0f);
                            uint32_t ur = static_cast<uint32_t>(c0_r + (c1_r - c0_r) * t);
                            uint32_t ug = static_cast<uint32_t>(c0_g + (c1_g - c0_g) * t);
                            uint32_t ub = static_cast<uint32_t>(c0_b + (c1_b - c0_b) * t);
                            uint32_t ua = static_cast<uint32_t>(c0_a + (c1_a - c0_a) * t);
                            uint32_t effA = (mask == static_cast<uint32_t>(numSamples))
                                ? ua
                                : ((ua * mask + (numSamples / 2)) / numSamples);
                            *rowDst = blendPixel(*rowDst, ur, ug, ub, effA);
                        }
                    }
                }

                for (size_t i = 0; i < count; ++i) e[i] += stepX[i];
                curT += gStepTx;
            }
        }
    }

    void renderTexturedConvexPolygon(
        const Point2D* inputPts, size_t count,
        const uint8_t* rgba, int imgW, int imgH,
        Point2D p0, Point2D p1, Point2D p2, Point2D p3,
        float w0, float w1, float w2, float w3,
        float lightShade = 1.0f
    ) override {
        (void)p2;
        (void)w2;
        if (!inputPts || count < 3 || count > 32 || !rgba || imgW <= 0 || imgH <= 0 || framebuffer_.empty()) return;

        float fMinX = inputPts[0].x, fMaxX = inputPts[0].x;
        float fMinY = inputPts[0].y, fMaxY = inputPts[0].y;
        for (size_t i = 1; i < count; ++i) {
            fMinX = std::min(fMinX, inputPts[i].x);
            fMaxX = std::max(fMaxX, inputPts[i].x);
            fMinY = std::min(fMinY, inputPts[i].y);
            fMaxY = std::max(fMaxY, inputPts[i].y);
        }

        int clipMinX, clipMaxX, clipMinY, clipMaxY;
        getClipBounds(clipMinX, clipMaxX, clipMinY, clipMaxY);
        int minX = std::max(clipMinX, static_cast<int>(std::floor(fMinX)));
        int maxX = std::min(clipMaxX, static_cast<int>(std::ceil(fMaxX)));
        int minY = std::max(clipMinY, static_cast<int>(std::floor(fMinY)));
        int maxY = std::min(clipMaxY, static_cast<int>(std::ceil(fMaxY)));

        if (minX > maxX || minY > maxY) return;

        // Check signed area to determine winding
        float area = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            size_t next = (i + 1) % count;
            area += (inputPts[i].x * inputPts[next].y - inputPts[next].x * inputPts[i].y);
        }
        if (std::abs(area) < 1e-4f) return;

        std::array<Point2D, 32> pts;
        if (area > 0.0f) {
            for (size_t i = 0; i < count; ++i) pts[i] = inputPts[i];
        } else {
            for (size_t i = 0; i < count; ++i) pts[i] = inputPts[count - 1 - i];
        }

        // Edge deltas
        std::array<float, 32> dx, dy, stepX;
        for (size_t i = 0; i < count; ++i) {
            size_t next = (i + 1) % count;
            dx[i] = pts[next].x - pts[i].x;
            dy[i] = pts[next].y - pts[i].y;
            stepX[i] = -dy[i];
        }

        // Setup perspective-correct plane interpolation
        float u_0 = 0.0f, v_0 = 0.0f;
        float u_1 = w1,   v_1 = 0.0f;
        float u_3 = 0.0f, v_3 = w3;

        float dX1 = p1.x - p0.x, dY1 = p1.y - p0.y;
        float dX2 = p3.x - p0.x, dY2 = p3.y - p0.y;
        float det = dX1 * dY2 - dX2 * dY1;

        float Aw = 0.0f, Bw = 0.0f;
        float Au = 0.0f, Bu = 0.0f;
        float Av = 0.0f, Bv = 0.0f;

        if (std::abs(det) > 1e-6f) {
            float invDet = 1.0f / det;
            Aw = ((w1 - w0) * dY2 - (w3 - w0) * dY1) * invDet;
            Bw = ((w3 - w0) * dX1 - (w1 - w0) * dX2) * invDet;

            Au = ((u_1 - u_0) * dY2 - (u_3 - u_0) * dY1) * invDet;
            Bu = ((u_3 - u_0) * dX1 - (u_1 - u_0) * dX2) * invDet;

            Av = ((v_1 - v_0) * dY2 - (v_3 - v_0) * dY1) * invDet;
            Bv = ((v_3 - v_0) * dX1 - (v_1 - v_0) * dX2) * invDet;
        }

        const float startPx = static_cast<float>(minX) + 0.5f;
        const float shade = std::clamp(lightShade, 0.2f, 1.5f);

        // Subpixel Sampling: 2x Fast or 4x RGSS
        const int numSamples = (antiAliasingMode_ == 1) ? 2 : 4;
        const float ox[4] = { 0.375f, -0.125f,  0.125f, -0.375f };
        const float oy[4] = {-0.125f, -0.375f,  0.375f,  0.125f };

        std::array<std::array<float, 4>, 32> de;
        std::array<float, 32> minDe, maxDe;
        for (size_t i = 0; i < count; ++i) {
            for (int s = 0; s < numSamples; ++s) {
                de[i][s] = dx[i] * oy[s] - dy[i] * ox[s];
            }
            minDe[i] = de[i][0];
            maxDe[i] = de[i][0];
            for (int s = 1; s < numSamples; ++s) {
                minDe[i] = std::min(minDe[i], de[i][s]);
                maxDe[i] = std::max(maxDe[i], de[i][s]);
            }
        }

        for (int y = minY; y <= maxY; ++y) {
            const float py = static_cast<float>(y) + 0.5f;
            std::array<float, 32> e;
            for (size_t i = 0; i < count; ++i) {
                e[i] = dx[i] * (py - pts[i].y) - dy[i] * (startPx - pts[i].x);
            }

            float curW  = w0  + Aw * (startPx - p0.x) + Bw * (py - p0.y);
            float curUp = u_0 + Au * (startPx - p0.x) + Bu * (py - p0.y);
            float curVp = v_0 + Av * (startPx - p0.x) + Bv * (py - p0.y);

            uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + minX];
            bool hadInsideOnRow = false;

            for (int x = minX; x <= maxX; ++x, ++rowDst) {
                bool maybeInside = true;
                for (size_t i = 0; i < count; ++i) {
                    if (e[i] + maxDe[i] < 0.0f) { maybeInside = false; break; }
                }
                if (!maybeInside) {
                    if (hadInsideOnRow) {
                        // Convex polygon: once we exit the right edge, no further pixels can be inside
                        break;
                    }
                    for (size_t i = 0; i < count; ++i) e[i] += stepX[i];
                    curW  += Aw;
                    curUp += Au;
                    curVp += Av;
                    continue;
                }
                hadInsideOnRow = true;

                bool fullyInside = true;
                for (size_t i = 0; i < count; ++i) {
                    if (e[i] + minDe[i] < 0.0f) { fullyInside = false; break; }
                }

                uint32_t mask = numSamples;
                if (!fullyInside) {
                    mask = 0;
                    for (int s = 0; s < numSamples; ++s) {
                        bool sampleIn = true;
                        for (size_t i = 0; i < count; ++i) {
                            if (e[i] + de[i][s] < 0.0f) { sampleIn = false; break; }
                        }
                        if (sampleIn) ++mask;
                    }
                }

                if (mask > 0) {
                    float invW = (curW > 1e-4f) ? (1.0f / curW) : 1.0f;
                    float u = std::clamp(curUp * invW, 0.0f, 1.0f);
                    float v = std::clamp(curVp * invW, 0.0f, 1.0f);

                    float fx = u * static_cast<float>(imgW - 1);
                    float fy = v * static_cast<float>(imgH - 1);
                    int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, imgW - 1);
                    int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, imgH - 1);
                    int x1 = std::min(x0 + 1, imgW - 1);
                    int y1 = std::min(y0 + 1, imgH - 1);
                    float wx = fx - std::floor(fx);
                    float wy = fy - std::floor(fy);

                    const uint8_t* p00 = &rgba[(y0 * imgW + x0) * 4];
                    const uint8_t* p10 = &rgba[(y0 * imgW + x1) * 4];
                    const uint8_t* p01 = &rgba[(y1 * imgW + x0) * 4];
                    const uint8_t* p11 = &rgba[(y1 * imgW + x1) * 4];

                    float r0 = p00[0] * (1.0f - wx) + p10[0] * wx;
                    float r1 = p01[0] * (1.0f - wx) + p10[0] * wx;
                    float g0 = p00[1] * (1.0f - wx) + p10[1] * wx;
                    float g1 = p01[1] * (1.0f - wx) + p11[1] * wx;
                    float b0 = p00[2] * (1.0f - wx) + p10[2] * wx;
                    float b1 = p01[2] * (1.0f - wx) + p11[2] * wx;
                    float a0 = p00[3] * (1.0f - wx) + p10[3] * wx;
                    float a1 = p01[3] * (1.0f - wx) + p11[3] * wx;

                    float r = (r0 * (1.0f - wy) + r1 * wy) * shade;
                    float g = (g0 * (1.0f - wy) + g1 * wy) * shade;
                    float b = (b0 * (1.0f - wy) + b1 * wy) * shade;
                    float rawA = a0 * (1.0f - wy) + a1 * wy;

                    uint32_t ur = static_cast<uint32_t>(std::clamp(r, 0.0f, 255.0f));
                    uint32_t ug = static_cast<uint32_t>(std::clamp(g, 0.0f, 255.0f));
                    uint32_t ub = static_cast<uint32_t>(std::clamp(b, 0.0f, 255.0f));
                    uint32_t ua = static_cast<uint32_t>(std::clamp(rawA, 0.0f, 255.0f));

                    uint32_t effA = (mask == static_cast<uint32_t>(numSamples))
                        ? ua
                        : ((ua * mask + (numSamples / 2)) / numSamples);

                    *rowDst = blendPixel(*rowDst, ur, ug, ub, effA);
                }

                for (size_t i = 0; i < count; ++i) e[i] += stepX[i];
                curW  += Aw;
                curUp += Au;
                curVp += Av;
            }
        }
    }

    void renderTexturedConvexPolygonARGB(
        const Point2D* inputPts, size_t count,
        const uint32_t* argb32, int imgW, int imgH,
        Point2D p0, Point2D p1, Point2D p2, Point2D p3,
        float w0, float w1, float w2, float w3,
        float lightShade = 1.0f
    ) override {
        (void)p2;
        (void)w2;
        if (!inputPts || count < 3 || count > 32 || !argb32 || imgW <= 0 || imgH <= 0 || framebuffer_.empty()) return;

        float fMinX = inputPts[0].x, fMaxX = inputPts[0].x;
        float fMinY = inputPts[0].y, fMaxY = inputPts[0].y;
        for (size_t i = 1; i < count; ++i) {
            fMinX = std::min(fMinX, inputPts[i].x);
            fMaxX = std::max(fMaxX, inputPts[i].x);
            fMinY = std::min(fMinY, inputPts[i].y);
            fMaxY = std::max(fMaxY, inputPts[i].y);
        }

        int clipMinX, clipMaxX, clipMinY, clipMaxY;
        getClipBounds(clipMinX, clipMaxX, clipMinY, clipMaxY);
        int minX = std::max(clipMinX, static_cast<int>(std::floor(fMinX)));
        int maxX = std::min(clipMaxX, static_cast<int>(std::ceil(fMaxX)));
        int minY = std::max(clipMinY, static_cast<int>(std::floor(fMinY)));
        int maxY = std::min(clipMaxY, static_cast<int>(std::ceil(fMaxY)));

        if (minX > maxX || minY > maxY) return;

        // Check signed area to determine winding
        float area = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            size_t next = (i + 1) % count;
            area += (inputPts[i].x * inputPts[next].y - inputPts[next].x * inputPts[i].y);
        }
        if (std::abs(area) < 1e-4f) return;

        std::array<Point2D, 32> pts;
        if (area > 0.0f) {
            for (size_t i = 0; i < count; ++i) pts[i] = inputPts[i];
        } else {
            for (size_t i = 0; i < count; ++i) pts[i] = inputPts[count - 1 - i];
        }

        // Edge deltas
        std::array<float, 32> dx, dy, stepX;
        for (size_t i = 0; i < count; ++i) {
            size_t next = (i + 1) % count;
            dx[i] = pts[next].x - pts[i].x;
            dy[i] = pts[next].y - pts[i].y;
            stepX[i] = -dy[i];
        }

        // Setup perspective-correct plane interpolation
        float u_0 = 0.0f, v_0 = 0.0f;
        float u_1 = w1,   v_1 = 0.0f;
        float u_3 = 0.0f, v_3 = w3;

        float dX1 = p1.x - p0.x, dY1 = p1.y - p0.y;
        float dX2 = p3.x - p0.x, dY2 = p3.y - p0.y;
        float det = dX1 * dY2 - dX2 * dY1;

        float Aw = 0.0f, Bw = 0.0f;
        float Au = 0.0f, Bu = 0.0f;
        float Av = 0.0f, Bv = 0.0f;

        if (std::abs(det) > 1e-6f) {
            float invDet = 1.0f / det;
            Aw = ((w1 - w0) * dY2 - (w3 - w0) * dY1) * invDet;
            Bw = ((w3 - w0) * dX1 - (w1 - w0) * dX2) * invDet;

            Au = ((u_1 - u_0) * dY2 - (u_3 - u_0) * dY1) * invDet;
            Bu = ((u_3 - u_0) * dX1 - (u_1 - u_0) * dX2) * invDet;

            Av = ((v_1 - v_0) * dY2 - (v_3 - v_0) * dY1) * invDet;
            Bv = ((v_3 - v_0) * dX1 - (v_1 - v_0) * dX2) * invDet;
        }

        const float startPx = static_cast<float>(minX) + 0.5f;
        const float shade = std::clamp(lightShade, 0.2f, 1.5f);
        const uint32_t ishade = static_cast<uint32_t>(shade * 256.0f);
        const bool applyShade = (std::abs(shade - 1.0f) > 0.01f);

        // Subpixel Sampling: 2x Fast or 4x RGSS
        const int numSamples = (antiAliasingMode_ == 1) ? 2 : 4;
        const float ox[4] = { 0.375f, -0.125f,  0.125f, -0.375f };
        const float oy[4] = {-0.125f, -0.375f,  0.375f,  0.125f };

        std::array<std::array<float, 4>, 32> de;
        std::array<float, 32> minDe, maxDe;
        for (size_t i = 0; i < count; ++i) {
            for (int s = 0; s < numSamples; ++s) {
                de[i][s] = dx[i] * oy[s] - dy[i] * ox[s];
            }
            minDe[i] = de[i][0];
            maxDe[i] = de[i][0];
            for (int s = 1; s < numSamples; ++s) {
                minDe[i] = std::min(minDe[i], de[i][s]);
                maxDe[i] = std::max(maxDe[i], de[i][s]);
            }
        }

        const float fImgW = static_cast<float>(imgW - 1);
        const float fImgH = static_cast<float>(imgH - 1);

        for (int y = minY; y <= maxY; ++y) {
            const float py = static_cast<float>(y) + 0.5f;
            std::array<float, 32> e;
            for (size_t i = 0; i < count; ++i) {
                e[i] = dx[i] * (py - pts[i].y) - dy[i] * (startPx - pts[i].x);
            }

            float curW  = w0  + Aw * (startPx - p0.x) + Bw * (py - p0.y);
            float curUp = u_0 + Au * (startPx - p0.x) + Bu * (py - p0.y);
            float curVp = v_0 + Av * (startPx - p0.x) + Bv * (py - p0.y);

            uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + minX];
            bool hadInsideOnRow = false;

            for (int x = minX; x <= maxX; ++x, ++rowDst) {
                bool maybeInside = true;
                for (size_t i = 0; i < count; ++i) {
                    if (e[i] + maxDe[i] < 0.0f) { maybeInside = false; break; }
                }
                if (!maybeInside) {
                    if (hadInsideOnRow) {
                        break;
                    }
                    for (size_t i = 0; i < count; ++i) e[i] += stepX[i];
                    curW  += Aw;
                    curUp += Au;
                    curVp += Av;
                    continue;
                }
                hadInsideOnRow = true;

                bool fullyInside = true;
                for (size_t i = 0; i < count; ++i) {
                    if (e[i] + minDe[i] < 0.0f) { fullyInside = false; break; }
                }

                uint32_t mask = numSamples;
                if (!fullyInside) {
                    mask = 0;
                    for (int s = 0; s < numSamples; ++s) {
                        bool sampleIn = true;
                        for (size_t i = 0; i < count; ++i) {
                            if (e[i] + de[i][s] < 0.0f) { sampleIn = false; break; }
                        }
                        if (sampleIn) ++mask;
                    }
                }

                if (mask > 0) {
                    float invW = (curW > 1e-4f) ? (1.0f / curW) : 1.0f;
                    float u = std::clamp(curUp * invW, 0.0f, 1.0f);
                    float v = std::clamp(curVp * invW, 0.0f, 1.0f);

                    float fx = u * fImgW;
                    float fy = v * fImgH;
                    int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, imgW - 1);
                    int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, imgH - 1);
                    int x1 = std::min(x0 + 1, imgW - 1);
                    int y1 = std::min(y0 + 1, imgH - 1);
                    float wx = fx - std::floor(fx);
                    float wy = fy - std::floor(fy);

                    const uint32_t* srcRow0 = &argb32[y0 * imgW];
                    const uint32_t* srcRow1 = &argb32[y1 * imgW];
                    uint32_t c00 = srcRow0[x0], c10 = srcRow0[x1];
                    uint32_t c01 = srcRow1[x0], c11 = srcRow1[x1];

                    uint32_t iwx = static_cast<uint32_t>(wx * 256.0f);
                    uint32_t iwy = static_cast<uint32_t>(wy * 256.0f);
                    uint32_t iwx_inv = 256u - iwx;
                    uint32_t iwy_inv = 256u - iwy;

                    uint32_t w00 = iwx_inv * iwy_inv;
                    uint32_t w10 = iwx     * iwy_inv;
                    uint32_t w01 = iwx_inv * iwy;
                    uint32_t w11 = iwx     * iwy;

                    uint32_t r = (((c00 >> 16) & 0xFF) * w00 + ((c10 >> 16) & 0xFF) * w10 +
                                  ((c01 >> 16) & 0xFF) * w01 + ((c11 >> 16) & 0xFF) * w11) >> 16;
                    uint32_t g = (((c00 >> 8)  & 0xFF) * w00 + ((c10 >> 8)  & 0xFF) * w10 +
                                  ((c01 >> 8)  & 0xFF) * w01 + ((c11 >> 8)  & 0xFF) * w11) >> 16;
                    uint32_t b = (((c00 & 0xFF)         * w00 + (c10 & 0xFF)         * w10 +
                                   (c01 & 0xFF)         * w01 + (c11 & 0xFF)         * w11) >> 16);
                    uint32_t rawA = (((c00 >> 24) & 0xFF) * w00 + ((c10 >> 24) & 0xFF) * w10 +
                                     ((c01 >> 24) & 0xFF) * w01 + ((c11 >> 24) & 0xFF) * w11) >> 16;

                    if (applyShade) {
                        r = std::min(255u, (r * ishade) >> 8);
                        g = std::min(255u, (g * ishade) >> 8);
                        b = std::min(255u, (b * ishade) >> 8);
                    }

                    uint32_t effA = (mask == static_cast<uint32_t>(numSamples))
                        ? rawA
                        : ((rawA * mask + (numSamples / 2)) / numSamples);

                    *rowDst = blendPixel(*rowDst, r, g, b, effA);
                }

                for (size_t i = 0; i < count; ++i) e[i] += stepX[i];
                curW  += Aw;
                curUp += Au;
                curVp += Av;
            }
        }
    }

    void renderTexturedRoundedRect(
        float x, float y, float w, float h, float radius,
        const uint32_t* argb32, int imgW, int imgH,
        float opacity = 1.0f
    ) override {
        if (!argb32 || imgW <= 0 || imgH <= 0 || w <= 0.0f || h <= 0.0f || framebuffer_.empty()) return;

        float fMinX = x, fMaxX = x + w;
        float fMinY = y, fMaxY = y + h;

        int clipMinX, clipMaxX, clipMinY, clipMaxY;
        getClipBounds(clipMinX, clipMaxX, clipMinY, clipMaxY);
        int minX = std::max(clipMinX, static_cast<int>(std::floor(fMinX)));
        int maxX = std::min(clipMaxX, static_cast<int>(std::ceil(fMaxX)));
        int minY = std::max(clipMinY, static_cast<int>(std::floor(fMinY)));
        int maxY = std::min(clipMaxY, static_cast<int>(std::ceil(fMaxY)));

        if (minX > maxX || minY > maxY) return;

        float halfW = w * 0.5f;
        float halfH = h * 0.5f;
        float cx = x + halfW;
        float cy = y + halfH;
        float rad = std::clamp(radius, 0.0f, std::min(halfW, halfH));
        float innerW = halfW - rad;
        float innerH = halfH - rad;
        float radSq = rad * rad;

        float invW = 1.0f / w;
        float invH = 1.0f / h;

        const int numSamples = (antiAliasingMode_ == 0) ? 1 : ((antiAliasingMode_ == 1) ? 2 : 4);
        const float ox[4] = { 0.375f, -0.125f,  0.125f, -0.375f };
        const float oy[4] = {-0.125f, -0.375f,  0.375f,  0.125f };

        float texScaleX = static_cast<float>(imgW) * invW;
        float texScaleY = static_cast<float>(imgH) * invH;
        uint32_t opInt = static_cast<uint32_t>(std::clamp(opacity, 0.0f, 1.0f) * 255.0f);

        for (int curY = minY; curY <= maxY; ++curY) {
            float py = static_cast<float>(curY) + 0.5f;
            float qy = std::abs(py - cy) - innerH;
            if (qy > rad + 0.6f) continue;

            float rowExt = (qy <= 0.0f) ? halfW : (innerW + std::sqrt(std::max(0.0f, radSq - qy * qy)));
            float rowMinX = cx - rowExt;
            float rowMaxX = cx + rowExt;

            int rStart = std::max(minX, static_cast<int>(std::floor(rowMinX - 0.5f)));
            int rEnd   = std::min(maxX, static_cast<int>(std::ceil(rowMaxX + 0.5f)));
            if (rStart > rEnd) continue;

            uint32_t* rowDst = &framebuffer_[curY * viewportWidth_ + rStart];

            if (antiAliasingMode_ == 0) {
                // Point-sampled nearest-neighbor fast path
                int texY = std::clamp(static_cast<int>((py - y) * texScaleY), 0, imgH - 1);
                const uint32_t* srcRow = &argb32[texY * imgW];

                for (int curX = rStart; curX <= rEnd; ++curX, ++rowDst) {
                    float px = static_cast<float>(curX) + 0.5f;
                    int texX = std::clamp(static_cast<int>((px - x) * texScaleX), 0, imgW - 1);
                    uint32_t srcCol = srcRow[texX];
                    uint32_t sa = (srcCol >> 24) & 0xFF;
                    if (sa == 0) continue;

                    if (opInt < 255) {
                        sa = (sa * opInt) >> 8;
                    }

                    if (px >= rowMinX + 0.75f && px <= rowMaxX - 0.75f) {
                        if (sa == 255 && blendMode_ == BlendMode::Normal) {
                            *rowDst = srcCol;
                        } else {
                            *rowDst = blendPixel(*rowDst, (srcCol >> 16) & 0xFF, (srcCol >> 8) & 0xFF, srcCol & 0xFF, sa);
                        }
                        continue;
                    }

                    if (px >= rowMinX && px <= rowMaxX) {
                        *rowDst = blendPixel(*rowDst, (srcCol >> 16) & 0xFF, (srcCol >> 8) & 0xFF, srcCol & 0xFF, sa);
                    }
                }
            } else {
                // High-Quality Bilinear filtered path (AA modes 1 and 2)
                float fy = (py - y) * texScaleY - 0.5f;
                int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, imgH - 1);
                int y1 = std::min(y0 + 1, imgH - 1);
                uint32_t iwy = static_cast<uint32_t>(std::clamp(fy - std::floor(fy), 0.0f, 1.0f) * 256.0f);
                uint32_t invWy = 256 - iwy;

                const uint32_t* srcRow0 = &argb32[y0 * imgW];
                const uint32_t* srcRow1 = &argb32[y1 * imgW];

                for (int curX = rStart; curX <= rEnd; ++curX, ++rowDst) {
                    float px = static_cast<float>(curX) + 0.5f;
                    float fx = (px - x) * texScaleX - 0.5f;
                    int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, imgW - 1);
                    int x1 = std::min(x0 + 1, imgW - 1);
                    uint32_t iwx = static_cast<uint32_t>(std::clamp(fx - std::floor(fx), 0.0f, 1.0f) * 256.0f);
                    uint32_t invWx = 256 - iwx;

                    uint32_t p00 = srcRow0[x0], p10 = srcRow0[x1];
                    uint32_t p01 = srcRow1[x0], p11 = srcRow1[x1];

                    uint32_t tr = ((p00 >> 16) & 0xFF) * invWx + ((p10 >> 16) & 0xFF) * iwx;
                    uint32_t tg = ((p00 >> 8)  & 0xFF) * invWx + ((p10 >> 8)  & 0xFF) * iwx;
                    uint32_t tb = (p00         & 0xFF) * invWx + (p10         & 0xFF) * iwx;
                    uint32_t ta = ((p00 >> 24) & 0xFF) * invWx + ((p10 >> 24) & 0xFF) * iwx;

                    uint32_t br = ((p01 >> 16) & 0xFF) * invWx + ((p11 >> 16) & 0xFF) * iwx;
                    uint32_t bg = ((p01 >> 8)  & 0xFF) * invWx + ((p11 >> 8)  & 0xFF) * iwx;
                    uint32_t bb = (p01         & 0xFF) * invWx + (p11         & 0xFF) * iwx;
                    uint32_t ba = ((p01 >> 24) & 0xFF) * invWx + ((p11 >> 24) & 0xFF) * iwx;

                    uint32_t sr = (tr * invWy + br * iwy) >> 16;
                    uint32_t sg = (tg * invWy + bg * iwy) >> 16;
                    uint32_t sb = (tb * invWy + bb * iwy) >> 16;
                    uint32_t sa = (ta * invWy + ba * iwy) >> 16;
                    if (sa == 0) continue;

                    if (opInt < 255) {
                        sa = (sa * opInt) >> 8;
                    }

                    uint32_t srcCol = (sa << 24) | (sr << 16) | (sg << 8) | sb;

                    // Interior span
                    if (px >= rowMinX + 0.75f && px <= rowMaxX - 0.75f) {
                        if (sa == 255 && blendMode_ == BlendMode::Normal) {
                            *rowDst = srcCol;
                        } else {
                            *rowDst = blendPixel(*rowDst, sr, sg, sb, sa);
                        }
                        continue;
                    }

                    // Silhouette boundary pixel: evaluate antialiased corner samples
                    uint32_t mask = 0;
                    for (int s = 0; s < numSamples; ++s) {
                        float spx = px + ox[s];
                        float spy = py + oy[s];
                        float sqx = std::abs(spx - cx) - innerW;
                        float sqy = std::abs(spy - cy) - innerH;
                        if (sqx <= 0.0f || sqy <= 0.0f) {
                            if (sqx <= rad && sqy <= rad) ++mask;
                        } else {
                            if (sqx * sqx + sqy * sqy <= radSq) ++mask;
                        }
                    }

                    if (mask > 0) {
                        uint32_t effA = (mask == static_cast<uint32_t>(numSamples))
                            ? sa
                            : ((sa * mask + (numSamples / 2)) / numSamples);
                        *rowDst = blendPixel(*rowDst, sr, sg, sb, effA);
                    }
                }
            }
        }
    }

private:
    void rasterizeRoundedRect(const Vertex2D& q0, const Vertex2D& q1, const Vertex2D& q2, const Vertex2D& q3, float radius) {
        float fMinX = std::min({q0.x, q1.x, q2.x, q3.x});
        float fMaxX = std::max({q0.x, q1.x, q2.x, q3.x});
        float fMinY = std::min({q0.y, q1.y, q2.y, q3.y});
        float fMaxY = std::max({q0.y, q1.y, q2.y, q3.y});

        int clipMinX, clipMaxX, clipMinY, clipMaxY;
        getClipBounds(clipMinX, clipMaxX, clipMinY, clipMaxY);
        int minX = std::max(clipMinX, static_cast<int>(std::floor(fMinX)));
        int maxX = std::min(clipMaxX, static_cast<int>(std::ceil(fMaxX)));
        int minY = std::max(clipMinY, static_cast<int>(std::floor(fMinY)));
        int maxY = std::min(clipMaxY, static_cast<int>(std::ceil(fMaxY)));

        if (minX > maxX || minY > maxY) return;

        float halfW = (fMaxX - fMinX) * 0.5f;
        float halfH = (fMaxY - fMinY) * 0.5f;
        float cx = fMinX + halfW;
        float cy = fMinY + halfH;
        float outset = q0.v;
        if (outset > 0.5f) {
            float baseHalfW = std::max(0.0f, halfW - outset);
            float baseHalfH = std::max(0.0f, halfH - outset);
            float baseRad = std::clamp(radius, 0.0f, std::min(baseHalfW, baseHalfH));
            float innerSoft = outset * 0.35f;
            float invSpan = 1.0f / (outset + innerSoft);
            uint32_t ur = static_cast<uint32_t>(q0.color & 0xFF);
            uint32_t ug = static_cast<uint32_t>((q0.color >> 8) & 0xFF);
            uint32_t ub = static_cast<uint32_t>((q0.color >> 16) & 0xFF);
            float baseA = static_cast<float>((q0.color >> 24) & 0xFF) * (1.0f / 255.0f);
            const bool hasPrev = (std::abs(q0.localUV[0]) < 9000.0f);
            const float prevOffX = q0.localUV[0];
            const float prevOffY = q0.localUV[1];

            auto evalSdf = [&](float lx, float ly) noexcept -> float {
                float dx = std::abs(lx);
                float dy = std::abs(ly);
                if (radius <= 0.05f) {
                    float qx = dx - baseHalfW;
                    float qy = dy - baseHalfH;
                    if (qx > 0.0f || qy > 0.0f) {
                        float mx = std::max(qx, 0.0f);
                        float my = std::max(qy, 0.0f);
                        float euc = std::sqrt(mx * mx + my * my);
                        return std::max(qx, qy) * 0.70f + euc * 0.30f;
                    }
                    return std::max(qx, qy);
                }
                float qx = dx - baseHalfW + baseRad;
                float qy = dy - baseHalfH + baseRad;
                float mx = std::max(qx, 0.0f);
                float my = std::max(qy, 0.0f);
                return std::sqrt(mx * mx + my * my) + std::min(std::max(qx, qy), 0.0f) - baseRad;
            };

            for (int y = minY; y <= maxY; ++y) {
                float ly = (static_cast<float>(y) + 0.5f) - cy;
                uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + minX];
                for (int x = minX; x <= maxX; ++x, ++rowDst) {
                    float lx = (static_cast<float>(x) + 0.5f) - cx;
                    float sdf = evalSdf(lx, ly);
                    float t = std::clamp((outset - sdf) * invSpan, 0.0f, 1.0f);
                    float alpha = t * t * (3.0f - 2.0f * t);
                    float aCurr = baseA * alpha;
                    if (hasPrev && aCurr > 0.001f) {
                        float sdfP = evalSdf(lx - prevOffX, ly - prevOffY);
                        float tP = std::clamp((outset - sdfP) * invSpan, 0.0f, 1.0f);
                        float alphaP = tP * tP * (3.0f - 2.0f * tP);
                        float aPrev = baseA * alphaP;
                        aCurr = std::max(0.0f, aCurr - aPrev) / std::max(0.001f, 1.0f - aPrev);
                    }
                    uint32_t ua = static_cast<uint32_t>(std::clamp(aCurr * 255.0f + 0.5f, 0.0f, 255.0f));
                    if (ua > 0) {
                        *rowDst = blendPixel(*rowDst, ur, ug, ub, ua);
                    }
                }
            }
            return;
        }
        float rad = std::clamp(radius, 0.0f, std::min(halfW, halfH));
        float innerW = halfW - rad;
        float innerH = halfH - rad;
        float radSq = rad * rad;

        bool isSolid = (q0.color == q2.color);
        float c0_r = static_cast<float>(q0.color & 0xFF);
        float c0_g = static_cast<float>((q0.color >> 8) & 0xFF);
        float c0_b = static_cast<float>((q0.color >> 16) & 0xFF);
        float c0_a = static_cast<float>((q0.color >> 24) & 0xFF);

        float c1_r = static_cast<float>(q2.color & 0xFF);
        float c1_g = static_cast<float>((q2.color >> 8) & 0xFF);
        float c1_b = static_cast<float>((q2.color >> 16) & 0xFF);
        float c1_a = static_cast<float>((q2.color >> 24) & 0xFF);

        float invH = (fMaxY > fMinY) ? (1.0f / (fMaxY - fMinY)) : 1.0f;

        const int numSamples = (antiAliasingMode_ == 0) ? 1 : ((antiAliasingMode_ == 1) ? 2 : 4);
        const float ox[4] = { 0.375f, -0.125f,  0.125f, -0.375f };
        const float oy[4] = {-0.125f, -0.375f,  0.375f,  0.125f };

        for (int y = minY; y <= maxY; ++y) {
            float py = static_cast<float>(y) + 0.5f;
            float qy = std::abs(py - cy) - innerH;
            if (qy > rad + 0.6f) continue;

            float r, g, b, a;
            if (isSolid) {
                r = c0_r; g = c0_g; b = c0_b; a = c0_a;
            } else {
                float t = std::clamp((py - fMinY) * invH, 0.0f, 1.0f);
                r = c0_r + (c1_r - c0_r) * t;
                g = c0_g + (c1_g - c0_g) * t;
                b = c0_b + (c1_b - c0_b) * t;
                a = c0_a + (c1_a - c0_a) * t;
            }

            uint32_t ur = static_cast<uint32_t>(std::clamp(r, 0.0f, 255.0f));
            uint32_t ug = static_cast<uint32_t>(std::clamp(g, 0.0f, 255.0f));
            uint32_t ub = static_cast<uint32_t>(std::clamp(b, 0.0f, 255.0f));
            uint32_t ua = static_cast<uint32_t>(std::clamp(a, 0.0f, 255.0f));
            if (ua == 0) continue;

            float rowExt = (qy <= 0.0f) ? halfW : (innerW + std::sqrt(std::max(0.0f, radSq - qy * qy)));
            float rowMinX = cx - rowExt;
            float rowMaxX = cx + rowExt;

            int rStart = std::max(minX, static_cast<int>(std::floor(rowMinX - 0.5f)));
            int rEnd   = std::min(maxX, static_cast<int>(std::ceil(rowMaxX + 0.5f)));
            if (rStart > rEnd) continue;

            uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + rStart];

            for (int x = rStart; x <= rEnd; ++x, ++rowDst) {
                float px = static_cast<float>(x) + 0.5f;

                // Quick interior check: strictly inside horizontal span by > 0.75px
                if (px >= rowMinX + 0.75f && px <= rowMaxX - 0.75f) {
                    *rowDst = blendPixel(*rowDst, ur, ug, ub, ua);
                    continue;
                }

                // Silhouette boundary pixel: evaluate subpixel samples
                uint32_t mask = 0;
                if (antiAliasingMode_ == 0) {
                    float spx = px;
                    float spy = py;
                    float sqx = std::abs(spx - cx) - innerW;
                    float sqy = std::abs(spy - cy) - innerH;
                    if (sqx <= 0.0f && sqy <= 0.0f) {
                        mask = 1;
                    } else if (sqx <= 0.0f) {
                        if (sqy <= rad) mask = 1;
                    } else if (sqy <= 0.0f) {
                        if (sqx <= rad) mask = 1;
                    } else if (sqx * sqx + sqy * sqy <= radSq) {
                        mask = 1;
                    }
                } else {
                    for (int s = 0; s < numSamples; ++s) {
                        float spx = px + ox[s];
                        float spy = py + oy[s];
                        float sqx = std::abs(spx - cx) - innerW;
                        float sqy = std::abs(spy - cy) - innerH;
                        if (sqx <= 0.0f && sqy <= 0.0f) {
                            ++mask;
                        } else if (sqx <= 0.0f) {
                            if (sqy <= rad) ++mask;
                        } else if (sqy <= 0.0f) {
                            if (sqx <= rad) ++mask;
                        } else if (sqx * sqx + sqy * sqy <= radSq) {
                            ++mask;
                        }
                    }
                }

                if (mask > 0) {
                    uint32_t effA = (mask == static_cast<uint32_t>(numSamples)) ? ua : ((ua * mask + (numSamples / 2)) / numSamples);
                    *rowDst = blendPixel(*rowDst, ur, ug, ub, effA);
                }
            }
        }
    }

    void rasterizeQuad(const Vertex2D& q0, const Vertex2D& q1, const Vertex2D& q2, const Vertex2D& q3) {
        float fMinX = std::min({q0.x, q1.x, q2.x, q3.x});
        float fMaxX = std::max({q0.x, q1.x, q2.x, q3.x});
        float fMinY = std::min({q0.y, q1.y, q2.y, q3.y});
        float fMaxY = std::max({q0.y, q1.y, q2.y, q3.y});

        int clipMinX, clipMaxX, clipMinY, clipMaxY;
        getClipBounds(clipMinX, clipMaxX, clipMinY, clipMaxY);
        int minX = std::max(clipMinX, static_cast<int>(std::floor(fMinX)));
        int maxX = std::min(clipMaxX, static_cast<int>(std::ceil(fMaxX)));
        int minY = std::max(clipMinY, static_cast<int>(std::floor(fMinY)));
        int maxY = std::min(clipMaxY, static_cast<int>(std::ceil(fMaxY)));

        if (minX > maxX || minY > maxY) return;

        // Check if this is an axis-aligned solid or vertically gradient rectangle
        bool isAxisAligned = (std::abs(q0.y - q1.y) < 0.01f && std::abs(q1.x - q2.x) < 0.01f &&
                              std::abs(q2.y - q3.y) < 0.01f && std::abs(q3.x - q0.x) < 0.01f);

        if (isAxisAligned) {
            if (antiAliasingMode_ == 0 || q0.mode == 5) {
                // Integer-snapped fast path for No-AA mode or un-antialiased meshes
                int rMinX = std::max(clipMinX, static_cast<int>(std::round(fMinX)));
                int rMaxX = std::min(clipMaxX, static_cast<int>(std::round(fMaxX)) - 1);
                int rMinY = std::max(clipMinY, static_cast<int>(std::round(fMinY)));
                int rMaxY = std::min(clipMaxY, static_cast<int>(std::round(fMaxY)) - 1);

                if (rMinX <= rMaxX && rMinY <= rMaxY) {
                    if (q0.mode == 0 && q0.color == q1.color && q0.color == q2.color && q0.color == q3.color) {
                        // Solid Axis-Aligned Rectangle
                        uint32_t a = (q0.color >> 24) & 0xFF;
                        uint32_t c_r = q0.color & 0xFF;
                        uint32_t c_g = (q0.color >> 8) & 0xFF;
                        uint32_t c_b = (q0.color >> 16) & 0xFF;

                        if (blendMode_ == BlendMode::Normal && a >= 255) {
                            uint32_t col = (0xFF << 24) | (c_r << 16) | (c_g << 8) | c_b;
                            int count = rMaxX - rMinX + 1;
                            for (int y = rMinY; y <= rMaxY; ++y) {
                                std::fill_n(&framebuffer_[y * viewportWidth_ + rMinX], count, col);
                            }
                            return;
                        } else if (a > 0) {
                            for (int y = rMinY; y <= rMaxY; ++y) {
                                uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + rMinX];
                                for (int x = rMinX; x <= rMaxX; ++x, ++rowDst) {
                                    *rowDst = blendPixel(*rowDst, c_r, c_g, c_b, a);
                                }
                            }
                            return;
                        }
                    } else if (q0.mode == 0 && q0.color == q1.color && q2.color == q3.color) {
                        // Vertical Gradient Axis-Aligned Rectangle
                        float c0_r = static_cast<float>(q0.color & 0xFF);
                        float c0_g = static_cast<float>((q0.color >> 8) & 0xFF);
                        float c0_b = static_cast<float>((q0.color >> 16) & 0xFF);
                        float c0_a = static_cast<float>((q0.color >> 24) & 0xFF);

                        float c1_r = static_cast<float>(q2.color & 0xFF);
                        float c1_g = static_cast<float>((q2.color >> 8) & 0xFF);
                        float c1_b = static_cast<float>((q2.color >> 16) & 0xFF);
                        float c1_a = static_cast<float>((q2.color >> 24) & 0xFF);

                        float invH = (fMaxY > fMinY) ? (1.0f / (fMaxY - fMinY)) : 1.0f;
                        int count = rMaxX - rMinX + 1;
                        for (int y = rMinY; y <= rMaxY; ++y) {
                            float t = std::clamp((static_cast<float>(y) + 0.5f - fMinY) * invH, 0.0f, 1.0f);
                            float r = c0_r + (c1_r - c0_r) * t;
                            float g = c0_g + (c1_g - c0_g) * t;
                            float b = c0_b + (c1_b - c0_b) * t;
                            float a = c0_a + (c1_a - c0_a) * t;

                            uint32_t ur = static_cast<uint32_t>(std::clamp(r, 0.0f, 255.0f));
                            uint32_t ug = static_cast<uint32_t>(std::clamp(g, 0.0f, 255.0f));
                            uint32_t ub = static_cast<uint32_t>(std::clamp(b, 0.0f, 255.0f));
                            uint32_t ua = static_cast<uint32_t>(std::clamp(a, 0.0f, 255.0f));

                            if (blendMode_ == BlendMode::Normal && ua >= 255) {
                                uint32_t col = (0xFF << 24) | (ur << 16) | (ug << 8) | ub;
                                std::fill_n(&framebuffer_[y * viewportWidth_ + rMinX], count, col);
                            } else if (ua > 0) {
                                uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + rMinX];
                                for (int x = rMinX; x <= rMaxX; ++x, ++rowDst) {
                                    *rowDst = blendPixel(*rowDst, ur, ug, ub, ua);
                                }
                            }
                        }
                        return;
                    } else if (q0.mode == 0 && q0.color == q3.color && q1.color == q2.color) {
                        // Horizontal Gradient Axis-Aligned Rectangle
                        float c0_r = static_cast<float>(q0.color & 0xFF);
                        float c0_g = static_cast<float>((q0.color >> 8) & 0xFF);
                        float c0_b = static_cast<float>((q0.color >> 16) & 0xFF);
                        float c0_a = static_cast<float>((q0.color >> 24) & 0xFF);

                        float c1_r = static_cast<float>(q1.color & 0xFF);
                        float c1_g = static_cast<float>((q1.color >> 8) & 0xFF);
                        float c1_b = static_cast<float>((q1.color >> 16) & 0xFF);
                        float c1_a = static_cast<float>((q1.color >> 24) & 0xFF);

                        float invW = (fMaxX > fMinX) ? (1.0f / (fMaxX - fMinX)) : 1.0f;
                        for (int y = rMinY; y <= rMaxY; ++y) {
                            uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + rMinX];
                            for (int x = rMinX; x <= rMaxX; ++x, ++rowDst) {
                                float t = std::clamp((static_cast<float>(x) + 0.5f - fMinX) * invW, 0.0f, 1.0f);
                                float r = c0_r + (c1_r - c0_r) * t;
                                float g = c0_g + (c1_g - c0_g) * t;
                                float b = c0_b + (c1_b - c0_b) * t;
                                float a = c0_a + (c1_a - c0_a) * t;

                                uint32_t ur = static_cast<uint32_t>(std::clamp(r, 0.0f, 255.0f));
                                uint32_t ug = static_cast<uint32_t>(std::clamp(g, 0.0f, 255.0f));
                                uint32_t ub = static_cast<uint32_t>(std::clamp(b, 0.0f, 255.0f));
                                uint32_t ua = static_cast<uint32_t>(std::clamp(a, 0.0f, 255.0f));

                                if (ua > 0) {
                                    *rowDst = blendPixel(*rowDst, ur, ug, ub, ua);
                                }
                            }
                        }
                        return;
                    }
                }
            } else {
                // Analytical Subpixel Continuous Box Coverage for Anti-Aliased Rendering (antiAliasingMode_ > 0)
                int pMinX = std::max(clipMinX, static_cast<int>(std::floor(fMinX)));
                int pMaxX = std::min(clipMaxX, static_cast<int>(std::ceil(fMaxX)) - 1);
                int pMinY = std::max(clipMinY, static_cast<int>(std::floor(fMinY)));
                int pMaxY = std::min(clipMaxY, static_cast<int>(std::ceil(fMaxY)) - 1);

                if (pMinX <= pMaxX && pMinY <= pMaxY) {
                    if (q0.mode == 0 && q0.color == q1.color && q0.color == q2.color && q0.color == q3.color) {
                        // Solid Axis-Aligned Rectangle with Analytical Subpixel Edges
                        uint32_t a = (q0.color >> 24) & 0xFF;
                        if (a == 0) return;
                        uint32_t c_r = q0.color & 0xFF;
                        uint32_t c_g = (q0.color >> 8) & 0xFF;
                        uint32_t c_b = (q0.color >> 16) & 0xFF;

                        float covX_left  = (pMinX == pMaxX) ? std::clamp(fMaxX - fMinX, 0.0f, 1.0f)
                                                            : std::clamp(static_cast<float>(pMinX + 1) - fMinX, 0.0f, 1.0f);
                        float covX_right = (pMinX == pMaxX) ? covX_left
                                                            : std::clamp(fMaxX - static_cast<float>(pMaxX), 0.0f, 1.0f);

                        for (int y = pMinY; y <= pMaxY; ++y) {
                            float cy;
                            if (pMinY == pMaxY) {
                                cy = std::clamp(fMaxY - fMinY, 0.0f, 1.0f);
                            } else if (y == pMinY) {
                                cy = std::clamp(static_cast<float>(pMinY + 1) - fMinY, 0.0f, 1.0f);
                            } else if (y == pMaxY) {
                                cy = std::clamp(fMaxY - static_cast<float>(pMaxY), 0.0f, 1.0f);
                            } else {
                                cy = 1.0f;
                            }

                            if (cy <= 0.001f) continue;

                            uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + pMinX];

                            if (pMinX == pMaxX) {
                                float cov = covX_left * cy;
                                uint32_t effA = static_cast<uint32_t>(std::round(static_cast<float>(a) * cov));
                                if (effA > 0) {
                                    if (blendMode_ == BlendMode::Normal && effA >= 255) {
                                        *rowDst = (0xFF << 24) | (c_r << 16) | (c_g << 8) | c_b;
                                    } else {
                                        *rowDst = blendPixel(*rowDst, c_r, c_g, c_b, effA);
                                    }
                                }
                            } else {
                                // Left boundary pixel (x = pMinX)
                                float covL = covX_left * cy;
                                uint32_t effAL = static_cast<uint32_t>(std::round(static_cast<float>(a) * covL));
                                if (effAL > 0) {
                                    if (blendMode_ == BlendMode::Normal && effAL >= 255) {
                                        rowDst[0] = (0xFF << 24) | (c_r << 16) | (c_g << 8) | c_b;
                                    } else {
                                        rowDst[0] = blendPixel(rowDst[0], c_r, c_g, c_b, effAL);
                                    }
                                }

                                // Interior pixels (pMinX + 1 .. pMaxX - 1)
                                int innerCount = pMaxX - pMinX - 1;
                                if (innerCount > 0) {
                                    float covMid = cy; // covX is strictly 1.0f inside
                                    uint32_t effAMid = static_cast<uint32_t>(std::round(static_cast<float>(a) * covMid));
                                    if (effAMid > 0) {
                                        if (blendMode_ == BlendMode::Normal && effAMid >= 255) {
                                            uint32_t col = (0xFF << 24) | (c_r << 16) | (c_g << 8) | c_b;
                                            std::fill_n(rowDst + 1, innerCount, col);
                                        } else {
                                            for (int i = 1; i <= innerCount; ++i) {
                                                rowDst[i] = blendPixel(rowDst[i], c_r, c_g, c_b, effAMid);
                                            }
                                        }
                                    }
                                }

                                // Right boundary pixel (x = pMaxX)
                                float covR = covX_right * cy;
                                uint32_t effAR = static_cast<uint32_t>(std::round(static_cast<float>(a) * covR));
                                if (effAR > 0) {
                                    int lastIdx = pMaxX - pMinX;
                                    if (blendMode_ == BlendMode::Normal && effAR >= 255) {
                                        rowDst[lastIdx] = (0xFF << 24) | (c_r << 16) | (c_g << 8) | c_b;
                                    } else {
                                        rowDst[lastIdx] = blendPixel(rowDst[lastIdx], c_r, c_g, c_b, effAR);
                                    }
                                }
                            }
                        }
                        return;
                    } else if (q0.mode == 0 && q0.color == q1.color && q2.color == q3.color) {
                        // Vertical Gradient Axis-Aligned Rectangle with Subpixel AA
                        float c0_r = static_cast<float>(q0.color & 0xFF);
                        float c0_g = static_cast<float>((q0.color >> 8) & 0xFF);
                        float c0_b = static_cast<float>((q0.color >> 16) & 0xFF);
                        float c0_a = static_cast<float>((q0.color >> 24) & 0xFF);

                        float c1_r = static_cast<float>(q2.color & 0xFF);
                        float c1_g = static_cast<float>((q2.color >> 8) & 0xFF);
                        float c1_b = static_cast<float>((q2.color >> 16) & 0xFF);
                        float c1_a = static_cast<float>((q2.color >> 24) & 0xFF);

                        float invH = (fMaxY > fMinY) ? (1.0f / (fMaxY - fMinY)) : 1.0f;

                        float covX_left  = (pMinX == pMaxX) ? std::clamp(fMaxX - fMinX, 0.0f, 1.0f)
                                                            : std::clamp(static_cast<float>(pMinX + 1) - fMinX, 0.0f, 1.0f);
                        float covX_right = (pMinX == pMaxX) ? covX_left
                                                            : std::clamp(fMaxX - static_cast<float>(pMaxX), 0.0f, 1.0f);

                        for (int y = pMinY; y <= pMaxY; ++y) {
                            float cy;
                            if (pMinY == pMaxY) {
                                cy = std::clamp(fMaxY - fMinY, 0.0f, 1.0f);
                            } else if (y == pMinY) {
                                cy = std::clamp(static_cast<float>(pMinY + 1) - fMinY, 0.0f, 1.0f);
                            } else if (y == pMaxY) {
                                cy = std::clamp(fMaxY - static_cast<float>(pMaxY), 0.0f, 1.0f);
                            } else {
                                cy = 1.0f;
                            }
                            if (cy <= 0.001f) continue;

                            float t = std::clamp((static_cast<float>(y) + 0.5f - fMinY) * invH, 0.0f, 1.0f);
                            float r = c0_r + (c1_r - c0_r) * t;
                            float g = c0_g + (c1_g - c0_g) * t;
                            float b = c0_b + (c1_b - c0_b) * t;
                            float a = c0_a + (c1_a - c0_a) * t;

                            uint32_t ur = static_cast<uint32_t>(std::clamp(r, 0.0f, 255.0f));
                            uint32_t ug = static_cast<uint32_t>(std::clamp(g, 0.0f, 255.0f));
                            uint32_t ub = static_cast<uint32_t>(std::clamp(b, 0.0f, 255.0f));
                            float rawA = std::clamp(a, 0.0f, 255.0f);

                            uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + pMinX];

                            if (pMinX == pMaxX) {
                                float cov = covX_left * cy;
                                uint32_t effA = static_cast<uint32_t>(std::round(rawA * cov));
                                if (effA > 0) {
                                    if (blendMode_ == BlendMode::Normal && effA >= 255) {
                                        *rowDst = (0xFF << 24) | (ur << 16) | (ug << 8) | ub;
                                    } else {
                                        *rowDst = blendPixel(*rowDst, ur, ug, ub, effA);
                                    }
                                }
                            } else {
                                // Left boundary
                                float covL = covX_left * cy;
                                uint32_t effAL = static_cast<uint32_t>(std::round(rawA * covL));
                                if (effAL > 0) {
                                    if (blendMode_ == BlendMode::Normal && effAL >= 255) {
                                        rowDst[0] = (0xFF << 24) | (ur << 16) | (ug << 8) | ub;
                                    } else {
                                        rowDst[0] = blendPixel(rowDst[0], ur, ug, ub, effAL);
                                    }
                                }

                                // Interior
                                int innerCount = pMaxX - pMinX - 1;
                                if (innerCount > 0) {
                                    float covMid = cy;
                                    uint32_t effAMid = static_cast<uint32_t>(std::round(rawA * covMid));
                                    if (effAMid > 0) {
                                        if (blendMode_ == BlendMode::Normal && effAMid >= 255) {
                                            uint32_t col = (0xFF << 24) | (ur << 16) | (ug << 8) | ub;
                                            std::fill_n(rowDst + 1, innerCount, col);
                                        } else {
                                            for (int i = 1; i <= innerCount; ++i) {
                                                rowDst[i] = blendPixel(rowDst[i], ur, ug, ub, effAMid);
                                            }
                                        }
                                    }
                                }

                                // Right boundary
                                float covR = covX_right * cy;
                                uint32_t effAR = static_cast<uint32_t>(std::round(rawA * covR));
                                if (effAR > 0) {
                                    int lastIdx = pMaxX - pMinX;
                                    if (blendMode_ == BlendMode::Normal && effAR >= 255) {
                                        rowDst[lastIdx] = (0xFF << 24) | (ur << 16) | (ug << 8) | ub;
                                    } else {
                                        rowDst[lastIdx] = blendPixel(rowDst[lastIdx], ur, ug, ub, effAR);
                                    }
                                }
                            }
                        }
                        return;
                    } else if (q0.mode == 0 && q0.color == q3.color && q1.color == q2.color) {
                        // Horizontal Gradient Axis-Aligned Rectangle with Subpixel AA
                        float c0_r = static_cast<float>(q0.color & 0xFF);
                        float c0_g = static_cast<float>((q0.color >> 8) & 0xFF);
                        float c0_b = static_cast<float>((q0.color >> 16) & 0xFF);
                        float c0_a = static_cast<float>((q0.color >> 24) & 0xFF);

                        float c1_r = static_cast<float>(q1.color & 0xFF);
                        float c1_g = static_cast<float>((q1.color >> 8) & 0xFF);
                        float c1_b = static_cast<float>((q1.color >> 16) & 0xFF);
                        float c1_a = static_cast<float>((q1.color >> 24) & 0xFF);

                        float invW = (fMaxX > fMinX) ? (1.0f / (fMaxX - fMinX)) : 1.0f;

                        for (int y = pMinY; y <= pMaxY; ++y) {
                            float cy;
                            if (pMinY == pMaxY) {
                                cy = std::clamp(fMaxY - fMinY, 0.0f, 1.0f);
                            } else if (y == pMinY) {
                                cy = std::clamp(static_cast<float>(pMinY + 1) - fMinY, 0.0f, 1.0f);
                            } else if (y == pMaxY) {
                                cy = std::clamp(fMaxY - static_cast<float>(pMaxY), 0.0f, 1.0f);
                            } else {
                                cy = 1.0f;
                            }
                            if (cy <= 0.001f) continue;

                            uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + pMinX];
                            for (int x = pMinX; x <= pMaxX; ++x, ++rowDst) {
                                float cx;
                                if (pMinX == pMaxX) {
                                    cx = std::clamp(fMaxX - fMinX, 0.0f, 1.0f);
                                } else if (x == pMinX) {
                                    cx = std::clamp(static_cast<float>(pMinX + 1) - fMinX, 0.0f, 1.0f);
                                } else if (x == pMaxX) {
                                    cx = std::clamp(fMaxX - static_cast<float>(pMaxX), 0.0f, 1.0f);
                                } else {
                                    cx = 1.0f;
                                }

                                float t = std::clamp((static_cast<float>(x) + 0.5f - fMinX) * invW, 0.0f, 1.0f);
                                float r = c0_r + (c1_r - c0_r) * t;
                                float g = c0_g + (c1_g - c0_g) * t;
                                float b = c0_b + (c1_b - c0_b) * t;
                                float a = c0_a + (c1_a - c0_a) * t;

                                uint32_t ur = static_cast<uint32_t>(std::clamp(r, 0.0f, 255.0f));
                                uint32_t ug = static_cast<uint32_t>(std::clamp(g, 0.0f, 255.0f));
                                uint32_t ub = static_cast<uint32_t>(std::clamp(b, 0.0f, 255.0f));
                                float rawA = std::clamp(a, 0.0f, 255.0f);

                                float cov = cx * cy;
                                uint32_t effA = static_cast<uint32_t>(std::round(rawA * cov));
                                if (effA > 0) {
                                    *rowDst = blendPixel(*rowDst, ur, ug, ub, effA);
                                }
                            }
                        }
                        return;
                    }
                }
            }
        }

        // General Convex Quad:
        // If not a solid quad, split into 2 triangles with exact per-vertex color interpolation
        if (q0.color != q1.color || q0.color != q2.color || q0.color != q3.color) {
            rasterizeTriangle(q0, q1, q2);
            rasterizeTriangle(q0, q2, q3);
            return;
        }

        uint32_t a = (q0.color >> 24) & 0xFF;
        if (a == 0) return;
        uint32_t c_r = q0.color & 0xFF;
        uint32_t c_g = (q0.color >> 8) & 0xFF;
        uint32_t c_b = (q0.color >> 16) & 0xFF;

        float dx0 = q1.x - q0.x, dy0 = q1.y - q0.y;
        float dx1 = q2.x - q1.x, dy1 = q2.y - q1.y;
        float dx2 = q3.x - q2.x, dy2 = q3.y - q2.y;
        float dx3 = q0.x - q3.x, dy3 = q0.y - q3.y;

        // Verify winding (positive area)
        float area = (q1.x - q0.x) * (q2.y - q0.y) - (q1.y - q0.y) * (q2.x - q0.x);
        Vertex2D p0 = q0, p1 = q1, p2 = q2, p3 = q3;
        if (area < 0.0f) {
            std::swap(p1, p3);
            dx0 = p1.x - p0.x; dy0 = p1.y - p0.y;
            dx1 = p2.x - p1.x; dy1 = p2.y - p1.y;
            dx2 = p3.x - p2.x; dy2 = p3.y - p2.y;
            dx3 = p0.x - p3.x; dy3 = p0.y - p3.y;
        }

        const float stepX0 = -dy0, stepX1 = -dy1, stepX2 = -dy2, stepX3 = -dy3;
        const float startPx = static_cast<float>(minX) + 0.5f;

        if (antiAliasingMode_ == 0 || p0.mode == 5 || p1.mode == 5 || p2.mode == 5 || p3.mode == 5) {
            // Fast 1-Sample Point Sampling
            for (int y = minY; y <= maxY; ++y) {
                const float py = static_cast<float>(y) + 0.5f;
                float e0 = dx0 * (py - p0.y) - dy0 * (startPx - p0.x);
                float e1 = dx1 * (py - p1.y) - dy1 * (startPx - p1.x);
                float e2 = dx2 * (py - p2.y) - dy2 * (startPx - p2.x);
                float e3 = dx3 * (py - p3.y) - dy3 * (startPx - p3.x);

                uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + minX];
                for (int x = minX; x <= maxX; ++x, ++rowDst) {
                    if (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f && e3 >= 0.0f) {
                        *rowDst = blendPixel(*rowDst, c_r, c_g, c_b, a);
                    }
                    e0 += stepX0; e1 += stepX1; e2 += stepX2; e3 += stepX3;
                }
            }
            return;
        }

        // Subpixel Sampling: 2x Fast or 4x RGSS
        const int numSamples = (antiAliasingMode_ == 1) ? 2 : 4;
        const float ox[4] = { 0.375f, -0.125f,  0.125f, -0.375f };
        const float oy[4] = {-0.125f, -0.375f,  0.375f,  0.125f };

        float de0[4], de1[4], de2[4], de3[4];
        for (int s = 0; s < numSamples; ++s) {
            de0[s] = dx0 * oy[s] - dy0 * ox[s];
            de1[s] = dx1 * oy[s] - dy1 * ox[s];
            de2[s] = dx2 * oy[s] - dy2 * ox[s];
            de3[s] = dx3 * oy[s] - dy3 * ox[s];
        }

        float minDe0 = de0[0], maxDe0 = de0[0];
        float minDe1 = de1[0], maxDe1 = de1[0];
        float minDe2 = de2[0], maxDe2 = de2[0];
        float minDe3 = de3[0], maxDe3 = de3[0];
        for (int s = 1; s < numSamples; ++s) {
            minDe0 = std::min(minDe0, de0[s]); maxDe0 = std::max(maxDe0, de0[s]);
            minDe1 = std::min(minDe1, de1[s]); maxDe1 = std::max(maxDe1, de1[s]);
            minDe2 = std::min(minDe2, de2[s]); maxDe2 = std::max(maxDe2, de2[s]);
            minDe3 = std::min(minDe3, de3[s]); maxDe3 = std::max(maxDe3, de3[s]);
        }

        for (int y = minY; y <= maxY; ++y) {
            const float py = static_cast<float>(y) + 0.5f;
            float e0 = dx0 * (py - p0.y) - dy0 * (startPx - p0.x);
            float e1 = dx1 * (py - p1.y) - dy1 * (startPx - p1.x);
            float e2 = dx2 * (py - p2.y) - dy2 * (startPx - p2.x);
            float e3 = dx3 * (py - p3.y) - dy3 * (startPx - p3.x);

            uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + minX];

            for (int x = minX; x <= maxX; ++x, ++rowDst) {
                if (e0 + maxDe0 >= 0.0f && e1 + maxDe1 >= 0.0f && e2 + maxDe2 >= 0.0f && e3 + maxDe3 >= 0.0f) {
                    uint32_t mask = numSamples;
                    if (!(e0 + minDe0 >= 0.0f && e1 + minDe1 >= 0.0f && e2 + minDe2 >= 0.0f && e3 + minDe3 >= 0.0f)) {
                        mask = 0;
                        for (int s = 0; s < numSamples; ++s) {
                            if (e0 + de0[s] >= 0.0f && e1 + de1[s] >= 0.0f && e2 + de2[s] >= 0.0f && e3 + de3[s] >= 0.0f) {
                                ++mask;
                            }
                        }
                    }

                    if (mask > 0) {
                        uint32_t effA = (mask == static_cast<uint32_t>(numSamples)) ? a : ((a * mask + (numSamples / 2)) / numSamples);
                        *rowDst = blendPixel(*rowDst, c_r, c_g, c_b, effA);
                    }
                }
                e0 += stepX0; e1 += stepX1; e2 += stepX2; e3 += stepX3;
            }
        }
    }

    void rasterizeTriangle(const Vertex2D& v0, const Vertex2D& v1, const Vertex2D& v2) {
        int clipMinX, clipMaxX, clipMinY, clipMaxY;
        getClipBounds(clipMinX, clipMaxX, clipMinY, clipMaxY);
        int minX = std::max(clipMinX, static_cast<int>(std::floor(std::min({v0.x, v1.x, v2.x}))));
        int maxX = std::min(clipMaxX, static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x}))));
        int minY = std::max(clipMinY, static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y}))));
        int maxY = std::min(clipMaxY, static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y}))));

        if (minX > maxX || minY > maxY) return;

        Vertex2D t0 = v0;
        Vertex2D t1 = v1;
        Vertex2D t2 = v2;

        float area = (t1.x - t0.x) * (t2.y - t0.y) - (t1.y - t0.y) * (t2.x - t0.x);
        if (std::abs(area) < 1e-4f) return;
        if (area < 0.0f) {
            std::swap(t1, t2);
            area = -area;
        }

        const float invArea = 1.0f / area;

        // Edge 0: t0 -> t1
        const float dx0 = t1.x - t0.x;
        const float dy0 = t1.y - t0.y;
        const float stepX0 = -dy0;

        // Edge 1: t1 -> t2
        const float dx1 = t2.x - t1.x;
        const float dy1 = t2.y - t1.y;
        const float stepX1 = -dy1;

        // Edge 2: t2 -> t0
        const float dx2 = t0.x - t2.x;
        const float dy2 = t0.y - t2.y;
        const float stepX2 = -dy2;

        const bool isText = (t0.mode == 1 || t1.mode == 1 || t2.mode == 1);
        const bool isMonospace = (t0.mode == 4 || t1.mode == 4 || t2.mode == 4);
        const bool isSolid = (!isText && !isMonospace && t0.color == t1.color && t0.color == t2.color);

        const uint32_t c0_r = t0.color & 0xFF;
        const uint32_t c0_g = (t0.color >> 8) & 0xFF;
        const uint32_t c0_b = (t0.color >> 16) & 0xFF;
        const uint32_t c0_a = (t0.color >> 24) & 0xFF;

        const uint32_t c1_r = t1.color & 0xFF;
        const uint32_t c1_g = (t1.color >> 8) & 0xFF;
        const uint32_t c1_b = (t1.color >> 16) & 0xFF;
        const uint32_t c1_a = (t1.color >> 24) & 0xFF;

        const uint32_t c2_r = t2.color & 0xFF;
        const uint32_t c2_g = (t2.color >> 8) & 0xFF;
        const uint32_t c2_b = (t2.color >> 16) & 0xFF;
        const uint32_t c2_a = (t2.color >> 24) & 0xFF;

        const float startPx = static_cast<float>(minX) + 0.5f;

        if (antiAliasingMode_ == 0 || t0.mode == 5 || t1.mode == 5 || t2.mode == 5) {
            // Fast 1-Sample Point Sampling
            for (int y = minY; y <= maxY; ++y) {
                const float py = static_cast<float>(y) + 0.5f;
                float e0 = dx0 * (py - t0.y) - dy0 * (startPx - t0.x);
                float e1 = dx1 * (py - t1.y) - dy1 * (startPx - t1.x);
                float e2 = dx2 * (py - t2.y) - dy2 * (startPx - t2.x);

                uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + minX];
                for (int x = minX; x <= maxX; ++x, ++rowDst) {
                    if (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f) {
                        if (isSolid) {
                            *rowDst = blendPixel(*rowDst, c0_r, c0_g, c0_b, c0_a);
                        } else if (isMonospace) {
                            float u = (e1 * t0.u + e2 * t1.u + e0 * t2.u) * invArea;
                            float v = (e1 * t0.v + e2 * t1.v + e0 * t2.v) * invArea;
                            int gx = std::clamp(static_cast<int>(u * 128.0f), 0, 127);
                            int gy = std::clamp(static_cast<int>(v * 256.0f), 0, 255);
                            uint32_t fontAlpha = monospaceAtlas_.empty() ? 0 : monospaceAtlas_[static_cast<size_t>(gy * 128 + gx)];
                            if (fontAlpha > 0) {
                                uint32_t a = (c0_a * fontAlpha) / 255;
                                *rowDst = blendPixel(*rowDst, c0_r, c0_g, c0_b, a);
                            }
                        } else if (isText && !fontAtlas_.empty() && atlasWidth_ > 0 && atlasHeight_ > 0) {
                            float u = (e1 * t0.u + e2 * t1.u + e0 * t2.u) * invArea;
                            float v = (e1 * t0.v + e2 * t1.v + e0 * t2.v) * invArea;
                            float fx = u * static_cast<float>(atlasWidth_) - 0.5f;
                            float fy = v * static_cast<float>(atlasHeight_) - 0.5f;
                            int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, atlasWidth_ - 1);
                            int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, atlasHeight_ - 1);
                            int x1 = std::min(x0 + 1, atlasWidth_ - 1);
                            int y1 = std::min(y0 + 1, atlasHeight_ - 1);
                            float wx = fx - std::floor(fx), wy = fy - std::floor(fy);
                            float a00 = fontAtlas_[static_cast<size_t>(y0 * atlasWidth_ + x0)];
                            float a10 = fontAtlas_[static_cast<size_t>(y0 * atlasWidth_ + x1)];
                            float a01 = fontAtlas_[static_cast<size_t>(y1 * atlasWidth_ + x0)];
                            float a11 = fontAtlas_[static_cast<size_t>(y1 * atlasWidth_ + x1)];
                            float fontAlphaF = (a00 * (1.0f - wx) + a10 * wx) * (1.0f - wy) +
                                               (a01 * (1.0f - wx) + a11 * wx) * wy;
                            uint32_t fontAlpha = static_cast<uint32_t>(fontAlphaF + 0.5f);
                            if (fontAlpha > 0) {
                                uint32_t a = (c0_a * fontAlpha) / 255;
                                *rowDst = blendPixel(*rowDst, c0_r, c0_g, c0_b, a);
                            }
                        } else {
                            uint32_t r = static_cast<uint32_t>((e1 * c0_r + e2 * c1_r + e0 * c2_r) * invArea);
                            uint32_t g = static_cast<uint32_t>((e1 * c0_g + e2 * c1_g + e0 * c2_g) * invArea);
                            uint32_t b = static_cast<uint32_t>((e1 * c0_b + e2 * c1_b + e0 * c2_b) * invArea);
                            uint32_t a = static_cast<uint32_t>((e1 * c0_a + e2 * c1_a + e0 * c2_a) * invArea);
                            *rowDst = blendPixel(*rowDst, r, g, b, a);
                        }
                    }
                    e0 += stepX0; e1 += stepX1; e2 += stepX2;
                }
            }
            return;
        }

        // Subpixel Sampling: 2x Fast or 4x RGSS
        const int numSamples = (antiAliasingMode_ == 1) ? 2 : 4;
        const float ox[4] = { 0.375f, -0.125f,  0.125f, -0.375f };
        const float oy[4] = {-0.125f, -0.375f,  0.375f,  0.125f };

        float de0[4], de1[4], de2[4];
        for (int s = 0; s < numSamples; ++s) {
            de0[s] = dx0 * oy[s] - dy0 * ox[s];
            de1[s] = dx1 * oy[s] - dy1 * ox[s];
            de2[s] = dx2 * oy[s] - dy2 * ox[s];
        }

        float minDe0 = de0[0], maxDe0 = de0[0];
        float minDe1 = de1[0], maxDe1 = de1[0];
        float minDe2 = de2[0], maxDe2 = de2[0];
        for (int s = 1; s < numSamples; ++s) {
            minDe0 = std::min(minDe0, de0[s]); maxDe0 = std::max(maxDe0, de0[s]);
            minDe1 = std::min(minDe1, de1[s]); maxDe1 = std::max(maxDe1, de1[s]);
            minDe2 = std::min(minDe2, de2[s]); maxDe2 = std::max(maxDe2, de2[s]);
        }

        for (int y = minY; y <= maxY; ++y) {
            const float py = static_cast<float>(y) + 0.5f;

            float e0 = dx0 * (py - t0.y) - dy0 * (startPx - t0.x);
            float e1 = dx1 * (py - t1.y) - dy1 * (startPx - t1.x);
            float e2 = dx2 * (py - t2.y) - dy2 * (startPx - t2.x);

            uint32_t* rowDst = &framebuffer_[y * viewportWidth_ + minX];

            for (int x = minX; x <= maxX; ++x, ++rowDst) {
                // Quick reject if outside any edge's maximum subpixel reach
                if (e0 + maxDe0 >= 0.0f && e1 + maxDe1 >= 0.0f && e2 + maxDe2 >= 0.0f) {
                    uint32_t mask = numSamples;
                    // Check if interior (all subpixel samples guaranteed inside)
                    if (!(e0 + minDe0 >= 0.0f && e1 + minDe1 >= 0.0f && e2 + minDe2 >= 0.0f)) {
                        mask = 0;
                        for (int s = 0; s < numSamples; ++s) {
                            if (e0 + de0[s] >= 0.0f && e1 + de1[s] >= 0.0f && e2 + de2[s] >= 0.0f) {
                                ++mask;
                            }
                        }
                    }

                    if (mask > 0) {
                        if (isSolid) {
                            uint32_t effA = (mask == static_cast<uint32_t>(numSamples)) ? c0_a : ((c0_a * mask + (numSamples / 2)) / numSamples);
                            *rowDst = blendPixel(*rowDst, c0_r, c0_g, c0_b, effA);
                        } else if (isMonospace) {
                            float u = (e1 * t0.u + e2 * t1.u + e0 * t2.u) * invArea;
                            float v = (e1 * t0.v + e2 * t1.v + e0 * t2.v) * invArea;
                            int gx = std::clamp(static_cast<int>(u * 128.0f), 0, 127);
                            int gy = std::clamp(static_cast<int>(v * 256.0f), 0, 255);
                            uint32_t fontAlpha = monospaceAtlas_.empty() ? 0 : monospaceAtlas_[static_cast<size_t>(gy * 128 + gx)];
                            if (fontAlpha > 0) {
                                uint32_t effA = (mask == static_cast<uint32_t>(numSamples)) ? c0_a : ((c0_a * mask + (numSamples / 2)) / numSamples);
                                uint32_t a = (effA * fontAlpha) / 255;
                                *rowDst = blendPixel(*rowDst, c0_r, c0_g, c0_b, a);
                            }
                        } else if (isText && !fontAtlas_.empty() && atlasWidth_ > 0 && atlasHeight_ > 0) {
                            float u = (e1 * t0.u + e2 * t1.u + e0 * t2.u) * invArea;
                            float v = (e1 * t0.v + e2 * t1.v + e0 * t2.v) * invArea;

                            // Bilinear interpolation for smooth, crisp font glyph antialiasing
                            float fx = u * static_cast<float>(atlasWidth_) - 0.5f;
                            float fy = v * static_cast<float>(atlasHeight_) - 0.5f;
                            int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, atlasWidth_ - 1);
                            int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, atlasHeight_ - 1);
                            int x1 = std::min(x0 + 1, atlasWidth_ - 1);
                            int y1 = std::min(y0 + 1, atlasHeight_ - 1);
                            float wx = fx - std::floor(fx);
                            float wy = fy - std::floor(fy);

                            float a00 = fontAtlas_[static_cast<size_t>(y0 * atlasWidth_ + x0)];
                            float a10 = fontAtlas_[static_cast<size_t>(y0 * atlasWidth_ + x1)];
                            float a01 = fontAtlas_[static_cast<size_t>(y1 * atlasWidth_ + x0)];
                            float a11 = fontAtlas_[static_cast<size_t>(y1 * atlasWidth_ + x1)];
                            float fontAlphaF = (a00 * (1.0f - wx) + a10 * wx) * (1.0f - wy) +
                                               (a01 * (1.0f - wx) + a11 * wx) * wy;
                            uint32_t fontAlpha = static_cast<uint32_t>(fontAlphaF + 0.5f);

                            if (fontAlpha > 0) {
                                uint32_t a = (c0_a * fontAlpha) / 255;
                                if (mask < static_cast<uint32_t>(numSamples)) a = (a * mask + (numSamples / 2)) / numSamples;
                                *rowDst = blendPixel(*rowDst, c0_r, c0_g, c0_b, a);
                            }
                        } else {
                            // Smooth color gradient
                            uint32_t r = static_cast<uint32_t>((e1 * c0_r + e2 * c1_r + e0 * c2_r) * invArea);
                            uint32_t g = static_cast<uint32_t>((e1 * c0_g + e2 * c1_g + e0 * c2_g) * invArea);
                            uint32_t b = static_cast<uint32_t>((e1 * c0_b + e2 * c1_b + e0 * c2_b) * invArea);
                            uint32_t a = static_cast<uint32_t>((e1 * c0_a + e2 * c1_a + e0 * c2_a) * invArea);
                            uint32_t effA = (mask == static_cast<uint32_t>(numSamples)) ? a : ((a * mask + (numSamples / 2)) / numSamples);
                            *rowDst = blendPixel(*rowDst, r, g, b, effA);
                        }
                    }
                }
                e0 += stepX0;
                e1 += stepX1;
                e2 += stepX2;
            }
        }
    }

    bool scissorEnabled_{false};
    Rect2D scissor_{0.0f, 0.0f, 0.0f, 0.0f};
    std::vector<unsigned char> fontAtlas_;
    std::vector<uint8_t> monospaceAtlas_{};
    std::vector<uint32_t> framebuffer_;
    int atlasWidth_{1024};
    int atlasHeight_{1024};
    size_t totalVerticesRendered_{0};
    uint32_t viewportWidth_{1280};
    uint32_t viewportHeight_{800};
    bool directPresent_{true};
    int antiAliasingMode_{2};
    BlendMode blendMode_{BlendMode::Normal};
#if defined(_WIN32)
    HWND hwnd_{nullptr};
#endif

    inline uint32_t blendPixel(uint32_t dst, uint32_t srcR, uint32_t srcG, uint32_t srcB, uint32_t a) const noexcept {
        if (a == 0) return dst;
        uint32_t dr = (dst >> 16) & 0xFF;
        uint32_t dg = (dst >> 8) & 0xFF;
        uint32_t db = dst & 0xFF;

        if (blendMode_ == BlendMode::Normal) {
            if (a >= 255) return (0xFF << 24) | (srcR << 16) | (srcG << 8) | srcB;
            uint32_t invA = 255 - a;
            return (0xFF << 24) | (((srcR * a + dr * invA) / 255) << 16)
                                | (((srcG * a + dg * invA) / 255) << 8)
                                | ((srcB * a + db * invA) / 255);
        } else if (blendMode_ == BlendMode::Multiply) {
            uint32_t mulR = (srcR * dr) / 255;
            uint32_t mulG = (srcG * dg) / 255;
            uint32_t mulB = (srcB * db) / 255;
            uint32_t invA = 255 - a;
            return (0xFF << 24) | (((mulR * a + dr * invA) / 255) << 16)
                                | (((mulG * a + dg * invA) / 255) << 8)
                                | ((mulB * a + db * invA) / 255);
        } else if (blendMode_ == BlendMode::Screen) {
            uint32_t scrR = 255 - (((255 - srcR) * (255 - dr)) / 255);
            uint32_t scrG = 255 - (((255 - srcG) * (255 - dg)) / 255);
            uint32_t scrB = 255 - (((255 - srcB) * (255 - db)) / 255);
            uint32_t invA = 255 - a;
            return (0xFF << 24) | (((scrR * a + dr * invA) / 255) << 16)
                                | (((scrG * a + dg * invA) / 255) << 8)
                                | ((scrB * a + db * invA) / 255);
        } else if (blendMode_ == BlendMode::Add) {
            uint32_t outR = std::min(255u, dr + (srcR * a) / 255);
            uint32_t outG = std::min(255u, dg + (srcG * a) / 255);
            uint32_t outB = std::min(255u, db + (srcB * a) / 255);
            return (0xFF << 24) | (outR << 16) | (outG << 8) | outB;
        } else if (blendMode_ == BlendMode::Overlay) {
            auto overlayChan = [](uint32_t s, uint32_t d) -> uint32_t {
                return (d < 128) ? ((2 * s * d) / 255) : (255 - (2 * (255 - s) * (255 - d)) / 255);
            };
            uint32_t ovR = overlayChan(srcR, dr);
            uint32_t ovG = overlayChan(srcG, dg);
            uint32_t ovB = overlayChan(srcB, db);
            uint32_t invA = 255 - a;
            return (0xFF << 24) | (((ovR * a + dr * invA) / 255) << 16)
                                | (((ovG * a + dg * invA) / 255) << 8)
                                | ((ovB * a + db * invA) / 255);
        } else if (blendMode_ == BlendMode::SoftLight) {
            auto softLightChan = [](uint32_t s, uint32_t d) -> uint32_t {
                float sf = static_cast<float>(s) / 255.0f;
                float df = static_cast<float>(d) / 255.0f;
                float r = (1.0f - 2.0f * sf) * df * df + 2.0f * sf * df;
                return static_cast<uint32_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
            };
            uint32_t slR = softLightChan(srcR, dr);
            uint32_t slG = softLightChan(srcG, dg);
            uint32_t slB = softLightChan(srcB, db);
            uint32_t invA = 255 - a;
            return (0xFF << 24) | (((slR * a + dr * invA) / 255) << 16)
                                | (((slG * a + dg * invA) / 255) << 8)
                                | ((slB * a + db * invA) / 255);
        }
        return dst;
    }
};

// ============================================================================
// WebGPU Backend (WebAssembly / Native WebGPU WGSL Pipeline)
// ============================================================================
#if defined(EATS_ENABLE_WEBGPU)
static inline WGPUStringView makeWgpuStringView(const char* s) {
    return WGPUStringView{ s, WGPU_STRLEN };
}

class WebGpuBatchBackend : public IBatchRenderBackend {
public:
    WebGpuBatchBackend() = default;
    ~WebGpuBatchBackend() override {
        shutdown();
    }

    bool initialize(void* windowHandle, uint32_t width, uint32_t height) override {
        width_ = width;
        height_ = height;

#if defined(__EMSCRIPTEN__)
        if (!windowHandle) {
            std::cerr << "[WebGPU] WebGpuBatchBackend requires a valid window handle on Emscripten." << std::endl;
            return false;
        }

        int hasDevice = EM_ASM_INT({
            return (typeof Module !== 'undefined' && Module['preinitializedWebGPUDevice']) ? 1 : 0;
        });
        if (!hasDevice) {
            std::cerr << "[WebGPU] Module['preinitializedWebGPUDevice'] is not available! Browser WebGPU device was not initialized." << std::endl;
            return false;
        }

        device_ = emscripten_webgpu_get_device();
        if (!device_) {
            std::cerr << "[WebGPU] Failed to retrieve WebGPU device from Emscripten!" << std::endl;
            return false;
        }

        WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc{};
        canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
        canvasDesc.selector = makeWgpuStringView("#canvas");

        WGPUSurfaceDescriptor surfDesc{};
        surfDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&canvasDesc);

        WGPUInstanceDescriptor instDesc{};
        instance_ = wgpuCreateInstance(&instDesc);
        surface_ = wgpuInstanceCreateSurface(instance_, &surfDesc);
        surfaceFormat_ = WGPUTextureFormat_BGRA8Unorm;
#else
        WGPUInstanceDescriptor instDesc{};
        instance_ = wgpuCreateInstance(&instDesc);
        if (!instance_) {
            std::cerr << "[WebGPU] Failed to create WGPUInstance on desktop!\n";
            return false;
        }

        if (windowHandle) {
#if defined(_WIN32)
            HWND hwnd = static_cast<HWND>(windowHandle);
            WGPUSurfaceSourceWindowsHWND winDesc{};
            winDesc.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
            winDesc.hinstance = GetModuleHandle(nullptr);
            winDesc.hwnd = hwnd;

            WGPUSurfaceDescriptor surfDesc{};
            surfDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&winDesc);
            surface_ = wgpuInstanceCreateSurface(instance_, &surfDesc);
#endif
        }

        struct ReqData {
            WGPUAdapter adapter{nullptr};
            WGPUDevice device{nullptr};
        } req;

        WGPURequestAdapterOptions aOpt{};
        aOpt.compatibleSurface = surface_;
        aOpt.powerPreference = WGPUPowerPreference_HighPerformance;

        WGPURequestAdapterCallbackInfo aCb{};
        aCb.mode = WGPUCallbackMode_AllowProcessEvents;
        aCb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adp, WGPUStringView, void* u, void*) {
            if (status == WGPURequestAdapterStatus_Success) static_cast<ReqData*>(u)->adapter = adp;
        };
        aCb.userdata1 = &req;
        wgpuInstanceRequestAdapter(instance_, &aOpt, aCb);
        wgpuInstanceProcessEvents(instance_);
        adapter_ = req.adapter;
        if (!adapter_) {
            std::cerr << "[WebGPU] Failed to acquire WGPUAdapter on desktop!\n";
            return false;
        }

        WGPUDeviceDescriptor dDesc{};
        WGPURequestDeviceCallbackInfo dCb{};
        dCb.mode = WGPUCallbackMode_AllowProcessEvents;
        dCb.callback = [](WGPURequestDeviceStatus status, WGPUDevice dev, WGPUStringView, void* u, void*) {
            if (status == WGPURequestDeviceStatus_Success) static_cast<ReqData*>(u)->device = dev;
        };
        dCb.userdata1 = &req;
        wgpuAdapterRequestDevice(adapter_, &dDesc, dCb);
        wgpuInstanceProcessEvents(instance_);
        device_ = req.device;
        if (!device_) {
            std::cerr << "[WebGPU] Failed to acquire WGPUDevice on desktop!\n";
            return false;
        }

        surfaceFormat_ = WGPUTextureFormat_BGRA8Unorm;
        if (surface_) {
            WGPUSurfaceCapabilities caps{};
            wgpuSurfaceGetCapabilities(surface_, adapter_, &caps);
            bool foundUnorm = false;
            // Prefer non-sRGB Unorm format to preserve authored sRGB art assets without double-gamma wash
            for (size_t i = 0; i < caps.formatCount; ++i) {
                if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm) {
                    surfaceFormat_ = WGPUTextureFormat_BGRA8Unorm;
                    foundUnorm = true;
                    break;
                }
            }
            if (!foundUnorm) {
                for (size_t i = 0; i < caps.formatCount; ++i) {
                    if (caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) {
                        surfaceFormat_ = WGPUTextureFormat_RGBA8Unorm;
                        foundUnorm = true;
                        break;
                    }
                }
            }
            if (!foundUnorm && caps.formatCount > 0) {
                surfaceFormat_ = caps.formats[0];
            }
        }
        isSrgbSurface_ = (surfaceFormat_ == WGPUTextureFormat_BGRA8UnormSrgb ||
                          surfaceFormat_ == WGPUTextureFormat_RGBA8UnormSrgb);
        std::cout << "[WebGPU] Surface format: " << (isSrgbSurface_ ? "sRGB (linearized)" : "UNORM (direct)") << "\n";
#endif

        if (surface_) {
            WGPUSurfaceConfiguration config{};
            config.device = device_;
            config.format = surfaceFormat_;
            config.usage = WGPUTextureUsage_RenderAttachment;
            config.viewFormatCount = 0;
            config.viewFormats = nullptr;
            config.alphaMode = WGPUCompositeAlphaMode_Auto;
            config.width = width_;
            config.height = height_;
            config.presentMode = WGPUPresentMode_Fifo;
            wgpuSurfaceConfigure(surface_, &config);
        }

        queue_ = wgpuDeviceGetQueue(device_);
        initShadersAndPipeline();
        return true;
    }

    void shutdown() override {
        if (msaaTextureView_) {
            wgpuTextureViewRelease(msaaTextureView_);
            msaaTextureView_ = nullptr;
        }
        if (msaaTexture_) {
            wgpuTextureDestroy(msaaTexture_);
            wgpuTextureRelease(msaaTexture_);
            msaaTexture_ = nullptr;
        }
        if (!customTargetView_ && currentBackBufferView_) {
            wgpuTextureViewRelease(currentBackBufferView_);
        }
        currentBackBufferView_ = nullptr;
        customTargetView_ = nullptr;
        if (currentSurfaceTexture_.texture) {
            wgpuTextureRelease(currentSurfaceTexture_.texture);
            currentSurfaceTexture_.texture = nullptr;
        }
        if (rgbaTextureView_) {
            wgpuTextureViewRelease(rgbaTextureView_);
            rgbaTextureView_ = nullptr;
        }
        if (rgbaTexture_) {
            wgpuTextureDestroy(rgbaTexture_);
            wgpuTextureRelease(rgbaTexture_);
            rgbaTexture_ = nullptr;
        }
        if (bgTextureView_) {
            wgpuTextureViewRelease(bgTextureView_);
            bgTextureView_ = nullptr;
        }
        if (bgTexture_) {
            wgpuTextureDestroy(bgTexture_);
            wgpuTextureRelease(bgTexture_);
            bgTexture_ = nullptr;
        }
        if (bindGroup1x_) {
            wgpuBindGroupRelease(bindGroup1x_);
            bindGroup1x_ = nullptr;
        }
        if (bindGroup4x_) {
            wgpuBindGroupRelease(bindGroup4x_);
            bindGroup4x_ = nullptr;
        }
        bindGroup_ = nullptr;
        if (uniformBuffer_) {
            wgpuBufferDestroy(uniformBuffer_);
            wgpuBufferRelease(uniformBuffer_);
            uniformBuffer_ = nullptr;
        }
        if (fontSampler_) {
            wgpuSamplerRelease(fontSampler_);
            fontSampler_ = nullptr;
        }
        if (fontTextureView_) {
            wgpuTextureViewRelease(fontTextureView_);
            fontTextureView_ = nullptr;
        }
        if (fontTexture_) {
            wgpuTextureDestroy(fontTexture_);
            wgpuTextureRelease(fontTexture_);
            fontTexture_ = nullptr;
        }
        if (pipeline1x_) {
            wgpuRenderPipelineRelease(pipeline1x_);
            pipeline1x_ = nullptr;
        }
        if (pipeline4x_) {
            wgpuRenderPipelineRelease(pipeline4x_);
            pipeline4x_ = nullptr;
        }
        pipeline_ = nullptr;
        if (vertexBuffer_) {
            wgpuBufferDestroy(vertexBuffer_);
            wgpuBufferRelease(vertexBuffer_);
            vertexBuffer_ = nullptr;
            vertexBufferSize_ = 0;
        }
        if (queue_) {
            wgpuQueueRelease(queue_);
            queue_ = nullptr;
        }
        if (surface_) {
            wgpuSurfaceRelease(surface_);
            surface_ = nullptr;
        }
#if !defined(__EMSCRIPTEN__)
        if (device_) {
            wgpuDeviceRelease(device_);
            device_ = nullptr;
        }
        if (adapter_) {
            wgpuAdapterRelease(adapter_);
            adapter_ = nullptr;
        }
        if (instance_) {
            wgpuInstanceRelease(instance_);
            instance_ = nullptr;
        }
#endif
    }

    uint32_t surfaceConfiguredWidth_{0};
    uint32_t surfaceConfiguredHeight_{0};

    void setScissorRect(float x, float y, float w, float h) override {
        hasScissor_ = true;
        float rx = std::max(0.0f, x);
        float ry = std::max(0.0f, y);
        float rw = std::max(0.0f, w);
        float rh = std::max(0.0f, h);
        if (rx + rw > static_cast<float>(width_)) rw = std::max(0.0f, static_cast<float>(width_) - rx);
        if (ry + rh > static_cast<float>(height_)) rh = std::max(0.0f, static_cast<float>(height_) - ry);
        scissorX_ = static_cast<uint32_t>(std::round(rx));
        scissorY_ = static_cast<uint32_t>(std::round(ry));
        scissorW_ = static_cast<uint32_t>(std::round(rw));
        scissorH_ = static_cast<uint32_t>(std::round(rh));
    }

    void clearScissorRect() override {
        hasScissor_ = false;
    }

    void setAntiAliasingMode(int mode) override {
        antiAliasingMode_ = mode;
        uint32_t newSampleCount = (mode <= 1) ? 1 : 4;
        if (newSampleCount != sampleCount_) {
            sampleCount_ = newSampleCount;
            updateActivePipeline();
            if (device_) {
                reallocateMsaaTexture(width_, height_);
            }
        }
    }

    void updateActivePipeline() noexcept {
        if (sampleCount_ > 1 && pipeline4x_ && bindGroup4x_) {
            pipeline_ = pipeline4x_;
            bindGroup_ = bindGroup4x_;
        } else {
            pipeline_ = pipeline1x_;
            bindGroup_ = bindGroup1x_;
            sampleCount_ = 1;
        }
    }

    void reallocateMsaaTexture(uint32_t width, uint32_t height) {
        lastBgRgba_ = nullptr;
        if (msaaTextureView_) {
            wgpuTextureViewRelease(msaaTextureView_);
            msaaTextureView_ = nullptr;
        }
        if (msaaTexture_) {
            wgpuTextureDestroy(msaaTexture_);
            wgpuTextureRelease(msaaTexture_);
            msaaTexture_ = nullptr;
        }
        if (sampleCount_ <= 1 || !device_ || width == 0 || height == 0) {
            return;
        }

        WGPUTextureDescriptor msaaDesc{};
        msaaDesc.dimension = WGPUTextureDimension_2D;
        msaaDesc.size = WGPUExtent3D{ width, height, 1 };
        msaaDesc.format = surfaceFormat_;
        msaaDesc.usage = WGPUTextureUsage_RenderAttachment;
        msaaDesc.mipLevelCount = 1;
        msaaDesc.sampleCount = sampleCount_;
        msaaTexture_ = wgpuDeviceCreateTexture(device_, &msaaDesc);
        if (msaaTexture_) {
            WGPUTextureViewDescriptor msaaViewDesc{};
            msaaViewDesc.format = surfaceFormat_;
            msaaViewDesc.dimension = WGPUTextureViewDimension_2D;
            msaaViewDesc.baseMipLevel = 0;
            msaaViewDesc.mipLevelCount = 1;
            msaaViewDesc.baseArrayLayer = 0;
            msaaViewDesc.arrayLayerCount = 1;
            msaaViewDesc.aspect = WGPUTextureAspect_All;
            msaaTextureView_ = wgpuTextureCreateView(msaaTexture_, &msaaViewDesc);
        }
    }

    void resize(uint32_t width, uint32_t height) override {
        if (width == 0 || height == 0) return;
        if (width == surfaceConfiguredWidth_ && height == surfaceConfiguredHeight_ && surface_ && device_) {
            return;
        }
        surfaceConfiguredWidth_ = width;
        surfaceConfiguredHeight_ = height;
        width_ = width;
        height_ = height;
        if (currentBackBufferView_) {
            if (!customTargetView_) {
                wgpuTextureViewRelease(currentBackBufferView_);
            }
            currentBackBufferView_ = nullptr;
        }
        if (currentSurfaceTexture_.texture) {
            wgpuTextureRelease(currentSurfaceTexture_.texture);
            currentSurfaceTexture_.texture = nullptr;
        }
        if (surface_ && device_ && !customTargetView_) {
            WGPUSurfaceConfiguration config{};
            config.device = device_;
            config.format = surfaceFormat_;
            config.usage = WGPUTextureUsage_RenderAttachment;
            config.alphaMode = WGPUCompositeAlphaMode_Auto;
            config.width = width_;
            config.height = height_;
            config.presentMode = WGPUPresentMode_Fifo;
            wgpuSurfaceConfigure(surface_, &config);
        }
        reallocateMsaaTexture(width, height);
    }

    struct GpuUniforms {
        float viewport[2];
        float gamma;
        float time;
        float lightPos[3];
        float lightIntensity;
        float lightColor[4];
        float ambientLight[4];
    };

    void beginPass(float width, float height) override {
        width_ = static_cast<uint32_t>(std::round(width));
        height_ = static_cast<uint32_t>(std::round(height));
        if (device_ && uniformBuffer_) {
            GpuUniforms ubo{};
            ubo.viewport[0] = width;
            ubo.viewport[1] = height;
            ubo.gamma = isSrgbSurface_ ? 2.2f : 1.0f;
            ubo.time = shaderTime_;
            ubo.lightPos[0] = globalLight_.x;
            ubo.lightPos[1] = globalLight_.y;
            ubo.lightPos[2] = globalLight_.z;
            ubo.lightIntensity = globalLight_.intensity;
            ubo.lightColor[0] = lightColor_[0];
            ubo.lightColor[1] = lightColor_[1];
            ubo.lightColor[2] = lightColor_[2];
            ubo.lightColor[3] = lightColor_[3];
            ubo.ambientLight[0] = ambientLight_[0];
            ubo.ambientLight[1] = ambientLight_[1];
            ubo.ambientLight[2] = ambientLight_[2];
            ubo.ambientLight[3] = ambientLight_[3];
            wgpuQueueWriteBuffer(queue_, uniformBuffer_, 0, &ubo, sizeof(ubo));
        }
        if (!customTargetView_ && currentBackBufferView_) {
            wgpuTextureViewRelease(currentBackBufferView_);
        }
        currentBackBufferView_ = nullptr;
        if (currentSurfaceTexture_.texture) {
            wgpuTextureRelease(currentSurfaceTexture_.texture);
            currentSurfaceTexture_.texture = nullptr;
        }
        isFirstBatchOfFrame_ = true;
    }

    void updateFontTexture(int x, int y, int w, int h, const unsigned char* data, int atlasW, int atlasH) override {
        if (!device_ || !fontTexture_ || !data || w <= 0 || h <= 0) return;

        WGPUTexelCopyTextureInfo dst{};
        dst.texture = fontTexture_;
        dst.mipLevel = 0;
        dst.origin = WGPUOrigin3D{ static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0 };
        dst.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout{};
        layout.offset = static_cast<uint64_t>(y * atlasW + x);
        layout.bytesPerRow = static_cast<uint32_t>(atlasW);
        layout.rowsPerImage = static_cast<uint32_t>(atlasH);

        WGPUExtent3D writeSize{ static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        size_t totalDataSize = static_cast<size_t>(atlasW * atlasH);

        wgpuQueueWriteTexture(queue_, &dst, data, totalDataSize, &layout, &writeSize);
    }

    void renderBatch(const std::vector<Vertex2D>& vertices) override {
        if (vertices.empty() || !device_ || !pipeline_) return;
        if (!customTargetView_ && !surface_) return;

        if (customTargetView_) {
            currentBackBufferView_ = customTargetView_;
        } else if (!currentBackBufferView_) {
            wgpuSurfaceGetCurrentTexture(surface_, &currentSurfaceTexture_);
            if (currentSurfaceTexture_.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
                currentSurfaceTexture_.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
                return;
            }

            WGPUTextureViewDescriptor viewDesc{};
            viewDesc.format = surfaceFormat_;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.baseMipLevel = 0;
            viewDesc.mipLevelCount = 1;
            viewDesc.baseArrayLayer = 0;
            viewDesc.arrayLayerCount = 1;
            viewDesc.aspect = WGPUTextureAspect_All;

            currentBackBufferView_ = wgpuTextureCreateView(currentSurfaceTexture_.texture, &viewDesc);
            if (!currentBackBufferView_) return;
        }

        WGPUCommandEncoderDescriptor encDesc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encDesc);

        WGPURenderPassColorAttachment colorAttachment{};
        bool useMsaa = (sampleCount_ > 1 && msaaTextureView_ != nullptr);
        if (useMsaa) {
            colorAttachment.view = msaaTextureView_;
            colorAttachment.resolveTarget = currentBackBufferView_;
        } else {
            colorAttachment.view = currentBackBufferView_;
            colorAttachment.resolveTarget = nullptr;
        }
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachment.loadOp = isFirstBatchOfFrame_ ? WGPULoadOp_Clear : WGPULoadOp_Load;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = WGPUColor{0.08, 0.09, 0.12, 1.0}; // DAW Dark theme clear

        isFirstBatchOfFrame_ = false;

        WGPURenderPassDescriptor passDesc{};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
        if (bindGroup_) {
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
        }

        if (hasScissor_ && scissorW_ > 0 && scissorH_ > 0) {
            uint32_t sx = std::min(scissorX_, width_ > 0 ? width_ - 1 : 0);
            uint32_t sy = std::min(scissorY_, height_ > 0 ? height_ - 1 : 0);
            uint32_t sw = std::min(scissorW_, width_ - sx);
            uint32_t sh = std::min(scissorH_, height_ - sy);
            if (sw > 0 && sh > 0) {
                wgpuRenderPassEncoderSetScissorRect(pass, sx, sy, sw, sh);
            }
        }

        // Dynamic vertex upload
        uint64_t bufferSize = vertices.size() * sizeof(Vertex2D);
        if (bufferSize > 0) {
            if (bufferSize > vertexBufferSize_ || !vertexBuffer_) {
                if (vertexBuffer_) {
                    wgpuBufferRelease(vertexBuffer_);
                    vertexBuffer_ = nullptr;
                }
                uint64_t newCap = std::max<uint64_t>(bufferSize, 65536ULL);
                if (newCap < vertexBufferSize_ * 2) {
                    newCap = vertexBufferSize_ * 2;
                }
                WGPUBufferDescriptor bufDesc{};
                bufDesc.size = newCap;
                bufDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
                vertexBuffer_ = wgpuDeviceCreateBuffer(device_, &bufDesc);
                vertexBufferSize_ = newCap;
            }
            wgpuQueueWriteBuffer(queue_, vertexBuffer_, 0, vertices.data(), bufferSize);

            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer_, 0, bufferSize);
            wgpuRenderPassEncoderDraw(pass, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
        }

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        WGPUCommandBufferDescriptor cmdBufDesc{};
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &cmdBufDesc);
        wgpuCommandEncoderRelease(encoder);

        wgpuQueueSubmit(queue_, 1, &commands);
        wgpuCommandBufferRelease(commands);
    }

    void renderRgbaRegion(float x, float y, float w, float h, const uint8_t* rgba, int imgW, int imgH,
                          float u0, float v0, float u1, float v1, float opacity) override {
        WGPUTexture targetTex = bgTexture_ ? bgTexture_ : rgbaTexture_;
        if (!device_ || !targetTex || !rgba || imgW <= 0 || imgH <= 0 || w <= 0.0f || h <= 0.0f) return;

        uint32_t uploadW = static_cast<uint32_t>(std::min(imgW, 2048));
        uint32_t uploadH = static_cast<uint32_t>(std::min(imgH, 2048));

        // Upload to GPU texture only when source bitmap or dimensions change
        if (rgba != lastBgRgba_ || uploadW != lastBgUploadW_ || uploadH != lastBgUploadH_) {
            lastBgRgba_ = rgba;
            lastBgUploadW_ = uploadW;
            lastBgUploadH_ = uploadH;

            WGPUTexelCopyTextureInfo dst{};
            dst.texture = targetTex;
            dst.mipLevel = 0;
            dst.origin = WGPUOrigin3D{ 0, 0, 0 };
            dst.aspect = WGPUTextureAspect_All;

            uint32_t unalignedBytesPerRow = uploadW * 4;
            uint32_t alignedBytesPerRow = (unalignedBytesPerRow + 255u) & ~255u;

            WGPUTexelCopyBufferLayout layout{};
            layout.offset = 0;
            layout.bytesPerRow = alignedBytesPerRow;
            layout.rowsPerImage = uploadH;

            WGPUExtent3D writeSize{ uploadW, uploadH, 1 };

            if (alignedBytesPerRow == static_cast<uint32_t>(imgW * 4) && uploadW == static_cast<uint32_t>(imgW)) {
                wgpuQueueWriteTexture(queue_, &dst, rgba, static_cast<size_t>(alignedBytesPerRow) * uploadH, &layout, &writeSize);
            } else {
                size_t totalAlignedSize = static_cast<size_t>(alignedBytesPerRow) * uploadH;
                if (bgAlignedUploadBuf_.size() < totalAlignedSize) {
                    bgAlignedUploadBuf_.resize(totalAlignedSize);
                }
                for (uint32_t row = 0; row < uploadH; ++row) {
                    std::memcpy(
                        bgAlignedUploadBuf_.data() + static_cast<size_t>(row) * alignedBytesPerRow,
                        rgba + static_cast<size_t>(row) * (static_cast<size_t>(imgW) * 4),
                        unalignedBytesPerRow
                    );
                }
                wgpuQueueWriteTexture(queue_, &dst, bgAlignedUploadBuf_.data(), totalAlignedSize, &layout, &writeSize);
            }
        }

        float maxU = static_cast<float>(uploadW) / 2048.0f;
        float maxV = static_cast<float>(uploadH) / 2048.0f;
        uint8_t aByte = static_cast<uint8_t>(std::clamp(opacity * 255.0f, 0.0f, 255.0f));
        uint32_t col = (static_cast<uint32_t>(aByte) << 24) | 0x00FFFFFF;

        float qU0 = u0 * maxU;
        float qV0 = v0 * maxV;
        float qU1 = u1 * maxU;
        float qV1 = v1 * maxV;
        // Mode bit 16u enables uniform-based 3D scene lighting (radial pool + ambient + scene darkness)
        uint32_t vMode = (bgTexture_ ? VertexMode::BgTexture : 3u) | 16u;

        std::vector<Vertex2D> quad(6);
        quad[0] = { x,     y,     qU0, qV0, col, vMode, {0.0f, 0.0f} };
        quad[1] = { x + w, y,     qU1, qV0, col, vMode, {0.0f, 0.0f} };
        quad[2] = { x + w, y + h, qU1, qV1, col, vMode, {0.0f, 0.0f} };
        quad[3] = { x,     y,     qU0, qV0, col, vMode, {0.0f, 0.0f} };
        quad[4] = { x + w, y + h, qU1, qV1, col, vMode, {0.0f, 0.0f} };
        quad[5] = { x,     y + h, qU0, qV1, col, vMode, {0.0f, 0.0f} };

        renderBatch(quad);
    }

    void renderRgba(float x, float y, float w, float h, const uint8_t* rgba, int imgW, int imgH, float opacity) override {
        renderRgbaRegion(x, y, w, h, rgba, imgW, imgH, 0.0f, 0.0f, 1.0f, 1.0f, opacity);
    }

    void updateRgbaTexture(int x, int y, int w, int h, const uint8_t* data, int atlasW, int atlasH) override {
        if (!device_ || !rgbaTexture_ || !data || w <= 0 || h <= 0) return;

        WGPUTexelCopyTextureInfo dst{};
        dst.texture = rgbaTexture_;
        dst.mipLevel = 0;
        dst.origin = WGPUOrigin3D{ static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0 };
        dst.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout{};
        layout.offset = 0;
        layout.bytesPerRow = static_cast<uint32_t>(atlasW * 4);
        layout.rowsPerImage = static_cast<uint32_t>(atlasH);

        WGPUExtent3D writeSize{ static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        size_t totalDataSize = static_cast<size_t>(atlasW * atlasH * 4);

        wgpuQueueWriteTexture(queue_, &dst, data, totalDataSize, &layout, &writeSize);
    }

    void endPass() override {
#if !defined(__EMSCRIPTEN__)
        if (surface_) {
            wgpuSurfacePresent(surface_);
        }
#endif
        if (!customTargetView_ && currentBackBufferView_) {
            wgpuTextureViewRelease(currentBackBufferView_);
        }
        currentBackBufferView_ = nullptr;
        if (currentSurfaceTexture_.texture) {
            wgpuTextureRelease(currentSurfaceTexture_.texture);
            currentSurfaceTexture_.texture = nullptr;
        }
        if (instance_) {
            wgpuInstanceProcessEvents(instance_);
        }
    }

    RenderBackendType getBackendType() const noexcept override {
        return RenderBackendType::WebGPU;
    }

    void* getNativeDevice() const noexcept override {
        return static_cast<void*>(device_);
    }

    void* getNativeSurface() const noexcept override {
        return static_cast<void*>(surface_);
    }

    void setCustomRenderTargetView(void* view) override {
        customTargetView_ = static_cast<WGPUTextureView>(view);
    }

    void setGlobalLight(const LightSource2D& light,
                        float colR, float colG, float colB,
                        float ambR, float ambG, float ambB,
                        float sceneDarkness, float lightStrength) override {
        globalLight_ = light;
        lightColor_[0] = colR;
        lightColor_[1] = colG;
        lightColor_[2] = colB;
        lightColor_[3] = lightStrength;
        ambientLight_[0] = ambR;
        ambientLight_[1] = ambG;
        ambientLight_[2] = ambB;
        ambientLight_[3] = sceneDarkness;
        if (device_ && uniformBuffer_ && width_ > 0 && height_ > 0) {
            GpuUniforms ubo{};
            ubo.viewport[0] = static_cast<float>(width_);
            ubo.viewport[1] = static_cast<float>(height_);
            ubo.gamma = isSrgbSurface_ ? 2.2f : 1.0f;
            ubo.time = shaderTime_;
            ubo.lightPos[0] = globalLight_.x;
            ubo.lightPos[1] = globalLight_.y;
            ubo.lightPos[2] = globalLight_.z;
            ubo.lightIntensity = globalLight_.intensity;
            ubo.lightColor[0] = lightColor_[0];
            ubo.lightColor[1] = lightColor_[1];
            ubo.lightColor[2] = lightColor_[2];
            ubo.lightColor[3] = lightColor_[3];
            ubo.ambientLight[0] = ambientLight_[0];
            ubo.ambientLight[1] = ambientLight_[1];
            ubo.ambientLight[2] = ambientLight_[2];
            ubo.ambientLight[3] = ambientLight_[3];
            wgpuQueueWriteBuffer(queue_, uniformBuffer_, 0, &ubo, sizeof(ubo));
        }
    }

    void setShaderTime(float timeSeconds) override {
        shaderTime_ = timeSeconds;
    }

private:
    void initShadersAndPipeline() {
        static const char* wgslSource = R"(
            struct Uniforms {
                viewport: vec2<f32>,
                gamma: f32,
                time: f32,
                lightPos: vec3<f32>,
                lightIntensity: f32,
                lightColor: vec4<f32>,
                ambientLight: vec4<f32>,
            };

            @group(0) @binding(0) var fontTexture: texture_2d<f32>;
            @group(0) @binding(1) var fontSampler: sampler;
            @group(0) @binding(2) var<uniform> uniforms: Uniforms;
            @group(0) @binding(3) var rgbaTexture: texture_2d<f32>;
            @group(0) @binding(4) var bgTexture: texture_2d<f32>;

            struct VertexInput {
                @location(0) position: vec2<f32>,
                @location(1) uv: vec2<f32>,
                @location(2) color: u32,
                @location(3) mode: u32,
                @location(4) pad: vec2<f32>,
                @location(5) localUV: vec2<f32>,
            };

            struct VertexOutput {
                @builtin(position) clip_position: vec4<f32>,
                @location(0) uv: vec2<f32>,
                @location(1) color: vec4<f32>,
                @location(2) @interpolate(flat) mode: u32,
                @location(3) localPos: vec2<f32>,
                @location(4) @interpolate(flat) halfSize: vec2<f32>,
                @location(5) @interpolate(flat) radius: f32,
                @location(6) localUV: vec2<f32>,
                @location(7) worldPos: vec2<f32>,
            };

            @vertex
            fn vs_main(in: VertexInput) -> VertexOutput {
                var out: VertexOutput;
                let c = in.color;
                let r = f32(c & 255u) / 255.0;
                let g = f32((c >> 8u) & 255u) / 255.0;
                let b = f32((c >> 16u) & 255u) / 255.0;
                let a = f32((c >> 24u) & 255u) / 255.0;
                out.color = vec4<f32>(r, g, b, a);
                out.uv = in.uv;
                out.mode = in.mode;
                out.localPos = in.pad;
                out.halfSize = abs(in.pad);
                out.radius = in.uv.x;
                out.localUV = in.localUV;
                out.worldPos = in.position;

                // Screen pixels to NDC [-1, 1] using dynamic viewport
                let ndcX = (in.position.x / uniforms.viewport.x) * 2.0 - 1.0;
                let ndcY = 1.0 - (in.position.y / uniforms.viewport.y) * 2.0;
                out.clip_position = vec4<f32>(ndcX, ndcY, 0.0, 1.0);
                return out;
            }

            fn evalShadowSdf(pos: vec2<f32>, baseHalf: vec2<f32>, baseRad: f32, rawRad: f32) -> f32 {
                if (rawRad <= 0.05) {
                    let q = abs(pos) - baseHalf;
                    let outsideDist = mix(max(q.x, q.y), length(max(q, vec2<f32>(0.0, 0.0))), 0.30);
                    return select(max(q.x, q.y), outsideDist, q.x > 0.0 || q.y > 0.0);
                }
                let q = abs(pos) - baseHalf + vec2<f32>(baseRad, baseRad);
                return length(max(q, vec2<f32>(0.0, 0.0))) + min(max(q.x, q.y), 0.0) - baseRad;
            }

            @fragment
            fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
                var col = in.color;
                let baseMode = in.mode & 15u;
                let isSceneLit = (in.mode & 16u) != 0u;
                if (baseMode == 1u || baseMode == 4u) {
                    let alpha = textureSampleLevel(fontTexture, fontSampler, in.uv, 0.0).r;
                    col = vec4<f32>(in.color.rgb, in.color.a * alpha);
                } else if (baseMode == 2u) {
                    let outset = in.uv.y;
                    var finalAlpha = 0.0;
                    if (outset > 0.5) {
                        let baseHalf = max(vec2<f32>(0.0, 0.0), in.halfSize - vec2<f32>(outset, outset));
                        let baseRad = min(in.radius, min(baseHalf.x, baseHalf.y));
                        let innerSoft = outset * 0.35;
                        let invSpan = 1.0 / (outset + innerSoft);
                        let sdf = evalShadowSdf(in.localPos, baseHalf, baseRad, in.radius);
                        let t = clamp((outset - sdf) * invSpan, 0.0, 1.0);
                        let alpha = t * t * (3.0 - 2.0 * t);
                        var aCurr = in.color.a * alpha;

                        if (abs(in.localUV.x) < 9000.0 && aCurr > 0.001) {
                            let prevOff = in.localUV;
                            let sdfPrev = evalShadowSdf(in.localPos - prevOff, baseHalf, baseRad, in.radius);
                            let tPrev = clamp((outset - sdfPrev) * invSpan, 0.0, 1.0);
                            let alphaPrev = tPrev * tPrev * (3.0 - 2.0 * tPrev);
                            let aPrev = in.color.a * alphaPrev;
                            aCurr = max(0.0, aCurr - aPrev) / max(0.001, 1.0 - aPrev);
                        }
                        finalAlpha = aCurr;
                    } else {
                        let q = abs(in.localPos) - in.halfSize + vec2<f32>(in.radius, in.radius);
                        let dist = length(max(q, vec2<f32>(0.0, 0.0))) + min(max(q.x, q.y), 0.0) - in.radius;
                        finalAlpha = in.color.a * clamp(0.5 - dist, 0.0, 1.0);
                    }
                    if (finalAlpha <= 0.001) {
                        discard;
                    }
                    col = vec4<f32>(in.color.rgb, finalAlpha);
                } else if (baseMode == 3u) {
                    var sampleUV = in.uv;
                    let distortAmt = in.localPos.x;
                    if (distortAmt > 0.0001) {
                        let p = in.uv * 120.0;
                        let sVal = sin(p.x * 2.1 + p.y * 3.7) * cos(p.x * 1.3 - p.y * 2.9);
                        let scratchBand = abs(sin(p.x * 0.4 + p.y * 0.9 + sVal * 1.2));
                        if (scratchBand < 0.16) {
                            let dFade = (0.16 - scratchBand) * distortAmt;
                            let gradDir = vec2<f32>(cos(p.y * 4.0), sin(p.x * 4.0));
                            sampleUV = sampleUV + gradDir * dFade;
                        }
                    }
                    let tex = textureSampleLevel(rgbaTexture, fontSampler, sampleUV, 0.0);
                    col = tex * in.color;

                    // Procedural GPU Card Stock & Wear Conditioning
                    let cond = in.localPos.y;
                    if (cond > 0.05) {
                        let stockIdx = floor(cond / 10.0);
                        let wearLvl = cond - stockIdx * 10.0;

                        // 1. Paper Stock Procedural Finish
                        if (stockIdx >= 0.9 && stockIdx < 1.9) {
                            // Cambric Linen: woven micro-embossing grid
                            let linen = (sin(in.localUV.x * 320.0) * sin(in.localUV.y * 320.0)) * 0.042;
                            col = vec4<f32>(col.rgb * (1.0 + linen), col.a);
                        } else if (stockIdx >= 1.9 && stockIdx < 2.9) {
                            // Antique Laid: horizontal wire ribbing with chain lines
                            let laid = (sin(in.localUV.y * 420.0) * 0.038) + (sin(in.localUV.x * 28.0) * 0.018);
                            col = vec4<f32>(col.rgb * (1.0 + laid), col.a);
                        } else if (stockIdx >= 2.9 && stockIdx < 3.9) {
                            // Cotton Tooth: micro-fiber tooth grain
                            let toothSeed = in.localUV * 460.0;
                            let toothN = fract(sin(dot(toothSeed, vec2<f32>(12.9898, 78.233))) * 43758.5453);
                            col = vec4<f32>(col.rgb * (1.0 + (toothN - 0.5) * 0.06), col.a);
                        } else if (stockIdx >= 3.9 && stockIdx < 4.9) {
                            // Smooth Satin: soft luminous sheen
                            let satinGrad = 1.0 - length(in.localUV - vec2<f32>(0.5)) * 0.7;
                            col = vec4<f32>(col.rgb * (0.98 + 0.04 * satinGrad), col.a);
                        }

                        // 2. Physical Card Wear Patina & Aging
                        if (wearLvl >= 0.8) {
                            let dCenter = length((in.localUV - vec2<f32>(0.5)) * vec2<f32>(1.0, 1.4));
                            let vigStrength = select(0.12, 0.24, wearLvl >= 1.8);
                            let vignette = pow(clamp(dCenter, 0.0, 1.0), 2.2) * vigStrength;
                            let sepiaTint = vec3<f32>(1.0 - vignette * 0.35, 1.0 - vignette * 0.58, 1.0 - vignette * 0.88);
                            col = vec4<f32>(col.rgb * sepiaTint, col.a);
                        }

                        // 3. Vintage Foxing Specks & Handling Crease
                        if (wearLvl >= 1.8) {
                            let foxCell = floor(in.localUV * 24.0);
                            let foxH = fract(sin(dot(foxCell, vec2<f32>(37.17, 71.93))) * 23421.631);
                            if (foxH > 0.94) {
                                let dSpot = length(fract(in.localUV * 24.0) - vec2<f32>(0.5));
                                if (dSpot < 0.26) {
                                    let spotCore = (1.0 - dSpot / 0.26) * 0.24;
                                    let spotTint = vec3<f32>(1.0 - spotCore * 0.40, 1.0 - spotCore * 0.65, 1.0 - spotCore * 0.92);
                                    col = vec4<f32>(col.rgb * spotTint, col.a);
                                }
                            }
                            let creaseDist = abs(in.localUV.y - 0.44 - (in.localUV.x - 0.5) * 0.18);
                            if (creaseDist < 0.009) {
                                let fold = (1.0 - creaseDist / 0.009) * 0.05;
                                col = vec4<f32>(col.rgb * (1.0 + fold), col.a);
                            }
                        }
                    }
                } else if (baseMode == 6u) {
                    let tex = textureSampleLevel(bgTexture, fontSampler, in.uv, 0.0);
                    col = tex * in.color;
                }
                if (isSceneLit && !(baseMode == 2u && in.uv.y > 0.5)) {
                    let darkness = uniforms.ambientLight.w;
                    let strength = uniforms.lightColor.w * uniforms.lightIntensity;
                    let dxy = in.worldPos - uniforms.lightPos.xy;
                    let refR = max(300.0, max(uniforms.viewport.x, uniforms.viewport.y) * 0.62);
                    let r2 = dot(dxy, dxy) / (refR * refR);
                    let falloff = 1.0 / (1.0 + 1.25 * r2);
                    let baseAmb = 1.0 - darkness;
                    let lightPool = (darkness * strength + (strength - 1.0) * 0.24) * falloff;
                    let rawGain = clamp(baseAmb + lightPool, 0.06, 1.45);
                    var gain = rawGain;
                    var specHighlight = vec3<f32>(0.0);
                    if (baseMode == 3u) {
                        gain = mix(1.0, rawGain, 0.65);

                        // Real-world card camber (subtle longitudinal & lateral curvature)
                        let uCenter = in.localUV.x - 0.5;
                        let vCenter = in.localUV.y - 0.5;

                        // Camber scale driven by card wear setting
                        let cond = in.localPos.y;
                        let stockIdx = floor(cond / 10.0);
                        let wearVal = cond - stockIdx * 10.0;
                        // Pristine cards are flat (0.05); Natural is subtle (0.35); Played (0.80); Vintage (1.30)
                        let camberScale = select(0.05, select(0.35, select(0.80, 1.30, wearVal >= 2.5), wearVal >= 1.5), wearVal >= 0.5);

                        // Surface normal with subtle convex arch across the card face
                        let normX = -uCenter * (0.22 * camberScale);
                        let normY = -vCenter * (0.12 * camberScale);
                        let cardNorm = normalize(vec3<f32>(normX, normY, 1.0));

                        // 3D vector to dynamic light source
                        let toLight = vec3<f32>(uniforms.lightPos.xy - in.worldPos, max(140.0, uniforms.lightPos.z));
                        let lightDir = normalize(toLight);
                        let nDotL = max(0.0, dot(cardNorm, lightDir));

                        // Half-vector for semi-gloss cardstock specular reflection
                        let viewDir = vec3<f32>(0.0, 0.0, 1.0);
                        let halfDir = normalize(lightDir + viewDir);
                        let nDotH = max(0.0, dot(cardNorm, halfDir));
                        let specPower = mix(45.0, 22.0, clamp(camberScale * 0.7, 0.0, 1.0));
                        let specIntensity = pow(nDotH, specPower) * 0.15 * strength * falloff;
                        specHighlight = uniforms.lightColor.rgb * specIntensity;

                        // Modulate gain by physical curvature so edges shade naturally
                        gain = gain * mix(1.0 - 0.07 * camberScale, 1.0 + 0.07 * camberScale, nDotL);
                    }
                    let tint = mix(vec3<f32>(1.0, 1.0, 1.0), uniforms.lightColor.rgb, clamp(falloff * 0.40 * strength, 0.0, 1.0));
                    col = vec4<f32>(clamp(col.rgb * gain * tint + specHighlight, vec3<f32>(0.0), vec3<f32>(1.0)), col.a);
                }
                if (uniforms.gamma > 1.05) {
                    return vec4<f32>(pow(col.rgb, vec3<f32>(uniforms.gamma)), col.a);
                }
                return col;
            }
        )";

        WGPUShaderSourceWGSL wgslDesc{};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code = makeWgpuStringView(wgslSource);

        WGPUShaderModuleDescriptor smDesc{};
        smDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
        WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device_, &smDesc);

        // Vertex layout
        WGPUVertexAttribute attribs[6]{};
        attribs[0].format = WGPUVertexFormat_Float32x2; // position
        attribs[0].offset = 0;
        attribs[0].shaderLocation = 0;

        attribs[1].format = WGPUVertexFormat_Float32x2; // uv
        attribs[1].offset = offsetof(Vertex2D, u);
        attribs[1].shaderLocation = 1;

        attribs[2].format = WGPUVertexFormat_Uint32;     // color
        attribs[2].offset = offsetof(Vertex2D, color);
        attribs[2].shaderLocation = 2;

        attribs[3].format = WGPUVertexFormat_Uint32;     // mode
        attribs[3].offset = offsetof(Vertex2D, mode);
        attribs[3].shaderLocation = 3;

        attribs[4].format = WGPUVertexFormat_Float32x2; // pad / local coordinates
        attribs[4].offset = offsetof(Vertex2D, pad);
        attribs[4].shaderLocation = 4;

        attribs[5].format = WGPUVertexFormat_Float32x2; // localUV for edge bevels & rim lighting
        attribs[5].offset = offsetof(Vertex2D, localUV);
        attribs[5].shaderLocation = 5;

        WGPUVertexBufferLayout vbLayout{};
        vbLayout.arrayStride = sizeof(Vertex2D);
        vbLayout.stepMode = WGPUVertexStepMode_Vertex;
        vbLayout.attributeCount = 6;
        vbLayout.attributes = attribs;

        WGPURenderPipelineDescriptor plDesc{};
        plDesc.vertex.module = shaderModule;
        plDesc.vertex.entryPoint = makeWgpuStringView("vs_main");
        plDesc.vertex.bufferCount = 1;
        plDesc.vertex.buffers = &vbLayout;

        WGPUColorTargetState colorTarget{};
        colorTarget.format = surfaceFormat_;
        
        WGPUBlendState blend{};
        blend.color.operation = WGPUBlendOperation_Add;
        blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blend.alpha.operation = WGPUBlendOperation_Add;
        blend.alpha.srcFactor = WGPUBlendFactor_One;
        blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        colorTarget.blend = &blend;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment{};
        fragment.module = shaderModule;
        fragment.entryPoint = makeWgpuStringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;

        plDesc.fragment = &fragment;
        plDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        plDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
        plDesc.primitive.frontFace = WGPUFrontFace_CCW;
        plDesc.primitive.cullMode = WGPUCullMode_None;
        plDesc.multisample.count = 1;
        plDesc.multisample.mask = ~0u;
        plDesc.multisample.alphaToCoverageEnabled = false;

        pipeline1x_ = wgpuDeviceCreateRenderPipeline(device_, &plDesc);

        plDesc.multisample.count = 4;
        pipeline4x_ = wgpuDeviceCreateRenderPipeline(device_, &plDesc);

        // 1. Create font atlas texture (1024x1024 R8Unorm)
        WGPUTextureDescriptor texDesc{};
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.size = WGPUExtent3D{ 1024, 1024, 1 };
        texDesc.format = WGPUTextureFormat_R8Unorm;
        texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        fontTexture_ = wgpuDeviceCreateTexture(device_, &texDesc);

        WGPUTextureViewDescriptor tvDesc{};
        tvDesc.format = WGPUTextureFormat_R8Unorm;
        tvDesc.dimension = WGPUTextureViewDimension_2D;
        tvDesc.baseMipLevel = 0;
        tvDesc.mipLevelCount = 1;
        tvDesc.baseArrayLayer = 0;
        tvDesc.arrayLayerCount = 1;
        tvDesc.aspect = WGPUTextureAspect_All;
        fontTextureView_ = wgpuTextureCreateView(fontTexture_, &tvDesc);

        // 2. Create RGBA texture for rasterized SVG bitmaps & icons & card atlas (4096x4096 RGBA8Unorm)
        WGPUTextureDescriptor rgbaTexDesc{};
        rgbaTexDesc.dimension = WGPUTextureDimension_2D;
        rgbaTexDesc.size = WGPUExtent3D{ 4096, 4096, 1 };
        rgbaTexDesc.format = WGPUTextureFormat_RGBA8Unorm;
        rgbaTexDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        rgbaTexDesc.mipLevelCount = 1;
        rgbaTexDesc.sampleCount = 1;
        rgbaTexture_ = wgpuDeviceCreateTexture(device_, &rgbaTexDesc);

        WGPUTextureViewDescriptor rtvDesc{};
        rtvDesc.format = WGPUTextureFormat_RGBA8Unorm;
        rtvDesc.dimension = WGPUTextureViewDimension_2D;
        rtvDesc.baseMipLevel = 0;
        rtvDesc.mipLevelCount = 1;
        rtvDesc.baseArrayLayer = 0;
        rtvDesc.arrayLayerCount = 1;
        rtvDesc.aspect = WGPUTextureAspect_All;
        rgbaTextureView_ = wgpuTextureCreateView(rgbaTexture_, &rtvDesc);

        // 3. Create dedicated Background / Large Surface Texture (2048x2048 RGBA8Unorm)
        WGPUTextureDescriptor bgTexDesc = rgbaTexDesc;
        bgTexDesc.size = WGPUExtent3D{ 2048, 2048, 1 };
        bgTexture_ = wgpuDeviceCreateTexture(device_, &bgTexDesc);
        bgTextureView_ = wgpuTextureCreateView(bgTexture_, &rtvDesc);

        WGPUSamplerDescriptor sampDesc{};
        sampDesc.addressModeU = WGPUAddressMode_ClampToEdge;
        sampDesc.addressModeV = WGPUAddressMode_ClampToEdge;
        sampDesc.addressModeW = WGPUAddressMode_ClampToEdge;
        sampDesc.magFilter = WGPUFilterMode_Linear;
        sampDesc.minFilter = WGPUFilterMode_Linear;
        sampDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
        sampDesc.lodMinClamp = 0.0f;
        sampDesc.lodMaxClamp = 32.0f;
        sampDesc.maxAnisotropy = 1;
        fontSampler_ = wgpuDeviceCreateSampler(device_, &sampDesc);

        wgpuShaderModuleRelease(shaderModule);

        // Uniform buffer for viewport, lighting, and time upload
        WGPUBufferDescriptor uboDesc{};
        uboDesc.size = 64;
        uboDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uniformBuffer_ = wgpuDeviceCreateBuffer(device_, &uboDesc);
        GpuUniforms initUbo{};
        initUbo.viewport[0] = static_cast<float>(width_);
        initUbo.viewport[1] = static_cast<float>(height_);
        initUbo.gamma = isSrgbSurface_ ? 2.2f : 1.0f;
        initUbo.time = 0.0f;
        initUbo.lightPos[0] = globalLight_.x;
        initUbo.lightPos[1] = globalLight_.y;
        initUbo.lightPos[2] = globalLight_.z;
        initUbo.lightIntensity = globalLight_.intensity;
        initUbo.lightColor[0] = lightColor_[0];
        initUbo.lightColor[1] = lightColor_[1];
        initUbo.lightColor[2] = lightColor_[2];
        initUbo.lightColor[3] = lightColor_[3];
        initUbo.ambientLight[0] = ambientLight_[0];
        initUbo.ambientLight[1] = ambientLight_[1];
        initUbo.ambientLight[2] = ambientLight_[2];
        initUbo.ambientLight[3] = ambientLight_[3];
        wgpuQueueWriteBuffer(queue_, uniformBuffer_, 0, &initUbo, sizeof(initUbo));

        // Bind group matching layout (font texture, sampler, uniforms, rgba card atlas, bg texture)
        WGPUBindGroupEntry entries[5]{};
        entries[0].binding = 0;
        entries[0].textureView = fontTextureView_;
        entries[1].binding = 1;
        entries[1].sampler = fontSampler_;
        entries[2].binding = 2;
        entries[2].buffer = uniformBuffer_;
        entries[2].offset = 0;
        entries[2].size = 64;
        entries[3].binding = 3;
        entries[3].textureView = rgbaTextureView_;
        entries[4].binding = 4;
        entries[4].textureView = bgTextureView_;

        WGPUBindGroupDescriptor bgDesc{};
        if (pipeline1x_) {
            WGPUBindGroupLayout bgLayout1x = wgpuRenderPipelineGetBindGroupLayout(pipeline1x_, 0);
            bgDesc.layout = bgLayout1x;
            bgDesc.entryCount = 5;
            bgDesc.entries = entries;
            bindGroup1x_ = wgpuDeviceCreateBindGroup(device_, &bgDesc);
            wgpuBindGroupLayoutRelease(bgLayout1x);
        }

        if (pipeline4x_) {
            WGPUBindGroupLayout bgLayout4x = wgpuRenderPipelineGetBindGroupLayout(pipeline4x_, 0);
            bgDesc.layout = bgLayout4x;
            bgDesc.entryCount = 5;
            bgDesc.entries = entries;
            bindGroup4x_ = wgpuDeviceCreateBindGroup(device_, &bgDesc);
            wgpuBindGroupLayoutRelease(bgLayout4x);
        }

        updateActivePipeline();
        reallocateMsaaTexture(width_, height_);
    }

    uint32_t getFramebufferWidth() const noexcept override { return width_; }
    uint32_t getFramebufferHeight() const noexcept override { return height_; }

    WGPUInstance instance_{nullptr};
    WGPUAdapter adapter_{nullptr};
    WGPUDevice device_{nullptr};
    WGPUQueue queue_{nullptr};
    WGPUBuffer vertexBuffer_{nullptr};
    uint64_t vertexBufferSize_{0};
    WGPUSurface surface_{nullptr};
    WGPUTextureFormat surfaceFormat_{WGPUTextureFormat_BGRA8Unorm};
    bool isSrgbSurface_{false};
    WGPURenderPipeline pipeline1x_{nullptr};
    WGPURenderPipeline pipeline4x_{nullptr};
    WGPURenderPipeline pipeline_{nullptr};
    WGPUTexture msaaTexture_{nullptr};
    WGPUTextureView msaaTextureView_{nullptr};
    WGPUTexture fontTexture_{nullptr};
    WGPUTextureView fontTextureView_{nullptr};
    WGPUSampler fontSampler_{nullptr};
    WGPUTexture rgbaTexture_{nullptr};
    WGPUTextureView rgbaTextureView_{nullptr};
    WGPUTexture bgTexture_{nullptr};
    WGPUTextureView bgTextureView_{nullptr};
    WGPUBuffer uniformBuffer_{nullptr};
    LightSource2D globalLight_{640.0f, 200.0f, 550.0f, 1.0f};
    float lightColor_[4]{1.04f, 1.00f, 0.94f, 1.0f};
    float ambientLight_[4]{0.35f, 0.35f, 0.35f, 1.0f};
    float shaderTime_{0.0f};
    WGPUBindGroup bindGroup1x_{nullptr};
    WGPUBindGroup bindGroup4x_{nullptr};
    WGPUBindGroup bindGroup_{nullptr};
    WGPUSurfaceTexture currentSurfaceTexture_{};
    WGPUTextureView currentBackBufferView_{nullptr};
    WGPUTextureView customTargetView_{nullptr};
    bool isFirstBatchOfFrame_{true};
    uint32_t width_{1280};
    uint32_t height_{800};
    int antiAliasingMode_{2};
    uint32_t sampleCount_{4};
    bool hasScissor_{false};
    uint32_t scissorX_{0};
    uint32_t scissorY_{0};
    uint32_t scissorW_{0};
    uint32_t scissorH_{0};

    const uint8_t* lastBgRgba_{nullptr};
    uint32_t lastBgUploadW_{0};
    uint32_t lastBgUploadH_{0};
    std::vector<uint8_t> bgAlignedUploadBuf_;
};
#endif

// ============================================================================
// BatchRenderer2D Frontend
// ============================================================================
BatchRenderer2D::BatchRenderer2D() {
    vertices_.reserve(32768);
    std::lock_guard<std::mutex> lock(s_activeRendererMutex);
    if (!s_activeRenderer) {
        s_activeRenderer = this;
    }
}

BatchRenderer2D::~BatchRenderer2D() {
    {
        std::lock_guard<std::mutex> lock(s_activeRendererMutex);
        if (s_activeRenderer == this) {
            s_activeRenderer = nullptr;
        }
    }
    shutdown();
}

bool BatchRenderer2D::initialize(void* windowHandle, uint32_t width, uint32_t height, RenderBackendType backend) {
    physicalWidth_ = width;
    physicalHeight_ = height;
    currentWidth_ = static_cast<float>(width);
    currentHeight_ = static_cast<float>(height);

#if defined(__EMSCRIPTEN__) && defined(EATS_ENABLE_WEBGPU)
    if (backend == RenderBackendType::WebGPU && windowHandle != nullptr) {
        backend_ = std::make_unique<WebGpuBatchBackend>();
        bool ok = backend_->initialize(windowHandle, width, height);
        if (ok) {
            backend_->setAntiAliasingMode(antiAliasingMode_);
        }
        return ok;
    }
    return false;
#else
    if (backend == RenderBackendType::WebGPU) {
#if defined(EATS_ENABLE_WEBGPU)
        backend_ = std::make_unique<WebGpuBatchBackend>();
        if (backend_->initialize(windowHandle, width, height)) {
            backend_->setAntiAliasingMode(antiAliasingMode_);
            return true;
        }
#endif
    }

    backend_ = std::make_unique<FilamentBatchBackend>();
    bool ok = backend_->initialize(windowHandle, width, height);
    if (ok) {
        backend_->setAntiAliasingMode(antiAliasingMode_);
    }
    return ok;
#endif
}

void BatchRenderer2D::shutdown() {
    invalidateChromeCache();
    chromeCachePixels_.clear();
    textures_.clear();
    if (backend_) {
        backend_->shutdown();
        backend_.reset();
    }
    vertices_.clear();
}

void BatchRenderer2D::resize(uint32_t width, uint32_t height) {
    physicalWidth_ = width;
    physicalHeight_ = height;
    currentWidth_ = static_cast<float>(width);
    currentHeight_ = static_cast<float>(height);
    invalidateChromeCache();
    chromeCacheWidth_ = width;
    chromeCacheHeight_ = height;
    if (backend_) {
        backend_->resize(width, height);
    }
}

RenderBackendType BatchRenderer2D::getBackendType() const noexcept {
    return backend_ ? backend_->getBackendType() : RenderBackendType::Filament;
}

void BatchRenderer2D::beginFrame(float width, float height) {
    currentWidth_ = width;
    currentHeight_ = height;
    vertices_.clear();
    inFrame_ = true;
    blendMode_ = BlendMode::Normal;
    sceneLightingActive_ = false;
    sceneLitStartIdx_ = 0;
    scissorStack_.clear();
    resetRotation();

    if (backend_) {
        backend_->clearScissorRect();
        float passW = (physicalWidth_ > 0) ? static_cast<float>(physicalWidth_) : std::round(width * renderScaleX_);
        float passH = (physicalHeight_ > 0) ? static_cast<float>(physicalHeight_) : std::round(height * renderScaleY_);
        backend_->beginPass(passW, passH);
    }
}

void BatchRenderer2D::setSceneLightingActive(bool active) noexcept {
    if (sceneLightingActive_ == active) return;
    if (sceneLightingActive_) {
        for (size_t i = sceneLitStartIdx_; i < vertices_.size(); ++i) {
            vertices_[i].mode |= 16u;
        }
    }
    sceneLightingActive_ = active;
    sceneLitStartIdx_ = vertices_.size();
}

void BatchRenderer2D::setRotation(float angleDegrees, float originX, float originY) {
    if (std::abs(angleDegrees) < 0.001f) {
        resetRotation();
        return;
    }
    float rad = angleDegrees * 0.017453292519943295f; // deg to rad
    transformCos_ = std::cos(rad);
    transformSin_ = std::sin(rad);
    transformOriginX_ = originX;
    transformOriginY_ = originY;
    transformActive_ = true;
}

void BatchRenderer2D::resetRotation() noexcept {
    transformActive_ = false;
    transformCos_ = 1.0f;
    transformSin_ = 0.0f;
    transformOriginX_ = 0.0f;
    transformOriginY_ = 0.0f;
}

void BatchRenderer2D::endFrame() {
    if (!inFrame_) return;
    setSceneLightingActive(false);
    inFrame_ = false;
    resetRotation();

    if (backend_) {
        if (!vertices_.empty()) {
            if (std::abs(renderScaleX_ - 1.0f) > 0.001f || std::abs(renderScaleY_ - 1.0f) > 0.001f) {
                for (auto& v : vertices_) {
                    v.x *= renderScaleX_;
                    v.y *= renderScaleY_;
                    if ((v.mode & 15u) == 2u) {
                        v.u *= renderScaleX_;
                        v.v *= renderScaleX_;
                        v.pad[0] *= renderScaleX_;
                        v.pad[1] *= renderScaleY_;
                        if (std::abs(v.localUV[0]) < 9000.0f) {
                            v.localUV[0] *= renderScaleX_;
                            v.localUV[1] *= renderScaleY_;
                        }
                    }
                }
            }
            backend_->renderBatch(vertices_);
            vertices_.clear();
            sceneLitStartIdx_ = 0;
        }
        backend_->clearScissorRect();
        scissorStack_.clear();
        backend_->endPass();
    }
}

void BatchRenderer2D::setBlendMode(BlendMode mode) {
    if (blendMode_ == mode) return;
    flush();
    blendMode_ = mode;
    if (backend_) {
        backend_->setBlendMode(mode);
    }
}

void BatchRenderer2D::setGlobalLight(const LightSource2D& light,
                                    float colR, float colG, float colB,
                                    float ambR, float ambG, float ambB,
                                    float sceneDarkness, float lightStrength) {
    if (backend_) {
        backend_->setGlobalLight(light, colR, colG, colB, ambR, ambG, ambB, sceneDarkness, lightStrength);
    }
}

void BatchRenderer2D::setShaderTime(float timeSeconds) {
    if (backend_) {
        backend_->setShaderTime(timeSeconds);
    }
}

void BatchRenderer2D::flush() {
    if (!inFrame_ || !backend_ || vertices_.empty()) return;
    if (sceneLightingActive_) {
        for (size_t i = sceneLitStartIdx_; i < vertices_.size(); ++i) {
            vertices_[i].mode |= 16u;
        }
    }
    if (std::abs(renderScaleX_ - 1.0f) > 0.001f || std::abs(renderScaleY_ - 1.0f) > 0.001f) {
        for (auto& v : vertices_) {
            v.x *= renderScaleX_;
            v.y *= renderScaleY_;
            if ((v.mode & 15u) == 2u) {
                v.u *= renderScaleX_;
                v.v *= renderScaleX_;
                v.pad[0] *= renderScaleX_;
                v.pad[1] *= renderScaleY_;
                if (std::abs(v.localUV[0]) < 9000.0f) {
                    v.localUV[0] *= renderScaleX_;
                    v.localUV[1] *= renderScaleY_;
                }
            }
        }
    }
    backend_->renderBatch(vertices_);
    vertices_.clear();
    sceneLitStartIdx_ = 0;
}

void BatchRenderer2D::applyBackdropBlur(float radius, float dimFactor) {
    flush();
    if (backend_) {
        backend_->applyBackdropBlur(radius, dimFactor);
    }
}

void BatchRenderer2D::pushScissor(float x, float y, float w, float h) {
    flush();
    if (scissorStack_.empty()) {
        scissorStack_.push_back(Rect2D{x, y, w, h});
    } else {
        const auto& top = scissorStack_.back();
        float x0 = std::max(top.x, x);
        float y0 = std::max(top.y, y);
        float x1 = std::min(top.x + top.w, x + w);
        float y1 = std::min(top.y + top.h, y + h);
        scissorStack_.push_back(Rect2D{x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)});
    }
    if (backend_) {
        const auto& s = scissorStack_.back();
        backend_->setScissorRect(s.x * renderScaleX_, s.y * renderScaleY_, s.w * renderScaleX_, s.h * renderScaleY_);
    }
}

void BatchRenderer2D::popScissor() noexcept {
    flush();
    if (!scissorStack_.empty()) {
        scissorStack_.pop_back();
    }
    if (backend_) {
        if (scissorStack_.empty()) {
            backend_->clearScissorRect();
        } else {
            const auto& s = scissorStack_.back();
            backend_->setScissorRect(s.x * renderScaleX_, s.y * renderScaleY_, s.w * renderScaleX_, s.h * renderScaleY_);
        }
    }
}

// ----------------------------------------------------------------------------
// ScissorScope RAII Implementation
// ----------------------------------------------------------------------------
ScissorScope::ScissorScope(BatchRenderer2D* renderer, float x, float y, float w, float h)
    : renderer_(renderer) {
    if (renderer_) {
        renderer_->pushScissor(x, y, w, h);
    }
}

ScissorScope::ScissorScope(BatchRenderer2D* renderer, const Rect2D& r)
    : ScissorScope(renderer, r.x, r.y, r.w, r.h) {}

ScissorScope::ScissorScope(BatchRenderer2D& renderer, float x, float y, float w, float h)
    : ScissorScope(&renderer, x, y, w, h) {}

ScissorScope::ScissorScope(BatchRenderer2D& renderer, const Rect2D& r)
    : ScissorScope(&renderer, r.x, r.y, r.w, r.h) {}

ScissorScope::~ScissorScope() {
    reset();
}

ScissorScope::ScissorScope(ScissorScope&& other) noexcept
    : renderer_(other.renderer_) {
    other.renderer_ = nullptr;
}

ScissorScope& ScissorScope::operator=(ScissorScope&& other) noexcept {
    if (this != &other) {
        reset();
        renderer_ = other.renderer_;
        other.renderer_ = nullptr;
    }
    return *this;
}

void ScissorScope::reset() noexcept {
    if (renderer_) {
        renderer_->popScissor();
        renderer_ = nullptr;
    }
}

ScissorScope BatchRenderer2D::makeScissorScope(float x, float y, float w, float h) {
    return ScissorScope(this, x, y, w, h);
}

ScissorScope BatchRenderer2D::makeScissorScope(const Rect2D& r) {
    return ScissorScope(this, r);
}

// ----------------------------------------------------------------------------
// ScopedAntiAliasing RAII Implementation
// ----------------------------------------------------------------------------
ScopedAntiAliasing::ScopedAntiAliasing(BatchRenderer2D* renderer, int tempMode)
    : renderer_(renderer), previousMode_(renderer ? renderer->getAntiAliasingMode() : 2) {
    if (renderer_ && renderer_->getAntiAliasingMode() != tempMode) {
        renderer_->setAntiAliasingMode(tempMode);
    }
}

ScopedAntiAliasing::ScopedAntiAliasing(BatchRenderer2D& renderer, int tempMode)
    : ScopedAntiAliasing(&renderer, tempMode) {}

ScopedAntiAliasing::~ScopedAntiAliasing() {
    reset();
}

ScopedAntiAliasing::ScopedAntiAliasing(ScopedAntiAliasing&& other) noexcept
    : renderer_(other.renderer_), previousMode_(other.previousMode_) {
    other.renderer_ = nullptr;
}

ScopedAntiAliasing& ScopedAntiAliasing::operator=(ScopedAntiAliasing&& other) noexcept {
    if (this != &other) {
        reset();
        renderer_ = other.renderer_;
        previousMode_ = other.previousMode_;
        other.renderer_ = nullptr;
    }
    return *this;
}

void ScopedAntiAliasing::reset() noexcept {
    if (renderer_) {
        if (renderer_->getAntiAliasingMode() != previousMode_) {
            renderer_->setAntiAliasingMode(previousMode_);
        }
        renderer_ = nullptr;
    }
}

ScopedAntiAliasing BatchRenderer2D::makeAntiAliasingScope(int tempMode) {
    return ScopedAntiAliasing(this, tempMode);
}

// ----------------------------------------------------------------------------
// Static Chrome Offscreen Texture (FBO) Caching
// ----------------------------------------------------------------------------
void BatchRenderer2D::invalidateChromeCache() noexcept {
    chromeCacheValid_ = false;
    inChromeRecording_ = false;
}

bool BatchRenderer2D::beginChromeRecording() {
    if (!chromeCacheEnabled_) return false;
    flush();
    inChromeRecording_ = true;
    return true;
}

void BatchRenderer2D::endChromeRecording() {
    if (!inChromeRecording_) return;
    flush();
    inChromeRecording_ = false;

    if (backend_) {
        backend_->snapshotFramebuffer(chromeCachePixels_, chromeCacheWidth_, chromeCacheHeight_);
        if (!chromeCachePixels_.empty() && chromeCacheWidth_ > 0 && chromeCacheHeight_ > 0) {
            chromeCacheValid_ = true;
        } else {
            chromeCacheValid_ = false;
        }
    } else {
        // Headless testing when no hardware/software backend is attached
        chromeCacheWidth_ = static_cast<uint32_t>(currentWidth_);
        chromeCacheHeight_ = static_cast<uint32_t>(currentHeight_);
        if (chromeCacheWidth_ > 0 && chromeCacheHeight_ > 0) {
            chromeCachePixels_.assign(static_cast<size_t>(chromeCacheWidth_) * chromeCacheHeight_, 0xFF14171E);
            chromeCacheValid_ = true;
        }
    }
}

void BatchRenderer2D::drawCachedChrome(float opacity) {
    if (!chromeCacheValid_ || !chromeCacheEnabled_ || chromeCachePixels_.empty()) return;

    if (backend_) {
        backend_->restoreFramebuffer(chromeCachePixels_.data(), chromeCacheWidth_, chromeCacheHeight_, opacity);
    }
    chromeDrawCallsAvoided_++;
}

void BatchRenderer2D::drawCachedChromeRegion(float dstX, float dstY, float srcX, float srcY, float w, float h, float opacity) {
    if (!chromeCacheValid_ || !chromeCacheEnabled_ || chromeCachePixels_.empty()) return;

    if (backend_) {
        backend_->restoreFramebufferRegion(chromeCachePixels_.data(), chromeCacheWidth_, chromeCacheHeight_,
                                           dstX, dstY, srcX, srcY, w, h, opacity);
    }
    chromeDrawCallsAvoided_++;
}

// ----------------------------------------------------------------------------
// Primitive Tessellation
// ----------------------------------------------------------------------------
void BatchRenderer2D::drawTriangle(float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a) {
    uint32_t col = packColor(r, g, b, a);
    applyTransform(x0, y0);
    applyTransform(x1, y1);
    applyTransform(x2, y2);
    vertices_.push_back({x0, y0, 0.0f, 0.0f, col, 0, {0, 0}});
    vertices_.push_back({x1, y1, 0.0f, 0.0f, col, 0, {0, 0}});
    vertices_.push_back({x2, y2, 0.0f, 0.0f, col, 0, {0, 0}});
}

void BatchRenderer2D::drawTriangleTextured(float x0, float y0, float u0, float v0,
                                          float x1, float y1, float u1, float v1,
                                          float x2, float y2, float u2, float v2,
                                          float r, float g, float b, float a) {
    uint32_t col = packColor(r, g, b, a);
    applyTransform(x0, y0);
    applyTransform(x1, y1);
    applyTransform(x2, y2);
    vertices_.push_back({x0, y0, u0, v0, col, VertexMode::RgbaTexture, {0.0f, 0.0f}});
    vertices_.push_back({x1, y1, u1, v1, col, VertexMode::RgbaTexture, {0.0f, 0.0f}});
    vertices_.push_back({x2, y2, u2, v2, col, VertexMode::RgbaTexture, {0.0f, 0.0f}});
}

void BatchRenderer2D::drawTexturedPolygon(const Point2D* points, const Point2D* uvs, size_t count,
                                         float r, float g, float b, float a,
                                         float scratchDistortion,
                                         float conditioningParam,
                                         const Point2D* localUVs) {
    if (!points || !uvs || count < 3) return;
    uint32_t col = packColor(r, g, b, a);
    std::vector<Vertex2D> v(count);
    for (size_t i = 0; i < count; ++i) {
        float px = points[i].x;
        float py = points[i].y;
        applyTransform(px, py);
        Vertex2D vert{};
        vert.x = px;
        vert.y = py;
        vert.u = uvs[i].x;
        vert.v = uvs[i].y;
        vert.color = col;
        vert.mode = VertexMode::RgbaTexture;
        vert.pad[0] = scratchDistortion;
        vert.pad[1] = conditioningParam;
        if (localUVs) {
            vert.localUV[0] = localUVs[i].x;
            vert.localUV[1] = localUVs[i].y;
        } else {
            vert.localUV[0] = uvs[i].x;
            vert.localUV[1] = uvs[i].y;
        }
        v[i] = vert;
    }
    for (size_t i = 1; i + 1 < count; ++i) {
        vertices_.push_back(v[0]);
        vertices_.push_back(v[i]);
        vertices_.push_back(v[i + 1]);
    }
}

void BatchRenderer2D::drawConvexPolygon(const Point2D* points, size_t count, float r, float g, float b, float a) {
    if (!points || count < 3) return;
    flush();
    if (backend_) {
        uint32_t col = packColor(r, g, b, a);
        if (transformActive_ || renderScaleX_ != 1.0f || renderScaleY_ != 1.0f) {
            std::vector<Point2D> xf(count);
            for (size_t i = 0; i < count; ++i) {
                xf[i] = points[i];
                applyTransform(xf[i].x, xf[i].y);
            }
            backend_->renderConvexPolygon(xf.data(), count, col, col, Point2D{0.0f, 0.0f}, Point2D{0.0f, 0.0f});
        } else {
            backend_->renderConvexPolygon(points, count, col, col, Point2D{0.0f, 0.0f}, Point2D{0.0f, 0.0f});
        }
    }
}

void BatchRenderer2D::drawConvexPolygonGradient(const Point2D* points, size_t count,
                                               float r0, float g0, float b0,
                                               float r1, float g1, float b1,
                                               Point2D gradP0, Point2D gradP1,
                                               float a) {
    if (!points || count < 3) return;
    flush();
    if (backend_) {
        uint32_t col0 = packColor(r0, g0, b0, a);
        uint32_t col1 = packColor(r1, g1, b1, a);
        Point2D gp0 = gradP0;
        Point2D gp1 = gradP1;
        if (transformActive_ || renderScaleX_ != 1.0f || renderScaleY_ != 1.0f) {
            std::vector<Point2D> xf(count);
            for (size_t i = 0; i < count; ++i) {
                xf[i] = points[i];
                applyTransform(xf[i].x, xf[i].y);
            }
            applyTransform(gp0.x, gp0.y);
            applyTransform(gp1.x, gp1.y);
            backend_->renderConvexPolygon(xf.data(), count, col0, col1, gp0, gp1);
        } else {
            backend_->renderConvexPolygon(points, count, col0, col1, gp0, gp1);
        }
    }
}

void BatchRenderer2D::drawTexturedConvexPolygon(
    const Point2D* inputPts, size_t count,
    const uint8_t* rgba, int imgW, int imgH,
    Point2D p0, Point2D p1, Point2D p2, Point2D p3,
    float w0, float w1, float w2, float w3,
    float lightShade
) {
    if (!inputPts || count < 3 || !rgba || imgW <= 0 || imgH <= 0) return;
    flush();
    if (backend_) {
        backend_->renderTexturedConvexPolygon(inputPts, count, rgba, imgW, imgH,
                                              p0, p1, p2, p3, w0, w1, w2, w3, lightShade);
    }
}

void BatchRenderer2D::drawTexturedConvexPolygon(
    const Point2D* inputPts, size_t count,
    const uint32_t* argb32, int imgW, int imgH,
    Point2D p0, Point2D p1, Point2D p2, Point2D p3,
    float w0, float w1, float w2, float w3,
    float lightShade
) {
    if (!inputPts || count < 3 || !argb32 || imgW <= 0 || imgH <= 0) return;
    flush();
    if (backend_) {
        backend_->renderTexturedConvexPolygonARGB(inputPts, count, argb32, imgW, imgH,
                                                  p0, p1, p2, p3, w0, w1, w2, w3, lightShade);
    }
}

void BatchRenderer2D::drawTexturedRoundedRect(
    float x, float y, float w, float h, float radius,
    const uint32_t* argb32, int imgW, int imgH,
    float opacity
) {
    if (!argb32 || imgW <= 0 || imgH <= 0 || w <= 0.0f || h <= 0.0f) return;
    flush();
    if (backend_) {
        backend_->renderTexturedRoundedRect(x, y, w, h, radius, argb32, imgW, imgH, opacity);
    }
}

void BatchRenderer2D::updateRgbaTexture(int x, int y, int w, int h, const uint8_t* data, int atlasW, int atlasH) {
    if (backend_) {
        backend_->updateRgbaTexture(x, y, w, h, data, atlasW, atlasH);
    }
}

void BatchRenderer2D::drawQuadTextured3D(
    float x0, float y0, float u0, float v0,
    float x1, float y1, float u1, float v1,
    float x2, float y2, float u2, float v2,
    float x3, float y3, float u3, float v3,
    float r, float g, float b, float a
) {
    uint32_t col = packColor(r, g, b, a);
    Vertex2D q0{};
    q0.x = x0; q0.y = y0; q0.u = u0; q0.v = v0; q0.color = col; q0.mode = VertexMode::RgbaTexture;
    Vertex2D q1{};
    q1.x = x1; q1.y = y1; q1.u = u1; q1.v = v1; q1.color = col; q1.mode = VertexMode::RgbaTexture;
    Vertex2D q2{};
    q2.x = x2; q2.y = y2; q2.u = u2; q2.v = v2; q2.color = col; q2.mode = VertexMode::RgbaTexture;
    Vertex2D q3{};
    q3.x = x3; q3.y = y3; q3.u = u3; q3.v = v3; q3.color = col; q3.mode = VertexMode::RgbaTexture;

    applyTransform(q0.x, q0.y);
    applyTransform(q1.x, q1.y);
    applyTransform(q2.x, q2.y);
    applyTransform(q3.x, q3.y);

    vertices_.push_back(q0);
    vertices_.push_back(q1);
    vertices_.push_back(q2);

    vertices_.push_back(q0);
    vertices_.push_back(q2);
    vertices_.push_back(q3);
}

void BatchRenderer2D::drawTexturedRoundedRectGpu(
    float x, float y, float w, float h, float radius,
    float u0, float v0, float u1, float v1,
    float r, float g, float b, float a,
    float scratchDistortion,
    float conditioningParam
) {
    if (w <= 0.0f || h <= 0.0f) return;
    uint32_t col = packColor(r, g, b, a);

    constexpr float kC30 = 0.8660254f;
    constexpr float kS30 = 0.5000000f;
    float rad = std::min(radius, std::min(w, h) * 0.5f);
    float ru = rad / w;
    float rv = rad / h;

    const Point2D localUV[16] = {
        {1.0f - ru, 0.0f},
        {1.0f - ru + ru * kS30, rv - rv * kC30},
        {1.0f - ru + ru * kC30, rv - rv * kS30},
        {1.0f, rv},

        {1.0f, 1.0f - rv},
        {1.0f - ru + ru * kC30, 1.0f - rv + rv * kS30},
        {1.0f - ru + ru * kS30, 1.0f - rv + rv * kC30},
        {1.0f - ru, 1.0f},

        {ru, 1.0f},
        {ru - ru * kS30, 1.0f - rv + rv * kC30},
        {ru - ru * kC30, 1.0f - rv + rv * kS30},
        {0.0f, 1.0f - rv},

        {0.0f, rv},
        {ru - ru * kC30, rv - rv * kS30},
        {ru - ru * kS30, rv - rv * kC30},
        {ru, 0.0f}
    };

    Vertex2D v[16];
    for (size_t i = 0; i < 16; ++i) {
        v[i].x = x + localUV[i].x * w;
        v[i].y = y + localUV[i].y * h;
        applyTransform(v[i].x, v[i].y);
        v[i].u = u0 + localUV[i].x * (u1 - u0);
        v[i].v = v0 + localUV[i].y * (v1 - v0);
        v[i].color = col;
        v[i].mode = VertexMode::RgbaTexture;
        v[i].pad[0] = scratchDistortion;
        v[i].pad[1] = conditioningParam;
        v[i].localUV[0] = localUV[i].x;
        v[i].localUV[1] = localUV[i].y;
    }

    for (size_t i = 1; i + 1 < 16; ++i) {
        vertices_.push_back(v[0]);
        vertices_.push_back(v[i]);
        vertices_.push_back(v[i + 1]);
    }
}

void BatchRenderer2D::drawRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    uint32_t col = packColor(r, g, b, a);
    Vertex2D v0{x, y, 0.0f, 0.0f, col, 0, {0, 0}};
    Vertex2D v1{x + w, y, 0.0f, 0.0f, col, 0, {0, 0}};
    Vertex2D v2{x + w, y + h, 0.0f, 0.0f, col, 0, {0, 0}};
    Vertex2D v3{x, y + h, 0.0f, 0.0f, col, 0, {0, 0}};
    applyTransform(v0.x, v0.y);
    applyTransform(v1.x, v1.y);
    applyTransform(v2.x, v2.y);
    applyTransform(v3.x, v3.y);

    // Triangle 1
    vertices_.push_back(v0);
    vertices_.push_back(v1);
    vertices_.push_back(v2);

    // Triangle 2
    vertices_.push_back(v0);
    vertices_.push_back(v2);
    vertices_.push_back(v3);
}

void BatchRenderer2D::drawRectGradient(float x, float y, float w, float h,
                                       float r0, float g0, float b0,
                                       float r1, float g1, float b1, float a) {
    uint32_t col0 = packColor(r0, g0, b0, a);
    uint32_t col1 = packColor(r1, g1, b1, a);

    Vertex2D v0{x, y, 0.0f, 0.0f, col0, 0, {0, 0}};
    Vertex2D v1{x + w, y, 0.0f, 0.0f, col0, 0, {0, 0}};
    Vertex2D v2{x + w, y + h, 0.0f, 0.0f, col1, 0, {0, 0}};
    Vertex2D v3{x, y + h, 0.0f, 0.0f, col1, 0, {0, 0}};
    applyTransform(v0.x, v0.y);
    applyTransform(v1.x, v1.y);
    applyTransform(v2.x, v2.y);
    applyTransform(v3.x, v3.y);

    vertices_.push_back(v0);
    vertices_.push_back(v1);
    vertices_.push_back(v2);

    vertices_.push_back(v0);
    vertices_.push_back(v2);
    vertices_.push_back(v3);
}

void BatchRenderer2D::drawRectOutline(float x, float y, float w, float h,
                                      float r, float g, float b, float a, float lineWidth) {
    float lw = std::max(0.5f, lineWidth);
    // Top border
    drawRect(x, y, w, lw, r, g, b, a);
    // Bottom border
    drawRect(x, y + h - lw, w, lw, r, g, b, a);
    // Left border
    drawRect(x, y + lw, lw, std::max(0.0f, h - 2.0f * lw), r, g, b, a);
    // Right border
    drawRect(x + w - lw, y + lw, lw, std::max(0.0f, h - 2.0f * lw), r, g, b, a);
}

void BatchRenderer2D::drawRoundedRect(float x, float y, float w, float h, float radius,
                                      float r, float g, float b, float a, int /*cornerSegments*/) {
    if (w <= 0.0f || h <= 0.0f) return;
    float rad = std::min(radius, std::min(w, h) * 0.5f);
    if (rad <= 0.5f) {
        drawRect(x, y, w, h, r, g, b, a);
        return;
    }

    float halfW = w * 0.5f;
    float halfH = h * 0.5f;

    uint32_t col = packColor(r, g, b, a);
    Vertex2D v0{x, y, rad, 0.0f, col, 2, {-halfW, -halfH}};
    Vertex2D v1{x + w, y, rad, 0.0f, col, 2, {halfW, -halfH}};
    Vertex2D v2{x + w, y + h, rad, 0.0f, col, 2, {halfW, halfH}};
    Vertex2D v3{x, y + h, rad, 0.0f, col, 2, {-halfW, halfH}};

    applyTransform(v0.x, v0.y);
    applyTransform(v1.x, v1.y);
    applyTransform(v2.x, v2.y);
    applyTransform(v3.x, v3.y);

    vertices_.push_back(v0);
    vertices_.push_back(v1);
    vertices_.push_back(v2);

    vertices_.push_back(v0);
    vertices_.push_back(v2);
    vertices_.push_back(v3);
}

void BatchRenderer2D::drawRoundedRectGradient(float x, float y, float w, float h, float radius,
                                              float r0, float g0, float b0,
                                              float r1, float g1, float b1, float a, int /*cornerSegments*/) {
    if (w <= 0.0f || h <= 0.0f) return;
    float rad = std::min(radius, std::min(w, h) * 0.5f);
    if (rad <= 0.5f) {
        drawRectGradient(x, y, w, h, r0, g0, b0, r1, g1, b1, a);
        return;
    }

    float halfW = w * 0.5f;
    float halfH = h * 0.5f;

    uint32_t col0 = packColor(r0, g0, b0, a);
    uint32_t col1 = packColor(r1, g1, b1, a);

    Vertex2D v0{x, y, rad, 0.0f, col0, 2, {-halfW, -halfH}};
    Vertex2D v1{x + w, y, rad, 0.0f, col0, 2, {halfW, -halfH}};
    Vertex2D v2{x + w, y + h, rad, 0.0f, col1, 2, {halfW, halfH}};
    Vertex2D v3{x, y + h, rad, 0.0f, col1, 2, {-halfW, halfH}};

    applyTransform(v0.x, v0.y);
    applyTransform(v1.x, v1.y);
    applyTransform(v2.x, v2.y);
    applyTransform(v3.x, v3.y);

    vertices_.push_back(v0);
    vertices_.push_back(v1);
    vertices_.push_back(v2);

    vertices_.push_back(v0);
    vertices_.push_back(v2);
    vertices_.push_back(v3);
}

void BatchRenderer2D::drawRoundedRectOutline(float x, float y, float w, float h, float radius,
                                             float r, float g, float b, float a,
                                             float lineWidth, int cornerSegments) {
    if (w <= 0.0f || h <= 0.0f) return;
    float rad = std::min(radius, std::min(w, h) * 0.5f);
    float lw = std::max(0.5f, lineWidth);
    if (rad <= 0.5f) {
        drawRectOutline(x, y, w, h, r, g, b, a, lw);
        return;
    }

    uint32_t col = packColor(r, g, b, a);
    int segs = std::clamp(cornerSegments, 2, 32);

    // 4 Straight edge borders
    // Top border
    drawRect(x + rad, y, std::max(0.0f, w - 2.0f * rad), lw, r, g, b, a);
    // Bottom border
    drawRect(x + rad, y + h - lw, std::max(0.0f, w - 2.0f * rad), lw, r, g, b, a);
    // Left border
    drawRect(x, y + rad, lw, std::max(0.0f, h - 2.0f * rad), r, g, b, a);
    // Right border
    drawRect(x + w - lw, y + rad, lw, std::max(0.0f, h - 2.0f * rad), r, g, b, a);

    // 4 Curved corner strips
    constexpr float kHalfPi = 1.57079632679f;
    struct CornerDef { float cx, cy, startAngle; };
    const CornerDef corners[4] = {
        {x + w - rad, y + rad,         0.0f * kHalfPi},
        {x + rad,     y + rad,         1.0f * kHalfPi},
        {x + rad,     y + h - rad,     2.0f * kHalfPi},
        {x + w - rad, y + h - rad,     3.0f * kHalfPi}
    };
    const float angleStep = kHalfPi / static_cast<float>(segs);
    float rOut = rad;
    float rIn = std::max(0.0f, rad - lw);

    for (int c = 0; c < 4; ++c) {
        for (int s = 0; s < segs; ++s) {
            float a0 = corners[c].startAngle + static_cast<float>(s) * angleStep;
            float a1 = corners[c].startAngle + static_cast<float>(s + 1) * angleStep;
            float c0 = std::cos(a0), s0 = std::sin(a0);
            float c1 = std::cos(a1), s1 = std::sin(a1);

            Vertex2D v0{corners[c].cx + rIn * c0,  corners[c].cy - rIn * s0,  0.0f, 0.0f, col, 0, {0, 0}};
            Vertex2D v1{corners[c].cx + rOut * c0, corners[c].cy - rOut * s0, 0.0f, 0.0f, col, 0, {0, 0}};
            Vertex2D v2{corners[c].cx + rOut * c1, corners[c].cy - rOut * s1, 0.0f, 0.0f, col, 0, {0, 0}};
            Vertex2D v3{corners[c].cx + rIn * c1,  corners[c].cy - rIn * s1,  0.0f, 0.0f, col, 0, {0, 0}};

            applyTransform(v0.x, v0.y);
            applyTransform(v1.x, v1.y);
            applyTransform(v2.x, v2.y);
            applyTransform(v3.x, v3.y);

            vertices_.push_back(v0); vertices_.push_back(v1); vertices_.push_back(v2);
            vertices_.push_back(v0); vertices_.push_back(v2); vertices_.push_back(v3);
        }
    }
}

void BatchRenderer2D::drawCircle(float cx, float cy, float radius, float r, float g, float b, float a, int /*segments*/) {
    if (radius <= 0.0f) return;
    drawRoundedRect(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, radius, r, g, b, a);
}

void BatchRenderer2D::drawCircleRadialGradient(float cx, float cy, float radius,
                                               float innerR, float innerG, float innerB, float innerA,
                                               float outerR, float outerG, float outerB, float outerA,
                                               float offX, float offY, int segments) {
    if (radius <= 0.0f) return;

    // 1. Draw solid anti-aliased base circle underneath using mode 2 analytical SDF.
    // This seals the silhouette and prevents underlying chassis/halo colors from bleeding through
    // internal mesh edges during 4x MSAA subpixel rasterization.
    drawCircle(cx, cy, radius, outerR, outerG, outerB, outerA);

    int segs = std::clamp(segments, 8, 128);
    uint32_t colInner = packColor(innerR, innerG, innerB, innerA);
    uint32_t colOuter = packColor(outerR, outerG, outerB, outerA);

    float fx = cx + offX;
    float fy = cy + offY;
    Vertex2D centerV{fx, fy, 0.0f, 0.0f, colInner, 5, {0, 0}};
    applyTransform(centerV.x, centerV.y);

    const float step = 6.28318530718f / static_cast<float>(segs);
    for (int i = 0; i < segs; ++i) {
        float theta0 = static_cast<float>(i) * step;
        float theta1 = static_cast<float>(i + 1) * step;

        Vertex2D p0{cx + radius * std::cos(theta0), cy + radius * std::sin(theta0), 0.0f, 0.0f, colOuter, 5, {0, 0}};
        Vertex2D p1{cx + radius * std::cos(theta1), cy + radius * std::sin(theta1), 0.0f, 0.0f, colOuter, 5, {0, 0}};
        applyTransform(p0.x, p0.y);
        applyTransform(p1.x, p1.y);

        vertices_.push_back(centerV);
        vertices_.push_back(p0);
        vertices_.push_back(p1);
    }
}

void BatchRenderer2D::drawCircleRadial3StopGradient(float cx, float cy, float radius,
                                                   float innerR, float innerG, float innerB, float innerA,
                                                   float midR, float midG, float midB, float midA,
                                                   float outerR, float outerG, float outerB, float outerA,
                                                   float offX, float offY, float midStop,
                                                   int segments) {
    if (radius <= 0.0f) return;

    // 1. Draw solid anti-aliased base circle underneath using mode 2 analytical SDF.
    // This seals the silhouette and prevents underlying chassis/halo colors from bleeding through
    // internal mesh edges during 4x MSAA subpixel rasterization.
    drawCircle(cx, cy, radius, midR, midG, midB, outerA);

    int segs = std::clamp(segments, 8, 128);
    float clampedMidStop = std::clamp(midStop, 0.05f, 0.95f);
    float midRad = radius * clampedMidStop;

    uint32_t colInner = packColor(innerR, innerG, innerB, innerA);
    uint32_t colMid = packColor(midR, midG, midB, midA);
    uint32_t colOuter = packColor(outerR, outerG, outerB, outerA);

    float fx = cx + offX;
    float fy = cy + offY;
    Vertex2D centerV{fx, fy, 0.0f, 0.0f, colInner, 5, {0, 0}};
    applyTransform(centerV.x, centerV.y);

    // Mid ring center interpolates smoothly from focal point towards circle center
    float midCx = fx + (cx - fx) * clampedMidStop;
    float midCy = fy + (cy - fy) * clampedMidStop;

    const float step = 6.28318530718f / static_cast<float>(segs);
    for (int i = 0; i < segs; ++i) {
        float theta0 = static_cast<float>(i) * step;
        float theta1 = static_cast<float>(i + 1) * step;

        float c0 = std::cos(theta0), s0 = std::sin(theta0);
        float c1 = std::cos(theta1), s1 = std::sin(theta1);

        Vertex2D m0{midCx + midRad * c0, midCy + midRad * s0, 0.0f, 0.0f, colMid, 5, {0, 0}};
        Vertex2D m1{midCx + midRad * c1, midCy + midRad * s1, 0.0f, 0.0f, colMid, 5, {0, 0}};
        applyTransform(m0.x, m0.y);
        applyTransform(m1.x, m1.y);

        Vertex2D p0{cx + radius * c0, cy + radius * s0, 0.0f, 0.0f, colOuter, 5, {0, 0}};
        Vertex2D p1{cx + radius * c1, cy + radius * s1, 0.0f, 0.0f, colOuter, 5, {0, 0}};
        applyTransform(p0.x, p0.y);
        applyTransform(p1.x, p1.y);

        // Inner fan (inner -> mid)
        vertices_.push_back(centerV);
        vertices_.push_back(m0);
        vertices_.push_back(m1);

        // Outer ring quad (mid -> outer)
        vertices_.push_back(m0);
        vertices_.push_back(p0);
        vertices_.push_back(p1);

        vertices_.push_back(m0);
        vertices_.push_back(p1);
        vertices_.push_back(m1);
    }
}

void BatchRenderer2D::drawCircleLinearGradient(float cx, float cy, float radius,
                                              float r0, float g0, float b0, float a0,
                                              float r1, float g1, float b1, float a1,
                                              float angleRad, int segments) {
    if (radius <= 0.0f) return;

    // 1. Draw solid anti-aliased base circle underneath using mode 2 analytical SDF
    drawCircle(cx, cy, radius, (r0 + r1) * 0.5f, (g0 + g1) * 0.5f, (b0 + b1) * 0.5f, (a0 + a1) * 0.5f);

    int segs = std::clamp(segments, 8, 128);

    float dx = std::cos(angleRad);
    float dy = std::sin(angleRad);

    uint32_t colCenter = packColor((r0 + r1) * 0.5f, (g0 + g1) * 0.5f, (b0 + b1) * 0.5f, (a0 + a1) * 0.5f);
    Vertex2D centerV{cx, cy, 0.0f, 0.0f, colCenter, 0, {0, 0}};
    applyTransform(centerV.x, centerV.y);

    const float step = 6.28318530718f / static_cast<float>(segs);
    for (int i = 0; i < segs; ++i) {
        float theta0 = static_cast<float>(i) * step;
        float theta1 = static_cast<float>(i + 1) * step;

        float c0 = std::cos(theta0), s0 = std::sin(theta0);
        float c1 = std::cos(theta1), s1 = std::sin(theta1);

        float t0 = std::clamp(0.5f + 0.5f * (c0 * dx + s0 * dy), 0.0f, 1.0f);
        float t1 = std::clamp(0.5f + 0.5f * (c1 * dx + s1 * dy), 0.0f, 1.0f);

        uint32_t col0 = packColor(r0 + (r1 - r0) * t0, g0 + (g1 - g0) * t0, b0 + (b1 - b0) * t0, a0 + (a1 - a0) * t0);
        uint32_t col1 = packColor(r0 + (r1 - r0) * t1, g0 + (g1 - g0) * t1, b0 + (b1 - b0) * t1, a0 + (a1 - a0) * t1);

        Vertex2D p0{cx + radius * c0, cy + radius * s0, 0.0f, 0.0f, col0, 0, {0, 0}};
        Vertex2D p1{cx + radius * c1, cy + radius * s1, 0.0f, 0.0f, col1, 0, {0, 0}};
        applyTransform(p0.x, p0.y);
        applyTransform(p1.x, p1.y);

        vertices_.push_back(centerV);
        vertices_.push_back(p0);
        vertices_.push_back(p1);
    }
}

void BatchRenderer2D::drawDeclinedScoopGradient(float cx, float cy, float capRadius,
                                               float currentAngle, float chordDistRatio,
                                               float crestR, float crestG, float crestB, float crestA,
                                               float midR, float midG, float midB, float midA,
                                               float rimR, float rimG, float rimB, float rimA,
                                               float midStop, int segments) {
    if (capRadius <= 0.0f) return;

    // chordDistRatio is typically -0.12f to -0.15f (chord is slightly behind the center)
    float clampedRatio = std::clamp(chordDistRatio, -0.95f, 0.95f);
    float chordDist = clampedRatio * capRadius;
    float thetaChord = std::acos(clampedRatio); // Half-angle of arc spanning the scoop

    float sinA = std::sin(currentAngle);
    float cosA = std::cos(currentAngle);

    // Forward direction towards pointer
    float dirX = sinA;
    float dirY = -cosA;
    // Transverse direction along chord
    float perpX = -cosA;
    float perpY = -sinA;

    int segs = std::clamp(segments, 8, 64);
    float dPhi = (2.0f * thetaChord) / static_cast<float>(segs);
    float clampedMid = std::clamp(midStop, 0.05f, 0.95f);

    uint32_t colCrest = packColor(crestR, crestG, crestB, crestA);
    uint32_t colMid = packColor(midR, midG, midB, midA);
    uint32_t colRim = packColor(rimR, rimG, rimB, rimA);

    for (int i = 0; i < segs; ++i) {
        float phi0 = -thetaChord + static_cast<float>(i) * dPhi;
        float phi1 = -thetaChord + static_cast<float>(i + 1) * dPhi;

        // Arc coordinates in local frame (u along dir, v along perp)
        float uArc0 = capRadius * std::cos(phi0);
        float vArc0 = capRadius * std::sin(phi0);

        float uArc1 = capRadius * std::cos(phi1);
        float vArc1 = capRadius * std::sin(phi1);

        // Chord points in local frame (u = chordDist, v matching arc)
        float uCh0 = chordDist;
        float vCh0 = vArc0;

        float uCh1 = chordDist;
        float vCh1 = vArc1;

        // Intermediate mid-stop points
        float uMid0 = uCh0 + clampedMid * (uArc0 - uCh0);
        float vMid0 = vCh0 + clampedMid * (vArc0 - vCh0);

        float uMid1 = uCh1 + clampedMid * (uArc1 - uCh1);
        float vMid1 = vCh1 + clampedMid * (vArc1 - vCh1);

        // Convert local (u, v) points to world coordinates (mode 5: zero internal seam bleed)
        Vertex2D c0{cx + uCh0 * dirX + vCh0 * perpX, cy + uCh0 * dirY + vCh0 * perpY, 0.0f, 0.0f, colCrest, 5, {0, 0}};
        Vertex2D c1{cx + uCh1 * dirX + vCh1 * perpX, cy + uCh1 * dirY + vCh1 * perpY, 0.0f, 0.0f, colCrest, 5, {0, 0}};

        Vertex2D m0{cx + uMid0 * dirX + vMid0 * perpX, cy + uMid0 * dirY + vMid0 * perpY, 0.0f, 0.0f, colMid, 5, {0, 0}};
        Vertex2D m1{cx + uMid1 * dirX + vMid1 * perpX, cy + uMid1 * dirY + vMid1 * perpY, 0.0f, 0.0f, colMid, 5, {0, 0}};

        Vertex2D a0{cx + uArc0 * dirX + vArc0 * perpX, cy + uArc0 * dirY + vArc0 * perpY, 0.0f, 0.0f, colRim, 5, {0, 0}};
        Vertex2D a1{cx + uArc1 * dirX + vArc1 * perpX, cy + uArc1 * dirY + vArc1 * perpY, 0.0f, 0.0f, colRim, 5, {0, 0}};

        applyTransform(c0.x, c0.y);
        applyTransform(c1.x, c1.y);
        applyTransform(m0.x, m0.y);
        applyTransform(m1.x, m1.y);
        applyTransform(a0.x, a0.y);
        applyTransform(a1.x, a1.y);

        // Inner quad (Crest -> Mid)
        vertices_.push_back(c0);
        vertices_.push_back(c1);
        vertices_.push_back(m1);

        vertices_.push_back(c0);
        vertices_.push_back(m1);
        vertices_.push_back(m0);

        // Outer quad (Mid -> Rim)
        vertices_.push_back(m0);
        vertices_.push_back(m1);
        vertices_.push_back(a1);

        vertices_.push_back(m0);
        vertices_.push_back(a1);
        vertices_.push_back(a0);
    }
}

void BatchRenderer2D::drawRgbaBitmap(float x, float y, float w, float h, const uint8_t* rgba, int imgW, int imgH, float opacity) {
    drawRgbaBitmapRegion(x, y, w, h, rgba, imgW, imgH, 0.0f, 0.0f, 1.0f, 1.0f, opacity);
}

void BatchRenderer2D::drawRgbaBitmapRegion(float x, float y, float w, float h, const uint8_t* rgba, int imgW, int imgH,
                                           float u0, float v0, float u1, float v1, float opacity) {
    if (!rgba || imgW <= 0 || imgH <= 0 || w <= 0.0f || h <= 0.0f || opacity <= 0.001f) return;
    if (backend_ && !vertices_.empty()) {
        if (std::abs(renderScaleX_ - 1.0f) > 0.001f || std::abs(renderScaleY_ - 1.0f) > 0.001f) {
            for (auto& v : vertices_) {
                v.x *= renderScaleX_;
                v.y *= renderScaleY_;
                if (v.mode == 2) {
                    v.u *= renderScaleX_;
                    v.pad[0] *= renderScaleX_;
                    v.pad[1] *= renderScaleY_;
                }
            }
        }
        backend_->renderBatch(vertices_);
        vertices_.clear();
    }
    if (backend_) {
        backend_->renderRgbaRegion(x * renderScaleX_, y * renderScaleY_, w * renderScaleX_, h * renderScaleY_,
                                   rgba, imgW, imgH, u0, v0, u1, v1, opacity);
    }
}

void BatchRenderer2D::drawCachedVertices(const std::vector<Vertex2D>& vertices, float offsetX, float offsetY) {
    if (vertices.empty()) return;
    size_t oldSize = vertices_.size();
    vertices_.resize(oldSize + vertices.size());
    Vertex2D* dst = &vertices_[oldSize];
    const Vertex2D* src = vertices.data();
    for (size_t i = 0; i < vertices.size(); ++i) {
        dst[i] = src[i];
        dst[i].x += offsetX;
        dst[i].y += offsetY;
    }
}

void BatchRenderer2D::copyActiveVertices(std::vector<Vertex2D>& outVertices) const {
    outVertices = vertices_;
}

uint32_t BatchRenderer2D::createTexture(int width, int height, const uint8_t* rgbaPixels) {
    if (width <= 0 || height <= 0 || !rgbaPixels) return 0;
    uint32_t handle = nextTextureId_++;
    ImageTexture tex;
    tex.handle = handle;
    tex.width = width;
    tex.height = height;
    tex.channels = 4;
    size_t byteCount = static_cast<size_t>(width) * height * 4;
    tex.pixels.assign(rgbaPixels, rgbaPixels + byteCount);
    textures_[handle] = std::move(tex);
    return handle;
}

uint32_t BatchRenderer2D::createTextureFromMemory(const void* data, size_t len) {
    if (!data || len == 0) return 0;
    int w = 0, h = 0, comp = 0;
    stbi_uc* decoded = stbi_load_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(len), &w, &h, &comp, 4);
    if (!decoded || w <= 0 || h <= 0) {
        if (decoded) stbi_image_free(decoded);
        return 0;
    }
    uint32_t handle = createTexture(w, h, decoded);
    stbi_image_free(decoded);
    return handle;
}

uint32_t BatchRenderer2D::loadTextureFromUri(std::string_view uriOrPath) {
    if (uriOrPath.empty()) return 0;

    // 1. Try parsing as Data URI (e.g. data:image/png;base64,...)
    auto parsed = parseDataUri(uriOrPath);
    if (parsed && !parsed->data.empty()) {
        uint32_t handle = createTextureFromMemory(parsed->data.data(), parsed->data.size());
        if (handle > 0) return handle;
    }

    // 2. Try raw base64 decode if it could be base64
    auto rawBytes = base64Decode(uriOrPath);
    if (!rawBytes.empty()) {
        uint32_t handle = createTextureFromMemory(rawBytes.data(), rawBytes.size());
        if (handle > 0) return handle;
    }

    // 3. Fallback: try reading as local file
    std::ifstream file(std::string(uriOrPath), std::ios::binary);
    if (file.is_open()) {
        std::vector<uint8_t> fileBytes((std::istreambuf_iterator<char>(file)),
                                       std::istreambuf_iterator<char>());
        if (!fileBytes.empty()) {
            return createTextureFromMemory(fileBytes.data(), fileBytes.size());
        }
    }

    return 0;
}

bool BatchRenderer2D::destroyTexture(uint32_t handle) {
    auto it = textures_.find(handle);
    if (it != textures_.end()) {
        textures_.erase(it);
        return true;
    }
    return false;
}

bool BatchRenderer2D::hasTexture(uint32_t handle) const noexcept {
    return textures_.find(handle) != textures_.end();
}

BatchRenderer2D::TextureInfo BatchRenderer2D::getTextureInfo(uint32_t handle) const noexcept {
    auto it = textures_.find(handle);
    if (it != textures_.end()) {
        return TextureInfo{it->second.handle, it->second.width, it->second.height, it->second.channels, true};
    }
    return TextureInfo{0, 0, 0, 0, false};
}

const uint8_t* BatchRenderer2D::getTexturePixels(uint32_t handle) const noexcept {
    auto it = textures_.find(handle);
    return (it != textures_.end() && !it->second.pixels.empty()) ? it->second.pixels.data() : nullptr;
}

void BatchRenderer2D::drawTexturedQuad(uint32_t textureHandle, float x, float y, float w, float h,
                                       float u0, float v0, float u1, float v1,
                                       float opacity) {
    if (textureHandle == 0 || w <= 0.0f || h <= 0.0f || opacity <= 0.001f) return;
    auto it = textures_.find(textureHandle);
    if (it == textures_.end() || it->second.pixels.empty()) return;

    const auto& tex = it->second;
    drawRgbaBitmapRegion(x, y, w, h, tex.pixels.data(), tex.width, tex.height, u0, v0, u1, v1, opacity);
}

void BatchRenderer2D::drawFilmstripFrame(uint32_t textureHandle, float x, float y, float w, float h,
                                         int frameIndex, int totalFrames, bool isHorizontal,
                                         float opacity) {
    if (textureHandle == 0 || totalFrames <= 0) return;
    int clampedIndex = std::clamp(frameIndex, 0, totalFrames - 1);
    float step = 1.0f / static_cast<float>(totalFrames);

    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    if (isHorizontal) {
        u0 = static_cast<float>(clampedIndex) * step;
        u1 = u0 + step;
    } else {
        v0 = static_cast<float>(clampedIndex) * step;
        v1 = v0 + step;
    }

    drawTexturedQuad(textureHandle, x, y, w, h, u0, v0, u1, v1, opacity);
}

void BatchRenderer2D::drawCircleOutline(float cx, float cy, float radius,
                                        float r, float g, float b, float a,
                                        float lineWidth, int segments) {
    if (radius <= 0.0f || segments < 3) return;
    uint32_t col = packColor(r, g, b, a);
    float halfW = lineWidth * 0.5f;
    float rIn = std::max(0.0f, radius - halfW);
    float rOut = radius + halfW;
    const float step = 6.2831853f / static_cast<float>(segments);

    for (int i = 0; i < segments; ++i) {
        float theta0 = static_cast<float>(i) * step;
        float theta1 = static_cast<float>(i + 1) * step;

        float c0 = std::cos(theta0), s0 = std::sin(theta0);
        float c1 = std::cos(theta1), s1 = std::sin(theta1);

        Vertex2D v0{cx + rIn * c0, cy + rIn * s0, 0.0f, 0.0f, col, 0, {0, 0}};
        Vertex2D v1{cx + rOut * c0, cy + rOut * s0, 0.0f, 0.0f, col, 0, {0, 0}};
        Vertex2D v2{cx + rOut * c1, cy + rOut * s1, 0.0f, 0.0f, col, 0, {0, 0}};
        Vertex2D v3{cx + rIn * c1, cy + rIn * s1, 0.0f, 0.0f, col, 0, {0, 0}};

        vertices_.push_back(v0);
        vertices_.push_back(v1);
        vertices_.push_back(v2);

        vertices_.push_back(v0);
        vertices_.push_back(v2);
        vertices_.push_back(v3);
    }
}

void BatchRenderer2D::drawArc(float cx, float cy, float radius,
                              float startAngle, float endAngle,
                              float r, float g, float b, float a,
                              float lineWidth, int segments) {
    if (endAngle <= startAngle || radius <= 0.0f || segments < 2) return;
    uint32_t col = packColor(r, g, b, a);
    float halfW = lineWidth * 0.5f;
    float rIn = std::max(0.0f, radius - halfW);
    float rOut = radius + halfW;
    const float step = (endAngle - startAngle) / static_cast<float>(segments);

    for (int i = 0; i < segments; ++i) {
        float theta0 = startAngle + static_cast<float>(i) * step;
        float theta1 = startAngle + static_cast<float>(i + 1) * step;

        float x0_in = cx + rIn * std::sin(theta0), y0_in = cy - rIn * std::cos(theta0);
        float x0_out = cx + rOut * std::sin(theta0), y0_out = cy - rOut * std::cos(theta0);
        float x1_in = cx + rIn * std::sin(theta1), y1_in = cy - rIn * std::cos(theta1);
        float x1_out = cx + rOut * std::sin(theta1), y1_out = cy - rOut * std::cos(theta1);

        Vertex2D v0{x0_in, y0_in, 0.0f, 0.0f, col, 0, {0, 0}};
        Vertex2D v1{x0_out, y0_out, 0.0f, 0.0f, col, 0, {0, 0}};
        Vertex2D v2{x1_out, y1_out, 0.0f, 0.0f, col, 0, {0, 0}};
        Vertex2D v3{x1_in, y1_in, 0.0f, 0.0f, col, 0, {0, 0}};

        vertices_.push_back(v0);
        vertices_.push_back(v1);
        vertices_.push_back(v2);

        vertices_.push_back(v0);
        vertices_.push_back(v2);
        vertices_.push_back(v3);
    }
}

void BatchRenderer2D::drawLine(float x0, float y0, float x1, float y1,
                               float r, float g, float b, float a, float lineWidth) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0001f) return;

    float halfW = std::max(0.5f, lineWidth * 0.5f);
    float nx = (-dy / len) * halfW;
    float ny = (dx / len) * halfW;

    uint32_t col = packColor(r, g, b, a);
    Vertex2D v0{x0 + nx, y0 + ny, 0.0f, 0.0f, col, 0, {0, 0}};
    Vertex2D v1{x1 + nx, y1 + ny, 0.0f, 0.0f, col, 0, {0, 0}};
    Vertex2D v2{x1 - nx, y1 - ny, 0.0f, 0.0f, col, 0, {0, 0}};
    Vertex2D v3{x0 - nx, y0 - ny, 0.0f, 0.0f, col, 0, {0, 0}};
    applyTransform(v0.x, v0.y);
    applyTransform(v1.x, v1.y);
    applyTransform(v2.x, v2.y);
    applyTransform(v3.x, v3.y);

    vertices_.push_back(v0);
    vertices_.push_back(v1);
    vertices_.push_back(v2);

    vertices_.push_back(v0);
    vertices_.push_back(v2);
    vertices_.push_back(v3);
}

void BatchRenderer2D::drawCatenaryBezier(Point2D p0, Point2D cp0, Point2D cp1, Point2D p1,
                                         float r, float g, float b, float a, float lineWidth) {
    constexpr int SEGMENTS = 32;
    float prevX = p0.x;
    float prevY = p0.y;

    for (int i = 1; i <= SEGMENTS; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(SEGMENTS);
        float u = 1.0f - t;
        float tt = t * t;
        float uu = u * u;
        float uuu = uu * u;
        float ttt = tt * t;

        float currX = uuu * p0.x + 3.0f * uu * t * cp0.x + 3.0f * u * tt * cp1.x + ttt * p1.x;
        float currY = uuu * p0.y + 3.0f * uu * t * cp0.y + 3.0f * u * tt * cp1.y + ttt * p1.y;

        drawLine(prevX, prevY, currX, currY, r, g, b, a, lineWidth);
        prevX = currX;
        prevY = currY;
    }
}

void BatchRenderer2D::drawPath(const Point2D* points, size_t count, float lineWidth,
                               float r, float g, float b, float a, bool closed) {
    if (!points || count < 2 || lineWidth <= 0.0f || a <= 0.0f) return;
    float halfW = std::max(0.5f, lineWidth * 0.5f);
    uint32_t col = packColor(r, g, b, a);

    const size_t segCount = closed ? count : (count - 1);
    constexpr size_t STACK_LIMIT = 64;
    struct Vec2 { float x, y; };

    Vec2 stackNormals[STACK_LIMIT];
    std::vector<Vec2> heapNormals;
    Vec2* normals = stackNormals;
    if (segCount > STACK_LIMIT) {
        heapNormals.resize(segCount);
        normals = heapNormals.data();
    }

    for (size_t i = 0; i < segCount; ++i) {
        size_t nextIdx = (i + 1) % count;
        float dx = points[nextIdx].x - points[i].x;
        float dy = points[nextIdx].y - points[i].y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1e-4f) {
            normals[i] = { (-dy / len) * halfW, (dx / len) * halfW };
        } else {
            normals[i] = { 0.0f, halfW };
        }
    }

    Vec2 stackOffsets[STACK_LIMIT];
    std::vector<Vec2> heapOffsets;
    Vec2* offsets = stackOffsets;
    if (count > STACK_LIMIT) {
        heapOffsets.resize(count);
        offsets = heapOffsets.data();
    }

    for (size_t i = 0; i < count; ++i) {
        if (!closed && i == 0) {
            offsets[i] = normals[0];
        } else if (!closed && i == count - 1) {
            offsets[i] = normals[count - 2];
        } else {
            size_t prevSeg = (i == 0) ? (count - 1) : (i - 1);
            size_t nextSeg = i % segCount;
            float mx = 0.5f * (normals[prevSeg].x + normals[nextSeg].x);
            float my = 0.5f * (normals[prevSeg].y + normals[nextSeg].y);
            float mLenSq = mx * mx + my * my;
            if (mLenSq < 1e-4f) {
                offsets[i] = normals[prevSeg];
            } else {
                float s = (halfW * halfW) / mLenSq;
                s = std::min(s, 4.0f); // Miter limit clamp
                offsets[i] = { mx * s, my * s };
            }
        }
    }

    for (size_t i = 0; i < segCount; ++i) {
        size_t i0 = i;
        size_t i1 = (i + 1) % count;

        Vertex2D v0{ points[i0].x + offsets[i0].x, points[i0].y + offsets[i0].y, 0.0f, 0.0f, col, 0, {0, 0} };
        Vertex2D v1{ points[i1].x + offsets[i1].x, points[i1].y + offsets[i1].y, 0.0f, 0.0f, col, 0, {0, 0} };
        Vertex2D v2{ points[i1].x - offsets[i1].x, points[i1].y - offsets[i1].y, 0.0f, 0.0f, col, 0, {0, 0} };
        Vertex2D v3{ points[i0].x - offsets[i0].x, points[i0].y - offsets[i0].y, 0.0f, 0.0f, col, 0, {0, 0} };

        applyTransform(v0.x, v0.y);
        applyTransform(v1.x, v1.y);
        applyTransform(v2.x, v2.y);
        applyTransform(v3.x, v3.y);

        vertices_.push_back(v0);
        vertices_.push_back(v1);
        vertices_.push_back(v2);

        vertices_.push_back(v0);
        vertices_.push_back(v2);
        vertices_.push_back(v3);
    }
}

void BatchRenderer2D::drawPathGradient(const Point2D* points, size_t count, float lineWidth,
                                       float r0, float g0, float b0, float a0,
                                       float r1, float g1, float b1, float a1,
                                       bool closed, bool parametric) {
    if (!points || count < 2 || lineWidth <= 0.0f) return;
    float halfW = std::max(0.5f, lineWidth * 0.5f);

    constexpr size_t STACK_LIMIT = 64;
    uint32_t stackCols[STACK_LIMIT];
    std::vector<uint32_t> heapCols;
    uint32_t* cols = stackCols;
    if (count > STACK_LIMIT) {
        heapCols.resize(count);
        cols = heapCols.data();
    }

    if (parametric) {
        float totalLen = 0.0f;
        float stackLens[STACK_LIMIT];
        std::vector<float> heapLens;
        float* lens = stackLens;
        if (count > STACK_LIMIT) {
            heapLens.resize(count);
            lens = heapLens.data();
        }
        lens[0] = 0.0f;
        for (size_t i = 0; i + 1 < count; ++i) {
            float dx = points[i + 1].x - points[i].x;
            float dy = points[i + 1].y - points[i].y;
            totalLen += std::sqrt(dx * dx + dy * dy);
            lens[i + 1] = totalLen;
        }
        float invTotal = (totalLen > 1e-4f) ? (1.0f / totalLen) : 0.0f;
        for (size_t i = 0; i < count; ++i) {
            float t = std::clamp(lens[i] * invTotal, 0.0f, 1.0f);
            cols[i] = packColor(r0 + (r1 - r0) * t,
                                g0 + (g1 - g0) * t,
                                b0 + (b1 - b0) * t,
                                a0 + (a1 - a0) * t);
        }
    } else {
        // Vertical/spatial gradient matching background gradient
        float minY = points[0].y;
        float maxY = points[0].y;
        for (size_t i = 1; i < count; ++i) {
            minY = std::min(minY, points[i].y);
            maxY = std::max(maxY, points[i].y);
        }
        float invH = (maxY > minY + 1e-4f) ? (1.0f / (maxY - minY)) : 0.0f;
        for (size_t i = 0; i < count; ++i) {
            float t = std::clamp((points[i].y - minY) * invH, 0.0f, 1.0f);
            cols[i] = packColor(r0 + (r1 - r0) * t,
                                g0 + (g1 - g0) * t,
                                b0 + (b1 - b0) * t,
                                a0 + (a1 - a0) * t);
        }
    }

    const size_t segCount = closed ? count : (count - 1);
    struct Vec2 { float x, y; };

    Vec2 stackNormals[STACK_LIMIT];
    std::vector<Vec2> heapNormals;
    Vec2* normals = stackNormals;
    if (segCount > STACK_LIMIT) {
        heapNormals.resize(segCount);
        normals = heapNormals.data();
    }

    for (size_t i = 0; i < segCount; ++i) {
        size_t nextIdx = (i + 1) % count;
        float dx = points[nextIdx].x - points[i].x;
        float dy = points[nextIdx].y - points[i].y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1e-4f) {
            normals[i] = { (-dy / len) * halfW, (dx / len) * halfW };
        } else {
            normals[i] = { 0.0f, halfW };
        }
    }

    Vec2 stackOffsets[STACK_LIMIT];
    std::vector<Vec2> heapOffsets;
    Vec2* offsets = stackOffsets;
    if (count > STACK_LIMIT) {
        heapOffsets.resize(count);
        offsets = heapOffsets.data();
    }

    for (size_t i = 0; i < count; ++i) {
        if (!closed && i == 0) {
            offsets[i] = normals[0];
        } else if (!closed && i == count - 1) {
            offsets[i] = normals[count - 2];
        } else {
            size_t prevSeg = (i == 0) ? (count - 1) : (i - 1);
            size_t nextSeg = i % segCount;
            float mx = 0.5f * (normals[prevSeg].x + normals[nextSeg].x);
            float my = 0.5f * (normals[prevSeg].y + normals[nextSeg].y);
            float mLenSq = mx * mx + my * my;
            if (mLenSq < 1e-4f) {
                offsets[i] = normals[prevSeg];
            } else {
                float s = (halfW * halfW) / mLenSq;
                s = std::min(s, 4.0f);
                offsets[i] = { mx * s, my * s };
            }
        }
    }

    for (size_t i = 0; i < segCount; ++i) {
        size_t i0 = i;
        size_t i1 = (i + 1) % count;

        Vertex2D v0{ points[i0].x + offsets[i0].x, points[i0].y + offsets[i0].y, 0.0f, 0.0f, cols[i0], 0, {0, 0} };
        Vertex2D v1{ points[i1].x + offsets[i1].x, points[i1].y + offsets[i1].y, 0.0f, 0.0f, cols[i1], 0, {0, 0} };
        Vertex2D v2{ points[i1].x - offsets[i1].x, points[i1].y - offsets[i1].y, 0.0f, 0.0f, cols[i1], 0, {0, 0} };
        Vertex2D v3{ points[i0].x - offsets[i0].x, points[i0].y - offsets[i0].y, 0.0f, 0.0f, cols[i0], 0, {0, 0} };

        applyTransform(v0.x, v0.y);
        applyTransform(v1.x, v1.y);
        applyTransform(v2.x, v2.y);
        applyTransform(v3.x, v3.y);

        vertices_.push_back(v0);
        vertices_.push_back(v1);
        vertices_.push_back(v2);

        vertices_.push_back(v0);
        vertices_.push_back(v2);
        vertices_.push_back(v3);
    }
}

void BatchRenderer2D::drawPathBezier(const Point2D* controlPoints, size_t count, float lineWidth,
                                     float r, float g, float b, float a, int segmentsPerCurve) {
    if (!controlPoints || count < 4 || lineWidth <= 0.0f) return;
    int segs = std::clamp(segmentsPerCurve, 4, 64);

    size_t numCurves = (count - 1) / 3;
    if (numCurves == 0) return;

    std::vector<Point2D> pts;
    pts.reserve(numCurves * segs + 1);
    pts.push_back(controlPoints[0]);

    for (size_t c = 0; c < numCurves; ++c) {
        const Point2D& p0 = controlPoints[c * 3 + 0];
        const Point2D& cp0 = controlPoints[c * 3 + 1];
        const Point2D& cp1 = controlPoints[c * 3 + 2];
        const Point2D& p1 = controlPoints[c * 3 + 3];

        for (int i = 1; i <= segs; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(segs);
            float u = 1.0f - t;
            float tt = t * t;
            float uu = u * u;
            float uuu = uu * u;
            float ttt = tt * t;

            float x = uuu * p0.x + 3.0f * uu * t * cp0.x + 3.0f * u * tt * cp1.x + ttt * p1.x;
            float y = uuu * p0.y + 3.0f * uu * t * cp0.y + 3.0f * u * tt * cp1.y + ttt * p1.y;
            pts.push_back({ x, y });
        }
    }

    drawPath(pts.data(), pts.size(), lineWidth, r, g, b, a, false);
}

void BatchRenderer2D::drawPatternedRectGradient(float x, float y, float w, float h,
                                                float r0, float g0, float b0,
                                                float r1, float g1, float b1, float a,
                                                VectorPatternType pattern,
                                                float patternIntensity,
                                                float patternScale,
                                                float cornerRadius) {
    if (w <= 0.0f || h <= 0.0f || a <= 0.0f) return;

    // 1. Draw base procedural gradient quad (rounded if cornerRadius > 0.5f)
    if (cornerRadius > 0.5f) {
        drawRoundedRectGradient(x, y, w, h, cornerRadius, r0, g0, b0, r1, g1, b1, a);
    } else {
        drawRectGradient(x, y, w, h, r0, g0, b0, r1, g1, b1, a);
    }

    if (pattern == VectorPatternType::None || patternIntensity <= 0.001f) return;

    // 2. Scoped Scissor to keep all pattern lines/geometry strictly inside rectangle
    ScissorScope scissor(this, x, y, w, h);

    const float intensity = std::clamp(patternIntensity, 0.01f, 1.0f);
    const float scale = std::max(2.0f, patternScale);

    if (pattern == VectorPatternType::HorizontalSlats) {
        // Optimal 1D Separable pattern: Milled ribs / horizontal slats / brushed grain
        // Adaptive LOD: clamp step to bound vertex/scanline count regardless of fullscreen resolution
        const float stepY = std::max(scale, h / 80.0f);
        for (float sy = y + stepY; sy < y + h; sy += stepY) {
            float t = (h > 0.001f) ? ((sy - y) / h) : 0.0f;
            float rBase = r0 + (r1 - r0) * t;
            float gBase = g0 + (g1 - g0) * t;
            float bBase = b0 + (b1 - b0) * t;

            // Specular rib highlight on top edge (direct axis-aligned rect quad for peak performance)
            float rHi = std::min(1.0f, rBase + intensity * 1.5f);
            float gHi = std::min(1.0f, gBase + intensity * 1.5f);
            float bHi = std::min(1.0f, bBase + intensity * 1.5f);
            drawRect(x, sy, w, 1.0f, rHi, gHi, bHi, std::min(1.0f, a * 0.95f));

            // Milled groove recession shadow on bottom edge
            float rLo = std::max(0.0f, rBase - intensity * 1.1f);
            float gLo = std::max(0.0f, gBase - intensity * 1.1f);
            float bLo = std::max(0.0f, bBase - intensity * 1.1f);
            drawRect(x, sy + 1.0f, w, 1.0f, rLo, gLo, bLo, std::min(1.0f, a * 0.95f));
        }
    } else if (pattern == VectorPatternType::DiagonalMesh) {
        // Carbon fiber / Kevlar diagonal cross-hatch with adaptive LOD
        const float diagStep = std::max(scale, (w + h) / 80.0f);
        float rMid = (r0 + r1) * 0.5f;
        float gMid = (g0 + g1) * 0.5f;
        float bMid = (b0 + b1) * 0.5f;
        float rHi = std::min(1.0f, rMid + intensity * 1.6f);
        float gHi = std::min(1.0f, gMid + intensity * 1.6f);
        float bHi = std::min(1.0f, bMid + intensity * 1.6f);
        float rLo = std::max(0.0f, rMid - intensity * 1.2f);
        float gLo = std::max(0.0f, gMid - intensity * 1.2f);
        float bLo = std::max(0.0f, bMid - intensity * 1.2f);

        // +45 degree lines (highlight sheen)
        for (float d = -h; d < w + h; d += diagStep) {
            float x0 = x + d;
            float y0 = y;
            float x1 = x + d + h;
            float y1 = y + h;
            drawLine(x0, y0, x1, y1, rHi, gHi, bHi, std::min(1.0f, a * 0.9f), 1.0f);
        }
        // -45 degree lines (shadow weave)
        for (float d = 0.0f; d < w + 2.0f * h; d += diagStep) {
            float x0 = x + d;
            float y0 = y;
            float x1 = x + d - h;
            float y1 = y + h;
            drawLine(x0, y0, x1, y1, rLo, gLo, bLo, std::min(1.0f, a * 0.9f), 1.0f);
        }
    } else if (pattern == VectorPatternType::HexHoneycomb) {
        // Hexagonal mesh with adaptive radius clamping
        const float hexR = std::max(scale * 0.5f, std::max(w, h) / 50.0f);
        const float hexW = hexR * 1.7320508f;
        const float hexH = hexR * 1.5f;
        int row = 0;
        for (float hy = y + hexR; hy < y + h + hexR; hy += hexH, ++row) {
            float xOffset = (row % 2 == 1) ? (hexW * 0.5f) : 0.0f;
            for (float hx = x + xOffset; hx < x + w + hexW; hx += hexW) {
                float t = (h > 0.001f) ? std::clamp((hy - y) / h, 0.0f, 1.0f) : 0.0f;
                float rBase = r0 + (r1 - r0) * t;
                float gBase = g0 + (g1 - g0) * t;
                float bBase = b0 + (b1 - b0) * t;
                float rHex = std::min(1.0f, rBase + intensity * 1.6f);
                float gHex = std::min(1.0f, gBase + intensity * 1.6f);
                float bHex = std::min(1.0f, bBase + intensity * 1.6f);

                Point2D hexPts[6];
                for (int v = 0; v < 6; ++v) {
                    float ang = static_cast<float>(v) * 1.04719755f;
                    hexPts[v] = { hx + hexR * std::cos(ang), hy + hexR * std::sin(ang) };
                }
                drawPath(hexPts, 6, 1.0f, rHex, gHex, bHex, std::min(1.0f, a * 0.9f), true);
            }
        }
    } else if (pattern == VectorPatternType::DotGrid) {
        // Precision instrument breadboard / stipple matrix with fast point quads
        const float dotStep = std::max(scale, std::max(w, h) / 45.0f);
        for (float dy = y + dotStep * 0.5f; dy < y + h; dy += dotStep) {
            float t = (h > 0.001f) ? std::clamp((dy - y) / h, 0.0f, 1.0f) : 0.0f;
            float rBase = r0 + (r1 - r0) * t;
            float gBase = g0 + (g1 - g0) * t;
            float bBase = b0 + (b1 - b0) * t;
            float rDot = std::min(1.0f, rBase + intensity * 1.8f);
            float gDot = std::min(1.0f, gBase + intensity * 1.8f);
            float bDot = std::min(1.0f, bBase + intensity * 1.8f);

            for (float dx = x + dotStep * 0.5f; dx < x + w; dx += dotStep) {
                // High-performance direct quad dot instead of heavy analytical SDF circle
                drawRect(dx - 0.75f, dy - 0.75f, 1.5f, 1.5f, rDot, gDot, bDot, std::min(1.0f, a * 0.9f));
            }
        }
    } else if (pattern == VectorPatternType::CircuitTraces) {
        // Silkscreen PCB / synth circuit traces
        const float traceStep = std::max(scale * 2.0f, h / 16.0f);
        float rTrace = std::min(1.0f, (r0 + r1) * 0.5f + intensity * 2.2f);
        float gTrace = std::min(1.0f, (g0 + g1) * 0.5f + intensity * 2.2f);
        float bTrace = std::min(1.0f, (b0 + b1) * 0.5f + intensity * 2.2f);

        int traceIdx = 0;
        for (float cy = y + 14.0f; cy < y + h - 14.0f; cy += traceStep, ++traceIdx) {
            float startX = x + 16.0f + static_cast<float>((traceIdx * 23) % 40);
            float bendX1 = startX + 30.0f + static_cast<float>((traceIdx * 17) % 30);
            float bendY = cy + ((traceIdx % 2 == 0) ? 14.0f : -14.0f);
            float endX = std::min(x + w - 16.0f, bendX1 + 45.0f + static_cast<float>((traceIdx * 31) % 40));

            Point2D tracePts[4] = {
                { startX, cy },
                { bendX1, cy },
                { bendX1 + 14.0f, bendY },
                { endX, bendY }
            };
            drawPath(tracePts, 4, 1.4f, rTrace, gTrace, bTrace, std::min(1.0f, a * 0.95f), false);
            drawCircle(startX, cy, 3.0f, rTrace, gTrace, bTrace, std::min(1.0f, a * 0.95f), 12);
            drawCircle(endX, bendY, 3.0f, rTrace, gTrace, bTrace, std::min(1.0f, a * 0.95f), 12);
            drawCircle(startX, cy, 1.4f, r0, g0, b0, a, 8);
            drawCircle(endX, bendY, 1.4f, r0, g0, b0, a, 8);
        }
    } else if (pattern == VectorPatternType::BrushedSteel) {
        // Multi-frequency hairline brushed metal with high-contrast specular highlights & micro-abrasions
        const float baseStep = std::max(scale * 0.5f, h / 120.0f);
        int lineIdx = 0;
        for (float sy = y + 2.0f; sy < y + h - 1.0f; sy += baseStep, ++lineIdx) {
            float t = (h > 0.001f) ? ((sy - y) / h) : 0.0f;
            float rBase = r0 + (r1 - r0) * t;
            float gBase = g0 + (g1 - g0) * t;
            float bBase = b0 + (b1 - b0) * t;

            float jitter = static_cast<float>((lineIdx * 37) % 7) / 7.0f;
            float lineIntensity = intensity * (0.6f + 0.8f * jitter);

            float rHi = std::min(1.0f, rBase + lineIntensity * 1.8f);
            float gHi = std::min(1.0f, gBase + lineIntensity * 1.8f);
            float bHi = std::min(1.0f, bBase + lineIntensity * 1.8f);

            float rLo = std::max(0.0f, rBase - lineIntensity * 1.3f);
            float gLo = std::max(0.0f, gBase - lineIntensity * 1.3f);
            float bLo = std::max(0.0f, bBase - lineIntensity * 1.3f);

            if (lineIdx % 6 == 0) {
                float segW1 = w * 0.42f;
                float segGap = w * 0.08f;
                drawRect(x, sy, segW1, 1.0f, rHi, gHi, bHi, std::min(1.0f, a * 0.85f));
                drawRect(x + segW1 + segGap, sy, w - segW1 - segGap, 1.0f, rHi, gHi, bHi, std::min(1.0f, a * 0.85f));
            } else {
                drawRect(x, sy, w, 1.0f, rHi, gHi, bHi, std::min(1.0f, a * 0.85f));
            }
            drawRect(x, sy + 1.0f, w, 1.0f, rLo, gLo, bLo, std::min(1.0f, a * 0.85f));
        }
    } else if (pattern == VectorPatternType::WoodGrain) {
        // Organic wavy growth-ring lines (Walnut / Rosewood)
        const float grainStep = std::max(scale * 1.5f, h / 28.0f);
        int ringIdx = 0;
        for (float gy = y + grainStep * 0.6f; gy < y + h; gy += grainStep, ++ringIdx) {
            float t = (h > 0.001f) ? ((gy - y) / h) : 0.0f;
            float rBase = r0 + (r1 - r0) * t;
            float gBase = g0 + (g1 - g0) * t;
            float bBase = b0 + (b1 - b0) * t;

            float rWood = std::max(0.0f, rBase - intensity * 1.6f);
            float gWood = std::max(0.0f, gBase - intensity * 1.6f);
            float bWood = std::max(0.0f, bBase - intensity * 1.6f);

            Point2D pts[7];
            const int segCount = 6;
            float dx = w / static_cast<float>(segCount);
            float phase = static_cast<float>(ringIdx) * 1.15f;
            float amp = 4.0f + static_cast<float>(ringIdx % 4) * 2.5f;
            for (int s = 0; s <= segCount; ++s) {
                float sx = x + static_cast<float>(s) * dx;
                float sy = gy + amp * std::sin(static_cast<float>(s) * 0.9f + phase)
                              + (amp * 0.4f) * std::cos(static_cast<float>(s) * 1.7f + phase * 0.7f);
                pts[s] = { sx, sy };
            }
            drawPath(pts, segCount + 1, 1.5f, rWood, gWood, bWood, std::min(1.0f, a * 0.88f), false);
        }
    } else if (pattern == VectorPatternType::SpeakerGrill) {
        // Vintage amplifier interlocking diamond woven fabric grill
        const float grillStep = std::max(scale, (w + h) / 60.0f);
        float rHi = std::min(1.0f, (r0 + r1) * 0.5f + intensity * 1.7f);
        float gHi = std::min(1.0f, (g0 + g1) * 0.5f + intensity * 1.7f);
        float bHi = std::min(1.0f, (b0 + b1) * 0.5f + intensity * 1.7f);
        float rLo = std::max(0.0f, (r0 + r1) * 0.5f - intensity * 1.4f);
        float gLo = std::max(0.0f, (g0 + g1) * 0.5f - intensity * 1.4f);
        float bLo = std::max(0.0f, (b0 + b1) * 0.5f - intensity * 1.4f);

        for (float d = -h * 0.6f; d < w + h * 0.6f; d += grillStep) {
            drawLine(x + d, y, x + d + h * 0.577f, y + h, rHi, gHi, bHi, std::min(1.0f, a * 0.9f), 1.2f);
        }
        for (float d = 0.0f; d < w + h * 1.2f; d += grillStep) {
            drawLine(x + d, y, x + d - h * 0.577f, y + h, rLo, gLo, bLo, std::min(1.0f, a * 0.9f), 1.2f);
        }
        const float nodeStep = grillStep * 1.5f;
        for (float ny = y + nodeStep * 0.5f; ny < y + h; ny += nodeStep) {
            for (float nx = x + nodeStep * 0.5f; nx < x + w; nx += nodeStep) {
                drawRect(nx - 0.8f, ny - 0.8f, 1.6f, 1.6f, rHi, gHi, bHi, std::min(1.0f, a * 0.75f));
            }
        }
    } else if (pattern == VectorPatternType::VintagePatina) {
        // Physical vintage patina: subtle vignette darkening + deterministic dust flecks and hairline wear
        drawRectVignette(x, y, w, h, 0.0f, intensity * 1.8f, 0.0f, 0.0f, 0.0f);
        drawDustSpecks(x, y, w, h, 50, 0x1337BEEFu, 0.90f, 0.90f, 0.88f, intensity * 1.5f);
        drawLine(x, y + 1.0f, x + w, y + 1.0f, 1.0f, 1.0f, 1.0f, std::min(1.0f, intensity * 0.6f), 1.0f);
        drawLine(x + 1.0f, y, x + 1.0f, y + h, 1.0f, 1.0f, 1.0f, std::min(1.0f, intensity * 0.4f), 1.0f);
    }
}

void BatchRenderer2D::drawRectVignette(float x, float y, float w, float h,
                                       float innerAlpha, float outerAlpha,
                                       float r, float g, float b) {
    if (w <= 0.0f || h <= 0.0f) return;

    // 16-triangle center-fan Gouraud shaded mesh.
    // Center point (cx, cy) with innerAlpha, 16 perimeter points with outerAlpha.
    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;

    uint32_t colInner = packColor(r, g, b, std::clamp(innerAlpha, 0.0f, 1.0f));
    uint32_t colOuter = packColor(r, g, b, std::clamp(outerAlpha, 0.0f, 1.0f));

    Vertex2D centerV{cx, cy, 0.0f, 0.0f, colInner, 5, {0, 0}};
    applyTransform(centerV.x, centerV.y);

    Point2D perimeter[16] = {
        { x, y },
        { x + w * 0.25f, y },
        { x + w * 0.50f, y },
        { x + w * 0.75f, y },
        { x + w, y },
        { x + w, y + h * 0.25f },
        { x + w, y + h * 0.50f },
        { x + w, y + h * 0.75f },
        { x + w, y + h },
        { x + w * 0.75f, y + h },
        { x + w * 0.50f, y + h },
        { x + w * 0.25f, y + h },
        { x, y + h },
        { x, y + h * 0.75f },
        { x, y + h * 0.50f },
        { x, y + h * 0.25f }
    };

    for (int i = 0; i < 16; ++i) {
        Point2D p0 = perimeter[i];
        Point2D p1 = perimeter[(i + 1) % 16];

        Vertex2D v0{p0.x, p0.y, 0.0f, 0.0f, colOuter, 5, {0, 0}};
        Vertex2D v1{p1.x, p1.y, 0.0f, 0.0f, colOuter, 5, {0, 0}};
        applyTransform(v0.x, v0.y);
        applyTransform(v1.x, v1.y);

        vertices_.push_back(centerV);
        vertices_.push_back(v0);
        vertices_.push_back(v1);
    }
}

void BatchRenderer2D::drawDustSpecks(float x, float y, float w, float h,
                                     int count, uint32_t seed,
                                     float r, float g, float b,
                                     float maxAlpha) {
    if (w <= 0.0f || h <= 0.0f || count <= 0 || maxAlpha <= 0.001f) return;

    // Linear Congruential Generator (LCG) state for deterministic pseudo-random distribution
    uint32_t state = (seed != 0) ? seed : 0xDEADBEEFu;
    auto lcgNext = [&state]() -> uint32_t {
        state = state * 1664525u + 1013904223u;
        return state;
    };

    int numFlecks = std::clamp(count, 1, 300);
    for (int i = 0; i < numFlecks; ++i) {
        float rx = static_cast<float>((lcgNext() >> 16) % 10000) / 10000.0f;
        float ry = static_cast<float>((lcgNext() >> 16) % 10000) / 10000.0f;
        float rAlpha = static_cast<float>((lcgNext() >> 16) % 1000) / 1000.0f;
        uint32_t rType = lcgNext();

        float px = x + rx * w;
        float py = y + ry * h;
        float alpha = std::clamp(rAlpha * maxAlpha, 0.01f, 1.0f);

        // 80% micro-dust specks, 20% tiny hairline scratches/fibers
        if ((rType % 5) == 0) {
            float angle = static_cast<float>((rType >> 8) % 628) / 100.0f;
            float len = 2.0f + static_cast<float>((rType >> 16) % 3);
            float x1 = std::clamp(px + std::cos(angle) * len, x, x + w);
            float y1 = std::clamp(py + std::sin(angle) * len, y, y + h);
            drawLine(px, py, x1, y1, r, g, b, alpha, 1.0f);
        } else {
            float sz = ((rType % 7) == 0) ? 1.5f : 1.0f;
            drawRect(px, py, sz, sz, r, g, b, alpha);
        }
    }
}

void BatchRenderer2D::updateFontAtlas(int x, int y, int w, int h, const unsigned char* data, int atlasW, int atlasH) {
    if (backend_) {
        backend_->updateFontTexture(x, y, w, h, data, atlasW, atlasH);
    }
}

void BatchRenderer2D::drawTexturedTriangles(const float* verts, const float* tcoords, const unsigned int* colors, int nverts) {
    if (!verts || !tcoords || !colors || nverts <= 0) return;

    for (int i = 0; i < nverts; ++i) {
        Vertex2D v{};
        v.x = verts[i * 2 + 0];
        v.y = verts[i * 2 + 1];
        applyTransform(v.x, v.y);
        v.u = tcoords[i * 2 + 0];
        v.v = tcoords[i * 2 + 1];
        v.color = colors[i];
        v.mode = 1; // Font atlas mode
        vertices_.push_back(v);
    }
}

void BatchRenderer2D::drawCircleDropShadow(float cx, float cy, float radius, float elevation,
                                           const LightSource2D& light, float opacity) {
    if (radius <= 0.0f || elevation <= 0.0f || opacity <= 0.001f) return;
    float worldCx = cx;
    float worldCy = cy;
    applyTransform(worldCx, worldCy);
    ShadowOffset shadow = light.computeShadowOffset(worldCx, worldCy, elevation, opacity);

    float dx = shadow.dx;
    float dy = shadow.dy;
    if (transformActive_) {
        float rdx = dx * transformCos_ + dy * transformSin_;
        float rdy = -dx * transformSin_ + dy * transformCos_;
        dx = rdx;
        dy = rdy;
    }

    float sx = cx + dx;
    float sy = cy + dy;
    float outset = std::max(2.0f, shadow.blur * 1.15f);
    float totalHalf = radius + outset;

    uint32_t col = packColor(0.0f, 0.0f, 0.0f, std::clamp(shadow.opacity * 0.68f, 0.0f, 1.0f));
    Vertex2D v0{sx - totalHalf, sy - totalHalf, radius, outset, col, 2, {-totalHalf, -totalHalf}, {99999.0f, 99999.0f}};
    Vertex2D v1{sx + totalHalf, sy - totalHalf, radius, outset, col, 2, { totalHalf, -totalHalf}, {99999.0f, 99999.0f}};
    Vertex2D v2{sx + totalHalf, sy + totalHalf, radius, outset, col, 2, { totalHalf,  totalHalf}, {99999.0f, 99999.0f}};
    Vertex2D v3{sx - totalHalf, sy + totalHalf, radius, outset, col, 2, {-totalHalf,  totalHalf}, {99999.0f, 99999.0f}};

    applyTransform(v0.x, v0.y);
    applyTransform(v1.x, v1.y);
    applyTransform(v2.x, v2.y);
    applyTransform(v3.x, v3.y);

    vertices_.push_back(v0);
    vertices_.push_back(v1);
    vertices_.push_back(v2);
    vertices_.push_back(v0);
    vertices_.push_back(v2);
    vertices_.push_back(v3);
}

void BatchRenderer2D::drawRectDropShadow(float x, float y, float w, float h, float cornerRadius,
                                         float elevation, const LightSource2D& light, float opacity,
                                         float prevCardX, float prevCardY) {
    if (w <= 0.0f || h <= 0.0f || elevation <= 0.0f || opacity <= 0.001f) return;
    float worldCx = x + w * 0.5f;
    float worldCy = y + h * 0.5f;
    applyTransform(worldCx, worldCy);
    ShadowOffset shadow = light.computeShadowOffset(worldCx, worldCy, elevation, opacity);

    float scx = worldCx + shadow.dx;
    float scy = worldCy + shadow.dy;

    float relPrevX = 99999.0f;
    float relPrevY = 99999.0f;
    if (std::abs(prevCardX) < 9000.0f && std::abs(prevCardY) < 9000.0f) {
        float prevWorldCx = prevCardX + w * 0.5f;
        float prevWorldCy = prevCardY + h * 0.5f;
        ShadowOffset prevShadow = light.computeShadowOffset(prevWorldCx, prevWorldCy, elevation, opacity);
        relPrevX = (prevWorldCx + prevShadow.dx) - scx;
        relPrevY = (prevWorldCy + prevShadow.dy) - scy;
    }

    float outset = std::max(2.0f, shadow.blur * 1.15f);
    float halfW = w * 0.5f + outset;
    float halfH = h * 0.5f + outset;
    float effRadius = std::clamp(cornerRadius, 0.0f, std::min(w, h) * 0.5f);

    uint32_t col = packColor(0.0f, 0.0f, 0.0f, std::clamp(shadow.opacity * 0.62f, 0.0f, 1.0f));
    Vertex2D v0{scx - halfW, scy - halfH, effRadius, outset, col, 2, {-halfW, -halfH}, {relPrevX, relPrevY}};
    Vertex2D v1{scx + halfW, scy - halfH, effRadius, outset, col, 2, { halfW, -halfH}, {relPrevX, relPrevY}};
    Vertex2D v2{scx + halfW, scy + halfH, effRadius, outset, col, 2, { halfW,  halfH}, {relPrevX, relPrevY}};
    Vertex2D v3{scx - halfW, scy + halfH, effRadius, outset, col, 2, {-halfW,  halfH}, {relPrevX, relPrevY}};

    vertices_.push_back(v0);
    vertices_.push_back(v1);
    vertices_.push_back(v2);
    vertices_.push_back(v0);
    vertices_.push_back(v2);
    vertices_.push_back(v3);
}

void BatchRenderer2D::drawOrientedDropShadowQuad(float x0, float y0, float x1, float y1,
                                                 float x2, float y2, float x3, float y3,
                                                 float halfW, float halfH, float cornerRadius,
                                                 float feather, float opacity) {
    if (halfW <= 0.0f || halfH <= 0.0f || opacity <= 0.001f) return;
    float outset = std::max(1.5f, feather);
    float effRadius = std::max(0.0f, cornerRadius);
    uint32_t col = packColor(0.0f, 0.0f, 0.0f, std::clamp(opacity, 0.0f, 1.0f));

    Vertex2D v0{x0, y0, effRadius, outset, col, 2, {-halfW, -halfH}, {99999.0f, 99999.0f}};
    Vertex2D v1{x1, y1, effRadius, outset, col, 2, { halfW, -halfH}, {99999.0f, 99999.0f}};
    Vertex2D v2{x2, y2, effRadius, outset, col, 2, { halfW,  halfH}, {99999.0f, 99999.0f}};
    Vertex2D v3{x3, y3, effRadius, outset, col, 2, {-halfW,  halfH}, {99999.0f, 99999.0f}};

    applyTransform(v0.x, v0.y);
    applyTransform(v1.x, v1.y);
    applyTransform(v2.x, v2.y);
    applyTransform(v3.x, v3.y);

    vertices_.push_back(v0);
    vertices_.push_back(v1);
    vertices_.push_back(v2);
    vertices_.push_back(v0);
    vertices_.push_back(v2);
    vertices_.push_back(v3);
}

void BatchRenderer2D::drawFlutedRing(float cx, float cy, float rInner, float rOuter, int numFlutes,
                                      float fluteDepthRatio, float rotationAngle,
                                      const LightSource2D& light,
                                      float baseR, float baseG, float baseB,
                                      float highlightR, float highlightG, float highlightB,
                                      float shadowR, float shadowG, float shadowB) {
    if (numFlutes < 3 || rInner >= rOuter) return;

    Point lDir = light.getLightDirection(cx, cy);
    float depth = std::clamp(fluteDepthRatio, 0.02f, 0.45f) * (rOuter - rInner);
    (void)depth;

    // Determine sample density: 4 segments per flute, clamped to [32, 96]
    int segs = std::clamp(numFlutes * 4, 32, 96);
    float step = 6.283185307179586f / static_cast<float>(segs);
    float fluteScale = static_cast<float>(numFlutes) * 0.5f;

    struct ContourPt {
        float ox, oy;
        float ix, iy;
        uint32_t colOuter;
        uint32_t colInner;
    };

    auto computePt = [&](int i) -> ContourPt {
        float a = rotationAngle + static_cast<float>(i) * step;
        float phase = static_cast<float>(i) * step * fluteScale;
        float s = std::sin(phase);
        float fluteFactor = s * s; // [0, 1] smooth harmonic wave
        float curR = rOuter - depth * fluteFactor;

        float ca = std::cos(a);
        float sa = std::sin(a);

        // Compute surface normal with derivative for physical lighting
        float dr_da = -depth * fluteScale * std::sin(2.0f * phase);
        float tx = ca * dr_da - curR * sa;
        float ty = sa * dr_da + curR * ca;
        float tlen = std::hypot(tx, ty);
        float nx = (tlen > 0.001f) ? (ty / tlen) : ca;
        float ny = (tlen > 0.001f) ? (-tx / tlen) : sa;

        float dotL = nx * lDir.x + ny * lDir.y;
        float normDot = std::clamp(dotL, -1.0f, 1.0f);
        float blend = (normDot + 1.0f) * 0.5f; // [0, 1]

        // Ambient occlusion deepening in the flute troughs
        float ao = 1.0f - (fluteFactor * 0.32f);

        // Specular glint on crests
        float spec = 0.0f;
        if (dotL > 0.50f && fluteFactor < 0.35f) {
            spec = std::pow((dotL - 0.50f) / 0.50f, 3.2f) * 0.38f;
        }

        float rCol = (shadowR + blend * (baseR - shadowR)) * ao;
        float gCol = (shadowG + blend * (baseG - shadowG)) * ao;
        float bCol = (shadowB + blend * (baseB - shadowB)) * ao;

        if (dotL > 0.0f) {
            rCol += (highlightR - rCol) * (dotL * 0.45f) + spec;
            gCol += (highlightG - gCol) * (dotL * 0.45f) + spec;
            bCol += (highlightB - bCol) * (dotL * 0.45f) + spec;
        }

        uint32_t cOuter = packColor(std::clamp(rCol, 0.0f, 1.0f),
                                    std::clamp(gCol, 0.0f, 1.0f),
                                    std::clamp(bCol, 0.0f, 1.0f), 1.0f);

        // Inner rim is subtly recessed
        uint32_t cInner = packColor(std::clamp(rCol * 0.75f, 0.0f, 1.0f),
                                    std::clamp(gCol * 0.75f, 0.0f, 1.0f),
                                    std::clamp(bCol * 0.75f, 0.0f, 1.0f), 1.0f);

        return {cx + curR * ca, cy + curR * sa,
                cx + rInner * ca, cy + rInner * sa,
                cOuter, cInner};
    };

    // 1. Draw smooth anti-aliased base disc underneath using mode 2 analytical SDF.
    // This seals the silhouette against the chassis and guarantees smooth AA outer rim.
    drawCircle(cx, cy, rOuter, baseR, baseG, baseB, 1.0f);

    // 2. Emit continuous fluted quad strip using mode 5 (point-sampled, zero internal seams)
    ContourPt prev = computePt(0);
    for (int i = 1; i <= segs; ++i) {
        ContourPt curr = computePt(i);

        Vertex2D v0{prev.ix, prev.iy, 0.0f, 0.0f, prev.colInner, 5, {0, 0}};
        Vertex2D v1{prev.ox, prev.oy, 0.0f, 0.0f, prev.colOuter, 5, {0, 0}};
        Vertex2D v2{curr.ox, curr.oy, 0.0f, 0.0f, curr.colOuter, 5, {0, 0}};
        Vertex2D v3{curr.ix, curr.iy, 0.0f, 0.0f, curr.colInner, 5, {0, 0}};

        vertices_.push_back(v0);
        vertices_.push_back(v1);
        vertices_.push_back(v2);

        vertices_.push_back(v0);
        vertices_.push_back(v2);
        vertices_.push_back(v3);

        prev = curr;
    }
}

void BatchRenderer2D::drawKnurledRing(float cx, float cy, float rInner, float rOuter,
                                      int numTeeth, float rotationAngle,
                                      const LightSource2D& light,
                                      float baseR, float baseG, float baseB,
                                      float highlightR, float highlightG, float highlightB,
                                      float shadowR, float shadowG, float shadowB) {
    if (numTeeth < 6 || rInner >= rOuter) return;
    // Upgrade knurled ring to use continuous smooth fluted contouring
    float depthRatio = std::clamp((rOuter - rInner) / rOuter * 0.45f, 0.10f, 0.28f);
    drawFlutedRing(cx, cy, rInner, rOuter, numTeeth, depthRatio, rotationAngle, light,
                   baseR, baseG, baseB, highlightR, highlightG, highlightB, shadowR, shadowG, shadowB);
}

void BatchRenderer2D::drawChickenHeadContour(float cx, float cy, float radius, float rotationAngle,
                                            const LightSource2D& light,
                                            float baseR, float baseG, float baseB,
                                            float highlightR, float highlightG, float highlightB,
                                            float shadowR, float shadowG, float shadowB,
                                            float pointerR, float pointerG, float pointerB) {
    if (radius < 4.0f) return;

    Point lDir = light.getLightDirection(cx, cy);
    float cosA = std::cos(rotationAngle);
    float sinA = std::sin(rotationAngle);

    // Coordinate transformation helper: local (x, y) where +Y is forward pointer direction
    auto toScreen = [&](float lx, float ly) -> Point2D {
        return {cx + (lx * cosA - ly * sinA),
                cy + (lx * sinA + ly * cosA)};
    };

    // Authentic Davies chicken-head dimensions
    float noseLen = radius * 1.25f;
    float noseTipW = radius * 0.16f;
    float midW = radius * 0.72f;
    float rearR = radius * 0.65f;

    // Bevel shading for left flank vs right flank
    // Left flank normal tilts (-0.6, 0.35)
    float dotLeft = (-0.6f * cosA - 0.35f * sinA) * lDir.x + (-0.6f * sinA + 0.35f * cosA) * lDir.y;
    float blendLeft = (std::clamp(dotLeft, -1.0f, 1.0f) + 1.0f) * 0.5f;

    // Right flank normal tilts (+0.6, 0.35)
    float dotRight = (0.6f * cosA - 0.35f * sinA) * lDir.x + (0.6f * sinA + 0.35f * cosA) * lDir.y;
    float blendRight = (std::clamp(dotRight, -1.0f, 1.0f) + 1.0f) * 0.5f;

    uint32_t colLeft = packColor(std::clamp(shadowR + blendLeft * (highlightR - shadowR), 0.0f, 1.0f),
                                 std::clamp(shadowG + blendLeft * (highlightG - shadowG), 0.0f, 1.0f),
                                 std::clamp(shadowB + blendLeft * (highlightB - shadowB), 0.0f, 1.0f), 1.0f);

    uint32_t colRight = packColor(std::clamp(shadowR + blendRight * (highlightR - shadowR), 0.0f, 1.0f),
                                  std::clamp(shadowG + blendRight * (highlightG - shadowG), 0.0f, 1.0f),
                                  std::clamp(shadowB + blendRight * (highlightB - shadowB), 0.0f, 1.0f), 1.0f);

    uint32_t colCrest = packColor(baseR, baseG, baseB, 1.0f);

    Point2D pNoseTip = toScreen(0.0f, -noseLen);
    Point2D pNoseL = toScreen(-noseTipW, -noseLen + radius * 0.12f);
    Point2D pNoseR = toScreen(noseTipW, -noseLen + radius * 0.12f);
    Point2D pMidL = toScreen(-midW, -radius * 0.15f);
    Point2D pMidR = toScreen(midW, -radius * 0.15f);
    Point2D pCenter = toScreen(0.0f, 0.0f);
    Point2D pRearCenter = toScreen(0.0f, rearR);

    // Left flank triangle fan (mode 5: zero internal seam bleed)
    Vertex2D vl0{pCenter.x, pCenter.y, 0.0f, 0.0f, colCrest, 5, {0, 0}};
    Vertex2D vl1{pNoseTip.x, pNoseTip.y, 0.0f, 0.0f, colLeft, 5, {0, 0}};
    Vertex2D vl2{pNoseL.x, pNoseL.y, 0.0f, 0.0f, colLeft, 5, {0, 0}};
    Vertex2D vl3{pMidL.x, pMidL.y, 0.0f, 0.0f, colLeft, 5, {0, 0}};

    vertices_.push_back(vl0); vertices_.push_back(vl1); vertices_.push_back(vl2);
    vertices_.push_back(vl0); vertices_.push_back(vl2); vertices_.push_back(vl3);

    // Right flank triangle fan (mode 5: zero internal seam bleed)
    Vertex2D vr0{pCenter.x, pCenter.y, 0.0f, 0.0f, colCrest, 5, {0, 0}};
    Vertex2D vr1{pNoseTip.x, pNoseTip.y, 0.0f, 0.0f, colRight, 5, {0, 0}};
    Vertex2D vr2{pNoseR.x, pNoseR.y, 0.0f, 0.0f, colRight, 5, {0, 0}};
    Vertex2D vr3{pMidR.x, pMidR.y, 0.0f, 0.0f, colRight, 5, {0, 0}};

    vertices_.push_back(vr0); vertices_.push_back(vr2); vertices_.push_back(vr1);
    vertices_.push_back(vr0); vertices_.push_back(vr3); vertices_.push_back(vr2);

    // Rear semicircle heel (mode 5: zero internal seam bleed)
    constexpr int kRearSegs = 14;
    float rearStep = 3.14159265f / static_cast<float>(kRearSegs);
    for (int i = 0; i < kRearSegs; ++i) {
        float a0 = static_cast<float>(i) * rearStep;
        float a1 = a0 + rearStep;
        Point2D r0 = toScreen(std::cos(a0) * rearR, std::sin(a0) * rearR);
        Point2D r1 = toScreen(std::cos(a1) * rearR, std::sin(a1) * rearR);

        uint32_t colH = (i < kRearSegs / 2) ? colRight : colLeft;
        Vertex2D vc{pCenter.x, pCenter.y, 0.0f, 0.0f, colCrest, 5, {0, 0}};
        Vertex2D vrA{r0.x, r0.y, 0.0f, 0.0f, colH, 5, {0, 0}};
        Vertex2D vrB{r1.x, r1.y, 0.0f, 0.0f, colH, 5, {0, 0}};

        vertices_.push_back(vc);
        vertices_.push_back(vrA);
        vertices_.push_back(vrB);
    }

    // Outer perimeter bevel outline (anti-aliased outer silhouette)
    drawLine(pNoseTip.x, pNoseTip.y, pNoseL.x, pNoseL.y, shadowR, shadowG, shadowB, 0.75f, 1.0f);
    drawLine(pNoseL.x, pNoseL.y, pMidL.x, pMidL.y, shadowR, shadowG, shadowB, 0.75f, 1.0f);
    drawLine(pNoseTip.x, pNoseTip.y, pNoseR.x, pNoseR.y, shadowR, shadowG, shadowB, 0.75f, 1.0f);
    drawLine(pNoseR.x, pNoseR.y, pMidR.x, pMidR.y, shadowR, shadowG, shadowB, 0.75f, 1.0f);

    // Central raised ridge pointer stripe (high contrast white or colored inlay)
    Point2D pStripeStart = toScreen(0.0f, rearR * 0.70f);
    Point2D pStripeEnd = toScreen(0.0f, -noseLen + 1.5f);
    drawLine(pStripeStart.x, pStripeStart.y, pStripeEnd.x, pStripeEnd.y,
             pointerR, pointerG, pointerB, 1.0f, std::clamp(radius * 0.09f, 1.8f, 3.5f));

    // Center circular rotor cap
    drawCircleRadial3StopGradient(cx, cy, radius * 0.44f,
                                  highlightR, highlightG, highlightB, 1.0f,
                                  baseR, baseG, baseB, 1.0f,
                                  shadowR, shadowG, shadowB, 1.0f,
                                  -radius * 0.12f, -radius * 0.12f, 0.50f, 20);
    drawCircleOutline(cx, cy, radius * 0.44f, shadowR, shadowG, shadowB, 0.8f, 1.0f, 20);
}

// ============================================================================
// Monospace Text & High-Throughput Terminal Glyph Pipeline
// ============================================================================

void BatchRenderer2D::initDefaultMonospaceAtlas() {
    monospaceAtlasReady_ = true;
}

bool BatchRenderer2D::isBoxDrawingOrBlock(char32_t cp) noexcept {
    return (cp >= 0x2500 && cp <= 0x257F) || // Box Drawing
           (cp >= 0x2580 && cp <= 0x259F) || // Block Elements
           (cp >= 0x2800 && cp <= 0x28FF);   // Braille Patterns
}

void BatchRenderer2D::drawMonospaceCell(float x, float y, float w, float h,
                                       char32_t codepoint,
                                       uint32_t fgColor,
                                       uint32_t bgColor,
                                       uint8_t attrs,
                                       float subpixelOffsetX,
                                       float subpixelOffsetY) {
    float cx = x + subpixelOffsetX;
    float cy = y + subpixelOffsetY;

    // Attribute: Reverse / Invert video
    if (attrs & static_cast<uint8_t>(tui::TextAttr::Reverse)) {
        std::swap(fgColor, bgColor);
    }

    // Extract background color RGBA
    float bgR = static_cast<float>(bgColor & 0xFF) / 255.0f;
    float bgG = static_cast<float>((bgColor >> 8) & 0xFF) / 255.0f;
    float bgB = static_cast<float>((bgColor >> 16) & 0xFF) / 255.0f;
    float bgA = static_cast<float>((bgColor >> 24) & 0xFF) / 255.0f;

    if (bgA > 0.001f) {
        drawRect(cx, cy, w, h, bgR, bgG, bgB, bgA);
    }

    // Continuation cell: multi-cell ligature span continuation (non-rendering)
    if (attrs & static_cast<uint8_t>(tui::TextAttr::Continuation)) {
        return;
    }

    // Space or null codepoint: only background
    if (codepoint == ' ' || codepoint == 0) {
        if (attrs & static_cast<uint8_t>(tui::TextAttr::Underline)) {
            float fgR = static_cast<float>(fgColor & 0xFF) / 255.0f;
            float fgG = static_cast<float>((fgColor >> 8) & 0xFF) / 255.0f;
            float fgB = static_cast<float>((fgColor >> 16) & 0xFF) / 255.0f;
            float fgA = static_cast<float>((fgColor >> 24) & 0xFF) / 255.0f;
            drawRect(cx, cy + h - 1.5f, w, 1.0f, fgR, fgG, fgB, fgA);
        }
        return;
    }

    // Attribute: Dim
    if (attrs & static_cast<uint8_t>(tui::TextAttr::Dim)) {
        float r = static_cast<float>(fgColor & 0xFF) / 255.0f * 0.6f;
        float g = static_cast<float>((fgColor >> 8) & 0xFF) / 255.0f * 0.6f;
        float b = static_cast<float>((fgColor >> 16) & 0xFF) / 255.0f * 0.6f;
        float a = static_cast<float>((fgColor >> 24) & 0xFF) / 255.0f * 0.75f;
        fgColor = packColor(r, g, b, a);
    }

    // Attribute: Bold
    bool isBold = (attrs & static_cast<uint8_t>(tui::TextAttr::Bold)) != 0;

    float fgR = static_cast<float>(fgColor & 0xFF) / 255.0f;
    float fgG = static_cast<float>((fgColor >> 8) & 0xFF) / 255.0f;
    float fgB = static_cast<float>((fgColor >> 16) & 0xFF) / 255.0f;
    float fgA = static_cast<float>((fgColor >> 24) & 0xFF) / 255.0f;

    // Check if custom vector box-drawing / block / braille glyph
    if (isBoxDrawingOrBlock(codepoint)) {
        float lineThick = isBold ? std::max(2.0f, std::round(w * 0.16f)) : std::max(1.0f, std::round(w * 0.08f));
        float midX = cx + std::floor(w * 0.5f);
        float midY = cy + std::floor(h * 0.5f);
        float halfThick = std::floor(lineThick * 0.5f);

        // Box Drawing Characters (0x2500 - 0x257F)
        if (codepoint >= 0x2500 && codepoint <= 0x257F) {
            bool left = false, right = false, top = false, bottom = false;
            bool isDouble = false;

            switch (codepoint) {
                case 0x2500: case 0x2501: left = right = true; break; // ─ ━
                case 0x2502: case 0x2503: top = bottom = true; break; // │ ┃
                case 0x250C: case 0x250F: right = bottom = true; break; // ┌ ┏
                case 0x2510: case 0x2513: left = bottom = true; break;  // ┐ ┓
                case 0x2514: case 0x2517: right = top = true; break;    // └ ┗
                case 0x2518: case 0x251B: left = top = true; break;     // ┘ ┛
                case 0x251C: case 0x2523: top = bottom = right = true; break; // ├ ┣
                case 0x2524: case 0x252B: top = bottom = left = true; break;  // ┤ ┫
                case 0x252C: case 0x2533: left = right = bottom = true; break; // ┬ ┳
                case 0x2534: case 0x253B: left = right = top = true; break;    // ┴ ┻
                case 0x253C: case 0x254B: left = right = top = bottom = true; break; // ┼ ╋
                // Double lines
                case 0x2550: left = right = true; isDouble = true; break; // ═
                case 0x2551: top = bottom = true; isDouble = true; break; // ║
                case 0x2554: right = bottom = true; isDouble = true; break; // ╔
                case 0x2557: left = bottom = true; isDouble = true; break; // ╗
                case 0x255A: right = top = true; isDouble = true; break; // ╚
                case 0x255D: left = top = true; isDouble = true; break; // ╝
                case 0x2560: top = bottom = right = true; isDouble = true; break; // ╠
                case 0x2563: top = bottom = left = true; isDouble = true; break; // ╣
                case 0x2566: left = right = bottom = true; isDouble = true; break; // ╦
                case 0x2569: left = right = top = true; isDouble = true; break; // ╩
                case 0x256C: left = right = top = bottom = true; isDouble = true; break; // ╬
                // Arc corners
                case 0x256D: right = bottom = true; break; // ╭
                case 0x256E: left = bottom = true; break;  // ╮
                case 0x256F: left = top = true; break;     // ╯
                case 0x2570: right = top = true; break;    // ╰
                // Half lines
                case 0x2574: case 0x2578: left = true; break; // ╴ ╸
                case 0x2575: case 0x2579: top = true; break;  // ╵ ╹
                case 0x2576: case 0x257A: right = true; break; // ╶ ╺
                case 0x2577: case 0x257B: bottom = true; break; // ╷ ╻
                case 0x257C: case 0x257E: left = right = true; break;
                case 0x257D: case 0x257F: top = bottom = true; break;
                default: left = right = true; break;
            }

            if (isDouble) {
                float gap = std::max(2.0f, std::round(lineThick * 1.5f));
                if (left && right) {
                    drawRect(cx, midY - gap * 0.5f, w, 1.0f, fgR, fgG, fgB, fgA);
                    drawRect(cx, midY + gap * 0.5f, w, 1.0f, fgR, fgG, fgB, fgA);
                } else {
                    if (left) {
                        drawRect(cx, midY - gap * 0.5f, midX - cx, 1.0f, fgR, fgG, fgB, fgA);
                        drawRect(cx, midY + gap * 0.5f, midX - cx, 1.0f, fgR, fgG, fgB, fgA);
                    }
                    if (right) {
                        drawRect(midX, midY - gap * 0.5f, cx + w - midX, 1.0f, fgR, fgG, fgB, fgA);
                        drawRect(midX, midY + gap * 0.5f, cx + w - midX, 1.0f, fgR, fgG, fgB, fgA);
                    }
                }
                if (top && bottom) {
                    drawRect(midX - gap * 0.5f, cy, 1.0f, h, fgR, fgG, fgB, fgA);
                    drawRect(midX + gap * 0.5f, cy, 1.0f, h, fgR, fgG, fgB, fgA);
                } else {
                    if (top) {
                        drawRect(midX - gap * 0.5f, cy, 1.0f, midY - cy, fgR, fgG, fgB, fgA);
                        drawRect(midX + gap * 0.5f, cy, 1.0f, midY - cy, fgR, fgG, fgB, fgA);
                    }
                    if (bottom) {
                        drawRect(midX - gap * 0.5f, midY, 1.0f, cy + h - midY, fgR, fgG, fgB, fgA);
                        drawRect(midX + gap * 0.5f, midY, 1.0f, cy + h - midY, fgR, fgG, fgB, fgA);
                    }
                }
            } else {
                if (left && right) {
                    drawRect(cx, midY - halfThick, w, lineThick, fgR, fgG, fgB, fgA);
                } else {
                    if (left) drawRect(cx, midY - halfThick, midX - cx + halfThick, lineThick, fgR, fgG, fgB, fgA);
                    if (right) drawRect(midX - halfThick, midY - halfThick, cx + w - midX + halfThick, lineThick, fgR, fgG, fgB, fgA);
                }
                if (top && bottom) {
                    drawRect(midX - halfThick, cy, lineThick, h, fgR, fgG, fgB, fgA);
                } else {
                    if (top) drawRect(midX - halfThick, cy, lineThick, midY - cy + halfThick, fgR, fgG, fgB, fgA);
                    if (bottom) drawRect(midX - halfThick, midY - halfThick, lineThick, cy + h - midY + halfThick, fgR, fgG, fgB, fgA);
                }
            }
        }
        // Block Elements (0x2580 - 0x259F)
        else if (codepoint >= 0x2580 && codepoint <= 0x259F) {
            float halfW = std::floor(w * 0.5f);
            float halfH = std::floor(h * 0.5f);

            switch (codepoint) {
                case 0x2588: // █ Full block
                    drawRect(cx, cy, w, h, fgR, fgG, fgB, fgA);
                    break;
                case 0x2580: // ▀ Upper half block
                    drawRect(cx, cy, w, halfH, fgR, fgG, fgB, fgA);
                    break;
                case 0x2581: //   Lower 1/8
                    drawRect(cx, cy + std::round(h * 0.875f), w, h - std::round(h * 0.875f), fgR, fgG, fgB, fgA);
                    break;
                case 0x2582: // ▂ Lower 1/4
                    drawRect(cx, cy + std::round(h * 0.75f), w, h - std::round(h * 0.75f), fgR, fgG, fgB, fgA);
                    break;
                case 0x2583: // ▃ Lower 3/8
                    drawRect(cx, cy + std::round(h * 0.625f), w, h - std::round(h * 0.625f), fgR, fgG, fgB, fgA);
                    break;
                case 0x2584: // ▄ Lower half block
                    drawRect(cx, cy + halfH, w, h - halfH, fgR, fgG, fgB, fgA);
                    break;
                case 0x2585: // ▅ Lower 5/8
                    drawRect(cx, cy + std::round(h * 0.375f), w, h - std::round(h * 0.375f), fgR, fgG, fgB, fgA);
                    break;
                case 0x2586: // ▆ Lower 3/4
                    drawRect(cx, cy + std::round(h * 0.25f), w, h - std::round(h * 0.25f), fgR, fgG, fgB, fgA);
                    break;
                case 0x2587: // ▇ Lower 7/8
                    drawRect(cx, cy + std::round(h * 0.125f), w, h - std::round(h * 0.125f), fgR, fgG, fgB, fgA);
                    break;
                case 0x2589: // ▉ Left 7/8
                    drawRect(cx, cy, std::round(w * 0.875f), h, fgR, fgG, fgB, fgA);
                    break;
                case 0x258A: // ▊ Left 3/4
                    drawRect(cx, cy, std::round(w * 0.75f), h, fgR, fgG, fgB, fgA);
                    break;
                case 0x258B: // ▋ Left 5/8
                    drawRect(cx, cy, std::round(w * 0.625f), h, fgR, fgG, fgB, fgA);
                    break;
                case 0x258C: // ▌ Left half block
                    drawRect(cx, cy, halfW, h, fgR, fgG, fgB, fgA);
                    break;
                case 0x258D: // ▍ Left 3/8
                    drawRect(cx, cy, std::round(w * 0.375f), h, fgR, fgG, fgB, fgA);
                    break;
                case 0x258E: // ▎ Left 1/4
                    drawRect(cx, cy, std::round(w * 0.25f), h, fgR, fgG, fgB, fgA);
                    break;
                case 0x258F: // ▏ Left 1/8
                    drawRect(cx, cy, std::round(w * 0.125f), h, fgR, fgG, fgB, fgA);
                    break;
                case 0x2590: // ▐ Right half block
                    drawRect(cx + halfW, cy, w - halfW, h, fgR, fgG, fgB, fgA);
                    break;
                case 0x2594: // ▔ Upper 1/8
                    drawRect(cx, cy, w, std::round(h * 0.125f), fgR, fgG, fgB, fgA);
                    break;
                case 0x2595: // ▕ Right 1/8
                    drawRect(cx + std::round(w * 0.875f), cy, w - std::round(w * 0.875f), h, fgR, fgG, fgB, fgA);
                    break;
                // Quadrants
                case 0x2596: // ▖ Lower left
                    drawRect(cx, cy + halfH, halfW, h - halfH, fgR, fgG, fgB, fgA);
                    break;
                case 0x2597: // ▗ Lower right
                    drawRect(cx + halfW, cy + halfH, w - halfW, h - halfH, fgR, fgG, fgB, fgA);
                    break;
                case 0x2598: // ▘ Upper left
                    drawRect(cx, cy, halfW, halfH, fgR, fgG, fgB, fgA);
                    break;
                case 0x259D: // ▝ Upper right
                    drawRect(cx + halfW, cy, w - halfW, halfH, fgR, fgG, fgB, fgA);
                    break;
                case 0x2599: // ▙ Upper left, lower left, lower right
                    drawRect(cx, cy, halfW, halfH, fgR, fgG, fgB, fgA);
                    drawRect(cx, cy + halfH, w, h - halfH, fgR, fgG, fgB, fgA);
                    break;
                case 0x259A: // ▚ Upper left, lower right
                    drawRect(cx, cy, halfW, halfH, fgR, fgG, fgB, fgA);
                    drawRect(cx + halfW, cy + halfH, w - halfW, h - halfH, fgR, fgG, fgB, fgA);
                    break;
                case 0x259B: // ▛ Upper left, upper right, lower left
                    drawRect(cx, cy, w, halfH, fgR, fgG, fgB, fgA);
                    drawRect(cx, cy + halfH, halfW, h - halfH, fgR, fgG, fgB, fgA);
                    break;
                case 0x259C: // ▜ Upper left, upper right, lower right
                    drawRect(cx, cy, w, halfH, fgR, fgG, fgB, fgA);
                    drawRect(cx + halfW, cy + halfH, w - halfW, h - halfH, fgR, fgG, fgB, fgA);
                    break;
                case 0x259E: // ▞ Upper right, lower left
                    drawRect(cx + halfW, cy, w - halfW, halfH, fgR, fgG, fgB, fgA);
                    drawRect(cx, cy + halfH, halfW, h - halfH, fgR, fgG, fgB, fgA);
                    break;
                case 0x259F: // ▟ Upper right, lower left, lower right
                    drawRect(cx + halfW, cy, w - halfW, halfH, fgR, fgG, fgB, fgA);
                    drawRect(cx, cy + halfH, w, h - halfH, fgR, fgG, fgB, fgA);
                    break;
                // Shades (0x2591, 0x2592, 0x2593)
                case 0x2591: // ░ 25%
                    drawRect(cx, cy, w, h, fgR, fgG, fgB, fgA * 0.25f);
                    break;
                case 0x2592: // ▒ 50%
                    drawRect(cx, cy, w, h, fgR, fgG, fgB, fgA * 0.50f);
                    break;
                case 0x2593: // ▓ 75%
                    drawRect(cx, cy, w, h, fgR, fgG, fgB, fgA * 0.75f);
                    break;
                default:
                    drawRect(cx, cy, w, h, fgR, fgG, fgB, fgA);
                    break;
            }
        }
        // Braille Patterns (0x2800 - 0x28FF)
        else if (codepoint >= 0x2800 && codepoint <= 0x28FF) {
            uint32_t mask = static_cast<uint32_t>(codepoint - 0x2800);
            float dotW = std::max(1.5f, std::round(w * 0.22f));
            float dotH = std::max(1.5f, std::round(h * 0.16f));

            float xCols[2] = { cx + std::round(w * 0.25f - dotW * 0.5f),
                               cx + std::round(w * 0.75f - dotW * 0.5f) };
            float yRows[4] = { cy + std::round(h * 0.15f - dotH * 0.5f),
                               cy + std::round(h * 0.38f - dotH * 0.5f),
                               cy + std::round(h * 0.62f - dotH * 0.5f),
                               cy + std::round(h * 0.85f - dotH * 0.5f) };

            if (mask & 0x01) drawRect(xCols[0], yRows[0], dotW, dotH, fgR, fgG, fgB, fgA);
            if (mask & 0x02) drawRect(xCols[0], yRows[1], dotW, dotH, fgR, fgG, fgB, fgA);
            if (mask & 0x04) drawRect(xCols[0], yRows[2], dotW, dotH, fgR, fgG, fgB, fgA);
            if (mask & 0x08) drawRect(xCols[1], yRows[0], dotW, dotH, fgR, fgG, fgB, fgA);
            if (mask & 0x10) drawRect(xCols[1], yRows[1], dotW, dotH, fgR, fgG, fgB, fgA);
            if (mask & 0x20) drawRect(xCols[1], yRows[2], dotW, dotH, fgR, fgG, fgB, fgA);
            if (mask & 0x40) drawRect(xCols[0], yRows[3], dotW, dotH, fgR, fgG, fgB, fgA);
            if (mask & 0x80) drawRect(xCols[1], yRows[3], dotW, dotH, fgR, fgG, fgB, fgA);
        }
    } else {
        bool rendered = false;
        if (monospaceGlyphHook_) {
            rendered = monospaceGlyphHook_(cx, cy, w, h, codepoint, fgColor);
            if (rendered && isBold) {
                monospaceGlyphHook_(cx + 0.75f, cy, w, h, codepoint, fgColor);
            }
        }

        if (!rendered) {
            // Standard ASCII & extended font glyph rendering from texture atlas
            if (!monospaceAtlasReady_) {
                initDefaultMonospaceAtlas();
            }

            uint8_t glyph = (codepoint < 256) ? static_cast<uint8_t>(codepoint) : static_cast<uint8_t>('?');
            int col = glyph % 16;
            int row = glyph / 16;

            constexpr float kAtlasW = 128.0f;
            constexpr float kAtlasH = 256.0f;
            float u0 = (col * 8.0f) / kAtlasW;
            float v0 = (row * 16.0f) / kAtlasH;
            float u1 = ((col + 1) * 8.0f) / kAtlasW;
            float v1 = ((row + 1) * 16.0f) / kAtlasH;

            float x0 = cx, y0 = cy;
            float x1 = cx + w, y1 = cy + h;

            Vertex2D v_tl{x0, y0, u0, v0, fgColor, 4, {0, 0}};
            Vertex2D v_tr{x1, y0, u1, v0, fgColor, 4, {0, 0}};
            Vertex2D v_br{x1, y1, u1, v1, fgColor, 4, {0, 0}};
            Vertex2D v_bl{x0, y1, u0, v1, fgColor, 4, {0, 0}};

            applyTransform(v_tl.x, v_tl.y);
            applyTransform(v_tr.x, v_tr.y);
            applyTransform(v_br.x, v_br.y);
            applyTransform(v_bl.x, v_bl.y);

            vertices_.push_back(v_tl);
            vertices_.push_back(v_tr);
            vertices_.push_back(v_br);

            vertices_.push_back(v_tl);
            vertices_.push_back(v_br);
            vertices_.push_back(v_bl);

            // Faux bold: render slightly offset second pass
            if (isBold) {
                Vertex2D b_tl{x0 + 0.75f, y0, u0, v0, fgColor, 4, {0, 0}};
                Vertex2D b_tr{x1 + 0.75f, y0, u1, v0, fgColor, 4, {0, 0}};
                Vertex2D b_br{x1 + 0.75f, y1, u1, v1, fgColor, 4, {0, 0}};
                Vertex2D b_bl{x0 + 0.75f, y1, u0, v1, fgColor, 4, {0, 0}};

                applyTransform(b_tl.x, b_tl.y);
                applyTransform(b_tr.x, b_tr.y);
                applyTransform(b_br.x, b_br.y);
                applyTransform(b_bl.x, b_bl.y);

                vertices_.push_back(b_tl);
                vertices_.push_back(b_tr);
                vertices_.push_back(b_br);

                vertices_.push_back(b_tl);
                vertices_.push_back(b_br);
                vertices_.push_back(b_bl);
            }
        }
    }

    // Attribute: Underline
    if (attrs & static_cast<uint8_t>(tui::TextAttr::Underline)) {
        drawRect(cx, cy + h - 1.5f, w, 1.0f, fgR, fgG, fgB, fgA);
    }
}

static inline uint32_t decodeUtf8Codepoint(const char*& p, const char* end) noexcept {
    if (p >= end) return 0;
    unsigned char c = static_cast<unsigned char>(*p++);
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && p < end) {
        uint32_t cp = (c & 0x1F) << 6;
        cp |= (static_cast<unsigned char>(*p++) & 0x3F);
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && p + 1 < end) {
        uint32_t cp = (c & 0x0F) << 12;
        cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 6;
        cp |= (static_cast<unsigned char>(*p++) & 0x3F);
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && p + 2 < end) {
        uint32_t cp = (c & 0x07) << 18;
        cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 12;
        cp |= (static_cast<unsigned char>(*p++) & 0x3F) << 6;
        cp |= (static_cast<unsigned char>(*p++) & 0x3F);
        return cp;
    }
    return c;
}

void BatchRenderer2D::drawMonospaceText(float x, float y, float charW, float charH,
                                       std::string_view text,
                                       uint32_t fgColor,
                                       uint32_t bgColor,
                                       uint8_t attrs,
                                       float subpixelOffsetX,
                                       float subpixelOffsetY) {
    float curX = x;
    float curY = y;
    const char* ptr = text.data();
    const char* end = ptr + text.size();
    while (ptr < end) {
        uint32_t cp = decodeUtf8Codepoint(ptr, end);
        if (cp == '\n') {
            curY += charH;
            curX = x;
            continue;
        } else if (cp == '\r') {
            curX = x;
            continue;
        } else if (cp == '\t') {
            float col = (curX - x) / charW;
            int nextCol = ((static_cast<int>(col) / 4) + 1) * 4;
            curX = x + nextCol * charW;
            continue;
        }
        drawMonospaceCell(curX, curY, charW, charH,
                          static_cast<char32_t>(cp),
                          fgColor, bgColor, attrs,
                          subpixelOffsetX, subpixelOffsetY);
        curX += charW;
    }
}

static inline uint32_t packTuiColor(const tui::Color& c, bool isBg) {
    if (c.isDefault) {
        return isBg ? 0x00000000 : 0xFFFFFFFF;
    }
    return (static_cast<uint32_t>(c.a) << 24) |
           (static_cast<uint32_t>(c.b) << 16) |
           (static_cast<uint32_t>(c.g) << 8) |
            static_cast<uint32_t>(c.r);
}

void BatchRenderer2D::drawTerminalGrid(float startX, float startY, float cellW, float cellH,
                                      const void* cellsPtr, int cols, int rows,
                                      float subpixelOffsetX, float subpixelOffsetY) {
    if (!cellsPtr || cols <= 0 || rows <= 0) return;
    const auto* cells = static_cast<const tui::Cell*>(cellsPtr);
    for (int r = 0; r < rows; ++r) {
        float y = startY + r * cellH;
        for (int c = 0; c < cols; ++c) {
            float x = startX + c * cellW;
            const tui::Cell& cell = cells[r * cols + c];
            uint32_t fg = packTuiColor(cell.fg, false);
            uint32_t bg = packTuiColor(cell.bg, true);
            drawMonospaceCell(x, y, cellW, cellH, cell.codepoint, fg, bg, cell.attrs,
                              subpixelOffsetX, subpixelOffsetY);
        }
    }
}

} // namespace eatsbits::ui

