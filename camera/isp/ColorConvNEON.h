/*
 * ColorConvNEON - RGB to YUV conversion
 */

#ifndef MOCHA_COLOR_CONV_NEON_H
#define MOCHA_COLOR_CONV_NEON_H

#include <cstdint>
#include <cstddef>

namespace mocha {

class ColorConvNEON {
public:
    ColorConvNEON();
    ~ColorConvNEON();

    int initialize(uint16_t width, uint16_t height);
    
    // Convert RGB to YUV420 (NV12 format)
    void rgbToNv12(const uint8_t* rgb, uint8_t* yPlane, uint8_t* uvPlane);
    
    // Convert RGB to RGBA
    void rgbToRgba(const uint8_t* rgb, uint8_t* rgba, uint32_t width, uint32_t height);

    // RGB → RGBA with WB gains, gamma LUT, and vertical flip in one pass
    void rgbToRgbaWbGamma(const uint8_t* rgb, uint8_t* rgba,
                          uint32_t width, uint32_t height,
                          float rGain, float gGain, float bGain,
                          const uint8_t* gammaLut, bool flipV);

private:
    uint16_t mWidth;
    uint16_t mHeight;
    bool mInitialized;
};

} // namespace mocha

#endif // MOCHA_COLOR_CONV_NEON_H