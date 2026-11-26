#include <libkern/OSByteOrder.h>
#include <mach/mach_types.h>
#include <ppc/proc_reg.h>
#include <IOKit/IOLib.h>
extern "C" int snprintf(char *, unsigned long, const char *, ...);
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
enum { kRegBlock = 0x400 };
class RageDdcTest : public IOService {
    OSDeclareDefaultStructors(RageDdcTest)
    IOPCIDevice *fPCI; IOMemoryMap *fRegMap; volatile UInt8 *fRegs;
    bool mapRegisters(void); UInt32 rd(UInt32 r); void wr(UInt32 r, UInt32 v);
    void ddcSet(int clk, int dat); int ddcClk(void); int ddcDat(void); bool ddcStart(void); void ddcStop(void);
    bool ddcPutByte(UInt8 b); UInt8 ddcGetByte(bool ack); bool readEdid(UInt8 *buf, int blockNo); void probeEdid(void); void ddcDiag(void); void ddcSample(void); void ddcScan(void);
public: virtual bool start(IOService *provider);
};
#define super IOService
OSDefineMetaClassAndStructors(RageDdcTest, IOService)
bool RageDdcTest::mapRegisters(void)
{
    if (fRegs) return true;
    IODeviceMemory *bar0 = fPCI ? fPCI->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0) : 0;
    IODeviceMemory *sub = bar0 ? IODeviceMemory::withSubRange(bar0, 0x7ff800, 0x800) : 0;
    if (!sub) return false;
    fRegMap = sub->map(kIOMapInhibitCache); sub->release();
    if (!fRegMap) return false;
    fRegs = (volatile UInt8 *)fRegMap->getVirtualAddress(); return fRegs != 0;
}
UInt32 RageDdcTest::rd(UInt32 reg) { return OSReadLittleInt32((volatile void *)fRegs, kRegBlock + reg); }
void RageDdcTest::wr(UInt32 reg, UInt32 val) { OSWriteLittleInt32((volatile void *)fRegs, kRegBlock + reg, val); eieio(); }
/* ---- DDC2B: read the monitor's EDID over I2C bit-banged on GP_IO (block offset 0x78) --------------------------------------------------------
 * Spec (RAGE PRO GCS 4.4/6.13, RAGE LT PRO RRG GP_IO 0_1E): DDC serial data = GIO0, serial clock = GIO4; GP_IO bit n is the pin level, bit 16+n its
 * direction (1 = output).  Open-drain emulation: a line is driven low by direction=1/value=0 and released (high, pulled up by the monitor
 * cable) by direction=0.  Only bits 0, 4, 16 and 20 are touched; the original value is restored afterwards. */
enum { kGpIo = 0x78, kDdcDat = 1u << 0, kDdcClk = 1u << 4, kDdcDatDir = 1u << 16, kDdcClkDir = 1u << 20 };
#define DDC_US 10
void RageDdcTest::ddcSet(int clk, int dat)
{
    UInt32 v = rd(kGpIo) & ~(kDdcDat | kDdcClk | kDdcDatDir | kDdcClkDir);
    v |= kDdcClk | kDdcDat;                       /* latches 1: a released line follows the pin */
    if (!clk) v = (v & ~kDdcClk) | kDdcClkDir;
    if (!dat) v = (v & ~kDdcDat) | kDdcDatDir;
    wr(kGpIo, v);
    IODelay(DDC_US);
    if (clk) for (int i = 0; i < 500 && !(rd(kGpIo) & kDdcClk); i++) IODelay(10);   /* clock stretching */
}
int RageDdcTest::ddcClk(void) { return (rd(kGpIo) & kDdcClk) != 0; }
int RageDdcTest::ddcDat(void) { return (rd(kGpIo) & kDdcDat) != 0; }
bool RageDdcTest::ddcStart(void)
{
    ddcSet(1, 1);
    ddcSet(1, 0); ddcSet(0, 0);
    return true;
}
void RageDdcTest::ddcStop(void) { ddcSet(0, 0); ddcSet(1, 0); ddcSet(1, 1); }
bool RageDdcTest::ddcPutByte(UInt8 b)
{
    for (int i = 7; i >= 0; i--) { int bit = (b >> i) & 1; ddcSet(0, bit); ddcSet(1, bit); ddcSet(0, bit); }
    ddcSet(0, 1); ddcSet(1, 1);
    int ack = !ddcDat();                                           /* the monitor pulls data low to acknowledge */
    ddcSet(0, 1);
    return ack != 0;
}
UInt8 RageDdcTest::ddcGetByte(bool ack)
{
    UInt8 v = 0;
    ddcSet(0, 1);
    for (int i = 0; i < 8; i++) { ddcSet(1, 1); v = (UInt8)((v << 1) | ddcDat()); ddcSet(0, 1); }
    ddcSet(0, ack ? 0 : 1); ddcSet(1, ack ? 0 : 1); ddcSet(0, ack ? 0 : 1);
    ddcSet(0, 1);
    return v;
}
/* one 128-byte EDID block (blockNo 0 = base block) from I2C address 0x50; true when it read back with a valid header and checksum */
bool RageDdcTest::readEdid(UInt8 *buf, int blockNo)
{
    if (!mapRegisters()) return false;
    UInt32 save = rd(kGpIo);
    bool ok = false;
    for (int i = 0; i < 9; i++) { ddcSet(0, 1); ddcSet(1, 1); }    /* bus recovery: nine clocks with data released, then a STOP */
    ddcStop(); IODelay(2000);
    IOLog("RageDdcTest: after recovery gp %08x\n", (unsigned)rd(kGpIo));
    {
        bool s1 = ddcStart(); UInt32 g1 = rd(kGpIo);
        bool a1 = s1 && ddcPutByte(0xa0); UInt32 g2 = rd(kGpIo);
        bool a2 = a1 && ddcPutByte((UInt8)(blockNo * 128)); UInt32 g3 = rd(kGpIo);
        bool s2 = a2 && ddcStart(); UInt32 g4 = rd(kGpIo);
        bool a3 = s2 && ddcPutByte(0xa1); UInt32 g5 = rd(kGpIo);
        IOLog("RageDdcTest: DDC trace: start %d (gp %08x) addrW ack %d (gp %08x) offset ack %d (gp %08x) restart %d (gp %08x) addrR ack %d (gp %08x)\n",
              s1, (unsigned)g1, a1, (unsigned)g2, a2, (unsigned)g3, s2, (unsigned)g4, a3, (unsigned)g5);
        if (a3) { for (int i = 0; i < 128; i++) buf[i] = ddcGetByte(i != 127); ok = true; }
    }
    ddcStop();
    wr(kGpIo, (rd(kGpIo) & ~(kDdcDat | kDdcClk | kDdcDatDir | kDdcClkDir)) | (save & (kDdcDat | kDdcClk | kDdcDatDir | kDdcClkDir)));
    if (!ok) return false;
    static const UInt8 hdr[8] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
    UInt8 sum = 0;
    for (int i = 0; i < 128; i++) sum = (UInt8)(sum + buf[i]);
    if (blockNo == 0) for (int i = 0; i < 8; i++) if (buf[i] != hdr[i]) return false;
    return sum == 0;
}

/* diagnostic: how do the GP_IO pin bits behave?  (value/direction combinations on the DDC lines, readback after each) */
void RageDdcTest::ddcDiag(void)
{
    if (!mapRegisters()) return;
    UInt32 save = rd(kGpIo);
    static const UInt32 bits[] = { 0, 4, 12 };      /* GIO0 = DDC data, GIO4 = DDC clock, GIOC = MONID0 */
    for (int k = 0; k < 3; k++) {
        UInt32 b = 1u << bits[k], d = b << 16, base = save & ~(b | d);
        UInt32 st[5];
        wr(kGpIo, base | b);            IODelay(50); st[0] = rd(kGpIo);   /* input, latch 1 */
        wr(kGpIo, base);                IODelay(50); st[1] = rd(kGpIo);   /* input, latch 0 */
        wr(kGpIo, base | d | b);        IODelay(50); st[2] = rd(kGpIo);   /* output high */
        wr(kGpIo, base | d);            IODelay(50); st[3] = rd(kGpIo);   /* output low */
        wr(kGpIo, base | b);            IODelay(50); st[4] = rd(kGpIo);   /* input again */
        IOLog("RageDdcTest: GP_IO bit %d: in/1 %08x  in/0 %08x  out/1 %08x  out/0 %08x  in/1 %08x\n", (int)bits[k], (unsigned)st[0], (unsigned)st[1], (unsigned)st[2], (unsigned)st[3], (unsigned)st[4]);
        wr(kGpIo, save);
    }
    IOLog("RageDdcTest: GP_IO restored %08x (was %08x)\\n", (unsigned)rd(kGpIo), (unsigned)save);
}

void RageDdcTest::probeEdid(void)
{
    if (!mapRegisters()) return;
    ddcDiag();
    IOLog("RageDdcTest: GP_IO before DDC = 0x%08x\n", (unsigned)rd(kGpIo));
    UInt8 e[128];
    bool ok = false;
    for (int t = 0; t < 3 && !ok; t++) ok = readEdid(e, 0);
    IOLog("RageDdcTest: GP_IO after DDC = 0x%08x, EDID %s\n", (unsigned)rd(kGpIo), ok ? "read, header and checksum valid" : "NOT read (no ack, bad header or bad checksum)");
    if (!ok) return;
    for (int r = 0; r < 8; r++)
        IOLog("RageDdcTest: EDID %02x: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n", r * 16,
              e[r*16+0], e[r*16+1], e[r*16+2], e[r*16+3], e[r*16+4], e[r*16+5], e[r*16+6], e[r*16+7],
              e[r*16+8], e[r*16+9], e[r*16+10], e[r*16+11], e[r*16+12], e[r*16+13], e[r*16+14], e[r*16+15]);
}


/* DDC1 check: both lines released, sample SDA for 1 s; a DDC1 monitor changes it at the vertical-sync rate (one bit per VSYNC) */
void RageDdcTest::ddcSample(void)
{
    if (!mapRegisters()) return;
    UInt32 save = rd(kGpIo);
    wr(kGpIo, save & ~(kDdcDat | kDdcClk | kDdcDatDir | kDdcClkDir));
    IODelay(1000);
    int trans = 0, last = ddcDat(), run = 0, shown = 0, ones = 0, n = 40000;
    char line[160]; int pos = 0;
    for (int i = 0; i < n; i++) {
        IODelay(25);
        int v = ddcDat(); ones += v; run++;
        if (v != last) { trans++; if (shown < 12) { pos += snprintf(line + pos, sizeof line - pos, "%d:%dus ", last, run * 25); shown++; } run = 0; last = v; }
    }
    IOLog("RageDdcTest: DDC1 sample 1 s: %d transitions, SDA high %d%% ; first runs %s\n", trans, ones * 100 / n, line);
    wr(kGpIo, save);
}

void RageDdcTest::ddcScan(void)
{
    if (!mapRegisters()) return;
    UInt32 save = rd(kGpIo);
    char line[200]; int pos = 0, found = 0;
    ddcSet(1, 1); ddcStop();
    for (int a = 0; a < 128; a++) {
        ddcStart();
        bool ack = ddcPutByte((UInt8)(a << 1));
        ddcStop();
        if (ack) { found++; pos += snprintf(line + pos, sizeof line - pos, "0x%02x ", a); }
    }
    IOLog("RageDdcTest: I2C scan on GIO0/GIO4: %d address(es) answered %s\n", found, line);
    wr(kGpIo, save);
}

bool RageDdcTest::start(IOService *provider)
{
    fPCI = OSDynamicCast(IOPCIDevice, provider); fRegMap = 0; fRegs = 0;
    if (!fPCI) return false;
    IOLog("RageDdcTest: start\n");
    ddcScan();
    if (fRegMap) { fRegMap->release(); fRegMap = 0; }
    return false;      /* decline: nothing stays attached, so the kext can be unloaded and reloaded without a reboot */
}
