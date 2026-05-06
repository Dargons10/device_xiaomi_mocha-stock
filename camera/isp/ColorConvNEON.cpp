/*
 * ColorConvNEON Implementation
 * RGB to YUV420 (NV12) conversion
 */

#include "isp/ColorConvNEON.h"
#include <cstring>
#include <algorithm>

namespace {
    inline int clamp(int val, int minVal, int maxVal) {
        return val < minVal ? minVal : (val > maxVal ? maxVal : val);
    }
}

namespace mocha {

ColorConvNEON::ColorConvNEON()
    : mWidth(0), mHeight(0), mInitialized(false) {
}

ColorConvNEON::~ColorConvNEON() {
}

int ColorConvNEON::initialize(uint16_t width, uint16_t height) {
    if (width == 0 || height == 0) {
        return -1;
    }
    mWidth = width;
    mHeight = height;
    mInitialized = true;
    return 0;
}

void ColorConvNEON::rgbToNv12(const uint8_t* rgb, uint8_t* yPlane, uint8_t* uvPlane) {
    if (!mInitialized || !rgb || !yPlane || !uvPlane) {
        return;
    }

    const int width = mWidth;
    const int height = mHeight;

    // Y plane: full resolution
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            uint8_t r = rgb[idx + 0];
            uint8_t g = rgb[idx + 1];
            uint8_t b = rgb[idx + 2];

            // BT.601 Y conversion
            int yVal = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            yPlane[y * width + x] = (uint8_t)clamp(yVal, 0, 255);
        }
    }

    // UV plane: half resolution (NV12 format)
    // 2x2 block shares U and V
    for (int y = 0; y < height; y += 2) {
        for (int x = 0; x < width; x += 2) {
            int idx = (y * width + x) * 3;
            uint8_t r = rgb[idx + 0];
            uint8_t g = rgb[idx + 1];
            uint8_t b = rgb[idx + 2];

            // BT.601 U/V conversion
            int uVal = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
            int vVal = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;

            int uvIdx = (y / 2) * width + x;
            uvPlane[uvIdx * 2 + 0] = (uint8_t)clamp(uVal, 0, 255);
            uvPlane[uvIdx * 2 + 1] = (uint8_t)clamp(vVal, 0, 255);
        }
    }
}

void ColorConvNEON::rgbToRgba(const uint8_t* rgb, uint8_t* rgba, uint32_t width, uint32_t height) {
    if (!rgb || !rgba) {
        return;
    }

    for (uint32_t i = 0; i < width * height; i++) {
        rgba[i * 4 + 0] = rgb[i * 3 + 0];  // R
        rgba[i * 4 + 1] = rgb[i * 3 + 1];  // G
        rgba[i * 4 + 2] = rgb[i * 3 + 2];  // B
        rgba[i * 4 + 3] = 255;              // A
    }
}

} // namespace mocha