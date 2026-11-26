/* hosttest/f32_test.c - rage_f32_pow2 / rage_f32_recip against the FPU */
#include <stdio.h>
#include <string.h>
#include "../common/rage_f32.h"
static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
int main(void)
{
    uint32_t n, bad = 0, cnt = 0; int sh;
    for (n = 1; n < (1u << 21); n++) { volatile float d = (float)n; if (rage_f32_recip(n) != bits(1.0f / d)) { if (bad++ < 5) printf("recip %u: %08x want %08x\n", n, rage_f32_recip(n), bits(1.0f / d)); } cnt++; }
    for (n = 1; n < (1u << 24); n += 7) { volatile float d = (float)n; if (rage_f32_recip(n) != bits(1.0f / d)) { if (bad++ < 5) printf("recip %u\n", n); } cnt++; }
    for (n = 0; n < (1u << 22); n += 3) for (sh = 0; sh < 20; sh += 3) { float f = (float)n; int k; for (k = 0; k < sh; k++) f *= 0.5f; cnt++; if (rage_f32_pow2(n, sh) != bits(f)) { if (bad++ < 5) printf("pow2 %u >> %d\\n", n, sh); } }
    printf("%u values, %u wrong\n", cnt, bad);
    return bad != 0;
}
