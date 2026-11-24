/* mach64_displays.c - list online displays (id, active/asleep, bounds).  Run in the GUI session.  Build: cc -o mach64_displays mach64_displays.c -framework ApplicationServices */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
int main(void){
 CGDirectDisplayID ids[8]; CGDisplayCount n=0,i; CGGetOnlineDisplayList(8,ids,&n); printf("online=%d\n",(int)n);
 for(i=0;i<n;i++){ CGRect b=CGDisplayBounds(ids[i]); printf(" id=0x%x active=%d asleep=%d mirror=%x main=%d bounds=(%d,%d %dx%d) pix=%lux%lu vendor=%x model=%x\n",(unsigned)ids[i],CGDisplayIsActive(ids[i]),CGDisplayIsAsleep(ids[i]),(unsigned)CGDisplayMirrorsDisplay(ids[i]),CGDisplayIsMain(ids[i]),(int)b.origin.x,(int)b.origin.y,(int)b.size.width,(int)b.size.height,CGDisplayPixelsWide(ids[i]),CGDisplayPixelsHigh(ids[i]),CGDisplayVendorNumber(ids[i]),CGDisplayModelNumber(ids[i])); }
 return 0;}
