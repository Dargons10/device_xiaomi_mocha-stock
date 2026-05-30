/*
 * DemosaicNEON - Bayer to RGB conversion using NEON
 * Malvar-He-Cutler algorithm
 */

#ifndef MOCHA_DEMOSAIC_NEON_H
#define MOCHA_DEMOSAIC_NEON_H

#include <cstdint>
#include <cstddef>

namespace mocha {

struct DemosaicParams {
    uint16_t width;
    uint16_t height;
    uint8_t  bayerPattern;  // 0=GBRG, 1=GRBG, 2=BGGR, 3=RGGB
    uint8_t  offset_x;      // X offset for pattern alignment
    uint8_t  offset_y;      // Y offset for pattern alignment
    uint16_t blackLevel;
};

class DemosaicNEON {
public:
    DemosaicNEON();
    ~DemosaicNEON();

    int initialize(const DemosaicParams& params);
    void process(const uint8_t* bayerInput, uint8_t* rgbOutput);

private:
    DemosaicParams mParams;
    bool mInitialized;
};

} // namespace mocha

#endif // MOCHA_DEMOSAIC_NEON_H