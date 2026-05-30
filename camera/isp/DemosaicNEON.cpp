#include "DemosaicNEON.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <arm_neon.h>
#include <cutils/log.h>

namespace mocha {

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

DemosaicNEON::~DemosaicNEON() {}

int DemosaicNEON::initialize(const DemosaicParams& params) {
    if (params.width == 0 || params.height == 0) return -1;
    mParams = params;
    mInitialized = true;
    return 0;
}

void DemosaicNEON::process(const uint8_t* bayerInput, uint8_t* rgbOutput) {
    if (!mInitialized || !bayerInput || !rgbOutput) return;

    const int w = mParams.width;
    const int h = mParams.height;
    const int ox = mParams.offset_x;
    const int oy = mParams.offset_y;
    const int pat = mParams.bayerPattern;

    uint8_t* bayer8 = (uint8_t*)malloc(w * h);
    if (!bayer8) return;

    /* Convert 16-bit RAW10 to 8-bit with black level subtraction */
    int total = w * h;
    int i = 0;
    for (; i + 16 <= total; i += 16) {
        uint16_t tmp[16];
        memcpy(tmp, bayerInput + i * 2, 32);
        uint16x8_t v16a = vld1q_u16(tmp);
        uint16x8_t v16b = vld1q_u16(tmp + 8);
        uint8x16_t v8 = vcombine_u8(vshrn_n_u16(v16a, 2), vshrn_n_u16(v16b, 2));
        uint8x16_t bl = vdupq_n_u8(mParams.blackLevel);
        v8 = vqsubq_u8(v8, bl);
        vst1q_u8(bayer8 + i, v8);
    }
    for (; i < total; i++) {
        uint16_t val;
        memcpy(&val, bayerInput + i * 2, 2);
        int pixel = (val >> 2) - mParams.blackLevel;
        bayer8[i] = pixel > 0 ? (uint8_t)pixel : 0;
    }

    /* Build position map for this Bayer pattern */
    int pos_map[4];
    if (pat == BAYER_RGGB) {
        pos_map[0]=0; pos_map[1]=1; pos_map[2]=2; pos_map[3]=3;
    } else if (pat == BAYER_GRBG) {
        pos_map[0]=1; pos_map[1]=0; pos_map[2]=3; pos_map[3]=2;
    } else if (pat == BAYER_GBRG) {
        pos_map[0]=2; pos_map[1]=0; pos_map[2]=3; pos_map[3]=1;
    } else {
        pos_map[0]=3; pos_map[1]=1; pos_map[2]=2; pos_map[3]=0;
    }

    /* DEBUG: compute average R, G, B from center region of raw bayer data */
    {
        int sumR=0, sumG=0, sumB=0, cntR=0, cntG=0, cntB=0;
        int sy = h/3, ey = 2*h/3, sx = w/3, ex = 2*w/3;
        for (int y = sy; y < ey; y++) {
            for (int x = sx; x < ex; x++) {
                int idx = y*w + x, pos = (((y+oy)&1)*2 + ((x+ox)&1));
                uint8_t val = bayer8[idx];
                if      (pos == pos_map[0]) { sumR += val; cntR++; }
                else if (pos == pos_map[1]) { sumG += val; cntG++; }
                else if (pos == pos_map[2]) { sumG += val; cntG++; }
                else                        { sumB += val; cntB++; }
            }
        }
        ALOGD("RAWavg[R,G,B]=[%d,%d,%d] pat=%d ox=%d oy=%d bl=%d",
              cntR?sumR/cntR:0, cntG?sumG/cntG:0, cntB?sumB/cntB:0,
              pat, ox, oy, mParams.blackLevel);
        /* Dump raw 16-bit values for first 4 pixels to check byte ordering */
        {
            uint16_t rawpix[4];
            memcpy(rawpix, bayerInput, 8);
            ALOGD("RAW16[0..3]=[0x%04x,0x%04x,0x%04x,0x%04x]",
                  rawpix[0], rawpix[1], rawpix[2], rawpix[3]);
        }
    }

    /* Simple bilinear demosaic for interior pixels (y=1..h-2, x=1..w-2) */
    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            int idx = y * w + x;
            int out = idx * 3;

            /* Determine position in 2x2 Bayer block */
            int pos = (((y + oy) & 1) * 2 + ((x + ox) & 1));

            int rv, gv, bv;

            if (pos == pos_map[0]) {
                /* R position: raw R, interpolate G and B */
                rv = bayer8[idx];
                gv = ((int)bayer8[idx-1] + (int)bayer8[idx+1] +
                      (int)bayer8[idx-w] + (int)bayer8[idx+w]) >> 2;
                bv = ((int)bayer8[idx-w-1] + (int)bayer8[idx-w+1] +
                      (int)bayer8[idx+w-1] + (int)bayer8[idx+w+1]) >> 2;
            } else if (pos == pos_map[3]) {
                /* B position: raw B, interpolate R and G */
                bv = bayer8[idx];
                gv = ((int)bayer8[idx-1] + (int)bayer8[idx+1] +
                      (int)bayer8[idx-w] + (int)bayer8[idx+w]) >> 2;
                rv = ((int)bayer8[idx-w-1] + (int)bayer8[idx-w+1] +
                      (int)bayer8[idx+w-1] + (int)bayer8[idx+w+1]) >> 2;
            } else if (pos == pos_map[1]) {
                /* G1 position (same row as R): raw G, R from horizontal, B from vertical */
                gv = bayer8[idx];
                rv = ((int)bayer8[idx-1] + (int)bayer8[idx+1]) >> 1;
                bv = ((int)bayer8[idx-w] + (int)bayer8[idx+w]) >> 1;
            } else {
                /* G2 position (same row as B): raw G, R from vertical, B from horizontal */
                gv = bayer8[idx];
                rv = ((int)bayer8[idx-w] + (int)bayer8[idx+w]) >> 1;
                bv = ((int)bayer8[idx-1] + (int)bayer8[idx+1]) >> 1;
            }

            rv = rv < 0 ? 0 : (rv > 255 ? 255 : rv);
            gv = gv < 0 ? 0 : (gv > 255 ? 255 : gv);
            bv = bv < 0 ? 0 : (bv > 255 ? 255 : bv);
            rgbOutput[out]   = (uint8_t)rv;
            rgbOutput[out+1] = (uint8_t)gv;
            rgbOutput[out+2] = (uint8_t)bv;
        }
    }

    /* DEBUG: average RGB of demosaiced center region */
    {
        int sR=0,sG=0,sB=0,cR=0,cG=0,cB=0;
        int sy=h/3, ey=2*h/3, sx=w/3, ex=2*w/3;
        for (int y = sy; y < ey; y++)
            for (int x = sx; x < ex; x++) {
                int out = (y*w + x)*3;
                sR += rgbOutput[out];   cR++;
                sG += rgbOutput[out+1]; cG++;
                sB += rgbOutput[out+2]; cB++;
            }
        ALOGD("RGBout avg[R,G,B]=[%d,%d,%d]",
              cR?sR/cR:0, cG?sG/cG:0, cB?sB/cB:0);
    }

    /* Fill edges by copying nearest valid pixel */
    for (int x = 0; x < w; x++) {
        int src = (1 * w + x) * 3;
        int dst = (0 * w + x) * 3;
        rgbOutput[dst]   = rgbOutput[src];
        rgbOutput[dst+1] = rgbOutput[src+1];
        rgbOutput[dst+2] = rgbOutput[src+2];
    }
    for (int x = 0; x < w; x++) {
        int src = ((h - 2) * w + x) * 3;
        int dst = ((h - 1) * w + x) * 3;
        rgbOutput[dst]   = rgbOutput[src];
        rgbOutput[dst+1] = rgbOutput[src+1];
        rgbOutput[dst+2] = rgbOutput[src+2];
    }
    for (int y = 0; y < h; y++) {
        int src = (y * w + 1) * 3;
        int dst = (y * w + 0) * 3;
        rgbOutput[dst]   = rgbOutput[src];
        rgbOutput[dst+1] = rgbOutput[src+1];
        rgbOutput[dst+2] = rgbOutput[src+2];
    }
    for (int y = 0; y < h; y++) {
        int src = (y * w + (w - 2)) * 3;
        int dst = (y * w + (w - 1)) * 3;
        rgbOutput[dst]   = rgbOutput[src];
        rgbOutput[dst+1] = rgbOutput[src+1];
        rgbOutput[dst+2] = rgbOutput[src+2];
    }

    free(bayer8);
}

} // namespace mocha
