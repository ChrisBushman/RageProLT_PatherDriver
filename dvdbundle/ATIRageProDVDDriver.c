/*
 * ATIRageProDVDDriver - M0 stub (docs/rage-dvd-bundle-design.md).
 * Implements the full DVDDriver* export set of DVD.framework's hardware-assist interface (modelled on
 * Apple's ATIRage128DVDDriver, docs/re-notes/r128-dvd-driver/).  M0: no hardware, no decode - it only
 * proves DVD.framework selects, loads and calls the bundle, and logs every call (args as hex) to
 * $HOME/dvdstub.log (or $DVDSTUB_LOG).  Integer-only, no threads, no AltiVec (build with -mcpu=750).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/time.h>
#include <ApplicationServices/ApplicationServices.h>
#include "recon.h"

typedef unsigned long u32;

static struct {                         /* mirrors Apple's G (spec C 1.2) */
    u32 displayMask, slotCount;
    void *services;
    void (*cb3)(u32, u32, u32);
    int (*cb4)(u32, u32, u32, u32, CGDirectDisplayID);
} G;

typedef struct { CGDirectDisplayID disp; u32 a4, a5, a6, flags, w, pitch, h; Recon rc; int rc_ok, cpu_mode, both; unsigned long cmp_n, cmp_exact, cmp_le1, cmp_le3, cmp_big, cmp_worst; rc_u8 *cmpbuf; unsigned long ndec, nshow, ndump; } Ctx;

/* Reconstruction path: default = Rage LT Pro motion compensation (docs/re-notes/mc-experiment-plan.md); the CPU path (recon.c) is bit-exact with MPEG-2.
 * The hardware rounds diagonal half-pel blocks twice (+1 in ~35% of their samples), so exactness-critical use selects the CPU path:
 * environment DVDSTUB_CPU=1 (tests) or Info.plist key RageDVDMode = "cpu".  If the hardware path cannot start, the CPU path is used. */
static int want_cpu_mode(void)
{
    const char *e = getenv("DVDSTUB_CPU");
    CFBundleRef b; CFTypeRef v;
    if (e) return atoi(e) != 0;
    b = CFBundleGetBundleWithIdentifier(CFSTR("com.local.ATIRageProDVDDriver"));
    if (b && (v = CFBundleGetValueForInfoDictionaryKey(b, CFSTR("RageDVDMode"))) && CFGetTypeID(v) == CFStringGetTypeID())
        return CFStringCompare((CFStringRef)v, CFSTR("cpu"), kCFCompareCaseInsensitive) == kCFCompareEqualTo;
    return 0;
}

static void LOG(const char *fmt, ...)
{
    char buf[256], path[512]; const char *p = getenv("DVDSTUB_LOG"); const char *h = getenv("HOME");
    va_list ap; int fd, n;
    if (!p) { if (!h) return; snprintf(path, sizeof path, "%s/dvdstub.log", h); p = path; }
    fd = open(p, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    if (getenv("DVDSTUB_TS")) { struct timeval tv; char ts[32]; int m; gettimeofday(&tv, 0); m = snprintf(ts, sizeof ts, "%ld.%03d ", (long)(tv.tv_sec % 10000), (int)(tv.tv_usec / 1000)); write(fd, ts, m); }
    va_start(ap, fmt); n = vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    if (n > 0) write(fd, buf, n < (int)sizeof buf ? n : (int)sizeof buf - 1);
    close(fd);
}

#define ARGS u32 a, u32 b, u32 c, u32 d, u32 e, u32 f
#define TRACE(name) LOG(name " %lx %lx %lx %lx %lx %lx\n", a, b, c, d, e, f)
#define STUB(name, ret) int DVDDriver##name(ARGS) { TRACE(#name); return (ret); }

void DVDInitializeLibrary(u32 services, u32 mask, void *cb3, void *cb4)
{
    G.services = (void *)services; G.displayMask = mask; G.cb3 = (void (*)(u32, u32, u32))cb3;
    G.cb4 = (int (*)(u32, u32, u32, u32, CGDirectDisplayID))cb4;
    { u32 i = 0, m = mask; G.slotCount = 0; while (m) { i++; if (m & 1) G.slotCount = i; m >>= 1; } }
    LOG("InitializeLibrary services=%lx mask=%lx cb3=%lx cb4=%lx slots=%lu\n", services, mask, (u32)cb3, (u32)cb4, G.slotCount);
}
void DVDTerminateLibrary(void) { LOG("TerminateLibrary\n"); memset(&G, 0, sizeof G); }

int DVDDriverGetVersion(int *a, int *b)
{
    LOG("GetVersion mask=%lx\n", G.displayMask);
    if (!G.displayMask) return (int)0xe00002bc;
    if (a) *a = 1;
    if (b) *b = 1;
    return 0;
}

int DVDDriverOpenDevice(Ctx **out, u32 *maxSize, CGDirectDisplayID disp, u32 a4, u32 a5, u32 a6,
                        u32 *flagsOut, u32 unused, short *nMP, short *nSP)
{
    Ctx *c; u32 mask;
    LOG("OpenDevice out=%lx maxSize=%lx disp=%lx a4=%lx a5=%lx a6=%lx flagsOut=%lx\n", (u32)out, (u32)maxSize, (u32)disp, a4, a5, a6, (u32)flagsOut);
    if (!out) return -7;
    *out = 0;
    mask = CGDisplayIDToOpenGLDisplayMask(disp);
    if (!mask || (mask & ~G.displayMask)) { LOG("OpenDevice: bad display mask %lx (G %lx)\n", mask, G.displayMask); return -5; }
    c = (Ctx *)calloc(1, sizeof *c);                   /* zeroed (Apple's is not) */
    if (!c) return -3;
    c->cpu_mode = want_cpu_mode();
    c->both = getenv("DVDSTUB_BOTH") != 0;       /* test mode: run the Rage MC path AND the CPU path on every Decode and compare the two pictures */
    LOG("OpenDevice: reconstruction mode %s\n", c->cpu_mode ? "CPU (exact)" : "Rage MC");
    c->disp = disp; c->a4 = a4; c->a5 = a5; c->a6 = a6;
    c->flags = 0x4020 | (CGDisplayBitsPerPixel(disp) == 16 ? 3 : 4);
    if (G.cb4 && G.cb4(a4, a5, a6, c->flags & 0x803f, disp) != 0) { LOG("OpenDevice: cb4 failed\n"); free(c); return -5; }
    c->w = 0x300; c->pitch = 0x300; c->h = 0x1e0;
    if (nMP) *nMP = 5;
    if (nSP) *nSP = 8;
    if (maxSize) { maxSize[0] = 0x300; maxSize[1] = 0x240; }
    if (flagsOut) *flagsOut = 0xf0;
    *out = c;
    LOG("OpenDevice ok ctx=%lx\n", (u32)c);
    return 0;
}
int DVDDriverCloseDevice(Ctx *c) { LOG("CloseDevice %lx\n", (u32)c);
    if (c && c->rc_ok) LOG("stats: decodes=%lu mbs=%lu types[0..7]=%lu %lu %lu %lu %lu %lu %lu %lu fieldDCT=%lu unknown=%lu\n", c->ndec, c->rc.stat_mb,
        c->rc.stat_type[0], c->rc.stat_type[1], c->rc.stat_type[2], c->rc.stat_type[3], c->rc.stat_type[4], c->rc.stat_type[5], c->rc.stat_type[6], c->rc.stat_type[7], c->rc.stat_fielddct, c->rc.stat_unknown_type); if (!c) return (int)0xe00002c2;
    if (c->both && c->cmp_n) LOG("BOTH summary: %lu decodes compared HW vs CPU: %lu identical, %lu with max diff <= 1, %lu with max diff <= 3, %lu with samples off by more than 8, worst max diff %lu\n", c->cmp_n, c->cmp_exact, c->cmp_le1, c->cmp_le3, c->cmp_big, c->cmp_worst);
    if (c->rc_ok && c->rc.hw) { unsigned long s[12]; rch_stats(&c->rc, s); LOG("rage MC: batches=%lu cmds=%lu residual-excess samples=%lu blocks with vectors outside picture (host edge clamp)=%lu uploads=%lu unsupported MBs=%lu fixed-up blocks=%lu\n", s[0], s[1], s[2], s[3], s[4], s[5], s[6]);
        LOG("rage MC time (ms): host IDCT+staging %lu, upload %lu, engine batches %lu, fix-ups %lu, readback for dumps %lu\n", s[7] / 1000, s[8] / 1000, s[9] / 1000, s[10] / 1000, s[11] / 1000); rch_free(&c->rc); }
    free(c); return 0; }

int DVDDriverGetKeyColor(Ctx *c, u32 *key)
{
    int bpp = c ? CGDisplayBitsPerPixel(c->disp) : 32;
    LOG("GetKeyColor bpp=%d\n", bpp);
    if (key) *key = bpp == 16 ? 0x40 : bpp == 32 ? 0x1000 : 0xdeadbeef;
    return 0;
}
int DVDDriverSetMPRects(Ctx *c, short *r)
{
    LOG("SetMPRects %lx rect=%d,%d,%d,%d\n", (u32)c, r ? r[0] : 0, r ? r[1] : 0, r ? r[2] : 0, r ? r[3] : 0);
    if (c && r) { c->w = r[3] - r[1]; c->pitch = (c->w + 63) & ~63UL; c->h = r[2] - r[0]; }
    return 0;
}
static void dump_frame(Ctx *c, int idx)
{
    const char *path = getenv("DVDSTUB_YUV"), *mx = getenv("DVDSTUB_FRAMES");
    int fd, y, w, h;
    if (!path || !c->rc_ok || idx < 0 || idx >= RC_NBUF) return;
    if (c->ndump >= (unsigned long)(mx ? atoi(mx) : 0)) return;
    fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    w = c->rc.w; h = c->rc.h;
    if (c->rc.hw) rch_sync(&c->rc, idx);
    for (y = 0; y < h; y++) write(fd, c->rc.pl[idx][0] + (long)y * c->rc.pitchY, w);
    for (y = 0; y < h / 2; y++) write(fd, c->rc.pl[idx][1] + (long)y * c->rc.pitchC, w / 2);
    for (y = 0; y < h / 2; y++) write(fd, c->rc.pl[idx][2] + (long)y * c->rc.pitchC, w / 2);
    close(fd);
    c->ndump++;
}
int DVDDriverShowMPBuffer(Ctx *c, int idx, unsigned short *mode)
{
    LOG("ShowMPBuffer idx=%d mode=%d\n", idx, mode ? *mode : 2);
    if (c) { c->nshow++; if (!getenv("DVDSTUB_DUMPDEC")) dump_frame(c, idx); }
    /* Apple calls cb3 only when the kernel buffer header flags say so; M0 has no kernel side. */
    return 0;
}

/* everything else: log and succeed (0) - unsupported multiprocessor entries return -5 like Apple's R128 driver */
int DVDDriverDecode(Ctx *c, unsigned char *p, short *rect)
{
    int r = 0;
    if (!c || !p) return -7;
    if (rect && (!c->rc_ok || c->rc.w != rect[3] - rect[1] || c->rc.h != rect[2] - rect[0])) {
        if (c->rc_ok) { rch_free(&c->rc); rc_free(&c->rc); }
        c->rc_ok = rc_init(&c->rc, rect[3] - rect[1], rect[2] - rect[0]) == 0;
        LOG("Decode: recon %dx%d init %s\n", rect[3] - rect[1], rect[2] - rect[0], c->rc_ok ? "ok" : "FAILED");
        if (c->rc_ok && !c->cpu_mode) {
            int hr = rch_init(&c->rc);
            LOG("Decode: Rage MC path %s (%d)\n", hr == 0 ? "ACTIVE" : "unavailable, CPU path", hr);
        }
    }
    if (c->ndec < 1000)
        LOG("Decode#%lu ptype=%d,%d st=%d,%d aux=%d,%d buf=%d,%d,%d\n", c->ndec, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]);
    if (c->rc_ok) {
        { const char *e = getenv("DVDSTUB_MBDBG"); c->rc.dbg_dec = e ? atol(e) : -1; c->rc.dbg_n = (int)c->ndec; }
        if (c->rc.hw && c->both) {
            Recon *rc = &c->rc; rc_u8 *bufs[3]; size_t ysz = (size_t)rc->pitchY * rc->hY, csz = (size_t)rc->pitchC * rc->hC;
            int bd = p[6] == 0xff ? 0 : p[6], rr, pl;
            r = rch_decode(rc, p);
            if (r >= 0 && rc_decode(rc, p) >= 0 && bd < RC_NBUF) {
                if (!c->cmpbuf) c->cmpbuf = (rc_u8 *)malloc(ysz + 2 * csz);
                if (c->cmpbuf) {
                    unsigned long cnt1 = 0, cnt3 = 0, cntbig = 0; int mx = 0;
                    bufs[0] = c->cmpbuf; bufs[1] = c->cmpbuf + ysz; bufs[2] = c->cmpbuf + ysz + csz;
                    rch_read(rc, bd, bufs);
                    for (pl = 0; pl < 3; pl++) {                      /* visible area only: the pitch padding is not defined */
                        int pw = pl ? rc->w >> 1 : rc->w, ph = pl ? rc->h >> 1 : rc->h, pitch = pl ? rc->pitchC : rc->pitchY, xx, yy;
                        for (yy = 0; yy < ph; yy++) for (xx = 0; xx < pw; xx++) {
                            int df = (int)bufs[pl][yy * pitch + xx] - (int)rc->pl[bd][pl][yy * pitch + xx]; if (df < 0) df = -df;
                            if (df) { cnt1++; if (df > 3) cnt3++; if (df > 8) cntbig++; if (df > mx) mx = df; }
                        }
                    }
                    (void)rr;
                    c->cmp_n++; if (!cnt1) c->cmp_exact++; if (mx <= 1) c->cmp_le1++; if (mx <= 3) c->cmp_le3++; if (cntbig) c->cmp_big++; if ((unsigned long)mx > c->cmp_worst) c->cmp_worst = mx;
                    if (mx > 3) LOG("  BOTH dec#%lu type %d,%d st %d,%d: HW vs CPU: %lu samples differ, %lu by >3, max %d\n", c->ndec, p[0], p[1], p[2], p[3], cnt1, cnt3, mx);
                }
            }
        } else
        r = c->rc.hw ? rch_decode(&c->rc, p) : rc_decode(&c->rc, p);
        if (getenv("DVDSTUB_DUMPDEC")) dump_frame(c, p[6]);   /* decode-order dump (one entry per Decode call: frame, or both fields) */
        if (c->ndec < 1000) LOG("  mbs=%lu unknown_type=%lu rc=%d\n", c->rc.stat_mb, c->rc.stat_unknown_type, r);
    }
    c->ndec++;
    return r < 0 ? -3 : 0;
}
STUB(EnableMP, 0)        STUB(ClearMP, 0)       STUB(BeginDecode, 0)
STUB(ContinueDecode, 0)  STUB(EndDecode, 0)     STUB(EnableSP, 0)       STUB(ClearSP, 0)
STUB(ShowSPBuffer, 0)    STUB(SetSPPalette, 0)  STUB(SetSPBuffer, 0)    STUB(GetSPBuffer, -5)
STUB(SetSPColorControl, 0) STUB(SetSPDefaultColorControl, 0)            STUB(ApplySPDCSQ, 0)
STUB(PrepareButton, 0)   STUB(EnableButton, 0)  STUB(SetMVLevel, 0)
STUB(GetFeatureParam, -7) STUB(SetFeatureParam, -5)
STUB(FrontEndDecode, -5) STUB(BackEndDecode, -5) STUB(FrontEndAndBackEndDecode, -5)
STUB(Deinterlace, -5)    STUB(CreateRenderer, -5)
