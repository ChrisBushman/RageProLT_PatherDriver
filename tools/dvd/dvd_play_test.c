/* dvd_play_test: drive DVDPlayback directly (no DVD Player GUI): open a VIDEO_TS folder, play it into a Carbon window on the main display for N seconds and print
 * state / elapsed time / errors every second.  All calls run from Carbon event-loop timers (DVDOpenMediaFile deadlocks when no loop is running).
 * Build: cc -o dvd_play_test dvd_play_test.c -framework Carbon -framework ApplicationServices -framework DVDPlayback
 * Usage: dvd_play_test /Volumes/TESTDVD/VIDEO_TS [seconds]   (run in the GUI session, DVD Player not running) */
#include <Carbon/Carbon.h>
#include <DVDPlayback/DVDPlayback.h>
#include <stdio.h>
#include <stdlib.h>
static const char *path; static int secs, tick; static WindowRef win; static FSRef ref;
static void ev(DVDEventCode c, UInt32 a, UInt32 b, UInt32 r) { printf("  event %lu: %lu %lu\n", (unsigned long)c, (unsigned long)a, (unsigned long)b); fflush(stdout); }
#define SAY(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)
static void raise_(EventLoopTimerRef t, void *u) { ProcessSerialNumber psn = { 0, kCurrentProcess }; SetFrontProcess(&psn); SelectWindow(win); BringToFront(win); }
static void setup(EventLoopTimerRef t, void *u)
{
    OSStatus s; Boolean valid = 0, sup = 0; CGDirectDisplayID d = CGMainDisplayID(); UInt16 w = 0, h = 0;
    s = DVDInitialize(); SAY("DVDInitialize %ld\n", (long)s);
    s = DVDSetVideoPort(GetWindowPort(win)); SAY("DVDSetVideoPort %ld\n", (long)s);                    /* documented order: port first (loads the video driver bundle) */
    s = DVDSetVideoDevice(GetMainDevice()); SAY("DVDSetVideoDevice(main) %ld\n", (long)s);
    s = DVDIsSupportedDisplay(d, &sup); SAY("DVDIsSupportedDisplay(main) -> %ld %d\n", (long)s, (int)sup);
    s = FSPathMakeRef((const UInt8 *)path, &ref, NULL); SAY("FSPathMakeRef %s -> %ld\n", path, (long)s);
    s = DVDIsValidMediaRef(&ref, &valid); SAY("DVDIsValidMediaRef -> %ld valid %d\n", (long)s, (int)valid);
    { DVDEventCode codes[] = { kDVDEventPlayback, kDVDEventTitleTime, kDVDEventError }; UInt32 id = 0; DVDRegisterEventCallBack(ev, codes, 3, 0, &id); }
    s = DVDOpenMediaFile(&ref); SAY("DVDOpenMediaFile %ld\n", (long)s);
    DVDGetNativeVideoSize(&w, &h); SAY("native video size %ux%u\n", w, h);
    s = DVDPlay(); SAY("DVDPlay %ld\n", (long)s);
}
static void poll(EventLoopTimerRef t, void *u)
{
    DVDState st = 0; DVDTimePosition tp = 0; UInt16 fr = 0; OSStatus e1 = DVDGetState(&st), e2 = DVDGetTime(kDVDTimeCodeElapsedSeconds, &tp, &fr);
    SAY("t=%2d state %ld (err %ld) elapsed %ld s (err %ld)\n", tick, (long)st, (long)e1, (long)tp, (long)e2);
    if (++tick >= secs) QuitApplicationEventLoop();
}
int main(int argc, char **argv)
{
    Rect r = { 60, 40, 60 + 480, 40 + 720 }; EventLoopTimerRef t1, t2;
    path = argc > 1 ? argv[1] : "/Volumes/TESTDVD/VIDEO_TS"; secs = argc > 2 ? atoi(argv[2]) : 15;
    InitCursor();
    { ProcessSerialNumber psn = { 0, kCurrentProcess }; TransformProcessType(&psn, kProcessTransformToForegroundApplication); SetFrontProcess(&psn); }   /* in front of the Terminal that rungui opens */
    CreateNewWindow(kDocumentWindowClass, kWindowStandardDocumentAttributes | kWindowStandardHandlerAttribute, &r, &win); ShowWindow(win); SelectWindow(win); BringToFront(win);
    InstallEventLoopTimer(GetCurrentEventLoop(), 0.5, 0, NewEventLoopTimerUPP(setup), NULL, &t1);
    InstallEventLoopTimer(GetCurrentEventLoop(), 3.0, 1.0, NewEventLoopTimerUPP(poll), NULL, &t2);
    InstallEventLoopTimer(GetCurrentEventLoop(), 2.0, 2.0, NewEventLoopTimerUPP(raise_), NULL, &t2);
    RunApplicationEventLoop();
    DVDCloseMediaFile(); DVDDispose(); SAY("done\n"); return 0;
}
