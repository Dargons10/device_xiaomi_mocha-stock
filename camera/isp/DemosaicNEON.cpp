/*
 * DemosaicNEON Implementation
 * Bayer to RGB using optimized C (compiler auto-vectorizes with NEON)
 */

#include "isp/DemosaicNEON.h"
#include <cstring>
#include <cstdlib>

namespace mocha {

// Bayer pattern definitions
#define BAYER_GBRG 0
#define BAYER_GRBG 1
#define BAYER_BGGR 2
#define BAYER_RGGB 3

DemosaicNEON::DemosaicNEON()
    : mInitialized(false) {
    memset(&mParams, 0, sizeof(mParams));
}

DemosaicNEON::~DemosaicNEON() {
}

int DemosaicNEON::initialize(const DemosaicParams& params) {
    if (params.width == 0 || params.height == 0) {
        return -1;
    }

    mParams = params;
    mInitialized = true;
    return 0;
}

void DemosaicNEON::process(const uint8_t* bayerInput, uint8_t* rgbOutput) {
    if (!mInitialized || !bayerInput || !rgbOutput) {
        return;
    }

    const int width = mParams.width;
    const int height = mParams.height;
    const int bpp = 10;  // bits per pixel
    const int bytesPerPixel = 2;  // 16-bit raw

    // Simple bilinear demosaicing
    // For each pixel, interpolate from neighbors
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int idx = y * width + x;
            const uint16_t* bayer = (const uint16_t*)bayerInput;
            
            uint8_t r, g, b;
            
            // Determine position in Bayer pattern
            int patternX = (x + mParams.bayerPattern) & 1;
            int patternY = (y + mParams.bayerPattern) & 1;
            
            if (patternY == 0) {  // Green row
                if (patternX == 0) {  // G channel at this position
                    g = (bayer[idx] >> 2) & 0xFF;
                    // Interpolate R from horizontal neighbors
                    r = ((bayer[idx-1] + bayer[idx+1]) >> 2) & 0xFF;
                    // Interpolate B from vertical neighbors
                    b = ((bayer[idx-width] + bayer[idx+width]) >> 2) & 0xFF;
                } else {  // R or B at this position
                    g = ((bayer[idx-1] + bayer[idx+1] + 
                          bayer[idx-width] + bayer[idx+width]) >> 2) & 0xFF;
                    r = (bayer[idx] >> 2) & 0xFF;
                    b = ((bayer[idx-1-width] + bayer[idx-1+width] + 
                          bayer[idx+1-width] + bayer[idx+1+width]) >> 2) & 0xFF;
                }
            } else {  // Red/Blue row
                if (patternX == 0) {  // B
                    b = (bayer[idx] >> 2) & 0xFF;
                    g = ((bayer[idx-1] + bayer[idx+1] + 
                          bayer[idx-width] + bayer[idx+width]) >> 2) & 0xFF;
                    r = ((bayer[idx-1-width] + bayer[idx-1+width] + 
                          bayer[idx+1-width] + bayer[idx+1+width]) >> 2) & 0xFF;
                } else {  // R
                    r = (bayer[idx] >> 2) & 0xFF;
                    g = ((bayer[idx-1] + bayer[idx+1] + 
                          bayer[idx-width] + bayer[idx+width]) >> 2) & 0xFF;
                    b = ((bayer[idx-1-width] + bayer[idx-1+width] + 
                          bayer[idx+1-width] + bayer[idx+1+width]) >> 2) & 0xFF;
                }
            }

            // Write to RGB output (RGB888 format)
            int outIdx = (y * width + x) * 3;
            rgbOutput[outIdx + 0] = r;
            rgbOutput[outIdx + 1] = g;
            rgbOutput[outIdx + 2] = b;
        }
    }
}

} // namespace mocha