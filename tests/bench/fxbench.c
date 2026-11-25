/* Isolation benchmark for the CPU rasteriser's per-pixel attribute maths: speed (ns/pixel, net of the shared edge loop) and precision (against a
 * double-precision reference that exists only in this test) for each candidate.  Build: cc -O2 -I../../gldriver fxbench.c -o fxbench  (run on the G4). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include "gld_fxmap.h"

typedef struct { int64_t x[3], y[3]; float s[3], t[3], r[3], w[3]; uint32_t argb[3]; int64_t area; } tri_t;
static uint32_t rng = 12345u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static float rf(float a, float b) { return a + (b - a) * (float)(rnd() & 0xffff) / 65535.0f; }

#define NTRI 40
static tri_t tris[NTRI];
static int TW = 256, TH = 256; static int SPL = 16;        /* span length in pixels (power of two) */

static void make(int size_px, float ratio, float tile)
{
    int i, j;
    for (i = 0; i < NTRI; i++) {
        tri_t *t = &tris[i]; int64_t bx = 8 + (rnd() % 400), by = 8 + (rnd() % 300);
        do {
            for (j = 0; j < 3; j++) {
                t->x[j] = (bx + (int)(rnd() % (unsigned)size_px)) * 4 + (int)(rnd() & 3); t->y[j] = (by + (int)(rnd() % (unsigned)size_px)) * 4 + (int)(rnd() & 3);
                t->w[j] = 1.0f / rf(1.0f, ratio); t->s[j] = rf(0, tile); t->t[j] = rf(0, tile); t->r[j] = rf(-1, 1); t->argb[j] = rnd() | (rnd() << 24);
            }
            t->area = (t->x[1] - t->x[0]) * (t->y[2] - t->y[0]) - (t->x[2] - t->x[0]) * (t->y[1] - t->y[0]);
            if (t->area < 0) { int64_t a = t->x[1], b = t->y[1]; float f; uint32_t c;
                t->x[1] = t->x[2]; t->y[1] = t->y[2]; t->x[2] = a; t->y[2] = b;
                f = t->s[1]; t->s[1] = t->s[2]; t->s[2] = f; f = t->t[1]; t->t[1] = t->t[2]; t->t[2] = f; f = t->w[1]; t->w[1] = t->w[2]; t->w[2] = f; f = t->r[1]; t->r[1] = t->r[2]; t->r[2] = f;
                c = t->argb[1]; t->argb[1] = t->argb[2]; t->argb[2] = c; t->area = -t->area; }
        } while (t->area < 4 * 16 * 4);
    }
}

typedef struct { double maxerr, sum; long n, mism; } stat_t;
static void upd(stat_t *st, double err, int mism) { if (err > st->maxerr) st->maxerr = err; st->sum += err; st->n++; st->mism += mism; }

/* the edge loop shared by every variant; MODE selects the per-pixel stage */
enum { M_NULL, M_FLOAT32, M_FLOAT, M_FX, M_FXSPAN, M_AFFINE, M_COL_DIV, M_COL_INC, M_CUBE_F, M_CUBE_X };
static volatile uint32_t sink;

static uint32_t run(int mode, stat_t *st, stat_t *st2)
{
    int ti; uint32_t acc = 0;
    for (ti = 0; ti < NTRI; ti++) {
        const tri_t *t = &tris[ti]; int64_t A[3], B[3], E[3], dx1 = t->x[1] - t->x[0], dy1 = t->y[1] - t->y[0], dx2 = t->x[2] - t->x[0], dy2 = t->y[2] - t->y[0], area = t->area;
        int i, minx, maxx, miny, maxy, px, py;
        fx_plane pP, pQ, pR, pC[4], pX, pY, pZ; int kP = 0, kR = 0, kC = 0; fx_inv iv;
        if (mode == M_FX || mode == M_FXSPAN || mode == M_AFFINE || mode == M_COL_INC || mode == M_CUBE_X) fx_inv_setup(&iv, area);
        for (i = 0; i < 3; i++) { int j = (i + 1) % 3; A[i] = -(t->y[j] - t->y[i]); B[i] = t->x[j] - t->x[i]; }
        minx = (int)(((t->x[0] < t->x[1] ? (t->x[0] < t->x[2] ? t->x[0] : t->x[2]) : (t->x[1] < t->x[2] ? t->x[1] : t->x[2]))) >> 2);
        maxx = (int)(((t->x[0] > t->x[1] ? (t->x[0] > t->x[2] ? t->x[0] : t->x[2]) : (t->x[1] > t->x[2] ? t->x[1] : t->x[2]))) >> 2);
        miny = (int)(((t->y[0] < t->y[1] ? (t->y[0] < t->y[2] ? t->y[0] : t->y[2]) : (t->y[1] < t->y[2] ? t->y[1] : t->y[2]))) >> 2);
        maxy = (int)(((t->y[0] > t->y[1] ? (t->y[0] > t->y[2] ? t->y[0] : t->y[2]) : (t->y[1] > t->y[2] ? t->y[1] : t->y[2]))) >> 2);
        /* per-triangle setup of the fixed-point candidates */
        if (mode == M_FX || mode == M_FXSPAN || mode == M_AFFINE) {
            float P[3], Q[3], mp = 0, mr = 0;
            for (i = 0; i < 3; i++) { P[i] = t->s[i] * t->w[i] * (float)TW * 256.0f; Q[i] = t->t[i] * t->w[i] * (float)TH * 256.0f;
                if (fabsf(P[i]) > mp) mp = fabsf(P[i]); if (fabsf(Q[i]) > mp) mp = fabsf(Q[i]); if (t->w[i] > mr) mr = t->w[i]; }
            kP = fx_scale_for(mp); kR = fx_scale_for(mr);
            fx_plane_setup(&pP, fx_from_float(P[0], kP), fx_from_float(P[1], kP), fx_from_float(P[2], kP), dx1, dy1, dx2, dy2, &iv);
            fx_plane_setup(&pQ, fx_from_float(Q[0], kP), fx_from_float(Q[1], kP), fx_from_float(Q[2], kP), dx1, dy1, dx2, dy2, &iv);
            fx_plane_setup(&pR, fx_from_float(t->w[0], kR), fx_from_float(t->w[1], kR), fx_from_float(t->w[2], kR), dx1, dy1, dx2, dy2, &iv);
        }
        if (mode == M_COL_INC) for (i = 0; i < 4; i++) { int sh = i * 8; fx_plane_setup(&pC[i], (int64_t)((t->argb[0] >> sh) & 255u) << 22, (int64_t)((t->argb[1] >> sh) & 255u) << 22, (int64_t)((t->argb[2] >> sh) & 255u) << 22, dx1, dy1, dx2, dy2, &iv); }
        if (mode == M_CUBE_X) {
            float mx = 0, S[3], T[3], R[3];
            for (i = 0; i < 3; i++) { S[i] = t->s[i] - 0.5f; T[i] = t->t[i] - 0.5f; R[i] = t->r[i]; S[i] *= t->w[i]; T[i] *= t->w[i]; R[i] *= t->w[i];
                if (fabsf(S[i]) > mx) mx = fabsf(S[i]); if (fabsf(T[i]) > mx) mx = fabsf(T[i]); if (fabsf(R[i]) > mx) mx = fabsf(R[i]); }
            kC = fx_scale_for(mx);
            fx_plane_setup(&pX, fx_from_float(S[0], kC), fx_from_float(S[1], kC), fx_from_float(S[2], kC), dx1, dy1, dx2, dy2, &iv);
            fx_plane_setup(&pY, fx_from_float(T[0], kC), fx_from_float(T[1], kC), fx_from_float(T[2], kC), dx1, dy1, dx2, dy2, &iv);
            fx_plane_setup(&pZ, fx_from_float(R[0], kC), fx_from_float(R[1], kC), fx_from_float(R[2], kC), dx1, dy1, dx2, dy2, &iv);
        }
        for (py = miny; py <= maxy; py++) {
            int64_t qy = 4 * (int64_t)py + 2, qx0 = 4 * (int64_t)minx + 2, PA = 0, QA = 0, RA = 0, CA[4] = { 0, 0, 0, 0 }, XA = 0, YA = 0, ZA = 0;
            int32_t su = 0, sv = 0, du = 0, dv = 0; int spx0 = 0, spx1 = 0;     /* span state (su, sv, du, dv in Q16 texels) */
            for (i = 0; i < 3; i++) E[i] = A[i] * (qx0 - t->x[i]) + B[i] * (qy - t->y[i]);
            if (mode == M_FX || mode == M_FXSPAN || mode == M_AFFINE) { PA = fx_plane_at(&pP, qx0 - t->x[0], qy - t->y[0]); QA = fx_plane_at(&pQ, qx0 - t->x[0], qy - t->y[0]); RA = fx_plane_at(&pR, qx0 - t->x[0], qy - t->y[0]); }
            if (mode == M_COL_INC) for (i = 0; i < 4; i++) CA[i] = fx_plane_at(&pC[i], qx0 - t->x[0], qy - t->y[0]);
            if (mode == M_CUBE_X) { XA = fx_plane_at(&pX, qx0 - t->x[0], qy - t->y[0]); YA = fx_plane_at(&pY, qx0 - t->x[0], qy - t->y[0]); ZA = fx_plane_at(&pZ, qx0 - t->x[0], qy - t->y[0]); }
            if (mode == M_FXSPAN) {                                          /* covered pixel range of this row, from the three edge equations (one divide per edge per ROW) */
                int64_t lo = minx, hi = maxx;
                for (i = 0; i < 3; i++) {
                    int64_t a4 = 4 * A[i];                                   /* E_i(px) = E_i + a4 * (px - minx) >= 0 */
                    if (a4 > 0) { int64_t need = (-E[i] + a4 - 1) / a4; if (E[i] >= 0) need = minx; else need += minx; if (need > lo) lo = need; }
                    else if (a4 < 0) { int64_t room = E[i] >= 0 ? E[i] / (-a4) : -1; if (room < 0) { hi = lo - 1; } else if (minx + room < hi) hi = minx + room; }
                    else if (E[i] < 0) hi = lo - 1;
                }
                spx0 = (int)lo; spx1 = (int)hi;
                if (spx0 > spx1) continue;
                { int64_t k = spx0 - minx; for (i = 0; i < 3; i++) E[i] += 4 * A[i] * k; PA += 4 * pP.gx * k; QA += 4 * pQ.gx * k; RA += 4 * pR.gx * k; }
            }
            for (px = (mode == M_FXSPAN ? spx0 : minx); px <= (mode == M_FXSPAN ? spx1 : maxx); px++) {
                if (mode == M_FXSPAN && ((px - spx0) & (SPL - 1)) == 0) {          /* exact at the span ends, linear between (done for every pixel position so a span is never stale) */
                    int n = spx1 - px; if (n > SPL - 1) n = SPL - 1;                    /* span covers px .. px + n */
                    su = fx_div(PA, RA, kP - kR); sv = fx_div(QA, RA, kP - kR);
                    if (n > 0) { int32_t u1 = fx_div(PA + (int64_t)n * 4 * pP.gx, RA + (int64_t)n * 4 * pR.gx, kP - kR), v1 = fx_div(QA + (int64_t)n * 4 * pQ.gx, RA + (int64_t)n * 4 * pR.gx, kP - kR);
                        du = (int32_t)(((int64_t)(u1 - su) * (65536 / n)) >> 8); dv = (int32_t)(((int64_t)(v1 - sv) * (65536 / n)) >> 8); }   /* per-pixel step in Q16 */
                    else du = dv = 0;
                    su <<= 8; sv <<= 8;
                }
                if ((E[0] | E[1] | E[2]) >= 0) {
                    int64_t w0 = E[1], w1 = E[2], w2 = E[0];
                    switch (mode) {
                    case M_FLOAT32:
                    case M_FLOAT: {
                        float f0 = mode == M_FLOAT ? (float)w0 : (float)(int32_t)w0, f1 = mode == M_FLOAT ? (float)w1 : (float)(int32_t)w1, f2 = mode == M_FLOAT ? (float)w2 : (float)(int32_t)w2, sw, tw, ww; int fu8, fv8;
                        sw = f0 * t->s[0] * t->w[0] + f1 * t->s[1] * t->w[1] + f2 * t->s[2] * t->w[2];
                        tw = f0 * t->t[0] * t->w[0] + f1 * t->t[1] * t->w[1] + f2 * t->t[2] * t->w[2];
                        ww = f0 * t->w[0] + f1 * t->w[1] + f2 * t->w[2];
                        if (ww != 0.0f) { sw /= ww; tw /= ww; }
                        fu8 = (int)(sw * (float)TW * 256.0f); fv8 = (int)(tw * (float)TH * 256.0f);
                        acc += (uint32_t)fu8 ^ (uint32_t)fv8;
                        if (st) { double rs = 0, rt = 0, rw = 0; int k; for (k = 0; k < 3; k++) { double l = (double)(k == 0 ? w0 : k == 1 ? w1 : w2) / (double)area; rs += l * t->s[k] * t->w[k]; rt += l * t->t[k] * t->w[k]; rw += l * t->w[k]; }
                            rs = rs / rw * TW; rt = rt / rw * TH;
                            upd(st, fabs(fu8 / 256.0 - rs) > fabs(fv8 / 256.0 - rt) ? fabs(fu8 / 256.0 - rs) : fabs(fv8 / 256.0 - rt), (int)floor(fu8 / 256.0) != (int)floor(rs) || (int)floor(fv8 / 256.0) != (int)floor(rt)); }
                        break; }
                    case M_FX: {
                        int32_t u = fx_div(PA, RA, kP - kR), v = fx_div(QA, RA, kP - kR);
                        acc += (uint32_t)u ^ (uint32_t)v;
                        if (st) { double rs = 0, rt = 0, rw = 0; int k; for (k = 0; k < 3; k++) { double l = (double)(k == 0 ? w0 : k == 1 ? w1 : w2) / (double)area; rs += l * t->s[k] * t->w[k]; rt += l * t->t[k] * t->w[k]; rw += l * t->w[k]; }
                            rs = rs / rw * TW; rt = rt / rw * TH;
                            upd(st, fabs(u / 256.0 - rs) > fabs(v / 256.0 - rt) ? fabs(u / 256.0 - rs) : fabs(v / 256.0 - rt), (int)floor(u / 256.0) != (int)floor(rs) || (int)floor(v / 256.0) != (int)floor(rt)); }
                        break; }
                    case M_FXSPAN: {
                        int32_t u, v;
                        { int k = (px - spx0) & (SPL - 1); u = (su + du * k) >> 8; v = (sv + dv * k) >> 8; }
                        acc += (uint32_t)u ^ (uint32_t)v;
                        if (st) { double rs = 0, rt = 0, rw = 0; int k; for (k = 0; k < 3; k++) { double l = (double)(k == 0 ? w0 : k == 1 ? w1 : w2) / (double)area; rs += l * t->s[k] * t->w[k]; rt += l * t->t[k] * t->w[k]; rw += l * t->w[k]; }
                            rs = rs / rw * TW; rt = rt / rw * TH;
                            upd(st, fabs(u / 256.0 - rs) > fabs(v / 256.0 - rt) ? fabs(u / 256.0 - rs) : fabs(v / 256.0 - rt), (int)floor(u / 256.0) != (int)floor(rs) || (int)floor(v / 256.0) != (int)floor(rt)); }
                        break; }
                    case M_AFFINE: {                                     /* valid only when w is constant over the triangle: u = P / R is linear, one divide per ROW */
                        int32_t u, v;
                        if (px == minx) { su = fx_div(PA, RA, kP - kR); sv = fx_div(QA, RA, kP - kR); du = fx_div(PA + 4 * pP.gx * 64, RA, kP - kR) - su; dv = fx_div(QA + 4 * pQ.gx * 64, RA, kP - kR) - sv; }
                        { int k = px - minx; u = su + (int32_t)(((int64_t)du * k) >> 6); v = sv + (int32_t)(((int64_t)dv * k) >> 6); }
                        acc += (uint32_t)u ^ (uint32_t)v;
                        break; }
                    case M_COL_DIV: {
                        uint32_t out = 0; int sh;
                        for (sh = 0; sh < 32; sh += 8) {
                            int64_t v = (w0 * (int64_t)((t->argb[0] >> sh) & 255u) + w1 * (int64_t)((t->argb[1] >> sh) & 255u) + w2 * (int64_t)((t->argb[2] >> sh) & 255u) + area / 2) / area;
                            if (v > 255) v = 255; out |= (uint32_t)v << sh;
                        }
                        acc += out;
                        if (st) { double ex; double l0 = (double)w0 / (double)area, l1 = (double)w1 / (double)area, l2 = (double)w2 / (double)area; double me = 0; int c;
                            for (c = 0; c < 4; c++) { ex = l0 * ((t->argb[0] >> (8 * c)) & 255) + l1 * ((t->argb[1] >> (8 * c)) & 255) + l2 * ((t->argb[2] >> (8 * c)) & 255); { double e = fabs((double)((out >> (8 * c)) & 255) - ex); if (e > me) me = e; } }
                            upd(st, me, 0); }
                        break; }
                    case M_COL_INC: {
                        uint32_t out = 0; int c;
                        for (c = 0; c < 4; c++) { int64_t v = (CA[c] + ((int64_t)1 << 37)) >> 38; if (v < 0) v = 0; else if (v > 255) v = 255; out |= (uint32_t)v << (8 * c); }
                        acc += out;
                        if (st) { double ex; double l0 = (double)w0 / (double)area, l1 = (double)w1 / (double)area, l2 = (double)w2 / (double)area; double me = 0;
                            for (c = 0; c < 4; c++) { ex = l0 * ((t->argb[0] >> (8 * c)) & 255) + l1 * ((t->argb[1] >> (8 * c)) & 255) + l2 * ((t->argb[2] >> (8 * c)) & 255); { double e = fabs((double)((out >> (8 * c)) & 255) - ex); if (e > me) me = e; } }
                            upd(st, me, 0); }
                        break; }
                    case M_CUBE_F: {                                     /* current float cube sampler: major axis, sc/ma, face coords in Q8 texels */
                        float f0 = (float)w0, f1 = (float)w1, f2 = (float)w2, s, tt, r, ww, as, at, ar, ma, sc, tc; int f, fu8, fv8;
                        s = f0 * (t->s[0] - 0.5f) * t->w[0] + f1 * (t->s[1] - 0.5f) * t->w[1] + f2 * (t->s[2] - 0.5f) * t->w[2];
                        tt = f0 * (t->t[0] - 0.5f) * t->w[0] + f1 * (t->t[1] - 0.5f) * t->w[1] + f2 * (t->t[2] - 0.5f) * t->w[2];
                        r = f0 * t->r[0] * t->w[0] + f1 * t->r[1] * t->w[1] + f2 * t->r[2] * t->w[2];
                        ww = f0 * t->w[0] + f1 * t->w[1] + f2 * t->w[2]; if (ww != 0.0f) { s /= ww; tt /= ww; r /= ww; }
                        as = s < 0 ? -s : s; at = tt < 0 ? -tt : tt; ar = r < 0 ? -r : r;
                        if (as >= at && as >= ar) { ma = as; if (s >= 0) { f = 0; sc = -r; tc = -tt; } else { f = 1; sc = r; tc = -tt; } }
                        else if (at >= ar) { ma = at; if (tt >= 0) { f = 2; sc = s; tc = r; } else { f = 3; sc = s; tc = -r; } }
                        else { ma = ar; if (r >= 0) { f = 4; sc = s; tc = -tt; } else { f = 5; sc = -s; tc = -tt; } }
                        if (ma == 0) ma = 1e-30f;
                        fu8 = (int)((0.5f * (sc / ma + 1.0f)) * (float)TW * 256.0f); fv8 = (int)((0.5f * (tc / ma + 1.0f)) * (float)TH * 256.0f);
                        acc += (uint32_t)(fu8 ^ fv8 ^ f);
                        if (st) { double ds = 0, dt = 0, dr = 0, dw = 0; int k; for (k = 0; k < 3; k++) { double l = (double)(k == 0 ? w0 : k == 1 ? w1 : w2) / (double)area; ds += l * (t->s[k] - 0.5) * t->w[k]; dt += l * (t->t[k] - 0.5) * t->w[k]; dr += l * t->r[k] * t->w[k]; dw += l * t->w[k]; }
                            { double a1 = fabs(ds), a2 = fabs(dt), a3 = fabs(dr), m, c1, c2; int rf2;
                              if (a1 >= a2 && a1 >= a3) { m = a1; if (ds >= 0) { rf2 = 0; c1 = -dr; c2 = -dt; } else { rf2 = 1; c1 = dr; c2 = -dt; } }
                              else if (a2 >= a3) { m = a2; if (dt >= 0) { rf2 = 2; c1 = ds; c2 = dr; } else { rf2 = 3; c1 = ds; c2 = -dr; } }
                              else { m = a3; if (dr >= 0) { rf2 = 4; c1 = ds; c2 = -dt; } else { rf2 = 5; c1 = -ds; c2 = -dt; } }
                              { double ru = 0.5 * (c1 / m + 1.0) * TW, rv = 0.5 * (c2 / m + 1.0) * TH; double e1 = fabs(fu8 / 256.0 - ru), e2 = fabs(fv8 / 256.0 - rv);
                                upd(st, rf2 == f ? (e1 > e2 ? e1 : e2) : 0, rf2 != f); } } (void)dw; }
                        break; }
                    case M_CUBE_X: {
                        int32_t s = (int32_t)(XA >> 16), tt = (int32_t)(YA >> 16), r = (int32_t)(ZA >> 16), as = s < 0 ? -s : s, at = tt < 0 ? -tt : tt, ar = r < 0 ? -r : r, ma, sc, tc; int f, e, fu8, fv8;
                        uint32_t rc; int64_t q;
                        if (as >= at && as >= ar) { ma = as; if (s >= 0) { f = 0; sc = -r; tc = -tt; } else { f = 1; sc = r; tc = -tt; } }
                        else if (at >= ar) { ma = at; if (tt >= 0) { f = 2; sc = s; tc = r; } else { f = 3; sc = s; tc = -r; } }
                        else { ma = ar; if (r >= 0) { f = 4; sc = s; tc = -tt; } else { f = 5; sc = -s; tc = -tt; } }
                        if (ma <= 0) ma = 1;
                        e = 31 - fx_clz((uint32_t)ma); rc = fx_recip_norm((uint32_t)ma << (31 - e));      /* 1/ma = rc / 2^(31+e) */
                        q = ((int64_t)sc * (int64_t)rc) >> (e + 31 - 15);                                 /* sc/ma in Q15, in [-1,1] */
                        fu8 = (int32_t)(((q + 32768) * TW * 256) >> 16);
                        q = ((int64_t)tc * (int64_t)rc) >> (e + 31 - 15);
                        fv8 = (int32_t)(((q + 32768) * TH * 256) >> 16);
                        acc += (uint32_t)(fu8 ^ fv8 ^ f);
                        if (st) { double ds = 0, dt = 0, dr = 0; int k; for (k = 0; k < 3; k++) { double l = (double)(k == 0 ? w0 : k == 1 ? w1 : w2) / (double)area; ds += l * (t->s[k] - 0.5) * t->w[k]; dt += l * (t->t[k] - 0.5) * t->w[k]; dr += l * t->r[k] * t->w[k]; }
                            { double a1 = fabs(ds), a2 = fabs(dt), a3 = fabs(dr), m, c1, c2; int rf2;
                              if (a1 >= a2 && a1 >= a3) { m = a1; if (ds >= 0) { rf2 = 0; c1 = -dr; c2 = -dt; } else { rf2 = 1; c1 = dr; c2 = -dt; } }
                              else if (a2 >= a3) { m = a2; if (dt >= 0) { rf2 = 2; c1 = ds; c2 = dr; } else { rf2 = 3; c1 = ds; c2 = -dr; } }
                              else { m = a3; if (dr >= 0) { rf2 = 4; c1 = ds; c2 = -dt; } else { rf2 = 5; c1 = -ds; c2 = -dt; } }
                              { double ru = 0.5 * (c1 / m + 1.0) * TW, rv = 0.5 * (c2 / m + 1.0) * TH; double e1 = fabs(fu8 / 256.0 - ru), e2 = fabs(fv8 / 256.0 - rv);
                                upd(st, rf2 == f ? (e1 > e2 ? e1 : e2) : 0, rf2 != f); } } }
                        break; }
                    default: acc += (uint32_t)(w0 ^ w1);
                    }
                    (void)st2;
                } else if (px > minx + 1 && ((E[0] & E[1] & E[2]) == 0) && 0) break;
                E[0] += 4 * A[0]; E[1] += 4 * A[1]; E[2] += 4 * A[2];
                if (mode == M_FX || mode == M_FXSPAN || mode == M_AFFINE) { PA += 4 * pP.gx; QA += 4 * pQ.gx; RA += 4 * pR.gx; }
                else if (mode == M_CUBE_X) { XA += 4 * pX.gx; YA += 4 * pY.gx; ZA += 4 * pZ.gx; }
                else if (mode == M_COL_INC) { CA[0] += 4 * pC[0].gx; CA[1] += 4 * pC[1].gx; CA[2] += 4 * pC[2].gx; CA[3] += 4 * pC[3].gx; }
            }
        }
    }
    return acc;
}

static double now(void) { struct timeval tv; gettimeofday(&tv, 0); return tv.tv_sec + tv.tv_usec * 1e-6; }
static long count_px(void)
{
    /* pixels inside: run the null variant's edge test */
    return 0;
}
static double timeit(int mode, long npx)
{
    double t0, t1; int reps = 0; uint32_t a = 0;
    t0 = now();
    do { a += run(mode, 0, 0); reps++; t1 = now(); } while (t1 - t0 < 0.6);
    sink = a;
    return (t1 - t0) / (double)reps / (double)npx * 1e9;
}
static long inside_px(void)
{
    long n = 0; int ti, px, py;
    for (ti = 0; ti < NTRI; ti++) { const tri_t *t = &tris[ti]; int64_t A[3], B[3];
        int i; for (i = 0; i < 3; i++) { int j = (i + 1) % 3; A[i] = -(t->y[j] - t->y[i]); B[i] = t->x[j] - t->x[i]; }
        for (py = 0; py < 480; py++) for (px = 0; px < 640; px++) { int64_t qx = 4 * px + 2, qy = 4 * py + 2; int ok = 1;
            for (i = 0; i < 3; i++) if (A[i] * (qx - t->x[i]) + B[i] * (qy - t->y[i]) < 0) ok = 0; n += ok; } }
    (void)count_px; return n;
}
static void report(const char *nm, int mode, long npx, double nullns)
{
    stat_t st; double ns;
    memset(&st, 0, sizeof st); run(mode, &st, 0); ns = timeit(mode, npx);
    printf("  %-34s %7.1f ns/px (net %6.1f)   max err %8.4f  mean %8.5f  nearest-texel mismatches %6.3f%%\n", nm, ns, ns - nullns, st.maxerr, st.n ? st.sum / st.n : 0, st.n ? 100.0 * st.mism / st.n : 0);
}
int main(int argc, char **argv)
{
    struct { const char *nm; int size; float ratio; float tile; } cfg[] = { { "big triangles (~120 px), mild perspective (w ratio 2), tile 1", 120, 2.0f, 1.0f },
        { "big triangles, strong perspective (w ratio 16), tile 8", 120, 16.0f, 8.0f },
        { "small triangles (~12 px), perspective 4, tile 1", 12, 4.0f, 1.0f },
        { "big triangles, no perspective (ratio 1), tile 4", 120, 1.0f, 4.0f },
        { "triangles ~24 px, perspective 4, tile 1", 24, 4.0f, 1.0f }, { "triangles ~40 px, perspective 4, tile 1", 40, 4.0f, 1.0f } };
    int c; (void)argc; (void)argv;
    fx_rcp_init();
    for (c = 0; c < 6; c++) {
        long npx; double nullns; rng = 777u + (uint32_t)c; make(cfg[c].size, cfg[c].ratio, cfg[c].tile); npx = inside_px(); nullns = timeit(M_NULL, npx);
        printf("%s  (%ld px total, shared edge loop %.1f ns/px)\n", cfg[c].nm, npx, nullns);
        report("texture coords: float (current)", M_FLOAT, npx, nullns);
        report("texture coords: float, int32 weights", M_FLOAT32, npx, nullns);
        report("texture coords: fixed + LUT recip", M_FX, npx, nullns);
        SPL = 16; report("texture coords: fixed, 16-px spans", M_FXSPAN, npx, nullns);
        SPL = 8; report("texture coords: fixed, 8-px spans", M_FXSPAN, npx, nullns);
        SPL = 4; report("texture coords: fixed, 4-px spans", M_FXSPAN, npx, nullns);
        if (c == 3) report("texture coords: affine (w const)", M_AFFINE, npx, nullns);
        report("colour: int64 barycentric divide", M_COL_DIV, npx, nullns);
        report("colour: incremental fixed-point", M_COL_INC, npx, nullns);
        report("cube map: float (current)", M_CUBE_F, npx, nullns);
        report("cube map: fixed + LUT recip", M_CUBE_X, npx, nullns);
    }
    return 0;
}
