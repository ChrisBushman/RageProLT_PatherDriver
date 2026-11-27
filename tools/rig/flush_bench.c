/* flush_bench.c [frames [delay_us]] - B1g-5: redraw a 600x400 window with different content every frame and flush it, so WindowServer must copy the window backing store
 * into the Rage framebuffer with the CPU (a solid fill would go to the 2D engine).  Prints frames/s and WindowServer's CPU time.  Run in the GUI session.
 * Build: cc -o flush_bench flush_bench.c -framework Carbon -framework ApplicationServices */
#include <Carbon/Carbon.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec + t.tv_usec * 1e-6; }
int main(int argc, char **argv)
{
    int frames = argc > 1 ? atoi(argv[1]) : 100, delay_us = argc > 2 ? atoi(argv[2]) : 0, f, i; WindowRef win = NULL; Rect r = { 30, 10, 430, 610 }; double t0, t1; char cmd[160]; FILE *p; char before[32] = "", after[32] = "";
    if (CreateNewWindow(kPlainWindowClass, kWindowNoShadowAttribute, &r, &win) != noErr || !win) { fprintf(stderr, "CreateNewWindow failed\n"); return 2; }
    ShowWindow(win); SetPortWindowPort(win);
    for (i = 0; i < 10; i++) { EventRef e; if (ReceiveNextEvent(0, NULL, 0.05, true, &e) == noErr) ReleaseEvent(e); }
    snprintf(cmd, sizeof cmd, "ps -o time= -p `ps auxww | grep '[W]indowServer' | grep -v ws_patcher | awk '{print $2}' | head -1`");
    p = popen(cmd, "r"); if (p) { fgets(before, sizeof before, p); pclose(p); }
    { CGDirectDisplayID d[4]; CGDisplayCount n = 0, k; CGRect wr = CGRectMake(10, 30, 600, 400); CGGetDisplaysWithRect(wr, 4, d, &n);
      for (k = 0; k < n; k++) printf("window on display %u: %lux%lu, %lu bpp, bounds origin (%d,%d)\n", (unsigned)d[k], CGDisplayPixelsWide(d[k]), CGDisplayPixelsHigh(d[k]), CGDisplayBitsPerPixel(d[k]), (int)CGDisplayBounds(d[k]).origin.x, (int)CGDisplayBounds(d[k]).origin.y); }
    t0 = now();
    for (f = 0; f < frames; f++) {
        for (i = 0; i < 40; i++) {
            RGBColor c; Rect s; c.red = (unsigned short)((f * 2111 + i * 6007) & 0xffff); c.green = (unsigned short)((f * 3331 + i * 1999) & 0xffff); c.blue = (unsigned short)((f * 777 + i * 4001) & 0xffff);
            RGBForeColor(&c); s.left = 0; s.right = 600; s.top = (short)(i * 10); s.bottom = (short)(i * 10 + 10); PaintRect(&s);
        }
        QDFlushPortBuffer(GetWindowPort(win), NULL);
        if (delay_us) usleep(delay_us);   /* paced: WindowServer finishes each flush before the next, so no coalescing */
    }
    t1 = now();
    p = popen(cmd, "r"); if (p) { fgets(after, sizeof after, p); pclose(p); }
    { char *nl; if ((nl = strchr(before, '\n'))) *nl = 0; if ((nl = strchr(after, '\n'))) *nl = 0; }
    printf("%d frames in %.2f s = %.1f fps (240000 px/frame); WindowServer cpu %s -> %s\n", frames, t1 - t0, frames / (t1 - t0), before, after);
    DisposeWindow(win);
    return 0;
}
