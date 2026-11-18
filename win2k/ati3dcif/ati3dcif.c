/*
 * ati3dcif.c - our ATI3DCIF.DLL for Windows 2000 on the ATI Rage Mobility-P.
 *
 * ATI never shipped an ati3dcif.dll for NT/2000; the SDK demos statically link a
 * loader shim (aticif2.obj) that LoadLibraryA("ati3dcif.dll") + GetProcAddress's the
 * CIF entry points at runtime, falling back to __cif_not_present__ (-> "Could not
 * initialize 3D driver") when the DLL is absent.  This DLL fills that slot, driving
 * the mach64 3D engine through our proven backend:
 *      CIF call -> m64_emit register/vertex stream -> m64ws_submit (bus-master DMA
 *      via win2k/kmd/mach64kmd.sys) -> mach64 setup engine.
 *
 * INCREMENT 1 (this file): bind to the shim + TRACE every CIF call (state ids,
 * vertex counts, texture regs).  Init/GetInfo/ContextCreate/SetState are real
 * bookkeeping; the Render* bodies log and return OK (no geometry yet).  Running a
 * demo now proves (a) the shim loads us (no init error) and (b) captures the exact
 * call sequence R3TEXCMP/specular/etc. make -- the spec for Increment 2's real
 * rendering.  CRT-free (kernel32/user32 only) so it always loads on Win2000.
 *
 * See win2k/ati3dcif/README.md for the bring-up ladder.
 */
#include <windows.h>
#include <stdarg.h>
#include "ATI3DCIF.h"
#include "mach64_winsys.h"          /* ../driver: m64ws_open/submit/reg/vram */
#include "mach64_emit.h"            /* ../../common: m64_cmdbuf, m64_reg      */
#include "mach64_regs.h"            /* ../../common: MACH64_* register offsets */

/* CRT-free libc bits used by mach64_emit.c / mach64_winsys.c and our present copy
 * (we link -nodefaultlibs, so provide them here -- same as mach64_winsys_test.c). */
void *memcpy(void *d, const void *s, size_t n)
{ unsigned char *a=d; const unsigned char *b=s; while(n--) *a++=*b++; return d; }
void *memset(void *d, int c, size_t n)
{ unsigned char *a=d; while(n--) *a++=(unsigned char)c; return d; }

/* ---- CRT-free trace log (wvsprintfA is in USER32; no %f -> log float bits as hex) */
static HANDLE g_log = INVALID_HANDLE_VALUE;
static void LOG(const char *fmt, ...)
{
    char buf[512];
    int  n;
    DWORD wr;
    va_list ap;
    if (g_log == INVALID_HANDLE_VALUE) {
        g_log = CreateFileA("C:\\ati3dcif.log", GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
                            NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_log == INVALID_HANDLE_VALUE) return;
    }
    va_start(ap, fmt);
    n = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    if (n > 0) WriteFile(g_log, buf, (DWORD)n, &wr, NULL);
}

/* ---- CRT-free allocation ------------------------------------------------- */
static void *xalloc(unsigned n)
{ void *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); return p; }
static void  xfree(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

/* ---- module-global device ------------------------------------------------ */
static m64_winsys *g_ws = NULL;     /* kmd/hardware device (NULL => sw-absent) */
static int         g_hw = 0;        /* 1 once the mach64 kmd is mapped         */

/* ---- rendering context --------------------------------------------------- */
#define CIF_CTX_MAGIC 0x43494658u   /* 'CIFX' */
typedef struct {
    unsigned    magic;
    C3D_EVERTEX vtype;              /* C3D_ERS_VERTEX_TYPE (default VTCF)       */
    C3D_EPRIM   prim;               /* C3D_ERS_PRIM_TYPE   (default TRI)        */
    C3D_ESHADE  shade;              /* C3D_ERS_SHADE_MODE  (default SMOOTH)     */
    int         tmap_en;            /* C3D_ERS_TMAP_EN                          */
    C3D_HTX     tmap;               /* C3D_ERS_TMAP_SELECT                      */
    int         tmap_light;         /* C3D_ERS_TMAP_LIGHT (NONE/MODULATE/DECAL) */
    int         spec_en;            /* C3D_ERS_SPECULAR_EN (add per-vertex specular) */
    int         z_mode, z_cmp;      /* C3D_ERS_Z_MODE / Z_CMP_FNC              */
    int         a_src, a_dst;       /* C3D_ERS_ALPHA_SRC / ALPHA_DST          */
    /* draw surface (from SURF_DRAW_* states) */
    void       *surf_ptr;           /* host pointer to the DirectDraw draw surface */
    unsigned    surf_pitch;
    C3D_EPIXFMT surf_pf;
    C3D_RECT    vport;
    void       *z_ptr;
    unsigned    z_pitch;
} cif_ctx;

static cif_ctx *ctx_check(C3D_HRC h)
{ cif_ctx *c = (cif_ctx *)h; return (c && c->magic == CIF_CTX_MAGIC) ? c : NULL; }

/* ========================================================================= *
 *  Module init / info
 * ========================================================================= */
C3D_EC WINAPI ATI3DCIF_Init(void)
{
    LOG("ATI3DCIF_Init()\r\n");
    if (!g_ws) {
        g_ws = m64ws_open();                 /* open \\.\Mach64Kmd, map FB+regs */
        g_hw = (g_ws != NULL);
    }
    LOG("  hw=%d ws=%p vram=%u\r\n", g_hw, g_ws, g_ws ? m64ws_vram_size(g_ws) : 0);
    /* Return OK even without the kmd so the shim-binding + call trace can be
     * validated independently of whether mach64kmd.sys is started.  Increment 2
     * will require g_hw for actual geometry. */
    return C3D_EC_OK;
}

C3D_EC WINAPI ATI3DCIF_Term(void)
{
    LOG("ATI3DCIF_Term()\r\n");
    if (g_ws) { m64ws_close(g_ws); g_ws = NULL; g_hw = 0; }
    return C3D_EC_OK;
}

C3D_EC WINAPI ATI3DCIF_GetInfo(PC3D_3DCIFINFO p)
{
    LOG("ATI3DCIF_GetInfo(p=%p size=%u)\r\n", p, p ? p->u32Size : 0);
    if (!p) return C3D_EC_BADPARAM;
    /* Rage Mobility-P (mach64 "LM", Rage Pro-class): 8MB VRAM, ASIC 0x4c4d.
     * Off-screen heap = the scratch region above the visible framebuffer (the
     * winsys/engine-proof used 0x400000 as safe off-screen).  Caps advertise the
     * fixed-function feature set the demos probe. */
    p->u32TotalRAM      = g_ws ? m64ws_vram_size(g_ws) : 0x800000u;
    p->u32FrameBuffBase = 0;                 /* host FB pointer: filled in Inc.2 */
    p->u32OffScreenHeap = 0x400000u;
    p->u32OffScreenSize = 0x400000u;
    p->u32ASICID        = 0x00004c4du;       /* 'LM' */
    p->u32ASICRevision  = 0;
    p->u32CIFCaps1 = C3D_CAPS1_FOG | C3D_CAPS1_POINT | C3D_CAPS1_RECT |
                     C3D_CAPS1_Z_BUFFER | C3D_CAPS1_CI4_TMAP | C3D_CAPS1_CI8_TMAP |
                     C3D_CAPS1_LOAD_OBJECT | C3D_CAPS1_DITHER_EN | C3D_CAPS1_SCISSOR;
    p->u32CIFCaps2 = C3D_CAPS2_TEXTURE_COMPOSITE | C3D_CAPS2_TEXTURE_CLAMP |
                     C3D_CAPS2_DESTINATION_ALPHA_BLEND;
    p->u32CIFCaps3 = 0;
    p->u32CIFCaps4 = 0;
    p->u32CIFCaps5 = 0;
    return C3D_EC_OK;
}

/* ========================================================================= *
 *  Context
 * ========================================================================= */
C3D_HRC WINAPI ATI3DCIF_ContextCreate(void)
{
    cif_ctx *c = (cif_ctx *)xalloc(sizeof(cif_ctx));
    if (c) {
        c->magic = CIF_CTX_MAGIC;
        c->vtype = C3D_EV_VTCF;              /* header-documented defaults */
        c->prim  = C3D_EPRIM_TRI;
        c->shade = C3D_ESH_SMOOTH;
        c->z_cmp = C3D_EZCMP_ALWAYS;
        c->z_mode= C3D_EZMODE_OFF;
        c->a_src = C3D_EASRC_ONE;
        c->a_dst = C3D_EADST_ZERO;
    }
    LOG("ATI3DCIF_ContextCreate() -> %p\r\n", c);
    return (C3D_HRC)c;
}

C3D_EC WINAPI ATI3DCIF_ContextDestroy(C3D_HRC hRC)
{
    cif_ctx *c = ctx_check(hRC);
    LOG("ATI3DCIF_ContextDestroy(%p)\r\n", hRC);
    if (!c) return C3D_EC_BADPARAM;
    c->magic = 0;
    xfree(c);
    return C3D_EC_OK;
}

C3D_EC WINAPI ATI3DCIF_ContextSetState(C3D_HRC hRC, C3D_ERSID eRStateID, C3D_PRSDATA pData)
{
    cif_ctx *c = ctx_check(hRC);
    unsigned v = pData ? *(unsigned *)pData : 0;   /* most states are a 32-bit value/enum */
    if (!c) { LOG("ATI3DCIF_ContextSetState(bad hRC=%p)\r\n", hRC); return C3D_EC_BADPARAM; }
    LOG("ATI3DCIF_ContextSetState(id=%u data=%p val=0x%x)\r\n", eRStateID, pData, v);
    switch (eRStateID) {
    case C3D_ERS_VERTEX_TYPE:     c->vtype = (C3D_EVERTEX)v; break;
    case C3D_ERS_PRIM_TYPE:       c->prim  = (C3D_EPRIM)v;   break;
    case C3D_ERS_SHADE_MODE:      c->shade = (C3D_ESHADE)v;  break;
    case C3D_ERS_TMAP_EN:         c->tmap_en = (int)v;       break;
    case C3D_ERS_TMAP_SELECT:     c->tmap = (C3D_HTX)(pData ? *(void **)pData : 0); break;
    case C3D_ERS_TMAP_LIGHT:      c->tmap_light = (int)v; break;   /* NONE=REPLACE, 1=MODULATE */
    case C3D_ERS_SPECULAR_EN:     c->spec_en = (int)v; break;
    case C3D_ERS_Z_MODE:          c->z_mode = (int)v;        break;
    case C3D_ERS_Z_CMP_FNC:       c->z_cmp  = (int)v;        break;
    case C3D_ERS_ALPHA_SRC:       c->a_src  = (int)v;        break;
    case C3D_ERS_ALPHA_DST:       c->a_dst  = (int)v;        break;
    case C3D_ERS_SURF_DRAW_PTR:   c->surf_ptr   = pData ? *(void **)pData : 0;
                                  LOG("    SURF_DRAW_PTR=%p\r\n", c->surf_ptr); break;
    case C3D_ERS_SURF_DRAW_PITCH: c->surf_pitch = v; break;
    case C3D_ERS_SURF_DRAW_PF:    c->surf_pf    = (C3D_EPIXFMT)v; break;
    case C3D_ERS_SURF_VPORT:      if (pData) c->vport = *(C3D_RECT *)pData; break;
    case C3D_ERS_SURF_Z_PTR:      c->z_ptr   = pData ? *(void **)pData : 0; break;
    case C3D_ERS_SURF_Z_PITCH:    c->z_pitch = v; break;
    default: break;                          /* trace-only for the rest (Inc.2) */
    }
    return C3D_EC_OK;
}

/* ========================================================================= *
 *  Rendering  (Increment 1: trace + OK; geometry lands in Increment 2)
 * ========================================================================= */
static cif_ctx *g_cur = NULL;               /* context bound by RenderBegin    */

/* ---- STAGE 1: 16bpp render target + present bridge -----------------------
 * Render into a 32bpp ARGB8888 VRAM back buffer (our fully-proven state,
 * dp_pix_width 0x60060666), then convert 8888 -> the demo's 16bpp surface on the
 * CPU present copy (same model as win2k mach64_gl.c's 8888->565).  Stage 1 proves
 * the whole path with a hardware 2D solid fill (mach64_fill_rect sequence, proven
 * on the Armada in the engine-proof); Stage 2 replaces the fill with real triangles.
 */
#define CIF_BACK_OFF 0x400000u              /* safe off-screen VRAM scratch (win2k) */
#define CIF_MAXW     1024u
#define CIF_PRES_H   480u                   /* full surface height (demo is 640x480) */
static unsigned char cif_rb[CIF_MAXW*CIF_PRES_H*4];  /* readback staging (~1.9MB)   */
static unsigned g_bw = 0;                   /* back-buffer/surface width (px)      */
static unsigned g_bh = 0;                   /* present height (rows)               */
static int      g_cleared = 0;              /* back buffer cleared this frame?     */
static unsigned g_zmin = 0xffffffffu, g_zmax = 0;  /* per-frame Z range trace       */
static float    g_wmin = 1e30f, g_wmax = -1e30f;   /* current-frame W (rhw) range    */
static float    g_wlo_prev = 0.0f, g_whi_prev = 0.0f; /* last frame's W range (w-buffer) */
static int      g_zdepth_on = 0;                   /* master depth-test enable (OFF: HW Z
                                                    * bring-up unfinished -- see memory;
                                                    * demo renders via painter's order) */
static int      g_zwrite    = 1;                   /* DIAG: Z-write enable (0=test-only) */
static int      g_diag_flatz = 0;                  /* DIAG: flat per-object Z by texoff  */
static unsigned g_czmin = 0xffffffffu, g_czmax = 0; /* per-Render-call z16 range (diag) */
static float    g_cymin = 1e30f, g_cymax = -1e30f;  /* per-Render-call screen-Y (diag)  */
static int      g_zdiag = 3;                        /* log per-prim depth for N frames  */

/* Hardware 2D solid-fill of a w*h rect at (0,0) in the 32bpp back buffer. */
static int cif_hw_fill(unsigned pitch_px, unsigned w, unsigned h, unsigned argb)
{
    static uint32_t store[64];
    m64_cmdbuf cb;
    if (!g_ws) return -1;
    m64_cb_init(&cb, store, 64);
    m64_reg(&cb, MACH64_Z_CNTL,           0);
    m64_reg(&cb, MACH64_SCALE_3D_CNTL,    0);
    m64_reg(&cb, MACH64_GUI_TRAJ_CNTL,    (1u<<0)|(1u<<1));       /* L->R, T->B      */
    m64_reg(&cb, MACH64_DP_PIX_WIDTH,     0x60060666u);           /* 32bpp everywhere */
    m64_reg(&cb, MACH64_DP_FRGD_CLR,      argb);
    m64_reg(&cb, MACH64_DP_WRITE_MASK,    0xffffffffu);
    m64_reg(&cb, MACH64_DP_MIX,           (3u<<0)|(7u<<16));      /* BKGD_MIX_D|FRGD_MIX_S */
    m64_reg(&cb, MACH64_DP_SRC,           (1u<<0)|(1u<<8));       /* both srcs=FRGD_CLR */
    m64_reg(&cb, MACH64_SC_LEFT_RIGHT,    (0x1fffu<<16));
    m64_reg(&cb, MACH64_SC_TOP_BOTTOM,    (0x1fffu<<16));
    m64_reg(&cb, MACH64_CLR_CMP_CNTL,     0);
    m64_reg(&cb, MACH64_DST_OFF_PITCH,    ((pitch_px/8)<<22)|(CIF_BACK_OFF>>3));
    m64_reg(&cb, MACH64_DST_X_Y,          0);
    m64_reg(&cb, MACH64_DST_WIDTH_HEIGHT, (h<<16)|w);             /* triggers the fill */
    m64_emit_terminator(&cb);
    return m64ws_submit(g_ws, &cb);
}

/* 16-bit Z buffer, one screen's worth above the texture heap (0x700000..0x796000
 * for 640x480x2).  The mach64 has no HW depth-clear, so we 2D-fill it to 0xFFFF
 * ("far", for the usual LESS/LEQUAL test) at the top of each depth-tested frame. */
#define CIF_ZBUF_OFF 0x700000u
/* CPU depth-clear to 0xFFFF ("far").  Slower than a 2D fill but format-exact; use it
 * to prove the depth path before optimizing.  Reuses cif_rb as the source staging. */
static void cif_z_clear(void)
{
    unsigned bytes = g_bw * g_bh * 2u, i;
    unsigned short *p = (unsigned short *)cif_rb;
    if (!g_ws || bytes > sizeof(cif_rb)) return;
    for (i = 0; i < g_bw*g_bh; i++) p[i] = 0xffff;
    m64ws_write_vram(g_ws, CIF_ZBUF_OFF, cif_rb, bytes);
    if (g_zdiag) {                              /* verify the clear actually landed */
        unsigned short chk[4] = {0,0,0,0};
        unsigned mid = (g_bh/2)*g_bw + g_bw/2;  /* a center texel */
        m64ws_read_vram(g_ws, CIF_ZBUF_OFF + mid*2u, chk, 8);
        LOG("  [zclear] mid readback = %04x %04x %04x %04x\r\n", chk[0],chk[1],chk[2],chk[3]);
    }
}

/* ---- texture table -------------------------------------------------------
 * Each TextureReg uploads the base level to VRAM (above the back buffer) and
 * records its offset/dims/format.  C3D_ETEXFMT values == mach64 datatype codes.
 * The mach64/LT-Pro has no usable HW mip chain (see gld_texture.c) -> base level
 * only, MIP_MAP_DISABLE. */
#define CIF_MAXTEX  64
#define CIF_TEXBASE 0x540000u               /* VRAM texture heap (above back buffer) */
typedef struct { int used; unsigned off, w, h, wl, hl, fmt; int cs, ct; } cif_tex;
static cif_tex  g_tex[CIF_MAXTEX];
static unsigned g_texalloc = CIF_TEXBASE;
static cif_tex *g_bound = NULL;             /* texture bound for the current draws  */

/* VQ: a palette (TexturePaletteCreate C3D_ECI_TMAP_VQ) is a 256-entry codebook,
 * each entry a 2x2 block of RGB1555 texels {ul,ur,ll,lr} (vqlib.h C3D_CODEBOOK).
 * We decode VQ textures to full RGB1555 in software at TextureReg time. */
typedef struct { unsigned short ul, ur, ll, lr; } cif_cbe;
#define CIF_MAXPAL 16
/* A palette is EITHER a VQ codebook (kind 4: 256 2x2 blocks) or a colour-index
 * palette (kind 1/2/3: 16/256 24-bit entries -> pre-converted to RGB565). */
typedef struct { int used, kind; cif_cbe cb[256]; unsigned short pal565[256]; } cif_pal;
static cif_pal g_pal[CIF_MAXPAL];
static unsigned char cif_vqbuf[1024*1024*2]; /* VQ/CI8 decode scratch (up to 1024x1024 16bpp) */

static unsigned cif_texbpp(int fmt)         /* mach64 datatype -> bytes/texel */
{ return (fmt==6)?4u : (fmt==2||fmt==7||fmt==8)?1u : 2u; }   /* 8888=4; CI8/332/Y8=1; else 2 (1555/565/4444) */

/* SPECULAR_LIGHT_EN = bit 31 of ALPHA_TST_CNTL (0x550), NOT SCALE_3D_CNTL -- per the DRI
 * driver (mesa_mach64_state.c mach64UpdateSpecularLighting sets it in alpha_tst_cntl).
 * The setup engine auto-computes the specular gradients from the per-vertex SPEC_ARGB, so
 * this single bit is all that's needed.  (Putting it in scale_3d_cntl blacked the frame.) */
#define CIF_SPECULAR_LIGHT_EN 0x80000000u

/* build the m64 state; textured when a texture is bound (g_bound), else gouraud */
static void cif_fill_state(m64_state *st)
{
    memset(st, 0, sizeof(*st));
    st->scale_3d_cntl = 0x000100c1u;                 /* shade, opaque, no tex/fog  */
    st->dst_off_pitch = ((g_bw/8)<<22) | (CIF_BACK_OFF>>3);
    st->sc_left_right = (0x1fffu<<16);
    st->sc_top_bottom = (0x1fffu<<16);
    st->dp_write_mask = 0xffffffffu;
    st->dp_mix        = 0x00070007u;                 /* BKGD_MIX_S|FRGD_MIX_S      */
    st->dp_src        = 0x00000505u;                 /* 3D scaler                  */
    if (g_bound) {
        unsigned ml = (g_bound->wl > g_bound->hl) ? g_bound->wl : g_bound->hl;
        /* dst 8888 (back buffer), SCALE/texture field [31:28] = texel format */
        st->dp_pix_width = 0x00060666u | ((unsigned)g_bound->fmt << 28);
        st->textured      = 1;
        /* FCN_TEXTURE base; TEX_LIGHT_FCN [23:22] from TMAP_LIGHT: 0=REPLACE (texel
         * passes through, ignore vertex colour), 1=MODULATE, 2=ALPHA_DECAL. */
        st->tex_scale_3d  = 0x0101009bu | ((unsigned)(g_cur ? (g_cur->tmap_light & 3) : 0) << 22);
        st->tex_cntl      = (1u<<9)|(1u<<23)|(1u<<30)
                          | (g_bound->cs ? (1u<<17):0u) | (g_bound->ct ? (1u<<18):0u);
        st->tex_size_pitch= (g_bound->wl<<0) | (ml<<4) | (g_bound->hl<<8);
        st->tex_0_off     = g_bound->off;
        st->tex_maxlog2   = (int)ml;
        st->tex_mip       = 0;
    } else {
        st->dp_pix_width  = 0x60060666u;             /* all 8888 (untextured)      */
    }
    /* specular add: bit 31 of ALPHA_TST_CNTL (setup engine interpolates SPEC_ARGB) */
    if (g_cur && g_cur->spec_en) st->alpha_tst_cntl |= CIF_SPECULAR_LIGHT_EN;
    /* depth test: C3D_EZCMP enum values match the mach64 Z_TEST field [6:4] one-to-one
     * (NEVER=0..ALWAYS=7).  TESTON_WRITEZ also enables Z writeback (Z_MASK_EN). */
    if (g_zdepth_on && g_cur && g_cur->z_mode != C3D_EZMODE_OFF) {
        unsigned test = g_diag_flatz ? MACH64_Z_TEST_GREATER   /* DIAG: force polarity */
                                     : (((unsigned)g_cur->z_cmp & 7u) << 4);
        st->z_cntl = MACH64_Z_EN | test
                   | ((g_zwrite && g_cur->z_mode == C3D_EZMODE_TESTON_WRITEZ) ? MACH64_Z_MASK_EN : 0u);
        st->z_off_pitch = ((g_bw/8)<<22) | (CIF_ZBUF_OFF>>3);
    }
}

/* C3D_VTCF colour floats -> 0xAARRGGBB (auto-detect [0,1] vs [0,255] range). */
static unsigned cif_pack_argb(const C3D_VTCF *v)
{
    float s = (v->r<=1.5f && v->g<=1.5f && v->b<=1.5f) ? 255.0f : 1.0f;
    int r=(int)(v->r*s), g=(int)(v->g*s), b=(int)(v->b*s), a=(int)(v->a*s);
    if(r<0)r=0; if(r>255)r=255; if(g<0)g=0; if(g>255)g=255;
    if(b<0)b=0; if(b>255)b=255; if(a<0)a=0; if(a>255)a=255;
    return ((unsigned)a<<24)|((unsigned)r<<16)|((unsigned)g<<8)|(unsigned)b;
}

/* Pack a vertex (C3D_VTCF or C3D_TLVERTEX, per g_cur->vtype) -> m64_vertex.
 * x,y,z are at the same offsets (0,4,8) in both formats.  The mach64 setup engine
 * takes RAW [0,1] s,t + the vertex W; VTCF s,t are perspective-PREMULTIPLIED (s=u*w,
 * GEOM.CPP:983) so divide W back out, while TLVERTEX s,t are already raw (+ rhw=1/w). */
static void cif_pack_any(const void *v, m64_vertex *mv)
{
    float wv, uu, vv;
    unsigned wb;
    memset(mv, 0, sizeof(*mv));
    if (g_cur && g_cur->vtype == C3D_EV_TLVERTEX) {
        const C3D_TLVERTEX *tl = (const C3D_TLVERTEX *)v;
        wv = (tl->rhw != 0.0f) ? (1.0f / tl->rhw) : 1.0f;   /* rhw = 1/w */
        mv->x_y = (((int)(tl->x*4.0f))<<16) | (((int)(tl->y*4.0f))&0xffff);
        uu = tl->tu;                                        /* raw texcoords */
        vv = 1.0f - tl->tv;
        mv->argb      = tl->color;                          /* packed 0xAARRGGBB */
        mv->spec_argb = tl->specular;                       /* packed specular   */
    } else {
        const C3D_VTCF *cv = (const C3D_VTCF *)v;
        wv = (cv->w != 0.0f) ? cv->w : 1.0f;
        mv->x_y = (((int)(cv->x*4.0f))<<16) | (((int)(cv->y*4.0f))&0xffff);
        uu = cv->s / wv;                                    /* undo perspective premultiply */
        vv = 1.0f - cv->t / wv;
        mv->argb      = cif_pack_argb(cv);
        mv->spec_argb = 0;
    }
    /* Depth via W-BUFFER: the Knight engine pins VTCF.z=1.0 and drives depth through W
     * (= rhw = 1/viewdepth) -- large W = near, small W = far.  rhw interpolates linearly
     * in screen space, matching the mach64's linear Z channel, so z = (Wmax-W)/(Wmax-Wmin)
     * gives near=0, far=65535 (LESS test -> nearer wins).  We self-normalize on the PREVIOUS
     * frame's W range (the camera moves slowly, so it tracks).  mach64 wants z<<15 (17.15). */
    {
        int z16 = 0;
        if (g_whi_prev > g_wlo_prev) {
            float t = (g_whi_prev - wv) / (g_whi_prev - g_wlo_prev);
            if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
            z16 = (int)(t * 65535.0f);
        }
        if (g_diag_flatz) {                        /* DIAG: clean flat depth per object */
            unsigned off = g_bound ? g_bound->off : 0;
            if (off==0x540000u||off==0x580000u) z16 = 60000;      /* sky   -> far  */
            else if (off==0x5e8000u||off==0x668000u) z16 = 40000; /* ground-> mid  */
            else z16 = 20000;                                     /* dragon-> near */
        }
        mv->z = (int32_t)((uint32_t)z16 << 15);
        if ((unsigned)z16 < g_zmin) g_zmin = (unsigned)z16;
        if ((unsigned)z16 > g_zmax) g_zmax = (unsigned)z16;
        if ((unsigned)z16 < g_czmin) g_czmin = (unsigned)z16;
        if ((unsigned)z16 > g_czmax) g_czmax = (unsigned)z16;
        { float yy = ((const float *)v)[1];         /* screen Y @ offset 4 */
          if (yy < g_cymin) g_cymin = yy; if (yy > g_cymax) g_cymax = yy; }
        if (wv < g_wmin) g_wmin = wv;
        if (wv > g_wmax) g_wmax = wv;
    }
    memcpy(&wb, &wv, 4); mv->w = (int)wb;
    memcpy(&mv->s, &uu, 4);
    memcpy(&mv->t, &vv, 4);
}

static float cif_vx(const void *v) { return ((const float *)v)[0]; }   /* x @ offset 0 */
static float cif_vy(const void *v) { return ((const float *)v)[1]; }   /* y @ offset 4 */

/* sample the demo's surface top-left (16bpp 565) as a 32bpp clear background */
static unsigned cif_sample_bg(const void *surf)
{
    unsigned short px; unsigned R,G,B;
    if(!surf) return 0x00204020u;
    px = *(const unsigned short *)surf;
    R=((px>>11)&0x1f)<<3; G=((px>>5)&0x3f)<<2; B=(px&0x1f)<<3;
    return (R<<16)|(G<<8)|B;
}

static void cif_frame_clear(unsigned bg)      /* 2D-fill the back buffer to bg */
{ cif_hw_fill(g_bw, g_bw, g_bh, bg); }

/* build state + emit ONE triangle + submit (each draw restores 3D state, since the
 * 2D frame-clear left the datapath in fill mode). */
static void cif_draw_tri(const void *a, const void *b, const void *c)
{
    static uint32_t store[256];
    m64_cmdbuf cb; m64_state st; m64_vertex v0,v1,v2;
    float area;
    const void *t;
    if(!g_ws) return;
    /* The mach64 setup engine only rasterizes positive-area (CCW) triangles; the CIF
     * does not cull, so normalize winding by swapping b,c when the signed area is <0. */
    area = (cif_vx(a)-cif_vx(c))*(cif_vy(b)-cif_vy(c)) - (cif_vy(a)-cif_vy(c))*(cif_vx(b)-cif_vx(c));
    if (area < 0.0f) { t = b; b = c; c = t; }
    cif_fill_state(&st);
    cif_pack_any(a,&v0); cif_pack_any(b,&v1); cif_pack_any(c,&v2);
    m64_cb_init(&cb, store, 256);
    m64_emit_state(&cb, &st);
    if(!m64_emit_triangle(&cb, &v0,&v1,&v2)) return;   /* degenerate -> drop */
    m64_emit_terminator(&cb);
    if(cb.overflow) return;
    m64ws_submit(g_ws, &cb);
}

static void cif_draw_quad(const void *a, const void *b, const void *c, const void *d)
{ cif_draw_tri(a,b,c); cif_draw_tri(a,c,d); }

/* present the frame: readback the 32bpp back buffer -> convert -> 16bpp surface.
 * Surface byte-pitch = width*2 (SURF_DRAW_PITCH is in PIXELS, = lPitch/2). */
static void cif_present(void *surf, int is1555)
{
    unsigned r,c, W=g_bw, H=g_bh, bstride;
    if(!g_ws||!surf||!W||!H) return;
    if(W>CIF_MAXW) W=CIF_MAXW;
    if(H>CIF_PRES_H) H=CIF_PRES_H;
    bstride = W*2;
    if(m64ws_read_vram(g_ws, CIF_BACK_OFF, cif_rb, W*H*4)) return;
    for(r=0;r<H;r++){
        unsigned short *drow=(unsigned short*)((unsigned char*)surf + r*bstride);
        const unsigned *srow=(const unsigned*)(cif_rb + r*W*4);
        for(c=0;c<W;c++){
            unsigned px=srow[c], R=(px>>16)&0xff, G=(px>>8)&0xff, B=px&0xff;
            drow[c]=is1555?(unsigned short)(((R>>3)<<10)|((G>>3)<<5)|(B>>3))
                          :(unsigned short)(((R>>3)<<11)|((G>>2)<<5)|(B>>3));
        }
    }
}

C3D_EC WINAPI ATI3DCIF_RenderBegin(C3D_HRC hRC)
{
    cif_ctx *c = ctx_check(hRC);
    LOG("ATI3DCIF_RenderBegin(%p) hw=%d\r\n", hRC, g_hw);
    if (!c) return C3D_EC_BADPARAM;
    g_cur = c;
    g_cleared = 0;                          /* new frame: clear back buffer on 1st draw */
    return C3D_EC_OK;
}

C3D_EC WINAPI ATI3DCIF_RenderEnd(void)
{
    LOG("ATI3DCIF_RenderEnd()\r\n");
    /* STAGE 2: present the rendered frame -- readback the 32bpp back buffer (bg +
     * hardware triangles) and convert to the demo's 16bpp surface. */
    if (g_hw && g_cur && g_cur->surf_ptr && g_cleared) {   /* g_cleared implies a valid frame */
        int is1555 = (g_cur->surf_pf == C3D_EPF_RGB1555);
        cif_present(g_cur->surf_ptr, is1555);
        LOG("  [stage2] present %ux%u -> surf=%p pitch_px=%u\r\n",
            g_bw, g_bh, g_cur->surf_ptr, g_cur->surf_pitch);
    }
    if (g_zdiag && g_ws) {                     /* did the HW actually write Z this frame? */
        unsigned short zc[4] = {0,0,0,0};
        unsigned mid = (g_bh/2)*g_bw + g_bw/2;
        m64ws_read_vram(g_ws, CIF_ZBUF_OFF + mid*2u, zc, 8);
        LOG("  [zafter] mid readback = %04x %04x %04x %04x\r\n", zc[0],zc[1],zc[2],zc[3]);
    }
    { unsigned wlo, whi; memcpy(&wlo,&g_wmin,4); memcpy(&whi,&g_wmax,4);
      LOG("  [z] z16=[%u..%u] w=[0x%08x..0x%08x] zmode=%d zcmp=%d\r\n",
          g_zmin, g_zmax, wlo, whi, g_cur?g_cur->z_mode:-1, g_cur?g_cur->z_cmp:-1); }
    if (g_wmax > g_wmin) { g_wlo_prev = g_wmin; g_whi_prev = g_wmax; }  /* feed next frame */
    if (g_zdiag > 0) g_zdiag--;
    g_zmin = 0xffffffffu; g_zmax = 0; g_wmin = 1e30f; g_wmax = -1e30f;
    g_cur = NULL;
    return C3D_EC_OK;
}

C3D_EC WINAPI ATI3DCIF_RenderSwitch(C3D_HRC hRC)
{
    cif_ctx *c = ctx_check(hRC);
    LOG("ATI3DCIF_RenderSwitch(%p)\r\n", hRC);
    if (!c) return C3D_EC_BADPARAM;
    g_cur = c;
    return C3D_EC_OK;
}

/* shared per-frame setup: set back-buffer width + lazily clear it to the demo's bg */
static int cif_frame_prep(void)
{
    if (!g_hw || !g_cur || !g_cur->surf_ptr) return 0;
    if (g_cur->vtype != C3D_EV_VTCF && g_cur->vtype != C3D_EV_TLVERTEX) return 0;
    /* SURF_DRAW_PITCH is in PIXELS.  Some apps (RProEx4) never set it and rely on the
     * CIF default = the (fullscreen) screen width; default to 640 when unset. */
    g_bw = g_cur->surf_pitch ? g_cur->surf_pitch : 640u;
    if (g_bw > CIF_MAXW) g_bw = CIF_MAXW;
    g_bh = CIF_PRES_H;                              /* 480 (demo surface height)    */
    /* resolve the texture bound by TMAP_EN + TMAP_SELECT for this Render call */
    g_bound = NULL;
    if (g_cur->tmap_en && g_cur->tmap) {
        unsigned h = (unsigned)(UINT_PTR)g_cur->tmap;
        if (h && h < CIF_MAXTEX && g_tex[h].used) g_bound = &g_tex[h];
    }
    if (!g_cleared) {
        cif_frame_clear(cif_sample_bg(g_cur->surf_ptr));
        if (g_cur->z_mode != C3D_EZMODE_OFF) cif_z_clear();   /* fresh depth each frame */
        g_cleared = 1;
    }
    return 1;
}

C3D_EC WINAPI ATI3DCIF_RenderPrimList(C3D_VLIST vList, C3D_UINT32 u32NumVert)
{
    const void *const *pv = (const void *const *)vList;          /* list = ptr array */
    unsigned i;
    LOG("ATI3DCIF_RenderPrimList(list=%p n=%u) prim=%d vtype=%d shade=%d tmap=%d\r\n",
        vList, u32NumVert, g_cur ? g_cur->prim : -1, g_cur ? g_cur->vtype : -1,
        g_cur ? g_cur->shade : -1, g_cur ? g_cur->tmap_en : -1);
    if (!vList || u32NumVert < 3 || !cif_frame_prep()) return C3D_EC_OK;
    g_czmin = 0xffffffffu; g_czmax = 0; g_cymin = 1e30f; g_cymax = -1e30f;
    if (g_cur->prim == C3D_EPRIM_QUAD) {             /* QUAD list: 4 verts -> 2 tris */
        for (i = 0; i + 3 < u32NumVert; i += 4)
            cif_draw_quad(pv[i], pv[i+1], pv[i+2], pv[i+3]);
    } else {                                         /* TRI list: groups of 3        */
        for (i = 0; i + 2 < u32NumVert; i += 3)
            cif_draw_tri(pv[i], pv[i+1], pv[i+2]);
    }
    if (g_zdiag) LOG("  [prim] texoff=0x%x z16=[%u..%u] y=[%d..%d]\r\n",
        g_bound?g_bound->off:0, g_czmin, g_czmax, (int)g_cymin, (int)g_cymax);
    return C3D_EC_OK;
}

C3D_EC WINAPI ATI3DCIF_RenderPrimStrip(C3D_VSTRIP vStrip, C3D_UINT32 u32NumVert)
{
    const char *base = (const char *)vStrip;         /* strip = contiguous verts    */
    unsigned i, stride;
    LOG("ATI3DCIF_RenderPrimStrip(strip=%p n=%u) prim=%d vtype=%d shade=%d\r\n",
        vStrip, u32NumVert, g_cur ? g_cur->prim : -1, g_cur ? g_cur->vtype : -1,
        g_cur ? g_cur->shade : -1);
    if (!vStrip || u32NumVert < 3 || !cif_frame_prep()) return C3D_EC_OK;
    g_czmin = 0xffffffffu; g_czmax = 0; g_cymin = 1e30f; g_cymax = -1e30f;
    stride = (g_cur->vtype == C3D_EV_TLVERTEX) ? sizeof(C3D_TLVERTEX) : sizeof(C3D_VTCF);
    for (i = 0; i + 2 < u32NumVert; i++)             /* TRI strip (winding normalized) */
        cif_draw_tri(base + i*stride, base + (i+1)*stride, base + (i+2)*stride);
    if (g_zdiag) LOG("  [prim-strip] texoff=0x%x z16=[%u..%u] y=[%d..%d]\r\n",
        g_bound?g_bound->off:0, g_czmin, g_czmax, (int)g_cymin, (int)g_cymax);
    return C3D_EC_OK;
}

C3D_EC WINAPI ATI3DCIF_RenderPrimMesh(C3D_PVARRAY pVArray, C3D_PUINT32 pu32Indicies,
                                      C3D_UINT32 u32NumIndicies)
{
    LOG("ATI3DCIF_RenderPrimMesh(arr=%p idx=%p n=%u)\r\n", pVArray, pu32Indicies, u32NumIndicies);
    return C3D_EC_OK;
}

/* ========================================================================= *
 *  Textures  (Increment 1: hand back opaque non-null handles + trace)
 * ========================================================================= */
static unsigned g_tex_seq = 1;

C3D_EC WINAPI ATI3DCIF_TextureReg(C3D_PTMAP p, C3D_PHTX phtmap)
{
    unsigned i, w, h, wl, hl, bpp, bytes, off;
    int fmt;
    if (!p || !phtmap) return C3D_EC_BADPARAM;
    wl = p->u32MaxMapXSizeLg2; hl = p->u32MaxMapYSizeLg2;
    w = 1u<<wl; h = 1u<<hl; fmt = (int)p->eTexFormat;
    LOG("ATI3DCIF_TextureReg fmt=%d %ux%u mip=%d clampST=%d,%d lvl0=%p\r\n",
        fmt, w, h, p->bMipMap, p->bClampS, p->bClampT, p->apvLevels[0]);
    for (i = 1; i < CIF_MAXTEX; i++) if (!g_tex[i].used) break;
    if (i >= CIF_MAXTEX) { *phtmap = (C3D_HTX)0; return C3D_EC_MEMALLOCFAIL; }
    if (fmt == C3D_ETF_VQ) {
        /* VQ: MaxMapLg2 is the FULL image dim (mandrill.vqt: 128x128, 64x64 index
         * bytes -- confirmed from the file layout).  So index dim = w/2; each index
         * -> a 2x2 codebook block filling the full w x h RGB1555 texture. */
        unsigned ph = (unsigned)(UINT_PTR)p->htxpalTexPalette;
        const unsigned char *idx = (const unsigned char *)p->apvLevels[0];
        unsigned iw = w>>1, ih = h>>1, ix, iy;
        if (w > 1024 || h > 1024 || (unsigned long)w*h*2 > sizeof(cif_vqbuf) ||
            ph == 0 || ph >= CIF_MAXPAL || !g_pal[ph].used || !idx) {
            *phtmap = (C3D_HTX)0; return C3D_EC_BADPARAM;
        }
        bytes = w*h*2; off = g_texalloc;
        if (off + bytes > CIF_ZBUF_OFF) { *phtmap = (C3D_HTX)0; return C3D_EC_MEMALLOCFAIL; }
        g_texalloc = (off + bytes + 31u) & ~31u;
        {   cif_cbe *cb = g_pal[ph].cb;
            unsigned short *d = (unsigned short *)cif_vqbuf;
            for (iy = 0; iy < ih; iy++) for (ix = 0; ix < iw; ix++) {
                cif_cbe *e = &cb[idx[iy*iw + ix]];
                unsigned x0 = 2*ix, y0 = 2*iy;
                d[(y0  )*w + x0] = e->ul;  d[(y0  )*w + x0+1] = e->ur;
                d[(y0+1)*w + x0] = e->ll;  d[(y0+1)*w + x0+1] = e->lr;
            }
            if (g_ws) m64ws_write_vram(g_ws, off, cif_vqbuf, bytes);
        }
        fmt = C3D_ETF_RGB565;                    /* VQ codebook texels are RGB565 (mandrill.vqt) */
        LOG("  VQ-decoded %ux%u (idx %ux%u pal=%u) -> off=0x%x\r\n", w, h, iw, ih, ph, off);
    } else if (fmt == C3D_ETF_CI8 || fmt == C3D_ETF_CI4) {
        /* colour-index: w*h index bytes -> full w*h RGB565 via the colour palette */
        unsigned ph = (unsigned)(UINT_PTR)p->htxpalTexPalette;
        const unsigned char *idx = (const unsigned char *)p->apvLevels[0];
        unsigned n = w*h, j;
        if (w > 1024 || h > 1024 || (unsigned long)w*h*2 > sizeof(cif_vqbuf) ||
            ph == 0 || ph >= CIF_MAXPAL || !g_pal[ph].used || !idx) {
            *phtmap = (C3D_HTX)0; return C3D_EC_BADPARAM;
        }
        bytes = w*h*2; off = g_texalloc;
        if (off + bytes > CIF_ZBUF_OFF) { *phtmap = (C3D_HTX)0; return C3D_EC_MEMALLOCFAIL; }
        g_texalloc = (off + bytes + 31u) & ~31u;
        {   const unsigned short *pal = g_pal[ph].pal565;
            unsigned short *d = (unsigned short *)cif_vqbuf;
            if (fmt == C3D_ETF_CI8)
                for (j = 0; j < n; j++) d[j] = pal[idx[j]];
            else                        /* CI4: two 4-bit indices per byte, low nibble first */
                for (j = 0; j < n; j++) { unsigned bb = idx[j>>1];
                    d[j] = pal[(j & 1) ? (bb >> 4) : (bb & 0xf)]; }
            if (g_ws) m64ws_write_vram(g_ws, off, cif_vqbuf, bytes);
        }
        fmt = C3D_ETF_RGB565;
        LOG("  CI-decoded %ux%u pal=%u -> off=0x%x\r\n", w, h, ph, off);
    } else {
        bpp = cif_texbpp(fmt); bytes = w*h*bpp; off = g_texalloc;
        if (off + bytes > CIF_ZBUF_OFF) { *phtmap = (C3D_HTX)0; return C3D_EC_MEMALLOCFAIL; }
        g_texalloc = (off + bytes + 31u) & ~31u;
        {   int direct = (fmt==3||fmt==4||fmt==6||fmt==7||fmt==8||fmt==15);
            if (direct && g_ws && p->apvLevels[0])
                m64ws_write_vram(g_ws, off, p->apvLevels[0], bytes);
            else if (!direct)
                LOG("  (fmt %d needs decode -- NOT uploaded; htxpal=%p)\r\n",
                    fmt, p->htxpalTexPalette);
        }
    }
    g_tex[i].used = 1; g_tex[i].off = off; g_tex[i].w = w; g_tex[i].h = h;
    g_tex[i].wl = wl; g_tex[i].hl = hl; g_tex[i].fmt = fmt;
    g_tex[i].cs = p->bClampS; g_tex[i].ct = p->bClampT;
    *phtmap = (C3D_HTX)(UINT_PTR)i;
    LOG("  -> handle=%u off=0x%x bytes=%u\r\n", i, off, bytes);
    return C3D_EC_OK;
}

C3D_EC WINAPI ATI3DCIF_TextureUnreg(C3D_HTX htx)
{
    unsigned i, h = (unsigned)(UINT_PTR)htx;
    LOG("ATI3DCIF_TextureUnreg(%u)\r\n", h);
    if (h && h < CIF_MAXTEX) g_tex[h].used = 0;
    for (i = 1; i < CIF_MAXTEX; i++) if (g_tex[i].used) return C3D_EC_OK;
    g_texalloc = CIF_TEXBASE;                  /* all freed -> reset the VRAM heap */
    return C3D_EC_OK;
}

C3D_EC WINAPI ATI3DCIF_TexturePaletteCreate(C3D_ECI_TMAP_TYPE epal, void *pPalette,
                                            C3D_PHTXPAL phtpal)
{
    unsigned i;
    LOG("ATI3DCIF_TexturePaletteCreate(kind=%d pal=%p)\r\n", epal, pPalette);
    if (!phtpal || !pPalette) return C3D_EC_BADPARAM;
    for (i = 1; i < CIF_MAXPAL; i++) if (!g_pal[i].used) break;
    if (i >= CIF_MAXPAL) { *phtpal = (C3D_HTXPAL)0; return C3D_EC_MEMALLOCFAIL; }
    if (epal == C3D_ECI_TMAP_VQ) {                       /* VQ codebook: 256 * 2x2 blocks */
        memcpy(g_pal[i].cb, pPalette, sizeof(g_pal[i].cb));
        g_pal[i].kind = 4;
        LOG("  -> VQ codebook palette handle=%u\r\n", i);
    } else {                                             /* colour-index (8BIT=256/4BIT=16) */
        const unsigned *pe = (const unsigned *)pPalette; /* C3D_PALETTENTRY = r,g,b,flags bytes */
        unsigned n = (epal == C3D_ECI_TMAP_8BIT) ? 256u : 16u, k;
        for (k = 0; k < n; k++) {
            unsigned c = pe[k], r = c & 0xffu, g = (c>>8)&0xffu, b = (c>>16)&0xffu;
            g_pal[i].pal565[k] = (unsigned short)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
        }
        g_pal[i].kind = 3;
        LOG("  -> CI colour palette (%u entries) handle=%u\r\n", n, i);
    }
    g_pal[i].used = 1;
    *phtpal = (C3D_HTXPAL)(UINT_PTR)i;
    return C3D_EC_OK;
}

C3D_EC WINAPI ATI3DCIF_TexturePaletteDestroy(C3D_HTXPAL htxpal)
{ LOG("ATI3DCIF_TexturePaletteDestroy(%p)\r\n", htxpal); return C3D_EC_OK; }

C3D_EC WINAPI ATI3DCIF_TexturePaletteAnimate(C3D_HTXPAL htxpal, C3D_UINT32 start,
                                             C3D_UINT32 num, C3D_PPALETTENTRY pal)
{ LOG("ATI3DCIF_TexturePaletteAnimate(%p s=%u n=%u)\r\n", htxpal, start, num); return C3D_EC_OK; }

C3D_EC WINAPI ATI3DCIF_LoadObject(C3D_PVOID src, C3D_PVOID dst, C3D_UINT32 size)
{ LOG("ATI3DCIF_LoadObject(src=%p dst=%p size=%u)\r\n", src, dst, size); return C3D_EC_OK; }

/* ========================================================================= *
 *  Loader-shim aliases: the SDK's aticif2 shim GetProcAddress's the exported
 *  function-POINTER symbols "ATI3DCIF_<fn>_lib" ("so GetProcAddress works without
 *  name mangling", per ATI3DCIF.h).  Export a pointer to each real function under
 *  that name (DATA in the .def).  We also export the plain undecorated names.
 * ========================================================================= */
FARPROC ATI3DCIF_Init_lib                  = (FARPROC)ATI3DCIF_Init;
FARPROC ATI3DCIF_Term_lib                  = (FARPROC)ATI3DCIF_Term;
FARPROC ATI3DCIF_GetInfo_lib               = (FARPROC)ATI3DCIF_GetInfo;
FARPROC ATI3DCIF_ContextCreate_lib         = (FARPROC)ATI3DCIF_ContextCreate;
FARPROC ATI3DCIF_ContextDestroy_lib        = (FARPROC)ATI3DCIF_ContextDestroy;
FARPROC ATI3DCIF_ContextSetState_lib       = (FARPROC)ATI3DCIF_ContextSetState;
FARPROC ATI3DCIF_RenderBegin_lib           = (FARPROC)ATI3DCIF_RenderBegin;
FARPROC ATI3DCIF_RenderEnd_lib             = (FARPROC)ATI3DCIF_RenderEnd;
FARPROC ATI3DCIF_RenderSwitch_lib          = (FARPROC)ATI3DCIF_RenderSwitch;
FARPROC ATI3DCIF_RenderPrimList_lib        = (FARPROC)ATI3DCIF_RenderPrimList;
FARPROC ATI3DCIF_RenderPrimStrip_lib       = (FARPROC)ATI3DCIF_RenderPrimStrip;
FARPROC ATI3DCIF_RenderPrimMesh_lib        = (FARPROC)ATI3DCIF_RenderPrimMesh;
FARPROC ATI3DCIF_TextureReg_lib            = (FARPROC)ATI3DCIF_TextureReg;
FARPROC ATI3DCIF_TextureUnreg_lib          = (FARPROC)ATI3DCIF_TextureUnreg;
FARPROC ATI3DCIF_TexturePaletteCreate_lib  = (FARPROC)ATI3DCIF_TexturePaletteCreate;
FARPROC ATI3DCIF_TexturePaletteDestroy_lib = (FARPROC)ATI3DCIF_TexturePaletteDestroy;
FARPROC ATI3DCIF_TexturePaletteAnimate_lib = (FARPROC)ATI3DCIF_TexturePaletteAnimate;
FARPROC ATI3DCIF_LoadObject_lib            = (FARPROC)ATI3DCIF_LoadObject;

/* CRT-free DLL entry (no C runtime init). */
BOOL WINAPI DllMainCRTStartup(HINSTANCE h, DWORD reason, LPVOID r)
{ (void)h; (void)r; if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h); return TRUE; }
