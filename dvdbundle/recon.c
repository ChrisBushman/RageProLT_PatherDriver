/* recon.c - see recon.h */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "recon.h"

const rc_u8 rc_zigzag[64] = {
 0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
 35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63 };
const rc_u8 rc_altscan[64] = {
 0,8,16,24,1,9,2,10,17,25,32,40,48,56,57,49,41,33,26,18,3,11,4,12,19,27,34,42,50,58,35,43,
 51,59,20,28,5,13,6,14,21,29,36,44,52,60,37,45,53,61,22,30,7,15,23,31,38,46,54,62,39,47,55,63 };

/* ---------------------------------------------------------------- IDCT (Chen-Wang, 32-bit integer) */
#define W1 2841
#define W2 2676
#define W3 2408
#define W5 1609
#define W6 1108
#define W7 565

static void idct_row(rc_s16 *b)
{
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = b[4] << 11) | (x2 = b[6]) | (x3 = b[2]) | (x4 = b[1]) | (x5 = b[7]) | (x6 = b[5]) | (x7 = b[3]))) {
        int v = b[0] << 3; b[0] = b[1] = b[2] = b[3] = b[4] = b[5] = b[6] = b[7] = (rc_s16)v; return;
    }
    x0 = (b[0] << 11) + 128;
    x8 = W7 * (x4 + x5); x4 = x8 + (W1 - W7) * x4; x5 = x8 - (W1 + W7) * x5;
    x8 = W3 * (x6 + x7); x6 = x8 - (W3 - W5) * x6; x7 = x8 - (W3 + W5) * x7;
    x8 = x0 + x1; x0 -= x1;
    x1 = W6 * (x3 + x2); x2 = x1 - (W2 + W6) * x2; x3 = x1 + (W2 - W6) * x3;
    x1 = x4 + x6; x4 -= x6; x6 = x5 + x7; x5 -= x7;
    x7 = x8 + x3; x8 -= x3; x3 = x0 + x2; x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8; x4 = (181 * (x4 - x5) + 128) >> 8;
    b[0] = (rc_s16)((x7 + x1) >> 8); b[1] = (rc_s16)((x3 + x2) >> 8); b[2] = (rc_s16)((x0 + x4) >> 8); b[3] = (rc_s16)((x8 + x6) >> 8);
    b[4] = (rc_s16)((x8 - x6) >> 8); b[5] = (rc_s16)((x0 - x4) >> 8); b[6] = (rc_s16)((x3 - x2) >> 8); b[7] = (rc_s16)((x7 - x1) >> 8);
}
static int clip256(int v) { return v < -256 ? -256 : v > 255 ? 255 : v; }
static void idct_col(rc_s16 *b)
{
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    x1 = b[8 * 4] << 8; x2 = b[8 * 6]; x3 = b[8 * 2]; x4 = b[8 * 1]; x5 = b[8 * 7]; x6 = b[8 * 5]; x7 = b[8 * 3];
    x0 = (b[0] << 8) + 8192;
    x8 = W7 * (x4 + x5) + 4; x4 = (x8 + (W1 - W7) * x4) >> 3; x5 = (x8 - (W1 + W7) * x5) >> 3;
    x8 = W3 * (x6 + x7) + 4; x6 = (x8 - (W3 - W5) * x6) >> 3; x7 = (x8 - (W3 + W5) * x7) >> 3;
    x8 = x0 + x1; x0 -= x1;
    x1 = W6 * (x3 + x2) + 4; x2 = (x1 - (W2 + W6) * x2) >> 3; x3 = (x1 + (W2 - W6) * x3) >> 3;
    x1 = x4 + x6; x4 -= x6; x6 = x5 + x7; x5 -= x7;
    x7 = x8 + x3; x8 -= x3; x3 = x0 + x2; x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8; x4 = (181 * (x4 - x5) + 128) >> 8;
    b[0]  = (rc_s16)clip256((x7 + x1) >> 14); b[8]  = (rc_s16)clip256((x3 + x2) >> 14);
    b[16] = (rc_s16)clip256((x0 + x4) >> 14); b[24] = (rc_s16)clip256((x8 + x6) >> 14);
    b[32] = (rc_s16)clip256((x8 - x6) >> 14); b[40] = (rc_s16)clip256((x0 - x4) >> 14);
    b[48] = (rc_s16)clip256((x3 - x2) >> 14); b[56] = (rc_s16)clip256((x7 - x1) >> 14);
}
void rc_idct8x8(rc_s16 blk[64])
{
    int i;
    for (i = 0; i < 8; i++) idct_row(blk + 8 * i);
    for (i = 0; i < 8; i++) idct_col(blk + i);
}
void rc_mismatch(rc_s16 blk[64])
{
    int i, sum = 0;
    for (i = 0; i < 64; i++) sum += blk[i];
    if (!(sum & 1)) blk[63] ^= 1;      /* even sum: toggle LSB of F[7][7] (odd -> -1, even -> +1) */
}

/* ---------------------------------------------------------------- helpers */
static int be16s(const rc_u8 *p) { return (rc_s16)((p[0] << 8) | p[1]); }
static int clip8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static int ceil16(int v) { return (v + 15) & ~15; }

int rc_init(Recon *rc, int w, int h)
{
    size_t ysz, csz; int i;
    memset(rc, 0, sizeof *rc);
    rc->w = w; rc->h = h;
    rc->mbw = w >> 4; rc->mbh = ceil16(h) >> 4;
    rc->pitchY = (w + 63) & ~63; rc->pitchC = rc->pitchY >> 1;
    rc->hY = rc->mbh * 16; rc->hC = rc->hY >> 1;
    ysz = (size_t)rc->pitchY * rc->hY; csz = (size_t)rc->pitchC * rc->hC;
    rc->mem = (rc_u8 *)calloc(RC_NBUF, ysz + 2 * csz);
    if (!rc->mem) return -1;
    for (i = 0; i < RC_NBUF; i++) {
        rc->pl[i][0] = rc->mem + (size_t)i * (ysz + 2 * csz);
        rc->pl[i][1] = rc->pl[i][0] + ysz;
        rc->pl[i][2] = rc->pl[i][1] + csz;
    }
    memset(rc->mem, 128, RC_NBUF * (ysz + 2 * csz));
    return 0;
}
void rc_free(Recon *rc) { free(rc->mem); rc->mem = 0; }

/* ---------------------------------------------------------------- motion compensation
 * Predict a bw x bh block from one plane of a reference picture.
 *  base/pitch : plane origin and its row pitch;   par: -1 = frame, 0/1 = field parity (lines of that parity only)
 *  pw/ph      : plane dimensions in the SELECTED coordinate system (ph = frame lines, or field lines for par>=0)
 *  bx,by      : block origin in that system;      mvx,mvy: half-sample vector in that system
 * Rounding per ISO 13818-2 7.6.4: (a+b+1)>>1 horizontally/vertically, (a+b+c+d+2)>>2 for both. */
static int fetch(const rc_u8 *base, int pitch, int par, int pw, int ph, int x, int y)
{
    if (x < 0) x = 0; else if (x >= pw) x = pw - 1;
    if (y < 0) y = 0; else if (y >= ph) y = ph - 1;
    return par < 0 ? base[y * pitch + x] : base[(2 * y + par) * pitch + x];
}
static void mc_block(rc_u8 *out, int ostride, const rc_u8 *base, int pitch, int par, int pw, int ph,
                     int bx, int by, int bw, int bh, int mvx, int mvy)
{
    int ix = bx + (mvx >> 1), iy = by + (mvy >> 1), fx = mvx & 1, fy = mvy & 1, x, y;
    int inside = ix >= 0 && iy >= 0 && ix + bw + fx <= pw && iy + bh + fy <= ph;
    for (y = 0; y < bh; y++) {
        for (x = 0; x < bw; x++) {
            int a, b, c, d, v;
            if (inside) {
                const rc_u8 *p = par < 0 ? base + (iy + y) * pitch + ix + x : base + (2 * (iy + y) + par) * pitch + ix + x;
                int st = par < 0 ? pitch : 2 * pitch;
                a = p[0]; b = fx ? p[1] : 0; c = fy ? p[st] : 0; d = (fx && fy) ? p[st + 1] : 0;
            } else {
                a = fetch(base, pitch, par, pw, ph, ix + x, iy + y);
                b = fx ? fetch(base, pitch, par, pw, ph, ix + x + 1, iy + y) : 0;
                c = fy ? fetch(base, pitch, par, pw, ph, ix + x, iy + y + 1) : 0;
                d = (fx && fy) ? fetch(base, pitch, par, pw, ph, ix + x + 1, iy + y + 1) : 0;
            }
            if (fx && fy) v = (a + b + c + d + 2) >> 2;
            else if (fx) v = (a + b + 1) >> 1;
            else if (fy) v = (a + c + 1) >> 1;
            else v = a;
            out[y * ostride + x] = (rc_u8)v;
        }
    }
}
static int half_trunc(int v) { return v / 2; }       /* chroma vector: integer division, truncation toward zero (C99/gcc semantics) */

/* One prediction segment into pred planes at MB-relative line offset `dl` stepping `ds` lines in the MB.
 * Luma is segment (bw x bh) at MB-relative x=0; chroma is the matching half-size segment. */
typedef struct { rc_u8 Y[256], U[64], V[64]; } Pred;

static void seg(Recon *rc, Pred *dst, int avg, int src_buf, int par, int mbx, int by_luma, int dl, int ds, int bh,
                int mvx, int mvy)
{
    rc_u8 ty[16 * 16], tu[8 * 8], tv[8 * 8];
    int pwY = rc->w, pwC = rc->w >> 1;
    int phY = par < 0 ? rc->hY : rc->hY >> 1, phC = par < 0 ? rc->hC : rc->hC >> 1;
    int cmx = half_trunc(mvx), cmy = half_trunc(mvy), r, c;
    mc_block(ty, 16, rc->pl[src_buf][0], rc->pitchY, par, pwY, phY, mbx, by_luma, 16, bh, mvx, mvy);
    mc_block(tu, 8, rc->pl[src_buf][1], rc->pitchC, par, pwC, phC, mbx >> 1, by_luma >> 1, 8, bh >> 1, cmx, cmy);
    mc_block(tv, 8, rc->pl[src_buf][2], rc->pitchC, par, pwC, phC, mbx >> 1, by_luma >> 1, 8, bh >> 1, cmx, cmy);
    for (r = 0; r < bh; r++) {
        rc_u8 *d = dst->Y + (dl + r * ds) * 16;
        for (c = 0; c < 16; c++) d[c] = avg ? (rc_u8)((d[c] + ty[r * 16 + c] + 1) >> 1) : ty[r * 16 + c];
    }
    for (r = 0; r < (bh >> 1); r++) {
        int dlc = ds == 1 ? (dl >> 1) + r : ((dl) + r * 2);   /* chroma line: frame/field-contiguous (ds==1) or interleaved (ds==2, dl = parity) */
        rc_u8 *du = dst->U + dlc * 8, *dv = dst->V + dlc * 8;
        for (c = 0; c < 8; c++) {
            du[c] = avg ? (rc_u8)((du[c] + tu[r * 8 + c] + 1) >> 1) : tu[r * 8 + c];
            dv[c] = avg ? (rc_u8)((dv[c] + tv[r * 8 + c] + 1) >> 1) : tv[r * 8 + c];
        }
    }
}

/* ---------------------------------------------------------------- picture decode */
static rc_s16 g_blk[64];

/* decode one pass (frame picture, or one field). Returns 0 ok. */
static int pass(Recon *rc, int st, int ptype, int second, int bd, int bf, int bb, int auxb,
                const rc_u8 **mbp, const rc_u8 **cop, rc_u8 *params)
{
    int nmb = rc->mbw * rc->mbh, n, x = 0, y = 0, cp = st == 2 ? 1 : 0;
    const rc_u8 *mb = *mbp, *co = *cop;
    (void)auxb; (void)params;
    if (st != 3) nmb >>= 1;
    for (n = 0; n < nmb; n++, mb += 0x1c) {
        Pred pr; int type = mb[0x14], flags = mb[0x15], b, fieldDct;
        rc_s16 *bl = g_blk;
        memset(&pr, 0, sizeof pr);
        rc->stat_mb++;
        if (rc->dbg_dec >= 0 && rc->dbg_n == rc->dbg_dec && n < nmb && y < 32) {
            int k; char *lp = getenv("DVDSTUB_LOGFILE"); FILE *lf = lp ? fopen(lp, "a") : stderr;
            if (lf) { fprintf(lf, "MBDBG dec=%d st=%d ptype=%d mb(%d,%d) type=%d flags=%d rec=", rc->dbg_n, st, ptype, x >> 4, y >> 4, type, flags);
              for (k = 0; k < 0x1c; k++) fprintf(lf, "%02x", mb[k]); fprintf(lf, "\n"); if (lp) fclose(lf); }
        }
        if (type < 8) rc->stat_type[type]++;
        if (st == 3 && flags) rc->stat_fielddct++;
        /* ---- prediction ---- */
        if (st == 3) {
            if (type == 1 || type == 2) {
                int s = type - 1;
                seg(rc, &pr, 0, s ? bb : bf, -1, x, y, 0, 1, 16, be16s(mb + 8 * 0 + 4 * s), be16s(mb + 8 * 0 + 4 * s + 2));
            } else if (type == 3) {
                seg(rc, &pr, 0, bf, -1, x, y, 0, 1, 16, be16s(mb + 0), be16s(mb + 2));
                seg(rc, &pr, 1, bb, -1, x, y, 0, 1, 16, be16s(mb + 4), be16s(mb + 6));
            } else if (type == 5 || type == 6 || type == 7) {
                int r;
                for (r = 0; r < 2; r++) {      /* r = destination field of the MB (0 top lines, 1 bottom lines) */
                    if (type != 6) seg(rc, &pr, 0, bf, mb[0x10 + 2 * r + 0], x, y >> 1, r, 2, 8, be16s(mb + 8 * r), be16s(mb + 8 * r + 2));
                    if (type == 6) seg(rc, &pr, 0, bb, mb[0x10 + 2 * r + 1], x, y >> 1, r, 2, 8, be16s(mb + 8 * r + 4), be16s(mb + 8 * r + 6));
                    if (type == 7) seg(rc, &pr, 1, bb, mb[0x10 + 2 * r + 1], x, y >> 1, r, 2, 8, be16s(mb + 8 * r + 4), be16s(mb + 8 * r + 6));
                }
            } else if (type != 0) rc->stat_unknown_type++;
        } else {
            int r;
            /* field picture: reference frame for forward refs depends on parity (second field of P may use the first field of this frame) */
#define FWD_BUF(par) ((ptype == 2 && second && (par) != cp) ? bd : bf)
            if (type == 1 || type == 2) {
                int s = type - 1, par = mb[0x10 + s];
                seg(rc, &pr, 0, s ? bb : FWD_BUF(par), par, x, y, 0, 1, 16, be16s(mb + 4 * s), be16s(mb + 4 * s + 2));
            } else if (type == 3) {
                seg(rc, &pr, 0, FWD_BUF(mb[0x10]), mb[0x10], x, y, 0, 1, 16, be16s(mb + 0), be16s(mb + 2));
                seg(rc, &pr, 1, bb, mb[0x11], x, y, 0, 1, 16, be16s(mb + 4), be16s(mb + 6));
            } else if (type == 5 || type == 6 || type == 7) {
                for (r = 0; r < 2; r++) {      /* r = upper/lower 16x8 half */
                    if (type != 6) seg(rc, &pr, 0, FWD_BUF(mb[0x10 + 2 * r]), mb[0x10 + 2 * r], x, y + 8 * r, 8 * r, 1, 8, be16s(mb + 8 * r), be16s(mb + 8 * r + 2));
                    if (type == 6) seg(rc, &pr, 0, bb, mb[0x10 + 2 * r + 1], x, y + 8 * r, 8 * r, 1, 8, be16s(mb + 8 * r + 4), be16s(mb + 8 * r + 6));
                    if (type == 7) seg(rc, &pr, 1, bb, mb[0x10 + 2 * r + 1], x, y + 8 * r, 8 * r, 1, 8, be16s(mb + 8 * r + 4), be16s(mb + 8 * r + 6));
                }
            } else if (type != 0) rc->stat_unknown_type++;
        }
        /* ---- residual + store ---- */
        fieldDct = (st == 3) && (flags != 0);
        for (b = 0; b < 6; b++) {
            int cnt = mb[0x16 + b], pos = -1, e;
            rc_s16 *res = bl;
            if (cnt || type == 0) {
                const rc_u8 *scan = rc->alt_scan ? rc_altscan : rc_zigzag;
                memset(bl, 0, 64 * sizeof(rc_s16));
                for (e = 0; e < cnt; e++, co += 4) {
                    if (rc->dbg_dec >= 0 && rc->dbg_n == rc->dbg_dec && type >= 5 && x == 17 * 16 && y == 8 * 16 && st == 2) {
                        char *lp = getenv("DVDSTUB_LOGFILE"); FILE *lf = lp ? fopen(lp, "a") : stderr;
                        if (lf) { fprintf(lf, "COEF blk=%d e=%d run=%d level=%d\n", b, e, co[0], (rc_s16)((co[2] << 8) | co[3])); if (lp) fclose(lf); }
                    }
                    pos += co[0] + 1;
                    if (pos <= 63) bl[scan[pos]] = (rc_s16)((co[2] << 8) | co[3]);
                }
                /* The framework tracks intra DC differentially from 0 (predictor reset value 0, dvdpb 0x8a742038 etc.), i.e.
                 * without ISO 13818-2's reset value 128<<3 = 1024 in dequantised units: add it back (== +128 per pixel). */
                if (type == 0) bl[0] = (rc_s16)(bl[0] + 1024);
                rc_mismatch(bl);
                rc_idct8x8(bl);
            } else res = 0;
            /* destination geometry */
            {
                int plane = b < 4 ? 0 : b - 3, bx = (b < 4 ? (b & 1) * 8 : 0), by = (b < 4 ? (b >> 1) * 8 : 0);
                int pitch = plane ? rc->pitchC : rc->pitchY, r, c, mbx = plane ? x >> 1 : x, mby = plane ? y >> 1 : y;
                rc_u8 *dst = rc->pl[bd][plane];
                const rc_u8 *prp = plane == 0 ? pr.Y : plane == 1 ? pr.U : pr.V;
                int pw = plane ? 8 : 16;
                for (r = 0; r < 8; r++) {
                    int pl_line = by + r, dline;          /* line within the MB plane block */
                    int srcline = pl_line;                /* line in pred array */
                    if (plane == 0 && fieldDct) {         /* luma blocks 0,1 = top-field lines, 2,3 = bottom-field lines */
                        srcline = (b >> 1) + 2 * r;
                        dline = mby + srcline;
                    } else dline = mby + pl_line;
                    if (st != 3) dline = 2 * dline + cp;   /* field picture: line in frame buffer */
                    for (c = 0; c < 8; c++) {
                        int v = prp[srcline * pw + bx + c] + (res ? res[r * 8 + c] : 0);
                        dst[dline * pitch + mbx + bx + c] = (rc_u8)clip8(v);
                    }
                }
            }
        }
        /* ---- MB position ---- */
        x += 16; if (x >= rc->w) { x = 0; y += 16; }
    }
    *mbp = mb; *cop = co;
    return 0;
}
/* Note: macroblocks with all-zero blocks and no prediction (type 0 with no coefficients) leave grey (pred = 0 + 0 => 0). */

int rc_decode(Recon *rc, rc_u8 *p)
{
    int f, passes = (p[2] == 3) ? 1 : 2;
    const rc_u8 *mb, *co;
    int ptype, st, bd, bf, bb;
    for (f = 0; f < passes; f++) {
        st = p[2 + f]; ptype = p[f];
        if (p[2] == 0 && f == 0) return 0;
        if (f == 1 && st != 1 && st != 2) break;   /* a Decode call may carry a single field (end of title): no second pass */
        bd = p[6]; bf = p[7]; bb = p[8];
        if (bd == 0xff) bd = p[6] = 0;
        if (bf == 0xff) bf = p[7] = 0;
        if (bb == 0xff) bb = p[8] = 0;
        if (bd >= RC_NBUF || bf >= RC_NBUF || bb >= RC_NBUF) return -1;
        if (f == 0) {
            memcpy(&mb, p + 12, sizeof mb);       /* native big-endian 32-bit pointers on PPC */
            memcpy(&co, p + 16, sizeof co);
        } else { mb = rc->mb_next; co = rc->coef_next; }
        rc->alt_scan = p[4 + f] & 1;           /* params +4/+5 = alternate_scan flag per pass (verified on an ffmpeg -alternate_scan 1 stream) */
        pass(rc, st, ptype, f, bd, bf, bb, p[4 + f], &mb, &co, p);
        rc->mb_next = mb; rc->coef_next = co;
    }
    return 0;
}
