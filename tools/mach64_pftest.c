/*
 * mach64_pftest.c - unit-test gldChoosePixelFormat's attribute parser.
 * Pure logic (no kext / no hardware); runs standalone on the Lombard.
 */
#include <stdio.h>
#include "../gldriver/gld.h"

static int check(const char *name, const int *attribs, int want_rc)
{
    void *pf = 0;
    int rc = gldChoosePixelFormat(&pf, attribs);
    int ok = (rc == want_rc);
    printf("%-22s -> rc=0x%04x (want 0x%04x) %s\n", name, rc, want_rc, ok?"OK":"FAIL");
    if (pf) {
        gld_pixel_format *p = (gld_pixel_format *)pf;
        printf("   color=%u alpha=%u depth=%u stencil=%u accum=%u aux=%u flags=0x%x\n",
               p->color_size, p->alpha_size, p->depth_size, p->stencil_size,
               p->accum_size, p->aux_buffers, p->flags);
        gldDestroyPixelFormat(pf);
    }
    return ok;
}

int main(void)
{
    int fail = 0;
    /* RGBA, color 32, alpha 8, depth 16, double-buffer, accelerated */
    int typical[] = { 8,32, 11,8, 12,16, 5, 73, 0 };
    /* value token keeps the max: color 16 then 32 -> 32 */
    int maxcolor[] = { 8,16, 8,32, 0 };
    /* unknown boolean token -> rejected like the reference */
    int unknown[]  = { 8,32, 0x60, 0 };
    /* depth clamps to 16 for this hw */
    int deepz[]    = { 12,24, 0 };

    fail |= !check("typical RGBA+depth", typical, 0);
    fail |= !check("max color-size",     maxcolor, 0);
    fail |= !check("unknown token",      unknown, 0x2710);
    fail |= !check("depth clamp 24->16", deepz, 0);
    fail |= (gldChoosePixelFormat(0, typical) != 0x271e);  /* null out */

    printf("\n%s\n", fail ? "*** PF TESTS FAILED ***" : "gldChoosePixelFormat parser OK");
    return fail;
}
