#include <stdio.h>
#include <stdint.h>
#include "common/mach64_regs.h"
static int bad;
#define T(name, got, want) do { int g=(got); printf("%-62s got=%d want=%d %s\n", name, g, want, g==(want)?"ok":"WRONG"); if (g!=(want)) bad++; } while (0)
int main(void)
{
    uint32_t pitch = 0x10000u, h = 0x10000u;
    T("span ending exactly at the window start is ok",       mach64_vram_span_ok(0x7ff000, 0x800), 1);
    T("one byte into the window refused",                    mach64_vram_span_ok(0x7ff000, 0x801), 0);
    T("start AT the window refused",                         mach64_vram_span_ok(0x7ff800, 0), 0);
    T("whole usable VRAM ok",                                mach64_vram_span_ok(0, 0x7ff800), 1);
    T("one byte more refused",                               mach64_vram_span_ok(0, 0x7ff801), 0);
    T("huge length refused",                                 mach64_vram_span_ok(0x100, 0xffffffffu), 0);
    /* how the callers compute len: 32-bit pitch*4*h wraps for garbage dimensions */
    T("caller math pitch=65536,h=65536 in 64-bit (as the plugin now does)", mach64_vram_span_ok(0x7ff000, (uint64_t)pitch * 4u * h), 0);
    T("(old 32-bit caller math would have wrapped to len 0)", (int)((uint32_t)(pitch * 4u * h) == 0), 1);
    return bad;
}
