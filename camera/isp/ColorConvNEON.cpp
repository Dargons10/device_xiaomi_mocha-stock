/*
 * ColorConvNEON Implementation
 * RGB to YUV420 (NV12) / RGBA conversion using ARM NEON intrinsics
 *
 * BT.601 coefficients (scaled by 1<<10):
 *   Y  = (  66*R + 129*G +  25*B + 128) >> 8 + 16
 *   U  = ( -38*R -  74*G + 112*B + 128) >> 8 + 128
 *   V  = ( 112*R -  94*G -  18*B + 128) >> 8 + 128
 *
 * NEON strategy:
 *   Y: 8 pixels/iteration, vld3_u8 + vmull/vmlal + vshrn + vqmovun
 *   UV: 16-pixel chunks with 2:1 chroma downsampling via vuzp,
 *       store interleaved with vst2_u8
 *   RGBA: 8 pixels/iteration, vld3_u8 + vst4_u8
 */

#include "ColorConvNEON.h"
#include <cstring>
#include <arm_neon.h>

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

    // BT.601 coefficients as NEON vectors
    const int16x8_t C_Y_R = vdupq_n_s16(66);
    const int16x8_t C_Y_G = vdupq_n_s16(129);
    const int16x8_t C_Y_B = vdupq_n_s16(25);
    const int16x8_t C_U_R = vdupq_n_s16(-38);
    const int16x8_t C_U_G = vdupq_n_s16(-74);
    const int16x8_t C_U_B = vdupq_n_s16(112);
    const int16x8_t C_V_R = vdupq_n_s16(112);
    const int16x8_t C_V_G = vdupq_n_s16(-94);
    const int16x8_t C_V_B = vdupq_n_s16(-18);
    const int16x8_t C_BIAS_Y = vdupq_n_s16(16);
    const int16x8_t C_BIAS_UV = vdupq_n_s16(128);
    const int32x4_t C_ROUND = vdupq_n_s32(128);

    // ─── Y PLANE ───────────────────────────────────────────────
    for (int y = 0; y < height; y++) {
        int row_base = y * width;
        int x = 0;

        for (; x + 8 <= width; x += 8) {
            uint8x8x3_t rgb8 = vld3_u8(rgb + (row_base + x) * 3);

            int16x8_t r16 = vreinterpretq_s16_u16(vmovl_u8(rgb8.val[0]));
            int16x8_t g16 = vreinterpretq_s16_u16(vmovl_u8(rgb8.val[1]));
            int16x8_t b16 = vreinterpretq_s16_u16(vmovl_u8(rgb8.val[2]));

            int32x4_t yl = vmull_s16(vget_low_s16(C_Y_R), vget_low_s16(r16));
            yl = vmlal_s16(yl, vget_low_s16(C_Y_G), vget_low_s16(g16));
            yl = vmlal_s16(yl, vget_low_s16(C_Y_B), vget_low_s16(b16));

            int32x4_t yh = vmull_s16(vget_high_s16(C_Y_R), vget_high_s16(r16));
            yh = vmlal_s16(yh, vget_high_s16(C_Y_G), vget_high_s16(g16));
            yh = vmlal_s16(yh, vget_high_s16(C_Y_B), vget_high_s16(b16));

            int16x8_t y16 = vcombine_s16(
                vshrn_n_s32(vaddq_s32(yl, C_ROUND), 8),
                vshrn_n_s32(vaddq_s32(yh, C_ROUND), 8));
            y16 = vaddq_s16(y16, C_BIAS_Y);

            vst1_u8(yPlane + row_base + x, vqmovun_s16(y16));
        }

        // Remainder pixels
        for (; x < width; x++) {
            int p = (row_base + x) * 3;
            int r = rgb[p], g = rgb[p+1], b = rgb[p+2];
            yPlane[row_base + x] = ((66*r + 129*g + 25*b + 128) >> 8) + 16;
        }
    }

    // ─── UV PLANE ──────────────────────────────────────────────
    for (int y = 0; y < height; y += 2) {
        int uv_row = (y / 2) * width;
        int x = 0;

        for (; x + 16 <= width; x += 16) {
            // Load 16 RGB pixels from even row (y)
            uint8x16x3_t rgb16 = vld3q_u8(rgb + (y * width + x) * 3);

            // Chroma 2:1 subsampling: keep only even-indexed pixels
            // vuzp splits {a0,a1,a2,...a15} into {a0,a2,...,a14} and {a1,a3,...,a15}
            uint8x8_t r_even = vuzp_u8(vget_low_u8(rgb16.val[0]), vget_high_u8(rgb16.val[0])).val[0];
            uint8x8_t g_even = vuzp_u8(vget_low_u8(rgb16.val[1]), vget_high_u8(rgb16.val[1])).val[0];
            uint8x8_t b_even = vuzp_u8(vget_low_u8(rgb16.val[2]), vget_high_u8(rgb16.val[2])).val[0];

            int16x8_t r16 = vreinterpretq_s16_u16(vmovl_u8(r_even));
            int16x8_t g16 = vreinterpretq_s16_u16(vmovl_u8(g_even));
            int16x8_t b16 = vreinterpretq_s16_u16(vmovl_u8(b_even));

            // U = (-38*R - 74*G + 112*B + 128) >> 8 + 128
            int32x4_t ul = vmull_s16(vget_low_s16(C_U_R), vget_low_s16(r16));
            ul = vmlal_s16(ul, vget_low_s16(C_U_G), vget_low_s16(g16));
            ul = vmlal_s16(ul, vget_low_s16(C_U_B), vget_low_s16(b16));

            int32x4_t uh = vmull_s16(vget_high_s16(C_U_R), vget_high_s16(r16));
            uh = vmlal_s16(uh, vget_high_s16(C_U_G), vget_high_s16(g16));
            uh = vmlal_s16(uh, vget_high_s16(C_U_B), vget_high_s16(b16));

            int16x8_t u16 = vcombine_s16(
                vshrn_n_s32(vaddq_s32(ul, C_ROUND), 8),
                vshrn_n_s32(vaddq_s32(uh, C_ROUND), 8));
            u16 = vaddq_s16(u16, C_BIAS_UV);

            // V = (112*R - 94*G - 18*B + 128) >> 8 + 128
            int32x4_t vl = vmull_s16(vget_low_s16(C_V_R), vget_low_s16(r16));
            vl = vmlal_s16(vl, vget_low_s16(C_V_G), vget_low_s16(g16));
            vl = vmlal_s16(vl, vget_low_s16(C_V_B), vget_low_s16(b16));

            int32x4_t vh = vmull_s16(vget_high_s16(C_V_R), vget_high_s16(r16));
            vh = vmlal_s16(vh, vget_high_s16(C_V_G), vget_high_s16(g16));
            vh = vmlal_s16(vh, vget_high_s16(C_V_B), vget_high_s16(b16));

            int16x8_t v16 = vcombine_s16(
                vshrn_n_s32(vaddq_s32(vl, C_ROUND), 8),
                vshrn_n_s32(vaddq_s32(vh, C_ROUND), 8));
            v16 = vaddq_s16(v16, C_BIAS_UV);

            uint8x8_t u8 = vqmovun_s16(u16);
            uint8x8_t v8 = vqmovun_s16(v16);

            // NV12: interleave U,V pairs
            // Byte offset in UV plane: cy * width + cx * 2
            // where cx = x/2 (chroma column), cy = y/2
            // offset = (y/2) * width + (x/2) * 2 = (y/2) * width + x
            // With x advancing by 16, each group stores 16 bytes
            uint8x8x2_t uv8;
            uv8.val[0] = u8;
            uv8.val[1] = v8;
            vst2_u8(uvPlane + uv_row + x, uv8);
        }

        // Remainder: process 2-column chroma pairs
        for (; x < width; x += 2) {
            if (x + 1 >= width) break;
            int p = (y * width + x) * 3;
            int r = rgb[p], g = rgb[p+1], b = rgb[p+2];
            int uv_off = uv_row + x;
            uvPlane[uv_off]     = ((-38*r - 74*g + 112*b + 128) >> 8) + 128;
            uvPlane[uv_off + 1] = ((112*r - 94*g - 18*b + 128) >> 8) + 128;
        }
    }
}

void ColorConvNEON::rgbToRgba(const uint8_t* rgb, uint8_t* rgba, uint32_t width, uint32_t height) {
    if (!rgb || !rgba) {
        return;
    }

    uint32_t total = width * height;
    uint32_t i = 0;

    // Process 8 pixels per NEON iteration: vld3 interleaved, vst4 interleaved
    for (; i + 8 <= total; i += 8) {
        uint8x8x3_t rgb3 = vld3_u8(rgb + i * 3);

        uint8x8x4_t rgba4;
        rgba4.val[0] = rgb3.val[0];
        rgba4.val[1] = rgb3.val[1];
        rgba4.val[2] = rgb3.val[2];
        rgba4.val[3] = vdup_n_u8(255);

        vst4_u8(rgba + i * 4, rgba4);
    }

    // Remainder
    for (; i < total; i++) {
        rgba[i*4]   = rgb[i*3];
        rgba[i*4+1] = rgb[i*3+1];
        rgba[i*4+2] = rgb[i*3+2];
        rgba[i*4+3] = 255;
    }
}

void ColorConvNEON::rgbToRgbaWbGamma(const uint8_t* rgb, uint8_t* rgba,
                                     uint32_t width, uint32_t height,
                                     float rGain, float gGain, float bGain,
                                     const uint8_t* gammaLut, bool flipV) {
    if (!rgb || !rgba || !gammaLut) return;

    /* Precompute 16-bit fixed-point WB gains (Q8.8) */
    int rG = (int)(rGain * 256.0f + 0.5f);
    int gG = (int)(gGain * 256.0f + 0.5f);
    int bG = (int)(bGain * 256.0f + 0.5f);

    uint32_t stride = width * 4;

    for (uint32_t y = 0; y < height; y++) {
        uint32_t srcRow = flipV ? (height - 1 - y) : y;
        const uint8_t* src = rgb + srcRow * width * 3;
        uint8_t* dst = rgba + y * stride;
        uint32_t x = 0;

        /* NEON: process 8 pixels per iteration */
        for (; x + 8 <= width; x += 8) {
            uint8x8x3_t rgb3 = vld3_u8(src + x * 3);

            uint16x8_t r16 = vmovl_u8(rgb3.val[0]);
            uint16x8_t g16 = vmovl_u8(rgb3.val[1]);
            uint16x8_t b16 = vmovl_u8(rgb3.val[2]);

            /* Apply WB gains: val = (pixel * gain_Q8 + 128) >> 8, widens to 32-bit */
            uint16x8_t rGv = vdupq_n_u16(rG);
            uint16x8_t gGv = vdupq_n_u16(gG);
            uint16x8_t bGv = vdupq_n_u16(bG);

            uint32x4_t r_lo = vmull_u16(vget_low_u16(r16), vget_low_u16(rGv));
            uint32x4_t r_hi = vmull_u16(vget_high_u16(r16), vget_high_u16(rGv));
            uint32x4_t g_lo = vmull_u16(vget_low_u16(g16), vget_low_u16(gGv));
            uint32x4_t g_hi = vmull_u16(vget_high_u16(g16), vget_high_u16(gGv));
            uint32x4_t b_lo = vmull_u16(vget_low_u16(b16), vget_low_u16(bGv));
            uint32x4_t b_hi = vmull_u16(vget_high_u16(b16), vget_high_u16(bGv));

            uint32x4_t round = vdupq_n_u32(128);
            uint16x8_t rWb = vcombine_u16(vshrn_n_u32(vaddq_u32(r_lo, round), 8),
                                          vshrn_n_u32(vaddq_u32(r_hi, round), 8));
            uint16x8_t gWb = vcombine_u16(vshrn_n_u32(vaddq_u32(g_lo, round), 8),
                                          vshrn_n_u32(vaddq_u32(g_hi, round), 8));
            uint16x8_t bWb = vcombine_u16(vshrn_n_u32(vaddq_u32(b_lo, round), 8),
                                          vshrn_n_u32(vaddq_u32(b_hi, round), 8));

            /* Clamp to 0-255 */
            uint8x8_t rClamp = vqmovn_u16(rWb);
            uint8x8_t gClamp = vqmovn_u16(gWb);
            uint8x8_t bClamp = vqmovn_u16(bWb);

            /* Apply gamma LUT and write RGBA (scalar: 24 LUT lookups is trivial) */
            uint8_t rgba_buf[32];
            for (int i = 0; i < 8; i++) {
                rgba_buf[i*4]   = gammaLut[rClamp[i]];
                rgba_buf[i*4+1] = gammaLut[gClamp[i]];
                rgba_buf[i*4+2] = gammaLut[bClamp[i]];
                rgba_buf[i*4+3] = 255;
            }
            memcpy(dst + x * 4, rgba_buf, 32);
        }

        /* Remainder */
        for (; x < width; x++) {
            int si = (int)(srcRow * width + x) * 3;
            int di = (int)(y * stride + x * 4);
            int r = (rgb[si]   * rG + 128) >> 8;
            int g = (rgb[si+1] * gG + 128) >> 8;
            int b = (rgb[si+2] * bG + 128) >> 8;
            rgba[di]   = gammaLut[r > 255 ? 255 : r];
            rgba[di+1] = gammaLut[g > 255 ? 255 : g];
            rgba[di+2] = gammaLut[b > 255 ? 255 : b];
            rgba[di+3] = 255;
        }
    }
}

} // namespace mocha
