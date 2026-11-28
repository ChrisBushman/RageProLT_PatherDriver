/* gl_yuv_tap: flat-namespace interposer (DYLD_FORCE_FLAT_NAMESPACE=1 DYLD_INSERT_LIBRARIES=./gl_yuv_tap.dylib) that logs the OpenGL texture uploads of
 * AppleAltiVecDVDDriver and, with TAP_DIR set, dumps every GL_YCBCR_422_APPLE rectangle upload (the decoded picture) to TAP_DIR/fNNNN.yuv. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <OpenGL/gl.h>
typedef void (*teximg_t)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
typedef void (*subimg_t)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *);
static int nlog, nframes; static FILE *lf; static int rowlen, skippx, skiprow;
/* the real GL entry points come from the OpenGL framework image itself (the Tiger stack when DYLD_FRAMEWORK_PATH points at it); RTLD_NEXT would find these wrappers again under a flat namespace */
static void *real(const char *name)
{
    static const struct mach_header *img; char p[512], sym[128]; NSSymbol ns;
    if (!img) { const char *fp = getenv("DYLD_FRAMEWORK_PATH"); snprintf(p, sizeof p, "%s%s", fp ? fp : "/System/Library/Frameworks", "/OpenGL.framework/Libraries/libGL.dylib"); img = NSAddImage(p, NSADDIMAGE_OPTION_RETURN_ON_ERROR); }
    if (!img) return NULL;
    snprintf(sym, sizeof sym, "_%s", name);
    ns = NSLookupSymbolInImage(img, sym, NSLOOKUPSYMBOLINIMAGE_OPTION_BIND_NOW | NSLOOKUPSYMBOLINIMAGE_OPTION_RETURN_ON_ERROR);
    return ns ? NSAddressOfSymbol(ns) : NULL;
}
static void note(const char *what, GLenum tgt, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, const GLvoid *px)
{
    const char *dir = getenv("TAP_DIR");
    if (!lf) { const char *lp = getenv("TAP_LOG"); lf = fopen(lp ? lp : "/dev/null", "a"); }
    if (lf && nlog < 400) { fprintf(lf, "%s tgt=%x x=%d y=%d %dx%d fmt=%x type=%x px=%p rowlen=%d\n", what, tgt, (int)x, (int)y, (int)w, (int)h, fmt, type, px, rowlen); fflush(lf); nlog++; }
    if (dir && px && fmt == 0x85B9 && tgt == 0x84F5 && nframes < 400) {
        /* snapshot the PREVIOUS upload's buffer now (about one frame time later): the decoder may still be finishing the second field of a picture when the texture upload is issued */
        static const void *prev; static size_t prevsz;
        if (prev) { char fn[512]; FILE *f; snprintf(fn, sizeof fn, "%s/f%04d.yuv", dir, nframes++); f = fopen(fn, "wb"); if (f) { fwrite(prev, 1, prevsz, f); fclose(f); } }
        prev = px; prevsz = (size_t)(rowlen ? rowlen : w) * h * 2;
    }
}
void glTexImage2D(GLenum t, GLint l, GLenum ifmt, GLsizei w, GLsizei h, GLint b, GLenum fmt, GLenum type, const GLvoid *px)
{ static teximg_t r; if (!r) r = (teximg_t)real("glTexImage2D"); note("TexImage2D", t, 0, 0, w, h, fmt, type, px); if (r && (void *)r != (void *)glTexImage2D) r(t, l, ifmt, w, h, b, fmt, type, px); }
void glTexSubImage2D(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, const GLvoid *px)
{ static subimg_t r; if (!r) r = (subimg_t)real("glTexSubImage2D"); note("TexSubImage2D", t, x, y, w, h, fmt, type, px); if (r && (void *)r != (void *)glTexSubImage2D) r(t, l, x, y, w, h, fmt, type, px); }

typedef void (*ps_t)(GLenum, GLint);
void glPixelStorei(GLenum pname, GLint param)
{ static ps_t r; if (!r) r = (ps_t)real("glPixelStorei");
  if (pname == 0x0CF2) rowlen = param;   /* GL_UNPACK_ROW_LENGTH */
  { const char *lp = getenv("TAP_LOG"); if (lp && nlog < 400) { FILE *f = fopen(lp, "a"); if (f) { fprintf(f, "PixelStorei %x %d\n", pname, (int)param); fclose(f); } } }
  if (r && (void *)r != (void *)glPixelStorei) r(pname, param); }
