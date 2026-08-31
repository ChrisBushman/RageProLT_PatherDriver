/*
 * mach64_texproof.c - standalone hardware texture proof on the Rage Mobility-P.
 * Upload an 8x8 ARGB8888 texture (4 coloured quadrants) to VRAM, draw a textured
 * quad (2 triangles) over the 32x32 target via common/mach64_emit + winsys DMA, and
 * verify the 4 texture colours appear in the readback. Register values are the proven
 * ones from gld_texture.c; texcoords are float s,t in [0,1] (gld_prims.c). CRT-free.
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

#define TEXOFF 0x500000u      /* texture texels in VRAM */
#define FBOFF  0x400000u      /* render target          */

static uint32_t texels[8*8];
static unsigned char fb[32*32*4];
static uint32_t cbs[512];

static void set_st(m64_vertex *v, float s, float t){ __builtin_memcpy(&v->s,&s,4); __builtin_memcpy(&v->t,&t,4); }

void _mainstart(void)
{
    o = GetStdHandle(STD_OUTPUT_HANDLE);
    m64_winsys *ws = m64ws_open();
    if (!ws) { outs("m64ws_open failed\n"); ExitProcess(2); }

    /* 8x8 texture: TL red, TR green, BL blue, BR white (ARGB8888) */
    { int x,y; for(y=0;y<8;y++) for(x=0;x<8;x++){
        uint32_t c = (x<4? (y<4?0xFFFF0000u:0xFF0000FFu) : (y<4?0xFF00FF00u:0xFFFFFFFFu));
        texels[y*8+x]=c; } }
    if (m64ws_write_vram(ws, TEXOFF, texels, sizeof(texels))) { outs("tex upload failed\n"); ExitProcess(3); }

    /* pre-clear target */
    { static uint32_t z[32*32]; unsigned i; for(i=0;i<32*32;i++) z[i]=0; m64ws_write_vram(ws, FBOFF, z, sizeof(z)); }

    /* textured state (values from gld_texture.c; 8x8 -> log2=3) */
    m64_state st; memset(&st,0,sizeof(st));
    st.setup_cntl=0; st.z_cntl=0; st.z_off_pitch=0;
    st.dp_pix_width=0x60060666u; st.dst_off_pitch=((32/8)<<22)|(FBOFF>>3);
    st.sc_left_right=0x1fff0000u; st.sc_top_bottom=0x1fff0000u;
    st.dp_write_mask=0xffffffffu; st.clr_cmp_cntl=0;
    st.dp_mix=0x00070007u; st.dp_src=0x00000505u; st.alpha_tst_cntl=0;
    st.scale_3d_cntl=0x000100c1u;
    st.textured=1;
    st.tex_scale_3d=0x0141009bu;                       /* FCN_TEXTURE, MODULATE, cpp4 */
    st.tex_cntl=(1u<<9)|(1u<<23)|(1u<<30)|(1u<<17)|(1u<<18);  /* +clamp S/T */
    st.tex_size_pitch=(3u<<0)|(3u<<4)|(3u<<8);          /* 8x8 */
    st.tex_0_off=TEXOFF; st.tex_maxlog2=3; st.tex_mip=0;

    /* quad corners (window == fb here): (0,0)(32,0)(32,32)(0,32), tex (0,0)(1,0)(1,1)(0,1),
     * white vertex colour so MODULATE passes the texel through. */
    float onef=1.0f; uint32_t wb; __builtin_memcpy(&wb,&onef,4);
    m64_vertex q[4]; unsigned i; for(i=0;i<4;i++){ memset(&q[i],0,sizeof(q[i])); q[i].w=(int32_t)wb; q[i].argb=0xFFFFFFFFu; }
    q[0].x_y=(0<<16)|0;      set_st(&q[0],0,0);
    q[1].x_y=(128<<16)|0;    set_st(&q[1],1,0);
    q[2].x_y=(128<<16)|128;  set_st(&q[2],1,1);
    q[3].x_y=(0<<16)|128;    set_st(&q[3],0,1);

    m64_cmdbuf cb; m64_cb_init(&cb, cbs, 512);
    m64_emit_state(&cb, &st);
    m64_emit_triangle(&cb, &q[0], &q[1], &q[2]);   /* one winding; if a tri culls, */
    m64_emit_triangle(&cb, &q[0], &q[2], &q[3]);   /* the other still shows texels  */
    m64_emit_terminator(&cb);
    if (m64ws_submit(ws, &cb)) { outs("submit failed\n"); ExitProcess(4); }

    m64ws_read_vram(ws, FBOFF, fb, sizeof(fb));
    unsigned red=0,grn=0,blu=0,wht=0,other=0,black=0;
    for(i=0;i<32*32;i++){ unsigned px=*(unsigned*)(fb+i*4)&0xffffff;
        if(px==0xFF0000)red++; else if(px==0x00FF00)grn++; else if(px==0x0000FF)blu++;
        else if(px==0xFFFFFF)wht++; else if(px==0)black++; else other++; }
    OUTF("textured quad: red=%u green=%u blue=%u white=%u black=%u other=%u\n", red,grn,blu,wht,black,other);
    int pass=(red>40 && grn>40 && blu>40 && wht>40);
    outs(pass ? "RESULT: TEXTURED triangle via mach64 HARDWARE (all 4 texel colours sampled)!\n"
              : "RESULT: texture mismatch (see counts)\n");
    m64ws_close(ws);
    ExitProcess(pass?0:1);
}
