/* mach64_peekproc.c -- read 32-bit words from another process WITHOUT stopping it (replaces `gdb -batch` attach, which kills the target
 * when any command in the script fails).
 *
 *   cc -o mach64_peekproc mach64_peekproc.c
 *   ./mach64_peekproc <pid> <addr>[+<off>][:<count>] ...
 *       addr / off in hex; '*' before addr dereferences it first:  *0x8e9124+0xe0  = word at (word at 0x8e9124) + 0xe0
 *
 * Prints "<expr> = 0x... 0x..." per argument, or an error for an unreadable address.  Same-user processes only (task_for_pid). */
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int rd(task_t t, vm_address_t a, uint32_t *v)
{
    vm_size_t got = 0;
    return vm_read_overwrite(t, a, 4, (vm_address_t)v, &got) == KERN_SUCCESS && got == 4 ? 0 : -1;
}

int main(int argc, char **argv)
{
    task_t t;
    int i;
    if (argc < 3) { fprintf(stderr, "usage: %s pid [*]addr[+off][:count] ...\n", argv[0]); return 1; }
    if (task_for_pid(mach_task_self(), atoi(argv[1]), &t) != KERN_SUCCESS) { fprintf(stderr, "task_for_pid failed\n"); return 1; }
    for (i = 2; i < argc; i++) {
        char buf[128], *p = buf, *plus, *colon;
        int deref = 0, n = 1, k;
        uint32_t a, off = 0, v;
        strncpy(buf, argv[i], sizeof buf - 1); buf[sizeof buf - 1] = 0;
        if (*p == '*') { deref = 1; p++; }
        if ((colon = strchr(p, ':'))) { *colon = 0; n = atoi(colon + 1); if (n < 1) n = 1; if (n > 64) n = 64; }
        if ((plus = strchr(p, '+'))) { *plus = 0; off = (uint32_t)strtoul(plus + 1, 0, 16); }
        a = (uint32_t)strtoul(p, 0, 16);
        if (deref) { if (rd(t, a, &v)) { printf("%s = <unreadable 0x%x>\n", argv[i], a); continue; } a = v; }
        a += off;
        printf("%s @0x%x =", argv[i], a);
        for (k = 0; k < n; k++) { if (rd(t, a + 4u * k, &v)) { printf(" <unreadable>"); break; } printf(" 0x%08x", v); }
        printf("\n");
    }
    return 0;
}
