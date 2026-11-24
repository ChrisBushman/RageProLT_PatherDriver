/* ws_patcher.c - work around a Panther 10.3.9 CoreGraphics bug that corrupts WindowServer's heap (see docs/re-notes/windowserver-cursor-doublefree.md).
 *
 * CoreGraphics setFrame() (0x936e8220), default case for a display whose cursor depth class is 0 or 5 (a framebuffer with no mode, e.g. the
 * second head of the GeForce 7800 GS with nothing attached), does  mr r3,r4 ; bl CGSFreeConversionBitmap  on the CALLER's bitmap: it frees the
 * shared converted-cursor buffer and a stack copy of the header, and MPSetCursor then frees the same buffer again.  Every display
 * reconfiguration (and other cursor rebuilds) then corrupts the heap and crashes WindowServer.
 *
 * This replaces that one bl (at 0x936e86e0) with a nop in the running process, only if the word there is exactly the expected bl, so a
 * different CoreGraphics build is left alone.  It polls until the framework is mapped, so it can be started before the target exec()s
 * WindowServer.  Nothing on disk is changed.
 *
 *   ws_patcher PID [seconds]         patch one process.  exit 0 = patched or already patched; 2 = unexpected instruction (not patched); 1 = gave up
 *   ws_patcher -name NAME [seconds]   watch the process list and patch every process whose name is NAME (default WindowServer) exactly once,
 *                                     until the time is up.  WindowServer forks into a -daemon child, so the PID known at launch is not the
 *                                     one that keeps running; this mode follows it.  exit 0 if at least one process was patched.
 * Build on Panther: cc -o ws_patcher ws_patcher.c     (run as root) */
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#define PATCH_ADDR  0x936e86e0u
#define WORD_BL     0x4bffca0du   /* bl 0x936e50ec <CGSFreeConversionBitmap> */
#define WORD_NOP    0x60000000u

/* 0 = patched or already patched, 2 = unexpected word (left alone), 1 = not possible yet (task or memory not available) */
static int patch_pid(int pid, int quiet)
{
    mach_port_t task = 0; kern_return_t kr; unsigned int w = 0; vm_size_t sz = 4;
    if ((kr = task_for_pid(mach_task_self(), pid, &task)) != KERN_SUCCESS) return 1;
    kr = vm_read_overwrite(task, PATCH_ADDR, 4, (vm_address_t)&w, &sz);
    if (kr != KERN_SUCCESS || sz != 4) return 1;
    if (w == WORD_NOP) { if (!quiet) printf("ws_patcher: pid %d already patched\n", pid); return 0; }
    if (w != WORD_BL)  { printf("ws_patcher: pid %d: unexpected word 0x%08x at 0x%08x - NOT patched\n", pid, w, PATCH_ADDR); return 2; }
    {
        unsigned int nop = WORD_NOP, back = 0; vm_address_t page = PATCH_ADDR & ~0xfffu;
        kr = vm_protect(task, page, 0x1000, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
        if (kr == KERN_SUCCESS) kr = vm_write(task, PATCH_ADDR, (vm_offset_t)&nop, 4);
        vm_protect(task, page, 0x1000, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
        sz = 4; vm_read_overwrite(task, PATCH_ADDR, 4, (vm_address_t)&back, &sz);
        if (kr == KERN_SUCCESS && back == WORD_NOP) { printf("ws_patcher: pid %d patched (0x%08x -> 0x%08x at 0x%08x)\n", pid, w, back, PATCH_ADDR); return 0; }
        printf("ws_patcher: pid %d write failed (kr=%d, read back 0x%08x)\n", pid, (int)kr, back);
        return 1;
    }
}

/* pids of processes whose p_comm equals name */
static int find_pids(const char *name, int *pids, int max)
{
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 }, n = 0, i; size_t len = 0; struct kinfo_proc *kp;
    if (sysctl(mib, 3, NULL, &len, NULL, 0) < 0 || !len) return 0;
    len += len / 4 + 4 * sizeof *kp; kp = (struct kinfo_proc *)malloc(len);
    if (!kp) return 0;
    if (sysctl(mib, 3, kp, &len, NULL, 0) < 0) { free(kp); return 0; }
    for (i = 0; i < (int)(len / sizeof *kp) && n < max; i++)
        if (!strncmp(kp[i].kp_proc.p_comm, name, MAXCOMLEN)) pids[n++] = kp[i].kp_proc.p_pid;
    free(kp);
    return n;
}

int main(int argc, char **argv)
{
    time_t t0 = time(0); int secs;
    if (argc > 2 && !strcmp(argv[1], "-name")) {
        const char *name = argv[2]; int done[64], ndone = 0, patched = 0, pids[64], n, i, j;
        secs = argc > 3 ? atoi(argv[3]) : 120;
        while (time(0) - t0 < secs) {
            n = find_pids(name, pids, 64);
            for (i = 0; i < n; i++) {
                for (j = 0; j < ndone; j++) if (done[j] == pids[i]) break;
                if (j < ndone || pids[i] == (int)getpid()) continue;
                j = patch_pid(pids[i], 1);
                if (j == 0 || j == 2) { if (ndone < 64) done[ndone++] = pids[i]; if (j == 0) patched++; }
            }
            usleep(50000);
        }
        printf("ws_patcher: watch of '%s' ended after %d s, %d process(es) handled\n", name, secs, ndone);
        return ndone && patched ? 0 : 1;
    }
    {
        int pid = argc > 1 ? atoi(argv[1]) : 0, rc;
        secs = argc > 2 ? atoi(argv[2]) : 120;
        if (pid <= 0) { fprintf(stderr, "usage: ws_patcher PID [seconds] | ws_patcher -name NAME [seconds]\n"); return 1; }
        for (;;) {
            rc = patch_pid(pid, 0);
            if (rc != 1) return rc;
            if (time(0) - t0 >= secs) { printf("ws_patcher: pid %d gave up after %d s\n", pid, secs); return 1; }
            usleep(50000);
        }
    }
}
