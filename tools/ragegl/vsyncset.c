/* tools/ragegl/vsyncset.c - set the kext's v-sync mode for experiments: vsyncset 0|1|3  (0 off, 1 poll, 3 sleep-then-spin = the default).  Device-global; restore with 3. */
#include "ragegl.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    ragegl_t c; if (argc < 2 || ragegl_open(&c)) { printf("usage: vsyncset 0|1|3\n"); return 2; }
    printf("config(9, %d) -> %d\n", atoi(argv[1]), (int)ragegl_config(c, 9, (uint32_t)atoi(argv[1]))); ragegl_close(c); return 0;
}
