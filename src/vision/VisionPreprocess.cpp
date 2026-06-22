// VisionPreprocess.cpp — crop → resize → CLAHE → normalize (OpenCV 동등 구현).
#include "VisionPreprocess.h"
#include "VisionConfig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace vision {
namespace {

inline std::uint8_t clampU8(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return static_cast<std::uint8_t>(v + 0.5f);
}

// ── sRGB ↔ linear (OpenCV BGR2Lab 의 srgb gamma 와 동일) ─────────────────────
inline float srgbToLinear(float c)
{
    return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float linearToSrgb(float c)
{
    if (c <= 0.0f) return 0.0f;
    if (c >= 1.0f) return 1.0f;
    return (c <= 0.0031308f) ? (12.92f * c) : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
}

constexpr float kXn = 0.950456f;
constexpr float kZn = 1.088754f;
constexpr float kEps = 0.008856f;     // (6/29)^3
constexpr float kKappa = 903.3f;

inline float labF(float t)
{
    return (t > kEps) ? std::cbrt(t) : (7.787f * t + 16.0f / 116.0f);
}

// RGB(0..255) → Lab 8bit(OpenCV 규약: L*255/100, a+128, b+128)
inline void rgbToLab(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                     std::uint8_t &L8, std::uint8_t &a8, std::uint8_t &b8)
{
    const float R = srgbToLinear(r / 255.0f);
    const float G = srgbToLinear(g / 255.0f);
    const float B = srgbToLinear(b / 255.0f);

    float X = 0.412453f * R + 0.357580f * G + 0.180423f * B;
    float Y = 0.212671f * R + 0.715160f * G + 0.072169f * B;
    float Z = 0.019334f * R + 0.119193f * G + 0.950227f * B;
    X /= kXn;
    Z /= kZn;

    const float fx = labF(X), fy = labF(Y), fz = labF(Z);
    const float L = (Y > kEps) ? (116.0f * std::cbrt(Y) - 16.0f) : (kKappa * Y);
    const float a = 500.0f * (fx - fy);
    const float bb = 200.0f * (fy - fz);

    L8 = clampU8(L * 255.0f / 100.0f);
    a8 = clampU8(a + 128.0f);
    b8 = clampU8(bb + 128.0f);
}

// Lab 8bit → RGB(0..255)
inline void labToRgb(std::uint8_t L8, std::uint8_t a8, std::uint8_t b8,
                     std::uint8_t &r, std::uint8_t &g, std::uint8_t &b)
{
    const float L = L8 * 100.0f / 255.0f;
    const float a = static_cast<float>(a8) - 128.0f;
    const float bb = static_cast<float>(b8) - 128.0f;

    const float fy = (L + 16.0f) / 116.0f;
    const float fx = fy + a / 500.0f;
    const float fz = fy - bb / 200.0f;

    auto finv = [](float f) {
        const float f3 = f * f * f;
        return (f3 > kEps) ? f3 : ((f - 16.0f / 116.0f) / 7.787f);
    };
    float X = finv(fx) * kXn;
    float Y = (L > (kKappa * kEps)) ? (fy * fy * fy) : (L / kKappa);
    float Z = finv(fz) * kZn;

    float R =  3.240479f * X - 1.537150f * Y - 0.498535f * Z;
    float G = -0.969256f * X + 1.875991f * Y + 0.041556f * Z;
    float B =  0.055648f * X - 0.204043f * Y + 1.057311f * Z;

    r = clampU8(linearToSrgb(R) * 255.0f);
    g = clampU8(linearToSrgb(G) * 255.0f);
    b = clampU8(linearToSrgb(B) * 255.0f);
}

// ── CLAHE (OpenCV createCLAHE 와 동등) — L 채널(kImgSize×kImgSize)에 in-place ──
void applyClaheL(std::vector<std::uint8_t> &L, int size)
{
    const int tiles = kClaheTiles;
    const int tileW = size / tiles;
    const int tileH = size / tiles;
    const int tileArea = tileW * tileH;
    constexpr int kHist = 256;

    int clipLimit = static_cast<int>(kClaheClip * tileArea / kHist);
    clipLimit = std::max(clipLimit, 1);

    // tiles×tiles 개의 LUT(각 256) 계산.
    std::vector<std::array<std::uint8_t, kHist>> luts(tiles * tiles);
    const float lutScale = static_cast<float>(kHist - 1) / static_cast<float>(tileArea);

    for (int ty = 0; ty < tiles; ++ty) {
        for (int tx = 0; tx < tiles; ++tx) {
            int hist[kHist] = {0};
            const int x0 = tx * tileW, y0 = ty * tileH;
            for (int y = 0; y < tileH; ++y) {
                const std::uint8_t *row = &L[(y0 + y) * size + x0];
                for (int x = 0; x < tileW; ++x)
                    ++hist[row[x]];
            }
            // clip + 잉여 재분배
            int clipped = 0;
            for (int i = 0; i < kHist; ++i) {
                if (hist[i] > clipLimit) { clipped += hist[i] - clipLimit; hist[i] = clipLimit; }
            }
            const int redistBatch = clipped / kHist;
            int residual = clipped - redistBatch * kHist;
            for (int i = 0; i < kHist; ++i) hist[i] += redistBatch;
            if (residual > 0) {
                const int step = std::max(kHist / residual, 1);
                for (int i = 0; i < kHist && residual > 0; i += step, --residual)
                    ++hist[i];
            }
            // CDF → LUT
            std::array<std::uint8_t, kHist> &lut = luts[ty * tiles + tx];
            int sum = 0;
            for (int i = 0; i < kHist; ++i) {
                sum += hist[i];
                lut[i] = clampU8(sum * lutScale);
            }
        }
    }

    // 타일 중심 기준 양선형 보간 (OpenCV CLAHE_Interpolation 과 동일).
    const float invTw = 1.0f / static_cast<float>(tileW);
    const float invTh = 1.0f / static_cast<float>(tileH);
    std::vector<std::uint8_t> dst(L.size());
    for (int y = 0; y < size; ++y) {
        float tyf = (y + 0.5f) * invTh - 0.5f;
        int ty1 = static_cast<int>(std::floor(tyf));
        float ya = tyf - ty1;
        int ty2 = ty1 + 1;
        ty1 = std::max(ty1, 0);
        ty2 = std::min(ty2, tiles - 1);
        if (ty1 == tiles - 1) ya = 0.0f;
        for (int x = 0; x < size; ++x) {
            float txf = (x + 0.5f) * invTw - 0.5f;
            int tx1 = static_cast<int>(std::floor(txf));
            float xa = txf - tx1;
            int tx2 = tx1 + 1;
            tx1 = std::max(tx1, 0);
            tx2 = std::min(tx2, tiles - 1);
            if (tx1 == tiles - 1) xa = 0.0f;

            const std::uint8_t v = L[y * size + x];
            const float r1 = luts[ty1 * tiles + tx1][v] * (1.0f - xa) + luts[ty1 * tiles + tx2][v] * xa;
            const float r2 = luts[ty2 * tiles + tx1][v] * (1.0f - xa) + luts[ty2 * tiles + tx2][v] * xa;
            dst[y * size + x] = clampU8(r1 * (1.0f - ya) + r2 * ya);
        }
    }
    L.swap(dst);
}

} // namespace

void preprocessFrame(const std::uint8_t *rgb, int width, int height, int stride,
                     std::vector<float> &out)
{
    const int S = kImgSize;
    out.assign(static_cast<std::size_t>(S) * S * 3, 0.0f);
    if (!rgb || width <= 0 || height <= 0) return;

    // crop 좌표를 실제 프레임 크기에 비례 스케일 후 클램프.
    const float sx = static_cast<float>(width)  / static_cast<float>(kCamWidth);
    const float sy = static_cast<float>(height) / static_cast<float>(kCamHeight);
    float cx1 = kCropX1 * sx, cx2 = kCropX2 * sx;
    float cy1 = kCropY1 * sy, cy2 = kCropY2 * sy;
    cx1 = std::clamp(cx1, 0.0f, static_cast<float>(width  - 1));
    cx2 = std::clamp(cx2, cx1 + 1.0f, static_cast<float>(width));
    cy1 = std::clamp(cy1, 0.0f, static_cast<float>(height - 1));
    cy2 = std::clamp(cy2, cy1 + 1.0f, static_cast<float>(height));
    const float cropW = cx2 - cx1;
    const float cropH = cy2 - cy1;

    // crop 영역 → S×S 양선형 리사이즈 (OpenCV INTER_LINEAR 좌표 규약).
    const float scaleX = cropW / static_cast<float>(S);
    const float scaleY = cropH / static_cast<float>(S);

    std::vector<std::uint8_t> rImg(static_cast<std::size_t>(S) * S);
    std::vector<std::uint8_t> gImg(static_cast<std::size_t>(S) * S);
    std::vector<std::uint8_t> bImg(static_cast<std::size_t>(S) * S);

    for (int dy = 0; dy < S; ++dy) {
        float fy = (dy + 0.5f) * scaleY - 0.5f + cy1;
        fy = std::clamp(fy, 0.0f, static_cast<float>(height - 1));
        int y0 = static_cast<int>(std::floor(fy));
        int y1 = std::min(y0 + 1, height - 1);
        float wy = fy - y0;
        for (int dx = 0; dx < S; ++dx) {
            float fx = (dx + 0.5f) * scaleX - 0.5f + cx1;
            fx = std::clamp(fx, 0.0f, static_cast<float>(width - 1));
            int x0 = static_cast<int>(std::floor(fx));
            int x1 = std::min(x0 + 1, width - 1);
            float wx = fx - x0;

            const std::uint8_t *p00 = rgb + y0 * stride + x0 * 3;
            const std::uint8_t *p01 = rgb + y0 * stride + x1 * 3;
            const std::uint8_t *p10 = rgb + y1 * stride + x0 * 3;
            const std::uint8_t *p11 = rgb + y1 * stride + x1 * 3;
            const std::size_t idx = static_cast<std::size_t>(dy) * S + dx;
            for (int c = 0; c < 3; ++c) {
                const float top = p00[c] * (1.0f - wx) + p01[c] * wx;
                const float bot = p10[c] * (1.0f - wx) + p11[c] * wx;
                const std::uint8_t v = clampU8(top * (1.0f - wy) + bot * wy);
                if (c == 0) rImg[idx] = v;
                else if (c == 1) gImg[idx] = v;
                else bImg[idx] = v;
            }
        }
    }

    // RGB → Lab, L 채널에 CLAHE, Lab → RGB.
    std::vector<std::uint8_t> Lch(static_cast<std::size_t>(S) * S);
    std::vector<std::uint8_t> Ach(static_cast<std::size_t>(S) * S);
    std::vector<std::uint8_t> Bch(static_cast<std::size_t>(S) * S);
    for (std::size_t i = 0; i < Lch.size(); ++i)
        rgbToLab(rImg[i], gImg[i], bImg[i], Lch[i], Ach[i], Bch[i]);

    applyClaheL(Lch, S);

    // 최종: RGB 정규화(/255) → NHWC float.
    for (std::size_t i = 0; i < Lch.size(); ++i) {
        std::uint8_t r, g, b;
        labToRgb(Lch[i], Ach[i], Bch[i], r, g, b);
        out[i * 3 + 0] = r / 255.0f;
        out[i * 3 + 1] = g / 255.0f;
        out[i * 3 + 2] = b / 255.0f;
    }
}

} // namespace vision
