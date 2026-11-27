/* tools/ragegl/ga_probe.c - does IOKit find and start the stock ATIRageProGA.plugin for the Rage framebuffer (the lookup IOPSAllocateBlitEngine makes)?
 * usage: ga_probe [framebuffer class, default RageStubFB].  Creates the plug-in interface from the framebuffer's IOCFPlugInTypes, calls Start (which opens the
 * type-2 client on our accelerator after the conformance gate) and then Stop/Release.  Read-only apart from the client open/close. */
#include <stdio.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <CoreFoundation/CoreFoundation.h>

int main(int argc, char **argv)
{
    mach_port_t master; io_service_t fb; IOCFPlugInInterface **plug = 0; SInt32 score = 0; kern_return_t kr; HRESULT hr;
    CFUUIDRef type = CFUUIDGetConstantUUIDWithBytes(NULL, 0xAC, 0xCF, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x0A, 0x27, 0x89, 0x90, 0x4E);
    CFUUIDRef iface = CFUUIDGetConstantUUIDWithBytes(NULL, 0x67, 0x66, 0xE9, 0x4A, 0, 0, 0, 0, 0, 0, 0x00, 0x0A, 0x27, 0x89, 0x90, 0x4E);   /* kIOGraphicsAcceleratorInterfaceID */
    IOMasterPort(MACH_PORT_NULL, &master);
    fb = IOServiceGetMatchingService(master, IOServiceMatching(argc > 1 ? argv[1] : "RageStubFB"));
    if (!fb) { printf("no framebuffer service\n"); return 2; }
    kr = IOCreatePlugInInterfaceForService(fb, type, iface, &plug, &score);
    printf("IOCreatePlugInInterfaceForService: 0x%x score %d plug %p\n", kr, (int)score, (void *)plug);
    if (kr || !plug) return 1;
    hr = 0; printf("plug-in interface obtained: IOCreatePlugInInterfaceForService already ran Probe and Start\n");
    printf("Start: 0x%x\n", (unsigned)hr);
    if (hr == 0) { printf("plugin started; press nothing, stopping\n");  }
    IODestroyPlugInInterface(plug);
    return hr ? 1 : 0;
}
