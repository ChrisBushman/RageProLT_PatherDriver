/* recon.h - MPEG-2 picture reconstruction for the Rage LT Pro DVD bundle (M1: CPU only).
 * Inputs are exactly what DVD.framework passes to DVDDriverDecode (docs/re-notes/r128-dvd-driver/A_decode_input.md,
 * D_framework_coefficients.md): per-macroblock records (0x1c bytes) + dequantised run/level coefficient entries.
 * Integer-only, no threads, no AltiVec, no float. Endian-neutral (fields are read bytewise as big-endian). */
#ifndef RECON_H
#define RECON_H

typedef unsigned char rc_u8;
typedef short rc_s16;

#define RC_NBUF 5

typedef struct {
    int w, h;                    /* luma size (w multiple of 16 expected) */
    int mbw, mbh;                /* macroblock columns / rows (frame) */
    int pitchY, pitchC;
    int hY, hC;                  /* allocated plane heights (rows) */
    rc_u8 *mem;
    rc_u8 *pl[RC_NBUF][3];       /* Y, U, V base of each picture buffer */
    int alt_scan;                /* 0 = zig-zag, 1 = alternate scan */
    /* continuation state for the second field pass (Apple keeps these in statics) */
    const rc_u8 *mb_next, *coef_next;
    unsigned long stat_unknown_type, stat_mb, stat_type[8], stat_fielddct;
    long dbg_dec; int dbg_n;   /* debug: print raw MB records of decode #dbg_dec (DVDSTUB_MBDBG) */
} Recon;

int  rc_init(Recon *rc, int w, int h);          /* 0 ok */
void rc_free(Recon *rc);
/* params: the DecodeParams block (A_decode_input.md 1.2). Returns 0 ok, -1 failure. */
int  rc_decode(Recon *rc, rc_u8 *params);

/* exported for unit tests */
void rc_idct8x8(rc_s16 blk[64]);                /* in: dequantised coefficients (raster order), out: residual samples */
void rc_mismatch(rc_s16 blk[64]);               /* ISO 13818-2 7.4.4 mismatch control */
extern const rc_u8 rc_zigzag[64], rc_altscan[64];

#endif
