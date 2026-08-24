/*
 * gld_pixelformat.c - gldChoosePixelFormat / gldDestroyPixelFormat.
 *
 * OpenGL.framework calls gldChoosePixelFormat(pf_out, attribs) to ask which
 * pixel-format configs our renderer offers; we return a linked list of config
 * nodes.  gliChoosePixelFormat links our list into a combined list (walking to
 * our tail and appending the next renderer), then glcPluginChoosePixelFormat's
 * filter matches each node against the request to compute npix.
 *
 * NODE FORMAT (0x34 bytes / 13 words) -- recovered by dumping a live framework
 * pf list under gdb (tools/capture-node.gdb): the accelerated request enumerated
 * a second renderer whose valid nodes sat right after our (empty) one.  Word[0]
 * is the linked-list next pointer; words[1..12] encode the config.  See the
 * per-field notes in docs/re-notes.  We emit the captured configs verbatim as
 * the configs OUR (accelerated) renderer offers -- the framework's matcher then
 * accepts the one matching the app's request (color 32 / depth 16) -> npix > 0.
 */
#include "gld.h"
#include <stdlib.h>
#include <string.h>

/* Captured valid config nodes (words [1..12]; [0]=next, set at runtime).  From
 * the live pf-list dump -- these are configs the framework's matcher accepts.
 * node4's word[11] (0x2c) held a renderer-private pointer (0xfffaf0) in the
 * capture; we zero it so the framework never derefs a pointer into a renderer
 * that isn't ours. */
#define GLD_NODE_WORDS 13
#define GLD_NNODES     4
/* node[0x08] bit 0x100 = the HARDWARE/ACCELERATED marker: cglChoosePixelFormat's
 * primary scoring loop skips any node without it (andi. 0x100 / beq).  The
 * captured configs came from the software "Generic" renderer so they lack it;
 * we OR it in (0x6dd -> 0x7dd) to mark our configs as accelerated. */
#define GLD_NODE_ACCEL 0x000007dd

/* node[0x04]: bits 16-23 = 0x02 (a gliCreateContext gate), bits 8-15 = RENDERER
 * TYPE CODE gliCreateContext looks up in GLEngine's registry ([renderer+0x104]),
 * bits 0-7 = per-config id.  Runtime trace proved OUR renderer is registered at
 * type 0x1000 (software Generic = 0x200/0x400).  The captured nodes claimed the
 * software 0x200/0x400 -> context creation picked software; stamping type 0x1000
 * (byte 0x10 at bits 8-15) makes gliCreateContext instantiate OUR renderer, so
 * gldCreateContext finally gets called.  (The CGL scorer skips the node[0x04]
 * match when the request doesn't set it, so the winning score is unaffected.) */
static const uint32_t gld_node_templates[GLD_NNODES][GLD_NODE_WORDS] = {
  /* next   [1]  0x04   [2]  0x08         [3] [4] 0x10    [5]0x14     [6] 0x18  [7]0x1c [8]0x20 [9][10][11] [12]0x30 */
  { 0, 0x00021041, GLD_NODE_ACCEL, 0, 0x00000200, 0x00000000, 0x00000400, 0x00000001, 4, 0,0,0, 7 },
  { 0, 0x00021042, GLD_NODE_ACCEL, 0, 0x00000200, 0x00000000, 0x00001000, 0x00000001, 4, 0,0,0, 7 },
  { 0, 0x00021048, GLD_NODE_ACCEL, 0, 0x00004000, 0x00000000, 0x00001000, 0x00000001, 4, 0,0,0, 7 },
  { 0, 0x00021000, GLD_NODE_ACCEL, 0, 0x00008000, 0x20000000, 0x00001000, 0x00000080, 4, 0,0,0, 7 },
};

int gldChoosePixelFormat(void **pf_out, const int *attribs)
{
    uint32_t *node[GLD_NNODES];
    int i;

    gld_log("gldChoosePixelFormat(attribs=%p):", (void*)attribs);
    { int k; for (k=0; attribs && k<48 && attribs[k]; k++) gld_log(" %d", attribs[k]); }
    gld_log(" [end]\n");
    if (!pf_out) return 0x271e;
    *pf_out = 0;

    /* Each node is a SEPARATE malloc: glcAddFormatToCache memcpy's every node
     * into its own cache and then frees our originals one-by-one (free() on each
     * list link) -- a contiguous block gave it mid-block pointers to free, which
     * tripped malloc's "pointer not malloced" guard.  We do NOT free these
     * ourselves (the framework owns them after return -> gldDestroyPixelFormat
     * is a no-op).  0x40 bytes each (>= the 0x34 the framework reads/copies). */
    for (i = 0; i < GLD_NNODES; i++) {
        node[i] = (uint32_t *)calloc(1, 0x40);
        if (!node[i]) return 0x2720;
        memcpy(node[i], gld_node_templates[i], GLD_NODE_WORDS * 4);
    }
    for (i = 0; i < GLD_NNODES - 1; i++)
        node[i][0] = (uint32_t)node[i + 1];   /* PPC32: pointer fits in a word */
    node[GLD_NNODES - 1][0] = 0;               /* our tail (framework appends here) */

    *pf_out = node[0];
    gld_log("  -> ACCEPT: emitted %d captured config nodes (separate allocs), head=%p\n",
            GLD_NNODES, (void *)node[0]);
    return 0;
}

int gldDestroyPixelFormat(void *pf)
{
    /* No-op: glcAddFormatToCache already freed the nodes we returned (it takes
     * ownership after copying them into its cache).  Freeing here would be a
     * double free. */
    (void)pf;
    return 0;
}
