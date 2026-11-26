/* tools/ragegl/xfer_test.c - texture/pixel DMA upload (RAGE_GL_VRAM_WRITE >= 8 KB -> system transfer).  Verifies against bytes computed in user space
 * (not against the kernel's own inverse), against the CPU path, with ownership refusal, concurrent clients and timing.   usage: xfer_test [time] */
#include "ragegl.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>
static int fails;
#define CHECK(name, cond) do { int ok_ = (cond); printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); if (!ok_) fails++; } while (0)
static double now(void) { struct timeval tv; gettimeofday(&tv, 0); return tv.tv_sec + tv.tv_usec / 1e6; }
static void fillpat(uint8_t *p, uint32_t len, uint32_t seed) { uint32_t i, x = seed * 2654435761u + 12345u; for (i = 0; i < len; i++) { x = x * 1664525u + 1013904223u; p[i] = (uint8_t)(x >> 24); } }
static void expect(uint8_t *dst, const uint8_t *src, uint32_t len, uint32_t fl)   /* the contract: SWAP32 reverses 4 bytes, then SWAP16 swaps within 16-bit units */
{
    uint32_t i; for (i = 0; i + 4 <= len; i += 4) { uint8_t b[4] = { src[i], src[i+1], src[i+2], src[i+3] }, t;
        if (fl & 2u) { t = b[0]; b[0] = b[3]; b[3] = t; t = b[1]; b[1] = b[2]; b[2] = t; }
        if (fl & 1u) { t = b[0]; b[0] = b[1]; b[1] = t; t = b[2]; b[2] = b[3]; b[3] = t; }
        dst[i] = b[0]; dst[i+1] = b[1]; dst[i+2] = b[2]; dst[i+3] = b[3]; }
}
int main(int argc, char **argv)
{
    ragegl_t c; uint32_t off = 0, off2 = 0, i, f, si, gen, feat, end, flg; kern_return_t kr;
    static const uint32_t sizes[] = { 4096, 8192, 12288, 65536, 262144, 266240, 524292 - 4 + 4096, 1048576 };
    uint8_t *src = malloc(0x100000), *want = malloc(0x100000), *got = malloc(0x100000);
    if (ragegl_open(&c)) { printf("cannot open the GL client\n"); return 99; }
    ragegl_info(c, &gen, &feat, &end, &flg);
    CHECK("alloc 2 MB", ragegl_alloc(c, 0x200000, 4096, &off) == 0);
    for (si = 0; si < sizeof sizes / sizeof sizes[0]; si++) for (f = 0; f < 4; f++) {
        uint32_t len = sizes[si] & ~3u; char nm[96];
        fillpat(src, len, si * 8 + f + 1); expect(want, src, len, f);
        ragegl_config(c, 2, 1);
        memset(got, 0xee, len);
        kr = ragegl_vwrite(c, off + 4u * (si & 1), src, len, f);                               /* DMA path for len >= 8 KB; odd start offsets exercised */
        ragegl_vread(c, off + 4u * (si & 1), got, len, 0);                                      /* raw bytes back (read waits for the transfer) */
        snprintf(nm, sizeof nm, "write %7u bytes flags %u at +%u: bytes match the contract", len, f, 4u * (si & 1));
        CHECK(nm, kr == 0 && !memcmp(got, want, len));
    }
    /* the CPU path (DMA upload off) must give the same bytes */
    ragegl_config(c, 2, 0); fillpat(src, 262144, 99); expect(want, src, 262144, 3);
    ragegl_vwrite(c, off + 0x100000, src, 262144, 3); memset(got, 0, 262144); ragegl_vread(c, off + 0x100000, got, 262144, 0);
    CHECK("CPU path (upload DMA off) 256 KB flags 3 matches the contract", !memcmp(got, want, 262144));
    ragegl_config(c, 2, 1);
    ragegl_vwrite(c, off + 0x100000, src, 262144, 3); memset(got, 0, 262144); ragegl_vread(c, off + 0x100000, got, 262144, 0);
    CHECK("same 256 KB through the DMA path is identical", !memcmp(got, want, 262144));
    /* refusals: nothing owned there / past the end of an allocation / bad flags / bad length: no hardware access, an error */
    CHECK("write outside any allocation refused", ragegl_vwrite(c, 0x7f0000, src, 16384, 0) != 0);
    CHECK("write past the end of the allocation refused", ragegl_vwrite(c, off + 0x200000 - 4096, src, 16384, 0) != 0);
    CHECK("bad flags refused", ragegl_vwrite(c, off, src, 16384, 8) != 0);
    CHECK("length not a multiple of 4 refused", ragegl_vwrite(c, off, src, 16386, 0) != 0);
    CHECK("the refused writes left the earlier data intact", (ragegl_vread(c, off + 0x100000, got, 262144, 0), !memcmp(got, want, 262144)));
    /* concurrency: 3 processes (+ this one) each upload their own region repeatedly and verify */
    {
        int k, kids = 3, bad = 0; pid_t pid[3];
        for (k = 0; k < kids; k++) if ((pid[k] = fork()) == 0) {
            ragegl_t c2; uint32_t o = 0, r, badc = 0; uint8_t *s2 = malloc(262144), *g2 = malloc(262144), *w2 = malloc(262144);
            if (ragegl_open(&c2) || ragegl_alloc(c2, 262144, 4096, &o)) _exit(90);
            for (r = 0; r < 60; r++) { fillpat(s2, 262144, 1000 * (k + 1) + r); expect(w2, s2, 262144, r & 3u);
                if (ragegl_vwrite(c2, o, s2, 262144, r & 3u)) badc++; if (ragegl_vread(c2, o, g2, 262144, 0)) badc++; if (memcmp(g2, w2, 262144)) badc++; }
            _exit(badc ? 1 : 0);
        }
        for (i = 0; i < 40; i++) { fillpat(src, 262144, 7000 + i); expect(want, src, 262144, i & 3u); ragegl_vwrite(c, off, src, 262144, i & 3u); ragegl_vread(c, off, got, 262144, 0); if (memcmp(got, want, 262144)) bad++; }
        for (k = 0; k < kids; k++) { int st = 0; waitpid(pid[k], &st, 0); if (!WIFEXITED(st) || WEXITSTATUS(st)) bad++; }
        CHECK("4 clients uploading 256 KB concurrently, every readback exact", bad == 0);
    }
    if (argc > 1) {                                                                            /* timing: time inside the vwrite call (the DMA path returns after the kick) */
        static const uint32_t ts[] = { 16384, 262144, 1048576 }; uint32_t t, r;
        for (t = 0; t < 3; t++) for (si = 0; si < 2; si++) {
            double t0, dt = 0, dtot; ragegl_config(c, 2, si == 0 ? 0 : 1); fillpat(src, ts[t], 5);
            ragegl_vwrite(c, off, src, ts[t], 3);
            t0 = now(); for (r = 0; r < 20; r++) { double a = now(); ragegl_vwrite(c, off, src, ts[t], 3); dt += now() - a; ragegl_vread(c, off, got, 4, 0); }
            dtot = now() - t0;
            printf("TIME %7u bytes %s: %.3f ms in the vwrite call, %.3f ms per upload+fence\n", ts[t], si == 0 ? "CPU path" : "DMA path", dt * 1000 / 20, dtot * 1000 / 20);
        }
    }
    (void)off2;
    printf(fails ? "FAILED (%d)\n" : "ALL PASSED\n", fails);
    return fails != 0;
}
