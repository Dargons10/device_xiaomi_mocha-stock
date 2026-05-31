#include "DemosaicNEON.h"
#include <cstring>
#include <cstdlib>
#include <arm_neon.h>

namespace mocha {

DemosaicNEON::DemosaicNEON()
    : mInitialized(false), mBayerBuf(nullptr), mBayerBufSize(0) {
    memset(&mParams, 0, sizeof(mParams));
}

DemosaicNEON::~DemosaicNEON() {
    free(mBayerBuf);
}

int DemosaicNEON::initialize(const DemosaicParams& params) {
    if (params.width == 0 || params.height == 0) return -1;
    mParams = params;
    uint32_t needed = params.width * params.height + 16;
    if (needed > mBayerBufSize) {
        uint8_t* buf = (uint8_t*)realloc(mBayerBuf, needed + 64);
        if (!buf) return -1;
        mBayerBuf = buf;
        mBayerBufSize = needed + 64;
    }
    mInitialized = true;
    return 0;
}

static void raw10_to_8bit(const uint8_t* in, uint8_t* out, int total, uint16_t bl) {
    int i = 0;
    uint8x16_t blv = vdupq_n_u8(bl);
    for (; i + 16 <= total; i += 16) {
        uint16_t tmp[16];
        memcpy(tmp, in + i * 2, 32);
        uint16x8_t a = vld1q_u16(tmp);
        uint16x8_t b = vld1q_u16(tmp + 8);
        uint8x16_t v = vcombine_u8(vshrn_n_u16(a, 2), vshrn_n_u16(b, 2));
        vst1q_u8(out + i, vqsubq_u8(v, blv));
    }
    for (; i < total; i++) {
        uint16_t v;
        memcpy(&v, in + i * 2, 2);
        int p = (v >> 2) - bl;
        out[i] = p > 0 ? (uint8_t)p : 0;
    }
}

static inline uint8_t clamp(int v) {
    return (uint8_t)((v >> 8) ? (v < 0 ? 0 : 255) : v);
}

static void fill_edges(uint8_t* rgb, int w, int h) {
    int s = w * 3;
    memcpy(rgb, rgb + s, s);
    memcpy(rgb + (h-1)*s, rgb + (h-2)*s, s);
    for (int y = 0; y < h; y++) {
        int off = y * s;
        rgb[off] = rgb[off + 3];
        rgb[off + 1] = rgb[off + 4];
        rgb[off + 2] = rgb[off + 5];
        rgb[off + (w-1)*3] = rgb[off + (w-2)*3];
        rgb[off + (w-1)*3 + 1] = rgb[off + (w-2)*3 + 1];
        rgb[off + (w-1)*3 + 2] = rgb[off + (w-2)*3 + 2];
    }
}

/* ─── process a row with NEON: 4 pairs (8 pixels) per batch ───            */
/* The row alternates between two color types A and B at even/odd columns.   */
/* The `is_arow` flag selects which formula pair applies:                    */
/*   is_arow=0:  even=A_type, odd=B_type                                     */
/*   is_arow=1:  even=B_type, odd=A_type  (swapped)                          */
/* Formula key:                                                              */
/*   type 0 (R):  R=center, G=HV, B=diag                                     */
/*   type 1 (G1): G=center, R=H, B=V                                         */
/*   type 2 (G2): G=center, R=V, B=H                                         */
/*   type 3 (B):  B=center, G=HV, R=diag                                     */
/* The A/B type codes are passed by the caller.                              */
static void neon_row(const uint8_t* row, const uint8_t* up, const uint8_t* dn,
                      uint8_t* rgb, int w, int out_base,
                      int ev_type, int od_type) {
    /* Handle edge column 0: no left neighbor — mirror from right */
    int o0 = out_base;
    int val = row[0];
    if (ev_type == 0) {
        int rv = val;
        int gv = (row[1] + row[1] + up[0] + dn[0]) >> 2;
        int bv = (up[1] + up[1] + dn[1] + dn[1]) >> 2;
        rgb[o0]   = clamp(rv);
        rgb[o0+1] = clamp(gv);
        rgb[o0+2] = clamp(bv);
    } else if (ev_type == 1) {
        int gv = val;
        int rv = (row[1] + row[1]) >> 1;
        int bv = (up[0] + dn[0]) >> 1;
        rgb[o0]   = clamp(rv);
        rgb[o0+1] = clamp(gv);
        rgb[o0+2] = clamp(bv);
    } else if (ev_type == 2) {
        int gv = val;
        int rv = (up[0] + dn[0]) >> 1;
        int bv = (row[1] + row[1]) >> 1;
        rgb[o0]   = clamp(rv);
        rgb[o0+1] = clamp(gv);
        rgb[o0+2] = clamp(bv);
    } else {
        int bv = val;
        int gv = (row[1] + row[1] + up[0] + dn[0]) >> 2;
        int rv = (up[1] + up[1] + dn[1] + dn[1]) >> 2;
        rgb[o0]   = clamp(rv);
        rgb[o0+1] = clamp(gv);
        rgb[o0+2] = clamp(bv);
    }

    /* Column 1: all neighbors available */
    int o1 = out_base + 3;
    val = row[1];
    if (od_type == 0) {
        int rv = val;
        int gv = (row[0] + row[2] + up[1] + dn[1]) >> 2;
        int bv = (up[0] + up[2] + dn[0] + dn[2]) >> 2;
        rgb[o1]   = clamp(rv);
        rgb[o1+1] = clamp(gv);
        rgb[o1+2] = clamp(bv);
    } else if (od_type == 1) {
        int gv = val;
        int rv = (row[0] + row[2]) >> 1;
        int bv = (up[1] + dn[1]) >> 1;
        rgb[o1]   = clamp(rv);
        rgb[o1+1] = clamp(gv);
        rgb[o1+2] = clamp(bv);
    } else if (od_type == 2) {
        int gv = val;
        int rv = (up[1] + dn[1]) >> 1;
        int bv = (row[0] + row[2]) >> 1;
        rgb[o1]   = clamp(rv);
        rgb[o1+1] = clamp(gv);
        rgb[o1+2] = clamp(bv);
    } else {
        int bv = val;
        int gv = (row[0] + row[2] + up[1] + dn[1]) >> 2;
        int rv = (up[0] + up[2] + dn[0] + dn[2]) >> 2;
        rgb[o1]   = clamp(rv);
        rgb[o1+1] = clamp(gv);
        rgb[o1+2] = clamp(bv);
    }

    /* ── NEON batches: 4 pairs (8 pixels) each, starting at x=2 ── */
    int x = 2;
    for (; x + 8 <= w - 2; x += 8) {
        uint8x8_t r_lo = vld1_u8(row + x - 1);
        uint8x8_t r_hi = vld1_u8(row + x + 1);
        uint8x8_t u_v  = vld1_u8(up + x);
        uint8x8_t d_v  = vld1_u8(dn + x);
        uint8x8_t u_lo = vld1_u8(up + x - 1);
        uint8x8_t d_lo = vld1_u8(dn + x - 1);

        /* Deinterleave: for x=2 (even), vuzp evens = even cols, odds = odd cols */
        uint8x8x2_t rl = vuzp_u8(r_lo, r_lo);
        uint8x8_t ev_center = rl.val[1];
        uint8x8_t ev_left   = rl.val[0];

        uint8x8x2_t rh = vuzp_u8(r_hi, r_hi);
        uint8x8_t od_center = rh.val[0];
        uint8x8_t od_right  = rh.val[1];
        uint8x8_t ev_right  = od_center;

        uint8x8x2_t uv = vuzp_u8(u_v, u_v);
        uint8x8_t u_even = uv.val[0];
        uint8x8_t u_odd  = uv.val[1];

        uint8x8x2_t dv = vuzp_u8(d_v, d_v);
        uint8x8_t d_even = dv.val[0];
        uint8x8_t d_odd  = dv.val[1];

        uint8x8x2_t ul = vuzp_u8(u_lo, u_lo);
        uint8x8_t u_diag = ul.val[0];

        uint8x8x2_t dl = vuzp_u8(d_lo, d_lo);
        uint8x8_t d_diag = dl.val[0];

        /* ── common interp values for even positions ── */
        uint8x8_t hv_g  = vshrn_n_u16(vaddq_u16(vaddl_u8(ev_left, ev_right),
                                                  vaddl_u8(u_even, d_even)), 2);
        uint8x8_t diag_b = vshrn_n_u16(vaddq_u16(vaddl_u8(u_diag, d_diag),
                                                   vaddl_u8(u_odd, d_odd)), 2);
        uint8x8_t hv_b   = diag_b;
        uint8x8_t diag_r = diag_b;

        /* ── common interp values for odd positions ── */
        uint8x8_t od_h_r = vrhadd_u8(ev_center, vext_u8(ev_center, od_right, 1));
        uint8x8_t od_v_b = vshrn_n_u16(vaddl_u8(u_odd, d_odd), 1);
        uint8x8_t od_v_r = od_v_b;
        uint8x8_t od_h_b = od_v_b;

        /* Even HV + odd V for type G1 */
        /* Even V + odd HV for type G2 */
        uint16x8_t ev_v_sum  = vaddl_u8(u_even, d_even);
        uint8x8_t ev_v_r     = vshrn_n_u16(ev_v_sum, 1);
        uint16x8_t od_h_sum  = vaddl_u8(ev_center, vext_u8(ev_center, od_right, 1));
        uint16x8_t od_hv_sum = vaddq_u16(od_h_sum, vaddl_u8(u_odd, d_odd));
        uint8x8_t od_hv_g    = vshrn_n_u16(od_hv_sum, 2);
        uint16x8_t od_diag_l = vaddl_u8(u_even, d_even);
        uint16x8_t od_diag_r = vaddl_u8(vext_u8(u_even, u_even, 1),
                                         vext_u8(d_even, d_even, 1));
        uint16x8_t od_diag_sum = vaddq_u16(od_diag_l, od_diag_r);
        uint8x8_t od_diag_r8  = vshrn_n_u16(od_diag_sum, 2);

        /* Column B (G2) interp */
        uint8x8_t ev_h_b = vrhadd_u8(ev_left, od_center);

        /* Extract to scalar arrays and map based on type */
        uint8_t ev_c[4], od_c[4];
        uint8_t ev_rv4[4], ev_gv4[4], ev_bv4[4];
        uint8_t od_rv4[4], od_gv4[4], od_bv4[4];
        memcpy(ev_c, &ev_center, 4);
        memcpy(od_c, &od_center, 4);

        switch (ev_type) {
        case 0:
            memcpy(ev_rv4, ev_c, 4);
            memcpy(ev_gv4, &hv_g, 4);
            memcpy(ev_bv4, &diag_b, 4);
            break;
        case 1:
            memcpy(ev_rv4, &od_h_r, 4);
            memcpy(ev_gv4, ev_c, 4);
            memcpy(ev_bv4, &od_v_b, 4);
            break;
        case 2:
            memcpy(ev_rv4, &ev_v_r, 4);
            memcpy(ev_gv4, ev_c, 4);
            memcpy(ev_bv4, &ev_h_b, 4);
            break;
        default:
            memcpy(ev_rv4, &diag_r, 4);
            memcpy(ev_gv4, &hv_g, 4);
            memcpy(ev_bv4, ev_c, 4);
            break;
        }
        switch (od_type) {
        case 0:
            memcpy(od_rv4, od_c, 4);
            memcpy(od_gv4, &od_hv_g, 4);
            memcpy(od_bv4, &od_diag_r8, 4);
            break;
        }
        switch (od_type) {
        case 0:
            memcpy(od_rv4, od_c, 4);
            memcpy(od_gv4, &od_hv_g, 4);
            memcpy(od_bv4, &od_diag_r8, 4);
            break;
        case 1:
            memcpy(od_rv4, &od_h_r, 4);
            memcpy(od_gv4, od_c, 4);
            memcpy(od_bv4, &od_v_b, 4);
            break;
        case 2:
            memcpy(od_rv4, &od_v_b, 4);
            memcpy(od_gv4, od_c, 4);
            memcpy(od_bv4, &od_h_b, 4);
            break;
        default:
            memcpy(od_rv4, &od_diag_r8, 4);
            memcpy(od_gv4, &od_hv_g, 4);
            memcpy(od_bv4, od_c, 4);
            break;
        }

        /* Store RGB for all 8 pixels in this batch */
        for (int i = 0; i < 4; i++) {
            int off = out_base + (x + 2 * i) * 3;
            rgb[off]   = ev_rv4[i];
            rgb[off+1] = ev_gv4[i];
            rgb[off+2] = ev_bv4[i];
            off = out_base + (x + 2 * i + 1) * 3;
            rgb[off]   = od_rv4[i];
            rgb[off+1] = od_gv4[i];
            rgb[off+2] = od_bv4[i];
        }
    }

    /* Scalar remainder */
    for (; x < w - 2; x += 2) {
        int off = out_base + x * 3;
        int val_e = row[x];
        int rv, gv, bv;
        switch (ev_type) {
        case 0:
            rv = val_e;
            gv = (row[x-1] + row[x+1] + up[x] + dn[x]) >> 2;
            bv = (up[x-1] + up[x+1] + dn[x-1] + dn[x+1]) >> 2;
            break;
        case 1:
            gv = val_e;
            rv = (row[x-1] + row[x+1]) >> 1;
            bv = (up[x] + dn[x]) >> 1;
            break;
        case 2:
            gv = val_e;
            rv = (up[x] + dn[x]) >> 1;
            bv = (row[x-1] + row[x+1]) >> 1;
            break;
        default:
            bv = val_e;
            gv = (row[x-1] + row[x+1] + up[x] + dn[x]) >> 2;
            rv = (up[x-1] + up[x+1] + dn[x-1] + dn[x+1]) >> 2;
            break;
        }
        rgb[off]   = clamp(rv);
        rgb[off+1] = clamp(gv);
        rgb[off+2] = clamp(bv);

        int val_o = row[x+1];
        off = out_base + (x+1)*3;
        switch (od_type) {
        case 0:
            rv = val_o;
            gv = (row[x] + row[x+2] + up[x+1] + dn[x+1]) >> 2;
            bv = (up[x] + up[x+2] + dn[x] + dn[x+2]) >> 2;
            break;
        case 1:
            gv = val_o;
            rv = (row[x] + row[x+2]) >> 1;
            bv = (up[x+1] + dn[x+1]) >> 1;
            break;
        case 2:
            gv = val_o;
            rv = (up[x+1] + dn[x+1]) >> 1;
            bv = (row[x] + row[x+2]) >> 1;
            break;
        default:
            bv = val_o;
            gv = (row[x] + row[x+2] + up[x+1] + dn[x+1]) >> 2;
            rv = (up[x] + up[x+2] + dn[x] + dn[x+2]) >> 2;
            break;
        }
        rgb[off]   = clamp(rv);
        rgb[off+1] = clamp(gv);
        rgb[off+2] = clamp(bv);
    }
}

void DemosaicNEON::process(const uint8_t* bayerInput, uint8_t* rgbOutput) {
    if (!mInitialized || !bayerInput || !rgbOutput || !mBayerBuf) return;

    const int w = mParams.width;
    const int h = mParams.height;
    const int s = w * 3;
    const int oy = mParams.offset_y & 1;
    const int ox = mParams.offset_x & 1;
    const int pat = mParams.bayerPattern;

    raw10_to_8bit(bayerInput, mBayerBuf, w * h, mParams.blackLevel);
    const uint8_t* b8 = mBayerBuf;

    static const uint8_t pos_color[4][4] = {
        {1, 0, 3, 2}, /* pat 0 = GBRG */
        {0, 1, 2, 3}, /* pat 1 = GRBG */
        {2, 3, 0, 1}, /* pat 2 = BGGR */
        {0, 1, 2, 3}, /* pat 3 = RGGB */
    };

    for (int y = 1; y < h - 1; y++) {
        int r = (y + oy) & 1;
        int pe = r * 2 + ox;
        int po = r * 2 + 1 - ox;
        int ev_type = pos_color[pat][pe];
        int od_type = pos_color[pat][po];
        int out_base = y * s;

        const uint8_t* row = b8 + y * w;
        const uint8_t* up  = b8 + (y - 1) * w;
        const uint8_t* dn  = b8 + (y + 1) * w;

        neon_row(row, up, dn, rgbOutput, w, out_base, ev_type, od_type);
    }

    fill_edges(rgbOutput, w, h);
}

} // namespace mocha
