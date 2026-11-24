/* mach64_wake.c - wake sleeping displays: declare user activity AND post a synthetic mouse move through the HID system (the MDD's displays
 * sleep when idle).  Run in the GUI session.  Build: cc -o mach64_wake mach64_wake.c -framework ApplicationServices -framework CoreServices */
#include <ApplicationServices/ApplicationServices.h>
#include <CoreServices/CoreServices.h>
#include <stdio.h>
#include <unistd.h>
int main(void)
{
    int i; CGPoint p;
    UpdateSystemActivity(UsrActivity);
    for (i = 0; i < 6; i++) {
        p.x = 400 + (i & 1) * 20; p.y = 300 + (i & 1) * 20;
        CGPostMouseEvent(p, true, 1, false);
        UpdateSystemActivity(UsrActivity);
        usleep(200000);
    }
    printf("activity + mouse moves posted\n");
    return 0;
}
