/* mkimg.c out.bmp : 520x8000 busy 24-bit BMP (every screenful differs) for the flush-heavy scroll test. */
#include <stdio.h>
static void le32(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc(v >> 24, f); }
static void le16(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8, f); }
int main(int argc, char **argv)
{
    int w = 520, h = 8000, x, y; unsigned s = 12345, sz = w * 3 * h; FILE *f = fopen(argv[1], "wb");
    fputc('B', f); fputc('M', f); le32(f, 54 + sz); le32(f, 0); le32(f, 54);
    le32(f, 40); le32(f, w); le32(f, h); le16(f, 1); le16(f, 24); le32(f, 0); le32(f, sz); le32(f, 2835); le32(f, 2835); le32(f, 0); le32(f, 0);
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {      /* w*3 = 1560 is a multiple of 4: no row padding */
        s = s * 1664525u + 1013904223u;
        fputc(((x ^ y) + (s >> 20)) & 255, f); fputc((y * 7 + (s >> 24)) & 255, f); fputc((x * 255 / w + (s >> 28)) & 255, f);
    }
    fclose(f); return 0;
}
