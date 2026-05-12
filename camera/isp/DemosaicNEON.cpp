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
    mParams.offset_x = 0;
    mParams.offset_y = 0;
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
    const uint16_t* bayer = (const uint16_t*)bayerInput;

    // Simple bilinear demosaicing for interior pixels
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int idx = y * width + x;
            
            uint8_t r, g, b;
            
            // Determine position in Bayer pattern
            int patternX = (x + mParams.offset_x) & 1;
            int patternY = (y + mParams.offset_y) & 1;
            
            if (patternY == 0) {  // Green row
                if (patternX == 0) {  // G channel at this position
                    g = (bayer[idx] >> 2) & 0xFF;
                    r = ((bayer[idx-1] + bayer[idx+1]) >> 2) & 0xFF;
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

            int outIdx = (y * width + x) * 3;
            rgbOutput[outIdx + 0] = r;
            rgbOutput[outIdx + 1] = g;
            rgbOutput[outIdx + 2] = b;
        }
    }

    // Fill edges with nearest valid pixel values
    // Top row (y=0) - copy from y=1
    for (int x = 0; x < width; x++) {
        int srcIdx = (1 * width + x) * 3;
        int dstIdx = (0 * width + x) * 3;
        rgbOutput[dstIdx + 0] = rgbOutput[srcIdx + 0];
        rgbOutput[dstIdx + 1] = rgbOutput[srcIdx + 1];
        rgbOutput[dstIdx + 2] = rgbOutput[srcIdx + 2];
    }

    // Bottom row (y=height-1) - copy from y=height-2
    for (int x = 0; x < width; x++) {
        int srcIdx = ((height - 2) * width + x) * 3;
        int dstIdx = ((height - 1) * width + x) * 3;
        rgbOutput[dstIdx + 0] = rgbOutput[srcIdx + 0];
        rgbOutput[dstIdx + 1] = rgbOutput[srcIdx + 1];
        rgbOutput[dstIdx + 2] = rgbOutput[srcIdx + 2];
    }

    // Left column (x=0) - copy from x=1
    for (int y = 0; y < height; y++) {
        int srcIdx = (y * width + 1) * 3;
        int dstIdx = (y * width + 0) * 3;
        rgbOutput[dstIdx + 0] = rgbOutput[srcIdx + 0];
        rgbOutput[dstIdx + 1] = rgbOutput[srcIdx + 1];
        rgbOutput[dstIdx + 2] = rgbOutput[srcIdx + 2];
    }

    // Right column (x=width-1) - copy from x=width-2
    for (int y = 0; y < height; y++) {
        int srcIdx = (y * width + (width - 2)) * 3;
        int dstIdx = (y * width + (width - 1)) * 3;
        rgbOutput[dstIdx + 0] = rgbOutput[srcIdx + 0];
        rgbOutput[dstIdx + 1] = rgbOutput[srcIdx + 1];
        rgbOutput[dstIdx + 2] = rgbOutput[srcIdx + 2];
    }
}

} // namespace mocha