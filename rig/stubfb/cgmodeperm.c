/* cgmodeperm ID W H [bits] : switch display ID to WxH PERMANENTLY (what System Preferences does): CGConfigureDisplayMode + CGCompleteDisplayConfiguration(kCGConfigurePermanently).
 * Nothing needs to stay running; the mode survives the process and is stored in the user's display preferences.  Build: cc -o cgmodeperm cgmodeperm.c -framework ApplicationServices */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    if (argc < 4) { printf("usage: cgmodeperm ID W H [bits]\n"); return 2; }
    CGDirectDisplayID d = (CGDirectDisplayID)atoi(argv[1]);
    boolean_t exact; CFDictionaryRef m = CGDisplayBestModeForParameters(d, argc > 4 ? atoi(argv[4]) : 16, atoi(argv[2]), atoi(argv[3]), &exact);
    printf("best mode exact=%d\n", (int)exact); if (!exact) return 1;
    CGDisplayConfigRef cfg; CGError e = CGBeginDisplayConfiguration(&cfg); if (e) { printf("begin %d\n", (int)e); return 1; }
    e = CGConfigureDisplayMode(cfg, d, m); if (e) { printf("configure %d\n", (int)e); CGCancelDisplayConfiguration(cfg); return 1; }
    e = CGCompleteDisplayConfiguration(cfg, kCGConfigurePermanently);
    printf("complete (permanent) -> %d\n", (int)e); return e ? 1 : 0;
}
