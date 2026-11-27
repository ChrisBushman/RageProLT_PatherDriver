/* tests/rage2d_guard_test.c - host tests for common/rage_2d_guard.h (B1b step 1).  Streams are built exactly as the stock plugin emits them
 * (docs/re-notes/ga-2d-stream-spec.md section 4); the refusals are the attacks and malformed streams of section 7/8. */
#include <stdio.h>
#include <string.h>
#include "rage_2d_guard.h"

static int fails, checks;
#define CHECK(name, cond) do { checks++; if (!(cond)) { fails++; printf("FAIL  %s\n", name); } } while (0)
#define W 1024
#define H 768

static uint32_t buf[1100]; static uint32_t nb;
static void begin(void) { nb = 0; }
static void P(uint32_t r, uint32_t v) { buf[nb++] = r; buf[nb++] = v; }
static uint32_t hdr_at;
static void hdr(uint32_t op) { hdr_at = nb; buf[nb++] = op << 24; }
static void word(uint32_t v) { buf[nb++] = v; }
static void endpkt(void) { buf[hdr_at] |= (nb - hdr_at); }             /* length from this header to the next, header included */
static void term(void) { buf[nb++] = 0x01000000u; }
static uint32_t YX(int x, int y) { return ((uint32_t)x << 16) | ((uint32_t)y & 0xffffu); }

static struct rage2d_op ops[64]; static uint32_t nops; static struct rage2d_shadow sh;
static int run(void) { return rage2d_validate(buf, nb, &sh, 0, W, H, ops, 64, &nops); }
static void fresh(void) { rage2d_shadow_init(&sh); nops = 99; }

static void copy_rect(int sx, int sy, int dx, int dy, int w, int h)   /* rProCopy per-rect emission (spec 4.1) */
{
    uint32_t c = 3;
    if ((uint32_t)dx > (uint32_t)sx) { sx += w - 1; dx += w - 1; c &= ~1u; }
    if ((uint32_t)dy > (uint32_t)sy) { sy += h - 1; dy += h - 1; c &= ~2u; }
    P(0x130, c); P(0x18c, YX(sx, sy)); P(0x190, (uint32_t)w); P(0x10c, YX(dx, dy)); P(0x118, ((uint32_t)w << 16) | (uint32_t)h);
}
static void copy_preamble(void) { hdr(0x0d); word(0); P(0x1b4, 0); P(0x2d8, 0x300); P(0x2d4, 0x70003); }

static void t_fill(void)
{
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x2d4, 0x70003); P(0x130, 3); P(0x2c4, 0x1234); P(0x10c, YX(10, 20)); P(0x118, (30u << 16) | 40u); endpkt(); term();
    CHECK("fill: accepted", run() == 0);
    CHECK("fill: one op", nops == 1 && ops[0].type == RAGE2D_OP_FILL);
    CHECK("fill: rect and colour", ops[0].x == 10 && ops[0].y == 20 && ops[0].w == 30 && ops[0].h == 40 && ops[0].color == 0x1234);
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x2d4, 0x70003); P(0x130, 3); P(0x2c4, 1); P(0x10c, YX(-5, -5)); P(0x118, (20u << 16) | 20u); endpkt(); term();
    CHECK("fill: negative origin clipped to the display", run() == 0 && nops == 1 && ops[0].x == 0 && ops[0].y == 0 && ops[0].w == 15 && ops[0].h == 15);
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x2d4, 0x70003); P(0x130, 3); P(0x2c4, 1); P(0x10c, YX(W + 10, 0)); P(0x118, (20u << 16) | 20u); endpkt(); term();
    CHECK("fill: fully off-display dropped", run() == 0 && nops == 0);
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x2d4, 0x70003); P(0x130, 3); P(0x2c4, 1); P(0x10c, YX(1000, 700)); P(0x118, (500u << 16) | 500u); endpkt(); term();
    CHECK("fill: overhanging clipped", run() == 0 && nops == 1 && ops[0].w == 24 && ops[0].h == 68);
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x2d4, 0x70003); P(0x130, 3); P(0x2c4, 7); P(0x308, 5); P(0x300, 0xabcd); P(0x304, 0xffff); P(0x10c, YX(1, 1)); P(0x118, (4u << 16) | 4u); P(0x308, 0); endpkt(); term();
    CHECK("fill: keyed fill carries the compare", run() == 0 && nops == 1 && ops[0].cmp == 5 && ops[0].cmp_clr == 0xabcd && ops[0].cmp_msk == 0xffff);
    CHECK("fill: shadow ends with compare off", sh.cmp_cntl == 0);
}

static void t_copy(void)
{
    fresh(); begin(); copy_preamble(); copy_rect(100, 100, 50, 60, 200, 100); endpkt(); term();
    CHECK("copy: accepted", run() == 0 && nops == 1 && ops[0].type == RAGE2D_OP_COPY);
    CHECK("copy: forward geometry", ops[0].sx == 100 && ops[0].sy == 100 && ops[0].x == 50 && ops[0].y == 60 && ops[0].w == 200 && ops[0].h == 100 && !ops[0].xrev && !ops[0].yrev);
    fresh(); begin(); copy_preamble(); copy_rect(50, 60, 100, 100, 200, 100); endpkt(); term();
    CHECK("copy: reversed (dest > source) decoded back to left/top", run() == 0 && nops == 1 && ops[0].sx == 50 && ops[0].sy == 60 && ops[0].x == 100 && ops[0].y == 100 && ops[0].w == 200 && ops[0].h == 100);
    CHECK("copy: reversed walk requested", ops[0].xrev == 1 && ops[0].yrev == 1);
    fresh(); begin(); copy_preamble(); copy_rect(0, 0, 0, 5, 100, 100); endpkt(); term();
    CHECK("copy: vertical overlap walks bottom-to-top only", run() == 0 && nops == 1 && ops[0].xrev == 0 && ops[0].yrev == 1);
    fresh(); begin(); copy_preamble(); copy_rect(W - 10, 0, 0, 0, 100, 10); endpkt(); term();
    CHECK("copy: source reaching past the display is clamped (never reads off-display)", run() == 0 && nops == 1 && ops[0].w == 10 && ops[0].sx == W - 10 && ops[0].x == 0);
    fresh(); begin(); copy_preamble(); copy_rect(-20, 0, 0, 0, 100, 10); endpkt(); term();
    CHECK("copy: negative source clamped", run() == 0 && nops == 1 && ops[0].sx == 0 && ops[0].x == 20 && ops[0].w == 80);
    fresh(); begin(); copy_preamble(); copy_rect(2000, 0, 0, 0, 100, 10); endpkt(); term();
    CHECK("copy: wholly off-display source dropped", run() == 0 && nops == 0);
    fresh(); begin(); copy_preamble(); copy_rect(0, 0, 10, 10, 8, 8); copy_rect(20, 20, 30, 30, 8, 8); endpkt(); term();
    CHECK("copy: two rects in one packet", run() == 0 && nops == 2 && ops[1].sx == 20);
    fresh(); begin(); hdr(0x09); word(0); P(0x1b4, 0); P(0x2d8, 0x300); P(0x2d4, 0x70003); copy_rect(5, 5, 9, 9, 16, 16); endpkt(); term();
    CHECK("copyregion (op 0x09, fb index 0) accepted", run() == 0 && nops == 1);
    fresh(); begin(); hdr(0x09); word(1); P(0x1b4, 0); P(0x2d8, 0x300); P(0x2d4, 0x70003); copy_rect(5, 5, 9, 9, 16, 16); endpkt(); term();
    CHECK("copyregion with the wrong framebuffer index refused", run() == RAGE2D_E_VAL && nops == 0);
    fresh(); begin(); copy_preamble(); P(0x18c, YX(0, 0)); P(0x190, 50); P(0x10c, YX(10, 10)); P(0x118, (60u << 16) | 10u); endpkt(); term();
    CHECK("copy: SRC_WIDTH1 != width refused", run() == RAGE2D_E_TRIG);
}

static void t_line(void)
{
    uint32_t major = 100, minor = 40;
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x2c4, 5); P(0x130, 0x803); P(0x10c, YX(10, 10)); P(0x124, 2 * minor - major); P(0x128, 2 * minor); P(0x12c, (uint32_t)(2 * (int)(minor - major))); P(0x120, major + 1); endpkt(); term();
    CHECK("line: accepted", run() == 0 && nops == 1 && ops[0].type == RAGE2D_OP_LINE && ops[0].major == 100 && ops[0].minor == 40 && ops[0].dcntl == 0x803 && ops[0].color == 5);
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x2c4, 5); P(0x130, 0x803); P(0x10c, YX(10, 10)); P(0x124, 1); P(0x128, 2 * minor); P(0x12c, (uint32_t)(2 * (int)(minor - major))); P(0x120, major + 1); endpkt(); term();
    CHECK("line: inconsistent error term refused", run() == RAGE2D_E_VAL);
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x2c4, 5); P(0x130, 0x803); P(0x10c, YX(10, 10)); P(0x124, 2 * minor - major); P(0x128, 2 * minor); P(0x12c, (uint32_t)(2 * (int)(minor - major))); P(0x120, 0x8000u | (major + 1)); endpkt(); term();
    CHECK("line: DRAW_TRAP bit refused", run() == RAGE2D_E_VAL);
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x2c4, 5); P(0x130, 0x803); P(0x10c, YX(-1, 10)); P(0x124, 2 * minor - major); P(0x128, 2 * minor); P(0x12c, (uint32_t)(2 * (int)(minor - major))); P(0x120, major + 1); endpkt(); term();
    CHECK("line: start off-display refused", run() == RAGE2D_E_VAL);
}

static void t_refusals(void)
{
    fresh(); begin(); hdr(0); P(0x100, 0x1000); endpkt(); term();
    CHECK("refuse DST_OFF_PITCH write (kernel-owned)", run() == RAGE2D_E_REG && nops == 0);
    fresh(); begin(); hdr(0); P(0x2d0, 0x66660000u); endpkt(); term();
    CHECK("refuse DP_PIX_WIDTH write", run() == RAGE2D_E_REG);
    fresh(); begin(); hdr(0); P(0x2a8, 0); endpkt(); term();
    CHECK("refuse scissor write", run() == RAGE2D_E_REG);
    fresh(); begin(); hdr(0); P(0x180, 0); endpkt(); term();
    CHECK("refuse SRC_OFF_PITCH write (only via ops 0x09/0x0d)", run() == RAGE2D_E_REG);
    fresh(); begin(); hdr(0); P(0x1fc, 0); endpkt(); term();
    CHECK("refuse pseudo-register 0x1fc", run() == RAGE2D_E_REG);
    fresh(); begin(); hdr(0); P(0x110, 0x100000u); endpkt(); term();
    CHECK("refuse the DST_WIDTH trigger alias", run() == RAGE2D_E_REG);
    fresh(); begin(); hdr(0); P(0x500, 0); endpkt(); term();
    CHECK("refuse register >= 0x400", run() == RAGE2D_E_REG);
    fresh(); begin(); hdr(0); P(0x1b4, 0x100); endpkt(); term();
    CHECK("refuse SRC_CNTL bus-master bit", run() == RAGE2D_E_VAL);
    fresh(); begin(); hdr(0); P(0x2d4, 0x00070007u); endpkt(); term();
    CHECK("refuse any DP_MIX but source copy", run() == RAGE2D_E_VAL);
    fresh(); begin(); hdr(0); P(0x2d8, 0x10001u); endpkt(); term();
    CHECK("refuse pattern DP_SRC in the first release", run() == RAGE2D_E_VAL);
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x2c4, 1); P(0x10c, YX(0, 0)); P(0x118, 0x80000000u | 5u); endpkt(); term();
    CHECK("refuse width with the DRAW_TRAP/FILL_DIS bits", run() == RAGE2D_E_VAL);
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x2c4, 1); P(0x10c, YX(0, 0)); P(0x118, (0x4000u << 16) | 5u); endpkt(); term();
    CHECK("refuse width > 0x3fff", run() == RAGE2D_E_VAL);
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x2c4, 1); P(0x118, (5u << 16) | 0x7fffu); endpkt(); term();
    CHECK("refuse height > 0x3fff", run() == RAGE2D_E_VAL);
    fresh(); begin(); hdr(0); P(0x118, (5u << 16) | 5u); endpkt(); term();
    CHECK("refuse a trigger before any source is set (DP_SRC 0)", run() == RAGE2D_E_TRIG);
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x130, 0x2); P(0x2c4, 1); P(0x118, (5u << 16) | 5u); endpkt(); term();
    CHECK("refuse a fill with DST_CNTL != 3", run() == RAGE2D_E_TRIG);
    fresh(); begin(); hdr(0x02); endpkt(); term();
    CHECK("refuse present (op 0x02, no surface binding)", run() == RAGE2D_E_HDR);
    fresh(); begin(); hdr(0x33); endpkt(); term();
    CHECK("refuse unknown op", run() == RAGE2D_E_HDR);
    fresh(); begin(); hdr(0); P(0x2c4, 1); buf[hdr_at] |= 99; term();
    CHECK("refuse a length beyond the buffer", run() == RAGE2D_E_LEN);
    fresh(); begin(); hdr(0); word(0x2c4); buf[hdr_at] |= 2; term();
    CHECK("refuse an odd payload (half a pair)", run() == RAGE2D_E_LEN);
    fresh(); begin(); hdr(0); P(0x2c4, 1); buf[hdr_at] |= 3; term();
    { uint32_t save = buf[hdr_at]; buf[hdr_at] = 0; CHECK("refuse a zero-length packet", run() == RAGE2D_E_LEN); buf[hdr_at] = save; }
    fresh(); begin(); hdr(0); P(0x2c4, 0x77); P(0x2d8, 0x100); P(0x2d4, 0x70003); P(0x130, 3); P(0x10c, YX(0, 0)); P(0x118, (5u << 16) | 5u); P(0x100, 0); endpkt(); term();
    { struct rage2d_shadow before = sh; int rc = run(); CHECK("atomic: a bad register after a good trigger drops everything", rc == RAGE2D_E_REG && nops == 0 && memcmp(&before, &sh, sizeof sh) == 0); }
    CHECK("null/size arguments refused", rage2d_validate(0, 4, &sh, 0, W, H, ops, 64, &nops) == RAGE2D_E_ARG && rage2d_validate(buf, 0, &sh, 0, W, H, ops, 64, &nops) == RAGE2D_E_ARG);
}

static void t_state(void)
{
    /* a preamble in one buffer and its trigger in the next: the shadow persists */
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x2d4, 0x70003); P(0x130, 3); P(0x2c4, 0x55); endpkt(); term();
    CHECK("state: preamble buffer ok", run() == 0 && nops == 0);
    begin(); hdr(0); P(0x10c, YX(1, 2)); P(0x118, (3u << 16) | 4u); endpkt(); term();
    CHECK("state: trigger in the next buffer uses the saved preamble", run() == 0 && nops == 1 && ops[0].color == 0x55 && ops[0].w == 3 && ops[0].h == 4);
    /* odd register count padded with the kernel's -1 pair */
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x2c4, 1); P(0xffffffffu, 0xffffffffu); endpkt(); term();
    CHECK("state: the kernel's -1,-1 pad pair is skipped", run() == 0);
    /* a buffer without the terminator but full length is also fine (the kernel supplies the end) */
    fresh(); begin(); hdr(0); P(0x2d8, 0x100); P(0x2c4, 1); endpkt();
    CHECK("state: buffer ending at the length boundary ok", run() == 0);
}

int main(void)
{
    t_fill(); t_copy(); t_line(); t_refusals(); t_state();
    printf("rage2d_guard_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
