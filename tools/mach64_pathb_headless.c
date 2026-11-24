/*
 * mach64_pathb_headless.c - drive the REAL Path-B GL plugin (gldriver/*.c, statically linked with the bundle's own
 * flags) with no display and no GLEngine: fake pixel-format/drawable, the plugin opens Apple's patched ATIRagePro GL
 * client, creates + shapes + binds a surface, peeks the render target, and renders through its normal render slot.
 * Verification = read the render target straight out of VRAM through Mach64Accelerator's BAR0 mapping (both kexts
 * coexist).  Needs the card initialised (tools/biosemu/run-live.sh) and Mach64Accelerator loaded.
 *
 *   mach64_pathb_headless [attach|render]     (default attach: no geometry, safe)
 * env: PB_W/PB_H (surface size, default 256x256).  Plugin trace: /tmp/mach64gld.log.
 */
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../gldriver/gld.h"
#include "../common/mach64_abi.h"

static unsigned char *FB;                       /* Mach64Accelerator BAR0 mapping (8MB VRAM) */
static unsigned vrd(unsigned o) { return OSReadLittleInt32((void *)FB, o); }

/* GLEngine gli-vertex: float X/Y (y-up window coords) at +0x00/+0x04, 1/w at +0x0c, clip z at +0x18,
 * float R/G/B/A at +0x30/34/38/3c (see gldriver/gld_prims.c). */
static unsigned *snap;
static const struct { unsigned r; const char *n; } rl[] = { {0x738,"GUI_STAT"},{0x5b4,"SRC_CNTL"},{0x518,"DST_HEIGHT_WIDTH"},{0x50c,"DST_Y_X"},{0x0d0,"GEN_TEST_CNTL"},{0x254,"V1_ARGB"},{0x250,"V1_Z"},{0x24c,"V1_SPEC"},{0x248,"V1_W"},{0x710,"FIFO_STAT"},{0x500,"DST_OFF_PITCH"},{0x5fc,"SCALE_3D_CNTL"},{0x54c,"Z_CNTL"},{0x548,"Z_OFF_PITCH"},{0x304,"SETUP_CNTL"},
                {0x530,"DST_CNTL"},{0x6d0,"DP_PIX_WIDTH"},{0x6a8,"SC_LEFT_RIGHT"},{0x6b4,"SC_TOP_BOTTOM"},{0x6d4,"DP_MIX"},{0x6d8,"DP_SRC"},{0x6c8,"DP_WRITE_MASK"},
                {0x708,"CLR_CMP_CNTL"},{0x730,"GUI_TRAJ_CNTL"},{0x550,"ALPHA_TST_CNTL"},{0x300,"ONE_OVER_AREA"},{0x258,"VERTEX_1_X_Y"},{0x278,"VERTEX_2_X_Y"},{0x298,"VERTEX_3_X_Y"},
                {0x25c,"V1_ARGB?"},{0x2dc,"V2_ARGB?"},{0x2fc,"V3_ARGB?"},{0x4a0,"BUS_CNTL"},{0x47c,"HW_DEBUG"}};
static void put_vertex(unsigned char *v, int H, int x, int y, unsigned argb)
{
    float f[4], xf = (float)x, yf = (float)(H - y), w = 1.0f, z = 0.0f;
    int i;
    memset(v, 0, MACH64_GLIVERTEX_STRIDE);
    for (i = 0; i < 4; i++) f[i] = (float)((argb >> (i == 3 ? 24 : 16 - 8 * i)) & 0xff) / 255.0f;
    memcpy(v + 0x00, &xf, 4); memcpy(v + 0x04, &yf, 4); memcpy(v + 0x0c, &w, 4); memcpy(v + 0x18, &z, 4);
    memcpy(v + 0x30, &f[0], 4); memcpy(v + 0x34, &f[1], 4); memcpy(v + 0x38, &f[2], 4); memcpy(v + 0x3c, &f[3], 4);
}

int main(int argc, char **argv)
{
    int do_render = (argc > 1 && !strcmp(argv[1], "render"));
    int W = getenv("PB_W") ? atoi(getenv("PB_W")) : 256, H = getenv("PB_H") ? atoi(getenv("PB_H")) : 256;
    int attribs[] = { 8, 32, 12, 16, 5, 73, 0 };
    void *pf = 0; gld_context *ctx = 0; int rc, x, y;
    unsigned char *drawable, table[0x80], *gli; mach64_priv *p;
    io_service_t svc; io_connect_t conn; vm_address_t addr = 0; vm_size_t size = 0;
    char fs_w[16], fs_h[16];

    setbuf(stdout, NULL);
    snprintf(fs_w, sizeof fs_w, "%d", W); snprintf(fs_h, sizeof fs_h, "%d", H);
    setenv("MACH64_FS_SHRINK", "1", 1); setenv("MACH64_FS_W", fs_w, 1); setenv("MACH64_FS_H", fs_h, 1);

    /* our kext's BAR0 mapping: ground-truth VRAM reader */
    svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("Mach64Accelerator"));
    if (!svc || IOServiceOpen(svc, mach_task_self(), MACH64_GL_CONNECT_TYPE, &conn)) { fprintf(stderr, "Mach64Accelerator not loaded\n"); return 1; }
    if (IOConnectMapMemory(conn, MACH64_MEM_SURFACE, mach_task_self(), &addr, &size, kIOMapAnywhere | kIOMapInhibitCache)) { fprintf(stderr, "map VRAM\n"); return 1; }
    FB = (unsigned char *)addr;
    printf("VRAM mapped at %p (0x%x bytes)\n", (void *)FB, (unsigned)size);
    if (getenv("PB_EARLY_EXTREG")) { unsigned bc = vrd(0x7ff800 + 0x4a0); OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x4a0, bc | (1u << 27)); printf("BUS_CNTL 0x%08x -> 0x%08x\n", bc, vrd(0x7ff800 + 0x4a0)); }
    if (argc > 1 && !strcmp(argv[1], "regs")) {   /* read-only register dump, no GL */
        unsigned i; for (i = 0; i < sizeof rl / sizeof rl[0]; i++) printf("  REG %-14s 0x%03x = 0x%08x\n", rl[i].n, rl[i].r, vrd(0x7ff800 + rl[i].r));
        return 0;
    }

    gldInitializeLibrary(NULL, 0, 0, 0, 0);
    rc = gldChoosePixelFormat(&pf, attribs);
    printf("gldChoosePixelFormat -> %d pf=%p\n", rc, pf);
    if (rc || !pf) return 1;
    rc = gldCreateContext(&ctx, pf, 0, 0, calloc(1, 64), calloc(1, 0x10000), 0);
    printf("gldCreateContext -> 0x%x ctx=%p connect=0x%x\n", rc, (void *)ctx, ctx ? ctx->connect : 0);
    if (rc || !ctx) return 1;
    p = MACH64_PRIV(ctx);

    drawable = calloc(1, 0x80);
    ((unsigned *)drawable)[0] = 0x4242;          /* CGS-style surface id (unique) */
    rc = gldAttachDrawable(ctx, 0x36, drawable, 0);
    printf("gldAttachDrawable -> 0x%x\n", rc);
    printf("state: target_off=0x%x target_pitch(px)=? fb %ux%u dbuf=%u render_skip=%u present_unsafe=%u apple_surf_cn=0x%x regs=%p fb=%p\n",
           p->target_off, p->fb_w, p->fb_h, p->dbuf, p->render_skip, p->present_unsafe, (unsigned)p->apple_surf_cn, ctx->regs, ctx->fb);
    if (!do_render) { printf("\nATTACH OK (safe mode). Re-run with 'render' to draw.\n"); gldDestroyContext(ctx); gldDestroyPixelFormat(pf); return 0; }
    if (p->render_skip) { printf("no valid render target (render_skip=%u target_off=0x%x) -- not rendering\n", p->render_skip, p->target_off); return 2; }

    /* stand-in for GLEngine's context ("gli"): the plugin locates it from ctx+0x10 by scanning back for the object whose
     * +0x7898 back-points to ctx (gld_render.c mach64_gli), then reads GL state from it.  Colour mask = 4 bytes at +0x63b4. */
    gli = calloc(1, 0x100000);
    memset(gli + 0x63b4, 1, 4);
    *(uint32_t *)(gli + 0x6708) = 1;   /* glShadeModel(GL_SMOOTH) */
    *(uint32_t *)(gli + 0x7898) = (uint32_t)(uintptr_t)ctx;
    *(uint32_t *)((unsigned char *)ctx + 0x10) = (uint32_t)(uintptr_t)gli + 0x3610;
    /* clear the target through our BAR0 view, then draw one gouraud triangle via the plugin's own render slot */
    {   unsigned off = p->target_off, pitch = p->fb_pitch ? p->fb_pitch : (unsigned)W;
        unsigned char *va = calloc(3, MACH64_GLIVERTEX_STRIDE); gld_render_fn render_tris;
        for (y = 0; y < H; y++) for (x = 0; x < W; x++) OSWriteLittleInt32((void *)FB, off + y * pitch * 4 + x * 4, 0);
        { unsigned o; snap = malloc(0x800000); for (o = 0; o < 0x800000; o += 4) snap[o >> 2] = vrd(o); }
        memset(table, 0, sizeof table); { static uint32_t info[16]; gldUpdateDispatch(ctx, table, (uint32_t)(uintptr_t)info); }
        render_tris = *(gld_render_fn *)(table + 0x2c);
        printf("render slot = %p, target off=0x%x pitch=%upx\n", (void *)render_tris, off, pitch);
        put_vertex(va + 0 * MACH64_GLIVERTEX_STRIDE, H, 20, 16, 0xFFFF0000u);
        put_vertex(va + 1 * MACH64_GLIVERTEX_STRIDE, H, 110, 44, 0xFF00FF00u);
        put_vertex(va + 2 * MACH64_GLIVERTEX_STRIDE, H, 50, 116, 0xFF0000FFu);
        if (!render_tris) { printf("no render slot\n"); return 2; }
        render_tris(ctx, va, 3);
        gldFinish(ctx);
        {   /* where did pixels land?  diff all 8 MB against the pre-draw snapshot */
            unsigned o, blk = 0xffffffffu, cnt = 0, first = 0;
            for (o = 0; o < 0x800000; o += 4) {
                if (vrd(o) != snap[o >> 2]) { if ((o >> 12) != blk) { if (cnt) printf("  changed 4K block 0x%06x: %u dwords, first at 0x%06x\n", blk << 12, cnt, first); blk = o >> 12; cnt = 0; first = o; } cnt++; }
            }
            if (cnt) printf("  changed 4K block 0x%06x: %u dwords, first at 0x%06x\n", blk << 12, cnt, first);
        }
        {   
            unsigned i; for (i = 0; i < sizeof rl / sizeof rl[0]; i++) printf("  REG %-14s 0x%03x = 0x%08x\n", rl[i].n, rl[i].r, vrd(0x7ff800 + rl[i].r)); }
        {   unsigned a = vrd(0x7ff800 + 0x6a8), b = vrd(0x7ff800 + 0x6b4);
            OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x6a8, 0x1fff0000); OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x6b4, 0x3fff0000);
            printf("  SC regs after draw: LR=0x%08x TB=0x%08x ; direct-write readback LR=0x%08x TB=0x%08x\n", a, b, vrd(0x7ff800 + 0x6a8), vrd(0x7ff800 + 0x6b4)); }
        if (getenv("PB_PIO")) {   /* experiment: Path A's exact state + vertex recipe, but written by CPU (PIO) instead of DMA */
            static const int TX[3] = {20, 110, 50}, TY[3] = {16, 44, 116};
            static const unsigned VC[3] = {0xffff0000u, 0xff00ff00u, 0xff0000ffu};
            unsigned t = 0x600000, i, q, n; int xx[3], yy[3]; double area; float ooa; unsigned oob;
#define PW(r, v) do { n = 0; while ((vrd(0x7ff800 + 0x710) & 0xffff) < 0x8000 && n++ < 1000000) ; OSWriteLittleInt32((void *)FB, 0x7ff800 + (r), (v)); } while (0)
#define PIDLE() do { n = 0; while ((vrd(0x7ff800 + 0x738) & 1) && n++ < 1000000) ; } while (0)
            for (q = 0; q < 256 * 256; q++) OSWriteLittleInt32((void *)FB, t + q * 4, 0);
            for (i = 0; i < 3; i++) { xx[i] = TX[i] * 4; yy[i] = TY[i] * 4; }
            area = (1.0 / 16.0) * ((double)(xx[0] - xx[2]) * (yy[1] - yy[2]) - (double)(yy[0] - yy[2]) * (xx[1] - xx[2]));
            ooa = (float)(1.0 / area); memcpy(&oob, &ooa, 4);
            PIDLE();
            if (getenv("PB_EXTREG")) { unsigned bc = vrd(0x7ff800 + 0x4a0); OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x4a0, bc | (1u << 27)); printf("  BUS_CNTL 0x%08x -> 0x%08x\n", bc, vrd(0x7ff800 + 0x4a0)); }
            OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x5fc, 0x100c1); OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x304, 0); OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x54c, 0);
            OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x6d0, 0x60060666); OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x500, (32u << 22) | (t >> 3));
            OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x6a8, 0x1fff0000); OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x6b4, 0x3fff0000);
            OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x6c8, 0xffffffffu); OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x708, 0);
            OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x6d4, 0x00070007); OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x6d8, 0x505);
            PIDLE();
            for (i = 0; i < 3; i++) {
                unsigned b = 0x7ff800 + 0x240 + i * 0x20; PIDLE();
                OSWriteLittleInt32((void *)FB, b + 0x00, 0); OSWriteLittleInt32((void *)FB, b + 0x04, 0); OSWriteLittleInt32((void *)FB, b + 0x08, 0x3f800000);
                OSWriteLittleInt32((void *)FB, b + 0x0c, 0); OSWriteLittleInt32((void *)FB, b + 0x10, 0); OSWriteLittleInt32((void *)FB, b + 0x14, VC[i]);
                OSWriteLittleInt32((void *)FB, b + 0x18, ((unsigned)xx[i] << 16) | ((unsigned)yy[i] & 0xffff));
            }
            OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x300, oob);
            PIDLE(); OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x5b4, 0); PIDLE();
            { unsigned c = 0; for (q = 0; q < 256 * 256; q++) if (vrd(t + q * 4)) c++; printf("  PIO recipe: %u nonzero pixels at 0x%x\n", c, t); }
        }
        if (getenv("PB_REFIRE")) {   /* experiment: re-fire the setup trigger by CPU write through our own register map */
            unsigned n = 0, oa = vrd(0x7ff800 + 0x300);
            while ((vrd(0x7ff800 + 0x738) & 1) && n++ < 1000000) ;
            printf("  REFIRE: ONE_OVER_AREA=0x%08x GUI_STAT=0x%08x FIFO_STAT=0x%08x\n", oa, vrd(0x7ff800 + 0x738), vrd(0x7ff800 + 0x710));
            if (getenv("PB_REDIR")) { unsigned t = strtoul(getenv("PB_REDIR"), 0, 0); OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x500, (32u << 22) | (t >> 3));
                { unsigned q; for (q = 0; q < 256 * 256; q++) OSWriteLittleInt32((void *)FB, t + q * 4, 0); } printf("  REFIRE: redirected DST to 0x%x\n", t); }
            OSWriteLittleInt32((void *)FB, 0x7ff800 + 0x300, oa);
            n = 0; while ((vrd(0x7ff800 + 0x738) & 1) && n++ < 1000000) ;
            { unsigned o, c = 0, f = 0; for (o = 0; o < 0x7ff000; o += 4) if (vrd(o) != snap[o >> 2] && !(o >= 0x600000 && getenv("PB_REDIR") == 0 && 0)) { if (!c++) f = o; }
              printf("  REFIRE: %u dwords differ from pre-draw snapshot (first at 0x%x)\n", c, f); }
        }
        if (getenv("PB_REFIRE") && getenv("PB_REDIR")) off = strtoul(getenv("PB_REDIR"), 0, 0);
        {   int nfill = 0, nR = 0, nG = 0, nB = 0;
            for (y = 0; y < 124 && y < H; y += 4) {
                printf("  ");
                for (x = 0; x < 124 && x < W; x += 2) {
                    unsigned v = vrd(off + y * pitch * 4 + x * 4), r = (v >> 16) & 0xff, g = (v >> 8) & 0xff, b = v & 0xff;
                    if (!v) { putchar('.'); continue; }
                    nfill++; if (r >= g && r >= b) { putchar('R'); nR++; } else if (g >= r && g >= b) { putchar('G'); nG++; } else { putchar('B'); nB++; }
                }
                putchar('\n');
            }
            printf("\ncovered=%d R=%d G=%d B=%d -> %s\n", nfill, nR, nG, nB, (nfill > 50 && (nR > 0) + (nG > 0) + (nB > 0) >= 2) ? "PATH-B GL RENDER CONFIRMED" : "no/odd render");
        }
    }
    gldDestroyContext(ctx); gldDestroyPixelFormat(pf);
    return 0;
}
