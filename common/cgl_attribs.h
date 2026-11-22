/* CGL pixel-format attribute list scanner (host-testable, no dependencies).
 *
 * The attribute list is a zero-terminated array of ints.  Boolean attributes (e.g. kCGLPFADoubleBuffer = 5) are a lone token;
 * attributes that carry a value (kCGLPFADepthSize = 12, ...) are followed by a VALUE word, and that value may legitimately be 0
 * or equal to a token number.  A scan that treats every word as a token (the old code) both stops early at a zero value
 * (`{ kCGLPFAStencilSize, 0, kCGLPFADoubleBuffer, 0 }` lost the double-buffer request) and mistakes a value word equal to 5 for
 * kCGLPFADoubleBuffer.  Token numbers are from Panther's CGLTypes.h. */
#ifndef CGL_ATTRIBS_H
#define CGL_ATTRIBS_H

#define CGL_PFA_DOUBLEBUFFER 5

/* 1 if token `t` is followed by a value word. */
static inline int cgl_attr_has_value(int t)
{
    switch (t) {
    case 7:   /* AuxBuffers */      case 8:   /* ColorSize */      case 11:  /* AlphaSize */
    case 12:  /* DepthSize */       case 13:  /* StencilSize */    case 14:  /* AccumSize */
    case 55:  /* SampleBuffers */   case 56:  /* Samples */        case 70:  /* RendererID */
    case 84:  /* DisplayMask */     case 128: /* VirtualScreenCount */
        return 1;
    default:
        return 0;
    }
}

/* Walk the list (at most `max` words); returns 1 if kCGLPFADoubleBuffer is requested.  If `ntokens` is non-NULL it receives the
 * number of attribute tokens seen (values excluded). */
static inline int cgl_attribs_double_buffer(const int *a, int max, int *ntokens)
{
    int k = 0, want = 0, n = 0;
    while (a && k < max && a[k]) {
        int t = a[k++];
        n++;
        if (t == CGL_PFA_DOUBLEBUFFER) want = 1;
        if (cgl_attr_has_value(t)) k++;          /* skip the value word (may be 0 or any number) */
    }
    if (ntokens) *ntokens = n;
    return want;
}
#endif
