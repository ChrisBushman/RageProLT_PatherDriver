/* dvd_display_probe: ask DVDPlayback which active displays it considers DVD-capable (DVDIsSupportedDisplay).
 * Build on the G4: cc -o dvd_display_probe dvd_display_probe.c -framework ApplicationServices -framework DVDPlayback -framework Carbon */
#include <ApplicationServices/ApplicationServices.h>
#include <DVDPlayback/DVDPlayback.h>
#include <stdio.h>
int main(void)
{
    CGDirectDisplayID ids[8]; CGDisplayCount n = 0, i;
    OSStatus s = DVDInitialize(); printf("DVDInitialize -> %ld\n", (long)s);
    CGGetActiveDisplayList(8, ids, &n);
    for (i = 0; i < n; i++) {
        Boolean ok = 0; OSStatus e = DVDIsSupportedDisplay(ids[i], &ok);
        printf("display %lu: %lux%lu main=%d  DVDIsSupportedDisplay -> err %ld supported %d\n", (unsigned long)ids[i],
               (unsigned long)CGDisplayPixelsWide(ids[i]), (unsigned long)CGDisplayPixelsHigh(ids[i]), (int)CGDisplayIsMain(ids[i]), (long)e, (int)ok);
    }
    DVDDispose(); return 0;
}
