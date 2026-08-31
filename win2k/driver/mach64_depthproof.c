/*
 * mach64_depthproof.c - standalone hardware depth-test proof on the Rage Mobility-P.
 * Draw a NEAR green triangle, then a FAR red triangle overlapping it. With GL_LESS
 * depth testing the far red must NOT overwrite the near green where they overlap
 * (occlusion), even though red is drawn second. Verifies via readback. CRT-free.
 *
 * Z recipe (gld_render.c): vertex z = (int)(depth[0,1]*65535) << 15; Z_CNTL =
 * Z_EN|Z_TEST_LESS|Z_MASK_EN; z_off_pitch = (pitch/8<<22)|(off>>3); clear Z to 0xFFFF.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "mach64_emit.h"
#include "mach64_winsys.h"

void *memcpy(void *d, const void *s, size_t n){ unsigned char*a=d; const unsigned char*b=s; while(n--)*a++=*b++; return d; }
void *memset(void *d, int c, size_t n){ unsigned char*a=d; while(n--)*a++=(unsigned char)c; return d; }
__declspec(dllimport) int WINAPIV wsprintfA(LPSTR, LPCSTR, ...);
static HANDLE o; static void outs(const char*s){DWORD n;WriteFile(o,s,lstrlenA(s),&n,0);}
static char b[256];
#define OUTF(...) do{ wsprintfA(b,__VA_ARGS__); outs(b);}while(0)

#define FBOFF 0x400000u
#define ZOFF  0x600000u       /* 16-bit Z buffer: 32*32*2 = 2KB */

/* Z_CNTL bits (mach64_regs.h) */
#define Z_EN        (1u<<0)
#define Z_TEST_LESS (1u<<4)
#define Z_MASK_EN   (1u<<8)

static unsigned char fb[32*32*4];
static uint32_t cbs[512];

static void mkv(m64_vertex *v, int x, int y, float z01, uint32_t argb){
    float onef=1.0f; uint32_t wb; __builtin_memcpy(&wb,&onef,4);
    memset(v,0,sizeof(*v));
    v->x_y = ((x*4)<<16) | ((y*4)&0xffff);
    { int zi=(int)(z01*65535.0f+0.5f); v->z=(int32_t)((uint32_t)zi<<15); }
    v->w=(int32_t)wb; v->argb=argb;
}
static void emit_tri(m64_cmdbuf *cb, m64_vertex *a, m64_vertex *b2, m64_vertex *c){
    int x0=(short)(a->x_y>>16),y0=(short)(a->x_y&0xffff);
    int x1=(short)(b2->x_y>>16),y1=(short)(b2->x_y&0xffff);
    int x2=(short)(c->x_y>>16),y2=(short)(c->x_y&0xffff);
    long area=(long)(x0-x2)*(y1-y2)-(long)(y0-y2)*(x1-x2);
    if (area<0) m64_emit_triangle(cb,a,c,b2); else m64_emit_triangle(cb,a,b2,c);
}

void _mainstart(void)
{
    o = GetStdHandle(STD_OUTPUT_HANDLE);
    m64_winsys *ws = m64ws_open();
    if (!ws) { outs("m64ws_open failed\n"); ExitProcess(2); }

    /* clear colour target to black, Z buffer to far (0xFFFF) */
    { static uint32_t z[32*32]; unsigned i; for(i=0;i<32*32;i++) z[i]=0; m64ws_write_vram(ws, FBOFF, z, sizeof(z)); }
    { static unsigned short zb[32*32]; unsigned i; for(i=0;i<32*32;i++) zb[i]=0xFFFF; m64ws_write_vram(ws, ZOFF, zb, sizeof(zb)); }

    m64_state st; memset(&st,0,sizeof(st));
    st.scale_3d_cntl=0x000100c1u; st.setup_cntl=0;
    st.z_cntl = Z_EN|Z_TEST_LESS|Z_MASK_EN;
    st.z_off_pitch = ((32/8)<<22)|(ZOFF>>3);
    st.dp_pix_width=0x60060666u; st.dst_off_pitch=((32/8)<<22)|(FBOFF>>3);
    st.sc_left_right=0x1fff0000u; st.sc_top_bottom=0x1fff0000u;
    st.dp_write_mask=0xffffffffu; st.clr_cmp_cntl=0;
    st.dp_mix=0x00070007u; st.dp_src=0x00000505u; st.alpha_tst_cntl=0;

    m64_vertex g0,g1,g2, r0,r1,r2;
    mkv(&g0, 8,10, 0.3f, 0xFF00FF00u); mkv(&g1,24,10,0.3f,0xFF00FF00u); mkv(&g2,16,26,0.3f,0xFF00FF00u); /* NEAR green */
    mkv(&r0, 2, 2, 0.7f, 0xFFFF0000u); mkv(&r1,30, 2,0.7f,0xFFFF0000u); mkv(&r2,16,30,0.7f,0xFFFF0000u); /* FAR  red  */

    m64_cmdbuf cb; m64_cb_init(&cb, cbs, 512);
    m64_emit_state(&cb, &st);
    emit_tri(&cb, &g0,&g1,&g2);     /* draw NEAR first  */
    emit_tri(&cb, &r0,&r1,&r2);     /* draw FAR second -- must be occluded where it overlaps */
    m64_emit_terminator(&cb);
    if (m64ws_submit(ws,&cb)) { outs("submit failed\n"); ExitProcess(3); }

    m64ws_read_vram(ws, FBOFF, fb, sizeof(fb));
    #define PX(x,y) (*(unsigned*)(fb+((y)*32+(x))*4)&0xffffff)
    unsigned center=PX(16,16), redonly=PX(16,5);
    unsigned i, grn=0, red=0; for(i=0;i<32*32;i++){ unsigned px=*(unsigned*)(fb+i*4)&0xffffff; if(px==0x00FF00)grn++; else if(px==0xFF0000)red++; }
    OUTF("depth: center(16,16)=0x%06x red-only(16,5)=0x%06x  green=%u red=%u\n", center, redonly, grn, red);
    /* PASS: overlap centre stays GREEN (near occludes the later-drawn far red), and a
     * red-only spot is RED -- proves hardware Z testing works. */
    int pass=(center==0x00FF00 && redonly==0xFF0000 && grn>40 && red>40);
    outs(pass ? "RESULT: hardware DEPTH TEST works -- near triangle occludes the far one!\n"
              : "RESULT: depth mismatch\n");
    m64ws_close(ws);
    ExitProcess(pass?0:1);
}
