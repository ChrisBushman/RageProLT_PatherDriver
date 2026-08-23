/*
 * gld_pixelformat.c - gldChoosePixelFormat / gldDestroyPixelFormat.
 *
 * OpenGL.framework calls gldChoosePixelFormat(pf_out, attribs) to ask whether we
 * can support a set of pixel-format attributes; we parse the attribs, build our
 * private gld_pixel_format, and return it (the framework hands it back to
 * gldCreateContext, then frees it via gldDestroyPixelFormat).
 *
 * attribs = a 0-terminated int32 array.  Token semantics recovered from the
 * reference gldChoosePixelFormat jump table (docs/re-notes/gld-layer.md):
 * boolean tokens stand alone; value tokens are followed by one int32 and the
 * driver keeps the max; unknown tokens are rejected.  (Error codes match the
 * reference: 0x271e bad arg, 0x2710 unsupported attr, 0x2720 out of memory.)
 */
#include "gld.h"
#include <stdlib.h>
#include <string.h>

/* GLI/CGL pixel-format attribute tokens (recovered) */
#define GLD_PFA_DOUBLEBUFFER  5     /* boolean -> flags bit3 */
#define GLD_PFA_STEREO        6     /* boolean -> flags bit1 */
#define GLD_PFA_AUX_BUFFERS   7     /* value */
#define GLD_PFA_COLOR_SIZE    8     /* value */
#define GLD_PFA_ALPHA_SIZE    11    /* value */
#define GLD_PFA_DEPTH_SIZE    12    /* value */
#define GLD_PFA_STENCIL_SIZE  13    /* value */
#define GLD_PFA_ACCUM_SIZE    14    /* value */
#define GLD_PFA_SAMPLE_BUF    0x14  /* value  (multisample group 0x14..0x1a) */
#define GLD_PFA_SAMPLES       0x15  /* value */
#define GLD_PFA_DISPLAY_MASK  0x54  /* value */
#define GLD_PFA_ACCELERATED   73    /* boolean (we are) */

/* value-taking tokens the reference reads a following int32 for.  Keeping this
 * in sync with the jump table is what stops the parser from desyncing on an
 * attribute we don't specifically handle. */
static int attr_takes_value(int tok)
{
    switch (tok) {
    case GLD_PFA_AUX_BUFFERS: case GLD_PFA_COLOR_SIZE: case GLD_PFA_ALPHA_SIZE:
    case GLD_PFA_DEPTH_SIZE:  case GLD_PFA_STENCIL_SIZE: case GLD_PFA_ACCUM_SIZE:
    case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1a:
    case GLD_PFA_DISPLAY_MASK:
        return 1;
    default:
        return 0;
    }
}

int gldChoosePixelFormat(void **pf_out, const int *attribs)
{
    gld_pixel_format p;
    gld_pixel_format *out;
    const int *a = attribs;

    if (!pf_out) return 0x271e;
    *pf_out = 0;
    memset(&p, 0, sizeof p);

    while (a && *a) {
        int tok = *a++;
        int v;
        switch (tok) {
        case GLD_PFA_DOUBLEBUFFER: p.flags |= GLD_PF_DOUBLEBUFFER; break;
        case GLD_PFA_STEREO:       p.flags |= GLD_PF_STEREO;       break;
        case GLD_PFA_ACCELERATED:  break;                 /* yes, accelerated */
        case GLD_PFA_AUX_BUFFERS:  p.aux_buffers  = (uint32_t)*a++; break;
        case GLD_PFA_COLOR_SIZE:   v=*a++; if((uint32_t)v>p.color_size)   p.color_size=v;   break;
        case GLD_PFA_ALPHA_SIZE:   v=*a++; if((uint32_t)v>p.alpha_size)   p.alpha_size=v;   break;
        case GLD_PFA_DEPTH_SIZE:   v=*a++; if((uint32_t)v>p.depth_size)   p.depth_size=v;   break;
        case GLD_PFA_STENCIL_SIZE: v=*a++; if((uint32_t)v>p.stencil_size) p.stencil_size=v; break;
        case GLD_PFA_ACCUM_SIZE:   v=*a++; if((uint32_t)v>p.accum_size)   p.accum_size=v;   break;
        default:
            if (attr_takes_value(tok)) { (void)*a++; break; }  /* skip its value */
            /* Unknown boolean attribute: the reference REJECTS it. Match that so
             * the framework won't hand us a format we can't honour. */
            return 0x2710;
        }
    }

    /* Capability clamp for this hardware: 32bpp ARGB8888 render target, optional
     * 16-bit depth; no hw stencil/accum (recorded but not offered). */
    if (p.color_size == 0) p.color_size = 32;
    if (p.depth_size > 16) p.depth_size = 16;

    out = (gld_pixel_format *)malloc(sizeof *out);
    if (!out) return 0x2720;
    *out = p;
    *pf_out = out;
    return 0;
}

int gldDestroyPixelFormat(void *pf)
{
    if (pf) free(pf);
    return 0;
}
