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
static const uint32_t gld_node_templates[GLD_NNODES][GLD_NODE_WORDS] = {
  /* next   [1]  0x04   [2]  0x08   [3] [4] 0x10    [5]0x14     [6] 0x18  [7]0x1c [8]0x20 [9][10][11] [12]0x30 */
  { 0, 0x00020241, 0x000006dd, 0, 0x00000200, 0x00000000, 0x00000400, 0x00000001, 4, 0,0,0, 7 },
  { 0, 0x00020242, 0x000006dd, 0, 0x00000200, 0x00000000, 0x00001000, 0x00000001, 4, 0,0,0, 7 },
  { 0, 0x00020248, 0x000006dd, 0, 0x00004000, 0x00000000, 0x00001000, 0x00000001, 4, 0,0,0, 7 },
  { 0, 0x00020400, 0x000006dd, 0, 0x00008000, 0x20000000, 0x00001000, 0x00000080, 4, 0,0,0, 7 },
};

int gldChoosePixelFormat(void **pf_out, const int *attribs)
{
    char *blk;
    uint32_t *node[GLD_NNODES];
    int i;

    gld_log("gldChoosePixelFormat(attribs=%p):", (void*)attribs);
    { int k; for (k=0; attribs && k<48 && attribs[k]; k++) gld_log(" %d", attribs[k]); }
    gld_log(" [end]\n");
    if (!pf_out) return 0x271e;
    *pf_out = 0;

    /* One contiguous block of GLD_NNODES nodes (0x40 each, padded past the 0x34
     * the framework reads) so gldDestroyPixelFormat frees it with the head ptr.
     * The framework only rewrites our TAIL's next (to append the next renderer);
     * our internal chain stays within the block. */
    blk = (char *)calloc(GLD_NNODES, 0x40);
    if (!blk) return 0x2720;
    for (i = 0; i < GLD_NNODES; i++) {
        node[i] = (uint32_t *)(blk + i * 0x40);
        memcpy(node[i], gld_node_templates[i], GLD_NODE_WORDS * 4);
    }
    for (i = 0; i < GLD_NNODES - 1; i++)
        node[i][0] = (uint32_t)node[i + 1];   /* PPC32: pointer fits in a word */
    node[GLD_NNODES - 1][0] = 0;               /* our tail (framework appends here) */

    *pf_out = blk;
    gld_log("  -> ACCEPT: emitted %d captured config nodes, head=%p\n",
            GLD_NNODES, (void *)blk);
    return 0;
}

int gldDestroyPixelFormat(void *pf)
{
    /* Head pointer is the block base; one free releases all our nodes.  (The
     * framework's appended nodes belong to the other renderer -- not ours to
     * free -- and live outside this block, so we never touch them.) */
    if (pf) free(pf);
    return 0;
}
