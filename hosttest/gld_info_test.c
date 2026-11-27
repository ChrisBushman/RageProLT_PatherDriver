/* host test: gld_renderer_info must match the CGL/CoreGraphics offsets (B1d, quartz-acceleration-study.md 1.6) */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include "../gldriver/gld_renderer_info.h"
int main(void){
    int bad = 0;
#define CHK(f,o) do{ if (offsetof(gld_renderer_info,f)!=(o)){ printf("FAIL %s at 0x%zx want 0x%x\n",#f,offsetof(gld_renderer_info,f),(o)); bad++; } }while(0)
    CHK(renderer_id,0x04); CHK(flags,0x08); CHK(buffer_modes,0x0c); CHK(video_memory,0x30); CHK(texture_memory,0x34);
    printf(bad?"gld_info_test FAIL\n":"gld_info_test PASS (video_memory +0x30, texture_memory +0x34, flags +0x08, renderer_id +0x04)\n");
    return bad;
}
