/* recon_hw.c - MPEG-2 picture reconstruction with the prediction done by the Rage LT Pro scaler pipe (milestone M2).
 *
 * Pipeline per decode pass (frame picture or one field):
 *   1. host: per macroblock the IDCT (recon.c rc_decode_block); intra macroblocks produce final pixels, inter macroblocks the residual as signed bytes
 *      (two's complement, clamped to -128..127) in a staging copy of the destination planes;
 *   2. the staging planes are uploaded into the destination frame in VRAM (a field: only its lines);
 *   3. one RAGE_GL_MC_BATCH adds the half-pel / bidirectional prediction of every inter block onto it: out = clip(prediction + residual).
 * Hardware behaviour (docs/re-notes/mc-experiment-plan.md): integer, h and v half-pel are MPEG-2 exact, hv half-pel rounds twice (+1 in ~35% of samples),
 * two-reference blocks average with (a+b+1)>>1.  The bit-exact alternative is the CPU path (recon.c), selected by the caller.
 * Integer only, no threads, no AltiVec. */
#include <stdlib.h>
#include <string.h>
#include "recon.h"
#include "ragegl.h"
#include <stdio.h>
#include <sys/time.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>

static void hwlog(const char *fmt, ...)            /* DVDSTUB_HWDBG=1: one line per batch into the bundle log */
{
    char buf[256], path[512]; const char *p = getenv("DVDSTUB_LOG"), *hm = getenv("HOME"); va_list ap; int fd, n;
    if (!getenv("DVDSTUB_HWDBG")) return;
    if (!p) { if (!hm) return; snprintf(path, sizeof path, "%s/dvdstub.log", hm); p = path; }
    fd = open(p, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    va_start(ap, fmt); n = vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    if (n > 0) write(fd, buf, n < (int)sizeof buf ? n : (int)sizeof buf - 1);
    close(fd);
}

typedef struct {
    ragegl_t c;
    uint32_t off[RC_NBUF][3];            /* VRAM offsets of the planes */
    rc_u8 *stage[3];
    RageMcCmd *cmds; int ncmd, cap;
    unsigned long n_batches, n_cmds, n_clamp, n_badvec, n_upload, n_unsup, n_fix;
    unsigned long us[5];                    /* microseconds: host (IDCT + staging + commands), upload, engine batches, fix-ups, sync */
    struct Fix *fix; int nfix, capfix;      /* blocks whose residual exceeded the signed byte: corrected after the engine pass (see pass()) */
} Hw;

static unsigned long now_us(void) { struct timeval tv; gettimeofday(&tv, 0); return (unsigned long)tv.tv_sec * 1000000UL + (unsigned long)tv.tv_usec; }
typedef struct { int buf, par, mvx, mvy; } Ref;
struct Fix { int plane; unsigned off, step; rc_s16 ex[64]; };      /* off = byte offset of the block's first pixel in the plane, step = bytes between its lines */

static int be16s(const rc_u8 *p) { return (rc_s16)((p[0] << 8) | p[1]); }
static int clip8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static int half_trunc(int v) { return v / 2; }

int rch_init(Recon *rc)
{
    Hw *h = (Hw *)calloc(1, sizeof *h); int i, p;
    uint32_t sz[3];
    if (!h) return -1;
    if (ragegl_open(&h->c)) { free(h); return -2; }
    sz[0] = (uint32_t)rc->pitchY * (rc->hY + 3); sz[1] = sz[2] = (uint32_t)rc->pitchC * (rc->hC + 3);
    for (i = 0; i < RC_NBUF; i++) for (p = 0; p < 3; p++)
        if (ragegl_alloc(h->c, (sz[p] + 7) & ~7u, 8, &h->off[i][p])) goto fail;     /* the kext frees everything when the client closes */
    h->stage[0] = (rc_u8 *)malloc((size_t)rc->pitchY * (rc->hY + 3)); h->stage[1] = (rc_u8 *)malloc((size_t)rc->pitchC * (rc->hC + 3)); h->stage[2] = (rc_u8 *)malloc((size_t)rc->pitchC * (rc->hC + 3));
    h->cap = 8192; h->cmds = (RageMcCmd *)malloc(h->cap * sizeof(RageMcCmd));
    if (!h->stage[0] || !h->stage[1] || !h->stage[2] || !h->cmds) goto fail;
    /* grey start, like the CPU path */
    for (p = 0; p < 3; p++) memset(h->stage[p], 128, p ? (size_t)rc->pitchC * rc->hC : (size_t)rc->pitchY * rc->hY);
    for (i = 0; i < RC_NBUF; i++) for (p = 0; p < 3; p++)
        ragegl_vwrite(h->c, h->off[i][p], h->stage[p], (uint32_t)((p ? (size_t)rc->pitchC * rc->hC : (size_t)rc->pitchY * rc->hY) & ~3u), 0);
    rc->hw = h;
    return 0;
fail:
    for (p = 0; p < 3; p++) free(h->stage[p]);
    free(h->cmds); ragegl_close(h->c); free(h);
    return -3;
}
void rch_free(Recon *rc)
{
    Hw *h = (Hw *)rc->hw; int p;
    if (!h) return;
    for (p = 0; p < 3; p++) free(h->stage[p]);
    free(h->cmds); free(h->fix); ragegl_close(h->c);                  /* closing the client releases its VRAM blocks */
    free(h); rc->hw = 0;
}
void rch_stats(Recon *rc, unsigned long *s)
{
    Hw *h = (Hw *)rc->hw;
    if (!h) { memset(s, 0, 12 * sizeof *s); return; }
    s[0] = h->n_batches; s[1] = h->n_cmds; s[2] = h->n_clamp; s[3] = h->n_badvec; s[4] = h->n_upload; s[5] = h->n_unsup; s[6] = h->n_fix;
    s[7] = h->us[0]; s[8] = h->us[1]; s[9] = h->us[2]; s[10] = h->us[3]; s[11] = h->us[4];
}
void rch_sync(Recon *rc, int idx)
{
    Hw *h = (Hw *)rc->hw; int p;
    unsigned long t0;
    if (!h || idx < 0 || idx >= RC_NBUF) return;
    t0 = now_us();
    for (p = 0; p < 3; p++) ragegl_vread(h->c, h->off[idx][p], rc->pl[idx][p], (uint32_t)((p ? (size_t)rc->pitchC * rc->hC : (size_t)rc->pitchY * rc->hY) & ~3u), 0);
    h->us[4] += now_us() - t0;
}

void rch_read(Recon *rc, int idx, rc_u8 *dst[3])
{
    Hw *h = (Hw *)rc->hw; int p;
    if (!h || idx < 0 || idx >= RC_NBUF) return;
    for (p = 0; p < 3; p++) ragegl_vread(h->c, h->off[idx][p], dst[p], (uint32_t)((p ? (size_t)rc->pitchC * rc->hC : (size_t)rc->pitchY * rc->hY) & ~3u), 0);
}

static int flush(Hw *h)
{
    int done = 0, rc = 0; unsigned long t0 = now_us();
    while (done < h->ncmd) {
        int n = h->ncmd - done > RAGE_MC_MAX_CMDS ? RAGE_MC_MAX_CMDS : h->ncmd - done;
        kern_return_t kr = IOConnectMethodScalarIScalarO(h->c, RAGE_GL_MC_BATCH, 2, 0, (uint32_t)(uintptr_t)(h->cmds + done), (uint32_t)n);
        hwlog("  MC_BATCH n=%d kr=0x%x\n", n, (unsigned)kr);
        h->n_batches++; h->n_cmds += n; done += n;
        if (kr) { rc = -1; break; }
    }
    h->ncmd = 0; h->us[2] += now_us() - t0;
    return rc;
}

/* host-side copy of the kernel's validation: a block that cannot be done by the engine (vector outside the picture) is counted and skipped */
static int cmd_ok(const RageMcCmd *c)
{
    static const int bwT[4] = { 16, 16, 8, 8 }, bhT[4] = { 16, 8, 8, 4 };
    int bw = bwT[c->size], bh = bhT[c->size], k, nref = (c->flags & RAGE_MC_TWOWAY) ? 2 : 1;
    if (c->x < 0 || c->y < 0 || c->x + bw > c->width || c->y + bh > c->rows) return 0;
    for (k = 0; k < nref; k++) {
        int sx = (k ? c->mv2x : c->mvx) + 2 * c->x, sy = (k ? c->mv2y : c->mvy) + 2 * c->y;
        if (sx < 0 || sy < 0 || (sx >> 1) + bw + (sx & 1) > c->width || (sy >> 1) + bh + (sy & 1) > c->rows) return 0;
    }
    return 1;
}

/* One prediction unit: luma block (16 wide, bh = 16 or 8 lines) at x, ylum of the system, and its chroma blocks.
 * dpar < 0: frame system; dpar >= 0: field system of that parity (references then carry their own parity). */
static void emit_unit(Recon *rc, Hw *h, int bd, int nref, const Ref *r, int dpar, int x, int ylum, int bh)
{
    int pl, k;
    for (pl = 0; pl < 3; pl++) {
        RageMcCmd *c; int pitch = pl ? rc->pitchC : rc->pitchY, width = pl ? rc->w >> 1 : rc->w, rows = pl ? rc->hC : rc->hY;
        int fld = dpar >= 0;
        if (h->ncmd + 1 > h->cap) {                      /* commands are only executed after the residual upload (the upload would overwrite them): grow, never flush here */
            RageMcCmd *nc = (RageMcCmd *)realloc(h->cmds, (size_t)h->cap * 2 * sizeof(RageMcCmd));
            if (!nc) return;
            h->cmds = nc; h->cap *= 2;
        }
        c = &h->cmds[h->ncmd];
        memset(c, 0, sizeof *c);
        c->stride = (uint16_t)(fld ? 2 * pitch : pitch); c->width = (uint16_t)width; c->rows = (uint16_t)(fld ? rows >> 1 : rows);
        c->dstOff = h->off[bd][pl] + (fld ? (uint32_t)dpar * pitch : 0);
        c->size = (uint8_t)(pl == 0 ? (bh == 16 ? 0 : 1) : (bh == 16 ? 2 : 3));
        c->x = (int16_t)(pl ? x >> 1 : x); c->y = (int16_t)(pl ? ylum >> 1 : ylum);
        for (k = 0; k < nref; k++) {
            uint32_t so = h->off[r[k].buf][pl] + (fld ? (uint32_t)r[k].par * pitch : 0);
            int mx = pl ? half_trunc(r[k].mvx) : r[k].mvx, my = pl ? half_trunc(r[k].mvy) : r[k].mvy;
            if (k == 0) { c->srcOff = so; c->mvx = (int16_t)mx; c->mvy = (int16_t)my; }
            else { c->srcOff2 = so; c->mv2x = (int16_t)mx; c->mv2y = (int16_t)my; c->flags |= RAGE_MC_TWOWAY; }
        }
        if (nref == 1) c->srcOff2 = c->srcOff;
        if (!cmd_ok(c)) { h->n_badvec++; continue; }
        h->ncmd++;
    }
}


/* DVDSTUB_HWDBG: after a batch, recompute the first few single-reference luma blocks on the host from the VRAM contents and compare with what the engine wrote */
static void selfcheck(Recon *rc, Hw *h, int bd, const RageMcCmd *cm, int ncm)
{
    int i, checked = 0;
    for (i = 0; i < ncm && checked < 6; i++) {
        const RageMcCmd *c = &cm[i]; int bw, bh, x, y, bad = 0, tot = 0; rc_u8 *sb, *db;
        if (c->size > 1 || (c->flags & RAGE_MC_TWOWAY) || c->stride != rc->pitchY) continue;
        bw = 16; bh = c->size == 0 ? 16 : 8;
        sb = (rc_u8 *)malloc((size_t)c->stride * (c->rows + 1)); db = (rc_u8 *)malloc((size_t)c->stride * c->rows);
        if (!sb || !db) { free(sb); free(db); return; }
        ragegl_vread(h->c, c->srcOff, sb, (uint32_t)(c->stride * (c->rows + 1)) & ~3u, 0); ragegl_vread(h->c, c->dstOff, db, (uint32_t)(c->stride * c->rows) & ~3u, 0);
        for (y = 0; y < bh; y++) for (x = 0; x < bw; x++) {
            int sx = c->mvx + 2 * c->x, sy = c->mvy + 2 * c->y, ax = sx >> 1, ay = sy >> 1, fx = sx & 1, fy = sy & 1, p;
            int a = sb[(ay + y) * c->stride + ax + x], b2 = fx ? sb[(ay + y) * c->stride + ax + x + 1] : 0, cc = fy ? sb[(ay + y + 1) * c->stride + ax + x] : 0, d = (fx && fy) ? sb[(ay + y + 1) * c->stride + ax + x + 1] : 0;
            int res = (signed char)h->stage[0][(c->y + y) * c->stride + c->x + x];
            p = fx && fy ? (((a + b2 + 1) >> 1) + ((cc + d + 1) >> 1) + 1) >> 1 : fx ? (a + b2 + 1) >> 1 : fy ? (a + cc + 1) >> 1 : a;
            p = clip8(p + res); tot++; if (db[(c->y + y) * c->stride + c->x + x] != p) bad++;
        }
        hwlog("    selfcheck cmd%d at %d,%d mv %d,%d: %d of %d differ (dst[0]=%02x expect residual+pred)\n", i, c->x, c->y, c->mvx, c->mvy, bad, tot, db[c->y * c->stride + c->x]);
        free(sb); free(db); checked++;
    }
}

static int pass(Recon *rc, Hw *h, int st, int ptype, int second, int bd, int bf, int bb, const rc_u8 **mbp, const rc_u8 **cop)
{
    int nmb = rc->mbw * rc->mbh, n, x = 0, y = 0, cp = st == 2 ? 1 : 0, b;
    const rc_u8 *mb = *mbp, *co = *cop;
    rc_s16 blk[64]; unsigned long t0 = now_us(), t1;
    if (st != 3) nmb >>= 1;
    memset(h->stage[0], 0, (size_t)rc->pitchY * rc->hY); memset(h->stage[1], 0, (size_t)rc->pitchC * rc->hC); memset(h->stage[2], 0, (size_t)rc->pitchC * rc->hC);
    h->ncmd = 0; h->nfix = 0;
#define FWD_BUF(par) ((ptype == 2 && second && (par) != cp) ? bd : bf)
    for (n = 0; n < nmb; n++, mb += 0x1c) {
        int type = mb[0x14], flags = mb[0x15], fieldDct = (st == 3) && (flags != 0), r;
        Ref rf[2];
        rc->stat_mb++;
        if (type < 8) rc->stat_type[type]++;
        if (st == 3 && flags) rc->stat_fielddct++;
        /* ---- prediction commands ---- */
        if (st == 3) {
            if (type == 1 || type == 2) {
                int s = type - 1;
                rf[0].buf = s ? bb : bf; rf[0].par = -1; rf[0].mvx = be16s(mb + 4 * s); rf[0].mvy = be16s(mb + 4 * s + 2);
                emit_unit(rc, h, bd, 1, rf, -1, x, y, 16);
            } else if (type == 3) {
                rf[0].buf = bf; rf[0].par = -1; rf[0].mvx = be16s(mb); rf[0].mvy = be16s(mb + 2);
                rf[1].buf = bb; rf[1].par = -1; rf[1].mvx = be16s(mb + 4); rf[1].mvy = be16s(mb + 6);
                emit_unit(rc, h, bd, 2, rf, -1, x, y, 16);
            } else if (type == 5 || type == 6 || type == 7) {
                for (r = 0; r < 2; r++) {
                    int k = 0;
                    if (type != 6) { rf[k].buf = bf; rf[k].par = mb[0x10 + 2 * r]; rf[k].mvx = be16s(mb + 8 * r); rf[k].mvy = be16s(mb + 8 * r + 2); k++; }
                    if (type >= 6) { rf[k].buf = bb; rf[k].par = mb[0x10 + 2 * r + 1]; rf[k].mvx = be16s(mb + 8 * r + 4); rf[k].mvy = be16s(mb + 8 * r + 6); k++; }
                    emit_unit(rc, h, bd, k, rf, r, x, y >> 1, 8);
                }
            } else if (type != 0) { rc->stat_unknown_type++; h->n_unsup++; }
        } else {
            if (type == 1 || type == 2) {
                int s = type - 1, par = mb[0x10 + s];
                rf[0].buf = s ? bb : FWD_BUF(par); rf[0].par = par; rf[0].mvx = be16s(mb + 4 * s); rf[0].mvy = be16s(mb + 4 * s + 2);
                emit_unit(rc, h, bd, 1, rf, cp, x, y, 16);
            } else if (type == 3) {
                rf[0].buf = FWD_BUF(mb[0x10]); rf[0].par = mb[0x10]; rf[0].mvx = be16s(mb); rf[0].mvy = be16s(mb + 2);
                rf[1].buf = bb; rf[1].par = mb[0x11]; rf[1].mvx = be16s(mb + 4); rf[1].mvy = be16s(mb + 6);
                emit_unit(rc, h, bd, 2, rf, cp, x, y, 16);
            } else if (type == 5 || type == 6 || type == 7) {
                for (r = 0; r < 2; r++) {
                    int k = 0;
                    if (type != 6) { rf[k].buf = FWD_BUF(mb[0x10 + 2 * r]); rf[k].par = mb[0x10 + 2 * r]; rf[k].mvx = be16s(mb + 8 * r); rf[k].mvy = be16s(mb + 8 * r + 2); k++; }
                    if (type >= 6) { rf[k].buf = bb; rf[k].par = mb[0x10 + 2 * r + 1]; rf[k].mvx = be16s(mb + 8 * r + 4); rf[k].mvy = be16s(mb + 8 * r + 6); k++; }
                    emit_unit(rc, h, bd, k, rf, cp, x, y + 8 * r, 8);
                }
            } else if (type != 0) { rc->stat_unknown_type++; h->n_unsup++; }
        }
        /* ---- residual / intra pixels into the staging planes ---- */
        for (b = 0; b < 6; b++) {
            int cnt = mb[0x16 + b], have = cnt || type == 0;
            int plane = b < 4 ? 0 : b - 3, bx = b < 4 ? (b & 1) * 8 : 0, by = b < 4 ? (b >> 1) * 8 : 0;
            int pitch = plane ? rc->pitchC : rc->pitchY, mbx = plane ? x >> 1 : x, mby = plane ? y >> 1 : y, rr, cc;
            rc_u8 *dst = h->stage[plane];
            if (have) co = rc_decode_block(rc, co, cnt, type, blk);
            else continue;                                    /* no residual: the staging stays 0 (inter) */
            {
                int step = 1, line0, anyex = 0; rc_s16 ex[64];
                line0 = mby + by;
                if (plane == 0 && fieldDct) { line0 = mby + (b >> 1); step = 2; }
                if (st != 3) { line0 = 2 * line0 + cp; step *= 2; }
                for (rr = 0; rr < 8; rr++) {
                    int dline = line0 + rr * step;
                    for (cc = 0; cc < 8; cc++) {
                        int v = blk[rr * 8 + cc];
                        if (type == 0) dst[dline * pitch + mbx + bx + cc] = (rc_u8)clip8(v);
                        else {
                            int e = 0;
                            if (v < -128) { e = v + 128; v = -128; } else if (v > 127) { e = v - 127; v = 127; }
                            ex[rr * 8 + cc] = (rc_s16)e; if (e) { anyex = 1; h->n_clamp++; }
                            dst[dline * pitch + mbx + bx + cc] = (rc_u8)(v & 0xff);
                        }
                    }
                }
                /* A residual outside -128..127 cannot be stored in the destination byte.  The engine then produces clip(pred + clamped); because that clip is
                 * monotone the exact result is clip(out + excess) (out saturated at 0/255 stays there), applied after the pass. */
                if (anyex) {
                    struct Fix *f;
                    if (h->nfix == h->capfix) { h->capfix = h->capfix ? h->capfix * 2 : 256; h->fix = (struct Fix *)realloc(h->fix, h->capfix * sizeof *h->fix); if (!h->fix) { h->capfix = h->nfix = 0; return -1; } }
                    f = &h->fix[h->nfix++];
                    f->plane = plane; f->off = (unsigned)(line0 * pitch + mbx + bx); f->step = (unsigned)(step * pitch); memcpy(f->ex, ex, sizeof ex);
                }
            }
        }
        x += 16; if (x >= rc->w) { x = 0; y += 16; }
    }
    *mbp = mb; *cop = co;
    t1 = now_us(); h->us[0] += t1 - t0;
    /* ---- upload the residual / intra planes, then let the engine add the predictions ---- */
    for (b = 0; b < 3; b++) {
        int pitch = b ? rc->pitchC : rc->pitchY, rows = b ? rc->hC : rc->hY;
        if (st == 3) { kern_return_t ur = ragegl_vwrite(h->c, h->off[bd][b], h->stage[b], (uint32_t)(pitch * rows), 0); if (ur) hwlog("  UPLOAD plane %d len %d FAILED 0x%x\n", b, pitch * rows, (unsigned)ur); }
        else IOConnectMethodScalarIScalarO(h->c, RAGE_GL_VRAM_WRITE_ROWS, 6, 0, h->off[bd][b] + cp * pitch, (uint32_t)(uintptr_t)(h->stage[b] + cp * pitch),
                                           (uint32_t)pitch | ((uint32_t)(rows >> 1) << 16), 2u * pitch, 2u * pitch, 0u);
        h->n_upload++;
    }
    if (getenv("DVDSTUB_HWDBG") && st == 3) for (b = 0; b < 3; b++) {          /* verify the upload landed */
        int pitch = b ? rc->pitchC : rc->pitchY, rows = b ? rc->hC : rc->hY, i, bad = 0; rc_u8 *chk = (rc_u8 *)malloc((size_t)pitch * rows);
        if (chk) { ragegl_vread(h->c, h->off[bd][b], chk, (uint32_t)(pitch * rows), 0); for (i = 0; i < pitch * rows; i++) if (chk[i] != h->stage[b][i]) bad++; free(chk); }
        hwlog("  VERIFY plane %d: %d of %d bytes differ after upload\n", b, bad, pitch * rows);
    }
    { rc_u8 t[8]; for (b = 0; b < 3; b++) ragegl_vread(h->c, h->off[bd][b], t, 8, 0); }      /* a read from the aperture pushes the posted upload writes out before the engine starts */
    h->us[1] += now_us() - t1;
    if (getenv("DVDSTUB_HWDBG") && st == 3) { RageMcCmd keep[16]; int nk = h->ncmd < 16 ? h->ncmd : 16; memcpy(keep, h->cmds, nk * sizeof(RageMcCmd)); if (flush(h)) return -1; selfcheck(rc, h, bd, keep, nk); }
    else if (flush(h)) return -1;
    t1 = now_us();
    for (n = 0; n < h->nfix; n++) {                       /* residual excess fix-ups (rare: large residuals), 8 bytes per line */
        const struct Fix *f = &h->fix[n]; int rr, cc;
        for (rr = 0; rr < 8; rr++) {
            uint32_t o = h->off[bd][f->plane] + f->off + (uint32_t)rr * f->step; rc_u8 t[8];
            if (ragegl_vread(h->c, o, t, 8, 0)) return -1;
            for (cc = 0; cc < 8; cc++) t[cc] = (rc_u8)clip8(t[cc] + f->ex[rr * 8 + cc]);
            if (ragegl_vwrite(h->c, o, t, 8, 0)) return -1;
        }
        h->n_fix++;
    }
    h->us[3] += now_us() - t1;
    return 0;
}

int rch_decode(Recon *rc, rc_u8 *p)
{
    Hw *h = (Hw *)rc->hw;
    int f, passes = (p[2] == 3) ? 1 : 2;
    const rc_u8 *mb, *co;
    int ptype, st, bd, bf, bb;
    if (!h) return -1;
    for (f = 0; f < passes; f++) {
        st = p[2 + f]; ptype = p[f];
        if (p[2] == 0 && f == 0) return 0;
        if (f == 1 && st != 1 && st != 2) break;
        bd = p[6]; bf = p[7]; bb = p[8];
        if (bd == 0xff) bd = p[6] = 0;
        if (bf == 0xff) bf = p[7] = 0;
        if (bb == 0xff) bb = p[8] = 0;
        if (bd >= RC_NBUF || bf >= RC_NBUF || bb >= RC_NBUF) return -1;
        if (f == 0) { memcpy(&mb, p + 12, sizeof mb); memcpy(&co, p + 16, sizeof co); }
        else { mb = rc->mb_next; co = rc->coef_next; }
        rc->alt_scan = p[4 + f] & 1;
        if (pass(rc, h, st, ptype, f, bd, bf, bb, &mb, &co) < 0) return -1;
        rc->mb_next = mb; rc->coef_next = co;
    }
    return 0;
}
