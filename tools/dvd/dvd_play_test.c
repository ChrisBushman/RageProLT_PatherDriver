/* dvd_play_test: drive DVDPlayback directly (no DVD Player GUI): open a VIDEO_TS folder (or a volume holding one), play it into a Carbon window on the main
 * display for N seconds, and print state, title time and errors every second.  Used to see whether the decoder produces frames independent of the GUI.
 * Build: cc -o dvd_play_test dvd_play_test.c -framework Carbon -framework ApplicationServices -framework DVDPlayback
 * Usage: dvd_play_test /Volumes/TESTDVD/VIDEO_TS [seconds]   (run in the GUI session) */
#include <Carbon/Carbon.h>
#include <DVDPlayback/DVDPlayback.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static void ev(DVDEventCode c, UInt32 a, UInt32 b, UInt32 r) { printf("  event %lu: %lu %lu\n", (unsigned long)c, (unsigned long)a, (unsigned long)b); fflush(stdout); }
int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/Volumes/TESTDVD/VIDEO_TS"; int secs = argc > 2 ? atoi(argv[2]) : 15, t;
    FSRef ref; OSStatus s; WindowRef win; Rect r = { 60, 40, 60 + 480, 40 + 720 };
    s = DVDInitialize(); printf("DVDInitialize %ld\n", (long)s);
    s = FSPathMakeRef((const UInt8 *)path, &ref, NULL); printf("FSPathMakeRef %s -> %ld\n", path, (long)s); if (s) return 1;
    Boolean valid = 0; s = DVDIsValidMediaRef(&ref, &valid); printf("DVDIsValidMediaRef -> %ld valid %d\n", (long)s, (int)valid);
    s = CreateNewWindow(kDocumentWindowClass, kWindowStandardDocumentAttributes | kWindowStandardHandlerAttribute, &r, &win); printf("CreateNewWindow %ld\n", (long)s);
    ShowWindow(win);
    CGDirectDisplayID d = CGMainDisplayID(); Boolean sup = 0; s = DVDIsSupportedDisplay(d, &sup); printf("DVDIsSupportedDisplay(main) -> %ld %d\n", (long)s, (int)sup);
    s = DVDSetVideoDisplay(d); printf("DVDSetVideoDisplay %ld\n", (long)s);
    s = DVDSetVideoPort(GetWindowPort(win)); printf("DVDSetVideoPort %ld\n", (long)s);
    { DVDEventCode codes[] = { kDVDEventPlayback, kDVDEventTitleTime, kDVDEventError }; UInt32 id = 0; DVDRegisterEventCallBack(ev, codes, 3, 0, &id); }
    s = DVDOpenMediaFile(&ref); printf("DVDOpenMediaFile %ld\n", (long)s);
    if (s) { s = DVDOpenMediaVolume(&ref); printf("DVDOpenMediaVolume %ld\n", (long)s); }
    UInt16 w = 0, h = 0; DVDGetNativeVideoSize(&w, &h); printf("native video size %ux%u\n", w, h);
    s = DVDPlay(); printf("DVDPlay %ld\n", (long)s);
    for (t = 0; t < secs; t++) {
        DVDState st = 0; DVDTimePosition tp = 0; UInt16 fr = 0; OSStatus e1 = DVDGetState(&st), e2 = DVDGetTime(kDVDTimeCodeElapsedSeconds, &tp, &fr);
        printf("t=%2d state %ld (err %ld) elapsed %ld s (err %ld)\n", t, (long)st, (long)e1, (long)tp, (long)e2); fflush(stdout);
        { int k; for (k = 0; k < 10; k++) { EventRef e; if (ReceiveNextEvent(0, NULL, 0.1, true, &e) == noErr) { SendEventToEventTarget(e, GetEventDispatcherTarget()); ReleaseEvent(e); } DVDIdle(); } }
    }
    DVDCloseMediaFile(); DVDCloseMediaVolume(); DVDDispose(); return 0;
}
