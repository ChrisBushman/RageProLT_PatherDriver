/* stubctl probe|notify : poke RageStubFB via IORegistryEntrySetCFProperties.
 *
 * notify also RESERVES VRAM offset 0 in Apple's VRAM heap.  Reason (found Nov 20 from a kernel panic, docs/re-notes/vertex-dma-plan.md): on a real card the display
 * framebuffer occupies the start of VRAM, so Apple's surface backing never lands at offset 0.  The headless rig has no framebuffer there, so the heap hands out
 * offset 0 to the first surface backing, and the surface-release code (kext 0xd778: `if (record[8] == 0) skip heap_free`) takes offset 0 to mean "never
 * allocated": the record stays linked in the heap list after its surface object is freed, i.e. a dangling node that the next allocation walks (garbage next
 * pointers => data-access panic in alloc_bestfit).  Holding a small block at offset 0 makes every later backing land above 0.
 * The block is intentionally never freed (the next notify re-initialises the heap).  `stubctl notify noguard` skips it. */
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>

static int reserve_offset0(void)
{
    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("ATIRagePro"));
    io_connect_t gl = 0; int i, rc = 1;
    if (!svc || IOServiceOpen(svc, mach_task_self(), 1, &gl)) { printf("guard: cannot open ATIRagePro GL client\n"); return 1; }
    for (i = 0; i < 20; i++) {                               /* the heap is re-initialised asynchronously by the notify: retry briefly */
        uint32_t in[2] = { 4 /* ALLOC_KEEP: owner 0, survives this process (an op-1 block is released when its client closes) */, 0x1000 }, out[2] = { 0, 0 }; size_t osz = 8;
        kern_return_t kr = IOConnectMethodStructureIStructureO(gl, 10, 8, &osz, in, out);
        if (kr == 0 && out[1] && out[0] == 0) { printf("guard: reserved VRAM [0,0x1000) (record 0x%x)\n", out[1]); rc = 0; break; }
        if (kr == 0 && out[1]) {                              /* landed elsewhere: the heap was not fresh (a persistent 4 KB block stays until the next notify re-initialises it) */
            printf("guard: first block at 0x%x, not 0 (heap in use) -- not reserved\n", out[0]); rc = 2; break;
        }
        usleep(100000);
    }
    if (rc == 1) printf("guard: ALLOC failed\n");
    IOServiceClose(gl);
    return rc;
}

int main(int argc, char **argv){
  io_service_t s = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("RageStubFB"));
  int probe = (argc>1 && !strcmp(argv[1],"probe")), edid = (argc>1 && !strcmp(argv[1],"edid")), setmode = (argc>2 && !strcmp(argv[1],"setmode")), dsp = (argc>2 && !strcmp(argv[1],"dsp")), guard = !probe && !edid && !setmode && !dsp && !(argc>2 && !strcmp(argv[2],"noguard")), r;
  const char *k = dsp ? "rage-dsp-apple" : setmode ? "rage-setmode" : probe ? "rage-probe" : edid ? "rage-edid" : "rage-notify";
  CFStringRef key = CFStringCreateWithCString(NULL,k,kCFStringEncodingASCII); int one = (setmode || dsp) ? atoi(argv[2]) : 1;   /* dsp N: 1 = Apple's FIFO formula at the next mode set, 0 = default */   /* setmode N: N = mode index (0 = 640x480, 1 = 800x600, 2 = 1024x768, 3 = 1280x1024); then the stub re-sends the mode-change notifications */ CFNumberRef v = CFNumberCreate(NULL,kCFNumberIntType,&one);
  CFDictionaryRef d = CFDictionaryCreate(NULL,(const void**)&key,(const void**)&v,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
  if(!s){printf("no stub\n");return 1;}
  r = (int)IORegistryEntrySetCFProperties(s,d);
  printf("%s -> 0x%x\n",k,(unsigned)r);
  if (guard && r == 0) { usleep(300000); reserve_offset0(); }
  return 0; }
