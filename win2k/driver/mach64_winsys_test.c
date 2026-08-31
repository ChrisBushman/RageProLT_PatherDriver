/*
 * mach64_winsys_test.c - prove the reusable winsys (mach64_winsys.c) drives the
 * hardware: build a triangle stream with common/mach64_emit, submit via
 * m64ws_submit (bus-master DMA through kmd), read it back with m64ws_read_vram.
 * Same triangle the kmd tridma test rendered -- now via the library the Mesa
 * backend will call. CRT-free.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "mach64_emit.h"
#include "mach64_winsys.h"

/* CRT-free: provide the libc bits common/mach64_emit.c needs */
void *memcpy(void *d, const void *s, size_t n){ unsigned char*a=d; const unsigned char*b=s; while(n--)*a++=*b++; return d; }
void *memset(void *d, int c, size_t n){ unsigned char*a=d; while(n--)*a++=(unsigned char)c; return d; }

__declspec(dllimport) int WINAPIV wsprintfA(LPSTR, LPCSTR, ...);
static HANDLE o; static void outs(const char*s){DWORD n;WriteFile(o,s,lstrlenA(s),&n,0);}
static char b[256];
#define OUTF(...) do{ wsprintfA(b,__VA_ARGS__); outs(b);}while(0)

/* mach64 register offsets (common/mach64_regs.h values) */
#define R_Z_CNTL 0x054c
#define R_SCALE_3D_CNTL 0x05fc
#define R_DP_PIX_WIDTH 0x06d0
#define R_DST_OFF_PITCH 0x0500
#define R_SC_LEFT_RIGHT 0x06a8
#define R_SC_TOP_BOTTOM 0x06b4
#define R_DP_WRITE_MASK 0x06c8
#define R_CLR_CMP_CNTL 0x0708
#define R_DP_MIX 0x06d4
#define R_DP_SRC 0x06d8
#define R_SETUP_CNTL 0x0304
#define R_ALPHA_TST_CNTL 0x0550

static unsigned char fb[32*32*4];
static uint32_t cbs[512];

void _mainstart(void)
{
    o = GetStdHandle(STD_OUTPUT_HANDLE);
    m64_winsys *ws = m64ws_open();
    if (!ws) { outs("m64ws_open failed (is mach64kmd started?)\n"); ExitProcess(2); }
    OUTF("winsys open; CHIP_ID=0x%08lx VRAM=%luMB\n",
         (unsigned long)m64ws_reg_read(ws, 0x04e0), (unsigned long)(m64ws_vram_size(ws)>>20));

    unsigned off=0x400000, pitch=32, W=32, H=32, color=0xFFFF8040;
    { static unsigned char z[32*32*4]; unsigned i; for(i=0;i<sizeof(z);i++) z[i]=0; m64ws_write_vram(ws, off, z, W*H*4); }

    m64_cmdbuf cb; m64_cb_init(&cb, cbs, 512);
    m64_state st; { unsigned char*p=(unsigned char*)&st; unsigned i; for(i=0;i<sizeof(st);i++)p[i]=0; }
    st.scale_3d_cntl=0x000100c1u; st.setup_cntl=0; st.z_cntl=0; st.z_off_pitch=0;
    st.dp_pix_width=0x60060666u; st.dst_off_pitch=((pitch/8)<<22)|(off>>3);
    st.sc_left_right=(0x1fffu<<16); st.sc_top_bottom=(0x1fffu<<16);
    st.dp_write_mask=0xffffffffu; st.clr_cmp_cntl=0; st.dp_mix=0x00070007u; st.dp_src=0x00000505u; st.alpha_tst_cntl=0;

    float onef=1.0f; uint32_t wb; memcpy(&wb,&onef,4);
    m64_vertex v0,v1,v2; { unsigned char*p=(unsigned char*)&v0; unsigned i; for(i=0;i<sizeof(v0);i++)p[i]=0; } v1=v0; v2=v0;
    v0.x_y=(16<<16)|16; v0.w=(int32_t)wb; v0.argb=color;
    v1.x_y=(112<<16)|16; v1.w=(int32_t)wb; v1.argb=color;
    v2.x_y=(16<<16)|112; v2.w=(int32_t)wb; v2.argb=color;

    m64_emit_state(&cb,&st);
    int okt=m64_emit_triangle(&cb,&v0,&v1,&v2);
    m64_emit_terminator(&cb);
    if(!okt||cb.overflow){ outs("emit failed\n"); ExitProcess(3); }

    if (m64ws_submit(ws,&cb)) { outs("m64ws_submit failed\n"); ExitProcess(4); }
    if (m64ws_read_vram(ws, off, fb, W*H*4)) { outs("read_vram failed\n"); ExitProcess(5); }

    unsigned i,match=0,bg=0; for(i=0;i<W*H;i++){ unsigned px=*(unsigned*)(fb+i*4)&0xffffff; if(px==0xFF8040)match++; else if(px==0)bg++; }
    unsigned inside=*(unsigned*)(fb+(10*32+10)*4)&0xffffff, corner=*(unsigned*)(fb+(28*32+28)*4)&0xffffff;
    OUTF("winsys tri: %u px=color %u px=bg inside=0x%06x corner=0x%06x\n", match,bg,inside,corner);
    int pass=(match>100&&bg>100&&inside==0xFF8040&&corner==0);
    outs(pass?"RESULT: mach64 winsys library drives the hardware!\n":"RESULT: winsys mismatch\n");
    m64ws_close(ws);
    ExitProcess(pass?0:1);
}
