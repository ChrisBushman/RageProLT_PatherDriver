/* common/rage_2d_guard.h - validator/interpreter for the stock ATIRageProGA 2D command stream (B1b step 1).
 * Spec and every rule: docs/re-notes/ga-2d-stream-spec.md (sections 2, 7, 8).  Portable: no libc, no floating point, header-only, so the kext and
 * host tests (tests/rage2d_guard_test.c) run the same code.
 *
 * The stream is NEVER passed to the chip.  rage2d_validate() parses a kernel-owned COPY of one command buffer, checks every packet and register
 * value against the spec, interprets triggers against a shadow register file and emits abstract ops (fill / copy / line) that the kernel executes
 * with its own hardware-proven engine code, clamped to the display.  The whole buffer passes or is dropped; the shadow is committed only on success.
 * Stream register numbers are the plugin's (stream 0x10c = kext fRegs offset 0x50c).  */
#ifndef RAGE_2D_GUARD_H
#define RAGE_2D_GUARD_H
#include "rage_types.h"

enum { RAGE2D_OK = 0, RAGE2D_E_HDR = -1, RAGE2D_E_LEN = -2, RAGE2D_E_REG = -3, RAGE2D_E_VAL = -4, RAGE2D_E_TRIG = -5, RAGE2D_E_FULL = -6, RAGE2D_E_ARG = -7 };
enum { RAGE2D_OP_FILL = 1, RAGE2D_OP_COPY = 2, RAGE2D_OP_LINE = 3 };

struct rage2d_op {
    uint32_t type;
    int32_t  x, y, w, h;      /* FILL: destination rect; COPY: destination rect (after clamping) */
    int32_t  sx, sy;          /* COPY: source origin of the clamped sub-rectangle (left/top) */
    uint32_t color;           /* FILL / LINE: foreground colour as written by the client (masked to depth by the kernel) */
    uint32_t cmp, cmp_clr, cmp_msk;  /* FILL: CLR_CMP_CNTL value {0,4,5} and its colour/mask (0 = no compare) */
    uint32_t dcntl, major, minor;    /* LINE: DST_CNTL (bit0 X_DIR, bit1 Y_DIR, bit2 Y_MAJOR), major = length-1, minor */
    uint32_t xrev, yrev;      /* COPY: the kernel must walk right-to-left / bottom-to-top (destination coordinate > source on that axis, signed) */
};

/* Shadow register file: persists across buffers of one context (a preamble and its trigger can sit in different buffers). */
struct rage2d_shadow {
    uint32_t dst_y_x, src_y_x, src_width1, dst_cntl, src_cntl, dp_src, dp_mix, frgd, bkgd, pat0, pat1;
    uint32_t cmp_clr, cmp_msk, cmp_cntl, bres_err, bres_inc, bres_dec;
};
static inline void rage2d_shadow_init(struct rage2d_shadow *s)
{
    s->dst_y_x = s->src_y_x = s->src_width1 = 0; s->dst_cntl = 3; s->src_cntl = 0; s->dp_src = 0; s->dp_mix = 0x00070003u;
    s->frgd = s->bkgd = s->pat0 = s->pat1 = 0; s->cmp_clr = 0; s->cmp_msk = 0xffffffffu; s->cmp_cntl = 0; s->bres_err = s->bres_inc = s->bres_dec = 0;
}

#define RAGE2D_MAXDIM 0x3fff
static inline int32_t rage2d_sext16(uint32_t v) { v &= 0xffffu; return (int32_t)(v ^ 0x8000u) - 0x8000; }
static inline int32_t rage2d_sext18(uint32_t v) { v &= 0x3ffffu; return (int32_t)(v ^ 0x20000u) - 0x20000; }
static inline int32_t rage2d_min(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t rage2d_max(int32_t a, int32_t b) { return a > b ? a : b; }

/* Apply one register write to the shadow after checking the value rule (spec 8.3).  Returns 0 or a RAGE2D_E_ code.  The two trigger registers
 * (0x118, 0x120) are handled by the caller. */
static inline int rage2d_reg_write(struct rage2d_shadow *s, uint32_t reg, uint32_t v)
{
    int32_t x, y;
    switch (reg) {
    case 0x10c: x = rage2d_sext16(v >> 16); y = rage2d_sext16(v); if (x < -8192 || x > 8191 || y < -16384 || y > 16383) return RAGE2D_E_VAL; s->dst_y_x = v; return 0;
    case 0x18c: x = rage2d_sext16(v >> 16); y = rage2d_sext16(v); if (x < -8192 || x > 8191 || y < -16384 || y > 16383) return RAGE2D_E_VAL; s->src_y_x = v; return 0;
    case 0x190: if (v > RAGE2D_MAXDIM) return RAGE2D_E_VAL; s->src_width1 = v; return 0;
    case 0x124: if (!(v < 0x20000u || v >= 0xfffe0000u)) return RAGE2D_E_VAL; s->bres_err = v & 0x3ffffu; return 0;
    case 0x128: if (!(v < 0x20000u || v >= 0xfffe0000u)) return RAGE2D_E_VAL; s->bres_inc = v & 0x3ffffu; return 0;
    case 0x12c: if (!(v < 0x20000u || v >= 0xfffe0000u)) return RAGE2D_E_VAL; s->bres_dec = v & 0x3ffffu; return 0;
    case 0x130: switch (v) { case 0x000: case 0x001: case 0x002: case 0x003: case 0x004: case 0x005: case 0x802: case 0x803: case 0x806: case 0x807: s->dst_cntl = v; return 0; default: return RAGE2D_E_VAL; }
    case 0x1b4: if (v != 0 && v != 2) return RAGE2D_E_VAL; s->src_cntl = v; return 0;
    case 0x280: s->pat0 = v; return 0;
    case 0x284: s->pat1 = v; return 0;
    case 0x2c0: s->bkgd = v; return 0;
    case 0x2c4: s->frgd = v; return 0;
    case 0x2d4: if (v != 0x00070003u) return RAGE2D_E_VAL; s->dp_mix = v; return 0;
    case 0x2d8: if (v != 0x100u && v != 0x300u) return RAGE2D_E_VAL; s->dp_src = v; return 0;   /* 0x10001 (pattern fill) refused in the first release */
    case 0x300: s->cmp_clr = v; return 0;
    case 0x304: if (v != 0xffffu && v != 0xffffffffu) return RAGE2D_E_VAL; s->cmp_msk = v; return 0;
    case 0x308: if (v != 0 && v != 4 && v != 5) return RAGE2D_E_VAL; s->cmp_cntl = v; return 0;
    default: return RAGE2D_E_REG;
    }
}

/* Rectangle trigger (write to 0x118). */
static inline int rage2d_trigger_rect(const struct rage2d_shadow *s, uint32_t v, int32_t W, int32_t H, struct rage2d_op *ops, uint32_t max_ops, uint32_t *n)
{
    int32_t w = (int32_t)(v >> 16), h = (int32_t)(v & 0xffffu);
    int32_t dx = rage2d_sext16(s->dst_y_x >> 16), dy = rage2d_sext16(s->dst_y_x);
    struct rage2d_op *o;
    if (w > RAGE2D_MAXDIM || h > RAGE2D_MAXDIM) return RAGE2D_E_VAL;          /* DRAW_TRAP (bit 15), FILL_DIS (bit 31) and absurd sizes */
    if (s->dp_src == 0x100u) {
        int32_t x0, y0, x1, y1;
        if (s->dst_cntl != 3) return RAGE2D_E_TRIG;
        if (w == 0 || h == 0) return 0;
        x0 = rage2d_max(dx, 0); y0 = rage2d_max(dy, 0); x1 = rage2d_min(dx + w, W); y1 = rage2d_min(dy + h, H);
        if (x1 <= x0 || y1 <= y0) return 0;
        if (*n >= max_ops) return RAGE2D_E_FULL;
        o = &ops[(*n)++]; o->type = RAGE2D_OP_FILL; o->x = x0; o->y = y0; o->w = x1 - x0; o->h = y1 - y0; o->sx = o->sy = 0; o->color = s->frgd;
        o->cmp = s->cmp_cntl; o->cmp_clr = s->cmp_clr; o->cmp_msk = s->cmp_msk; o->dcntl = o->major = o->minor = o->xrev = o->yrev = 0;
        return 0;
    }
    if (s->dp_src == 0x300u) {
        int32_t sx = rage2d_sext16(s->src_y_x >> 16), sy = rage2d_sext16(s->src_y_x);
        int32_t dxl, dyt, sxl, syt, x0, x1, y0, y1;
        int xdir, ydir;
        if (s->src_cntl != 0 || s->cmp_cntl != 0 || s->dst_cntl > 3 || s->src_width1 != (uint32_t)w) return RAGE2D_E_TRIG;
        if (w == 0 || h == 0) return 0;
        xdir = (s->dst_cntl & 1u) != 0; ydir = (s->dst_cntl & 2u) != 0;                 /* 1 = left-to-right / top-to-bottom; reversed axes start at the LAST pixel */
        sxl = xdir ? sx : sx - (w - 1); syt = ydir ? sy : sy - (h - 1);
        dxl = xdir ? dx : dx - (w - 1); dyt = ydir ? dy : dy - (h - 1);
        x0 = rage2d_max(0, rage2d_max(-sxl, -dxl)); x1 = rage2d_min(w, rage2d_min(W - sxl, W - dxl));      /* both source and destination stay inside the display */
        y0 = rage2d_max(0, rage2d_max(-syt, -dyt)); y1 = rage2d_min(h, rage2d_min(H - syt, H - dyt));
        if (x1 <= x0 || y1 <= y0) return 0;
        if (*n >= max_ops) return RAGE2D_E_FULL;
        o = &ops[(*n)++]; o->type = RAGE2D_OP_COPY; o->sx = sxl + x0; o->sy = syt + y0; o->x = dxl + x0; o->y = dyt + y0; o->w = x1 - x0; o->h = y1 - y0;
        o->color = 0; o->cmp = o->cmp_clr = o->cmp_msk = 0; o->dcntl = o->major = o->minor = 0;
        o->xrev = (o->x > o->sx); o->yrev = (o->y > o->sy);                              /* kernel walk direction from the CLIPPED coordinates, signed compares */
        return 0;
    }
    return RAGE2D_E_TRIG;
}

/* Line trigger (write to 0x120). */
static inline int rage2d_trigger_line(const struct rage2d_shadow *s, uint32_t v, int32_t W, int32_t H, struct rage2d_op *ops, uint32_t max_ops, uint32_t *n)
{
    uint32_t cn = s->dst_cntl, major, minor;
    int32_t x0 = rage2d_sext16(s->dst_y_x >> 16), y0 = rage2d_sext16(s->dst_y_x);
    struct rage2d_op *o;
    if (s->dp_src != 0x100u) return RAGE2D_E_TRIG;
    if (cn != 0x000 && cn != 0x001 && cn != 0x004 && cn != 0x005 && cn != 0x802 && cn != 0x803 && cn != 0x806 && cn != 0x807) return RAGE2D_E_TRIG;
    if (((cn >> 11) & 1u) != ((cn >> 1) & 1u)) return RAGE2D_E_TRIG;                     /* BRES_SIGN must follow Y_DIR */
    if (v < 1 || v > RAGE2D_MAXDIM) return RAGE2D_E_VAL;                                  /* DRAW_TRAP (bit 15), TRAIL_X bits */
    major = v - 1;
    if ((s->bres_inc & 1u) || (s->bres_inc >> 17)) return RAGE2D_E_VAL;                   /* INC must be a non-negative even 17-bit value */
    minor = s->bres_inc >> 1;
    if (minor > major) return RAGE2D_E_VAL;
    if (rage2d_sext18(s->bres_dec) != 2 * ((int32_t)minor - (int32_t)major)) return RAGE2D_E_VAL;
    if (rage2d_sext18(s->bres_err) != 2 * (int32_t)minor - (int32_t)major) return RAGE2D_E_VAL;
    if (x0 < 0 || y0 < 0 || x0 >= W || y0 >= H || major > (uint32_t)(W > H ? W : H)) return RAGE2D_E_VAL;
    if (*n >= max_ops) return RAGE2D_E_FULL;
    o = &ops[(*n)++]; o->type = RAGE2D_OP_LINE; o->x = x0; o->y = y0; o->w = o->h = o->sx = o->sy = 0; o->color = s->frgd;
    o->cmp = o->cmp_clr = o->cmp_msk = 0; o->dcntl = cn; o->major = major; o->minor = minor; o->xrev = o->yrev = 0;
    return 0;
}

/* Validate and interpret one command buffer copy.  `w` points at the packet-header word at buffer+0x1c, `nw` is the number of words from there to the end of the kernel's
 * copy (the kernel's own size, never the user-writable capacity word).  display_idx = the only accepted word1 of ops 0x09/0x0d.  W,H = the display extent (the kernel's mode).
 * On success *sh is updated and ops[0..*nops) are the abstract operations in order.  On any error nothing is committed and *nops = 0. */
static inline int rage2d_validate(const uint32_t *w, uint32_t nw, struct rage2d_shadow *sh, uint32_t display_idx, int32_t W, int32_t H,
                                  struct rage2d_op *ops, uint32_t max_ops, uint32_t *nops)
{
    struct rage2d_shadow s; uint32_t pos = 0, n = 0, i; int rc;
    if (!w || !sh || !ops || !nops || nw == 0 || nw > 0x1000u || W <= 0 || H <= 0) { if (nops) *nops = 0; return RAGE2D_E_ARG; }
    s = *sh;
    while (pos < nw) {
        uint32_t hdr = w[pos], op = hdr >> 24, len = hdr & 0x00ffffffu, payload, start;
        if (op == 0x01u) { if (len != 0) { rc = RAGE2D_E_HDR; goto fail; } break; }      /* terminator ends the buffer */
        if (op != 0x00u && op != 0x09u && op != 0x0du) { rc = RAGE2D_E_HDR; goto fail; }   /* 0x02 (present) needs a bound surface: refused */
        if (len < 1 || len > nw - pos) { rc = RAGE2D_E_LEN; goto fail; }
        payload = len - 1; start = pos + 1;
        if (op != 0x00u) {
            if (payload < 1 || ((payload - 1) & 1u)) { rc = RAGE2D_E_LEN; goto fail; }
            if (w[start] != (op == 0x0du ? 0u : display_idx)) { rc = RAGE2D_E_VAL; goto fail; }   /* 0x0d carries word1 = 0, 0x09 the framebuffer index */
            start++; payload--;
        } else if (payload & 1u) { rc = RAGE2D_E_LEN; goto fail; }
        for (i = 0; i < payload; i += 2) {
            uint32_t reg = w[start + i], v = w[start + i + 1];
            if (reg == 0xffffffffu && v == 0xffffffffu) continue;                        /* the kernel's own odd-count pad pair */
            if (reg & 3u || reg >= 0x400u) { rc = RAGE2D_E_REG; goto fail; }
            if (reg == 0x118u) rc = rage2d_trigger_rect(&s, v, W, H, ops, max_ops, &n);
            else if (reg == 0x120u) { rc = rage2d_trigger_line(&s, v, W, H, ops, max_ops, &n); }
            else rc = rage2d_reg_write(&s, reg, v);
            if (rc) goto fail;
        }
        pos += len;
    }
    *sh = s; *nops = n; return RAGE2D_OK;
fail:
    *nops = 0; return rc;
}
#endif
