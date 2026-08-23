/*
 * mach64_firstlight.c - FIRST LIGHT: load ATIRageProGLDriver.bundle the way
 * OpenGL.framework does (CFBundle + look up gld* by name) and drive the full
 * plugin lifecycle to render a triangle on the real hardware.
 *
 *   CFBundleCreate/LoadExecutable            (framework's bundle loader)
 *   gldInitializeLibrary                     (lib init)
 *   gldChoosePixelFormat(attribs) -> pf      (format negotiation)
 *   gldCreateContext(pf) -> ctx              (open kext, map regions)
 *   mach64_set_render_target(ctx, ...)       (bind off-screen target)
 *   gldUpdateDispatch(ctx, table) ; table[0x28](ctx, verts, 3)   (render)
 *   gldFinish ; read back ; gldDestroyContext ; gldDestroyPixelFormat
 *
 * Every entry point comes from the built bundle via CFBundleGetFunctionPointer
 * ForName -- nothing is statically linked from gldriver/.  Needs the kext loaded
 * (gldCreateContext opens Mach64Accelerator).  argv[1] = bundle path.
 * Build: -framework CoreFoundation -framework IOKit.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <libkern/OSByteOrder.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../gldriver/gld.h"

#define TARGET_OFF 0x600000u
#define PITCH_PIX  128
#define ROW_BYTES  (PITCH_PIX*4)

static void put_vertex(unsigned char *v, int x, int y, unsigned argb)
{
    int xy = ((x*4)<<16) | ((y*4)&0xffff);
    float w = 1.0f;
    memset(v, 0, MACH64_GLIVERTEX_STRIDE);
    *(int      *)(v + 0x30) = xy;
    *(unsigned *)(v + 0x34) = argb;
    *(int      *)(v + 0x38) = 0;
    memcpy(v + 0x08, &w, 4);
}

static void *sym(CFBundleRef b, const char *n)
{
    CFStringRef s = CFStringCreateWithCString(NULL, n, kCFStringEncodingASCII);
    void *p = CFBundleGetFunctionPointerForName(b, s);
    CFRelease(s);
    if (!p) { fprintf(stderr, "missing symbol: %s\n", n); exit(1); }
    return p;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "gldriver/ATIRageProGLDriver.bundle";
    CFURLRef url; CFBundleRef b;
    int (*g_init)(void *);
    int (*g_choose)(void **, const int *);
    int (*g_create)(gld_context **, void *, void *, void *, void *, void *, void *);
    void (*g_update)(gld_context *, void *, uint32_t);
    int (*g_finish)(gld_context *);
    int (*g_destroy)(gld_context *);
    int (*g_destroypf)(void *);
    void (*g_srt)(gld_context *, uint32_t, uint32_t);
    void *pf = 0; gld_context *ctx = 0; int rc, x, y;
    unsigned char table[0x80]; gld_render_fn render_tris;
    unsigned char *va; void *SURF; int attribs[] = { 8,32, 12,16, 5, 73, 0 };

    setbuf(stdout, NULL);   /* unbuffered: over SSH we must see WHERE it stops */

    url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, strlen(path), true);
    b = CFBundleCreate(NULL, url);
    if (!b) { fprintf(stderr, "CFBundleCreate failed: %s\n", path); return 1; }
    if (!CFBundleLoadExecutable(b)) { fprintf(stderr, "CFBundleLoadExecutable failed\n"); return 1; }
    printf("CFBundle loaded: %s\n", path);

    g_init      = (int(*)(void*))sym(b, "gldInitializeLibrary");
    g_choose    = (int(*)(void**,const int*))sym(b, "gldChoosePixelFormat");
    g_create    = (int(*)(gld_context**,void*,void*,void*,void*,void*,void*))sym(b, "gldCreateContext");
    g_update    = (void(*)(gld_context*,void*,uint32_t))sym(b, "gldUpdateDispatch");
    g_finish    = (int(*)(gld_context*))sym(b, "gldFinish");
    g_destroy   = (int(*)(gld_context*))sym(b, "gldDestroyContext");
    g_destroypf = (int(*)(void*))sym(b, "gldDestroyPixelFormat");
    g_srt       = (void(*)(gld_context*,uint32_t,uint32_t))sym(b, "mach64_set_render_target");
    printf("resolved gld* entry points by name via CFBundle (the framework's path)\n\n");

    g_init(NULL);
    rc = g_choose(&pf, attribs);
    printf("gldChoosePixelFormat -> %d, pf=%p\n", rc, pf);
    if (rc || !pf) return 1;

    rc = g_create(&ctx, pf, 0, 0, 0, 0, 0);
    printf("gldCreateContext -> %d, ctx=%p connect=0x%x\n", rc, (void*)ctx, ctx?ctx->connect:0);
    if (rc || !ctx) { fprintf(stderr, "create failed (is the kext loaded?)\n"); return 1; }

    g_srt(ctx, TARGET_OFF, PITCH_PIX);        /* no MMIO: sets shadow state only */
    SURF = ctx->regs;                         /* MACH64_MEM_SURFACE mapping */

    memset(table, 0, sizeof table);
    g_update(ctx, table, 0);
    render_tris = *(gld_render_fn *)(table + 0x28);
    printf("gldUpdateDispatch -> table[0x28]=%p\n", (void*)render_tris);

    /* Everything above touches NO engine registers (kext owns the engine).  The
     * render below activates bus-master DMA on the shared engine, so it is gated
     * behind an explicit 'render' arg -- run the safe lifecycle check first. */
    if (!(argc > 2 && strcmp(argv[2], "render") == 0)) {
        printf("\nLIFECYCLE OK (safe mode, no DMA). Re-run with 'render' to draw.\n");
        g_destroy(ctx); g_destroypf(pf);
        printf("gldDestroyContext + gldDestroyPixelFormat OK\n");
        return 0;
    }

    for (y=0;y<128;y++) for (x=0;x<128;x++)
        OSWriteLittleInt32(SURF, TARGET_OFF + y*ROW_BYTES + x*4, 0);
    va = calloc(3, MACH64_GLIVERTEX_STRIDE);
    put_vertex(va + 0*MACH64_GLIVERTEX_STRIDE,  20,  16, 0xFFFF0000u);
    put_vertex(va + 1*MACH64_GLIVERTEX_STRIDE, 110,  44, 0xFF00FF00u);
    put_vertex(va + 2*MACH64_GLIVERTEX_STRIDE,  50, 116, 0xFF0000FFu);

    printf("\ncalling table[0x28](ctx, verts, 3) -> render -> kext ring\n");
    render_tris(ctx, va, 3);
    g_finish(ctx);

    {
        int nfill=0,nR=0,nG=0,nB=0,classes;
        printf("off-screen readback (R/G/B = dominant channel):\n");
        for (y=0;y<124;y+=4){
            printf("  ");
            for (x=0;x<124;x+=2){
                unsigned v = OSReadLittleInt32(SURF, TARGET_OFF + y*ROW_BYTES + x*4);
                if (!v){ putchar('.'); continue; }
                unsigned r=(v>>16)&0xff, g=(v>>8)&0xff, bl=v&0xff; nfill++;
                if (r>=g && r>=bl){ putchar('R'); nR++; }
                else if (g>=r && g>=bl){ putchar('G'); nG++; }
                else { putchar('B'); nB++; }
            }
            putchar('\n');
        }
        classes=(nR>0)+(nG>0)+(nB>0);
        printf("\ncovered=%d R=%d G=%d B=%d\n", nfill,nR,nG,nB);
        printf("\n%s\n", (nfill>50 && classes>=2)
            ? "*** FIRST LIGHT *** CFBundle-loaded driver rendered gouraud via the full gld lifecycle!"
            : (nfill>50 ? "bundle+lifecycle ran, but single color" : "bundle+lifecycle ran, no triangle"));
    }

    g_destroy(ctx);
    g_destroypf(pf);
    printf("gldDestroyContext + gldDestroyPixelFormat OK\n");
    return 0;
}
