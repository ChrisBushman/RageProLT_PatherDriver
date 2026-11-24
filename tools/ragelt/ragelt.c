/* tools/ragelt/ragelt.c - read the RageLT driver's event ring.
 *   ragelt info                      ring head/capacity, refused batches, feature bits
 *   ragelt dump                      print every event still held, then exit
 *   ragelt mark N [M]                add a MARK event (line up test phases with driver events)
 *   ragelt daemon FILE [HOST:PORT]   follow the ring: append lines to FILE (fsync'd after every batch of events) and mirror each
 *                                    line over UDP.  Meant to run as root from boot so the last events before a hang are already
 *                                    outside the machine (Panther gives a kext no panic callback).
 * Build (Panther):  cc -O2 -Wall -std=gnu99 -I../../common -o ragelt ragelt.c -framework IOKit -framework CoreFoundation */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "rage_abi.h"

static volatile int stop_flag;
static void on_sig(int s) { (void)s; stop_flag = 1; }

static io_connect_t open_log(void)
{
    io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("RageLTDevice"));
    io_connect_t c = 0; kern_return_t kr;
    if (!svc) { fprintf(stderr, "ragelt: no RageLTDevice (is ATIRageProLT.kext loaded?)\n"); return 0; }
    kr = IOServiceOpen(svc, mach_task_self(), RAGE_CLIENT_LOG, &c);
    IOObjectRelease(svc);
    if (kr) { fprintf(stderr, "ragelt: IOServiceOpen failed 0x%x\n", kr); return 0; }
    return c;
}

static int read_ring(io_connect_t c, uint32_t from, struct rage_log_read_out *o)
{
    size_t sz = sizeof *o;
    kern_return_t kr = IOConnectMethodScalarIStructureO(c, RAGE_LOG_SEL_READ, 1, &sz, from, o);
    if (kr) { fprintf(stderr, "ragelt: read failed 0x%x\n", kr); return -1; }
    return 0;
}

int main(int argc, char **argv)
{
    io_connect_t c; const char *cmd = argc > 1 ? argv[1] : "";
    if (!strcmp(cmd, "info")) {
        uint32_t o[4] = { 0, 0, 0, 0 }; kern_return_t kr; unsigned long long v[4];
        mach_msg_type_number_t n = 4; (void)n; (void)o;
        if (!(c = open_log())) return 1;
        { unsigned int a, b, cc, d; kr = IOConnectMethodScalarIScalarO(c, RAGE_LOG_SEL_INFO, 0, 4, &a, &b, &cc, &d);
          v[0] = a; v[1] = b; v[2] = cc; v[3] = d; }
        if (kr) { fprintf(stderr, "ragelt: info failed 0x%x\n", kr); return 1; }
        printf("head=%llu capacity=%llu refused_batches=%llu features=0x%02llx\n", v[0], v[1], v[2], v[3]);
        return 0;
    }
    if (!strcmp(cmd, "mark")) {
        if (argc < 3 || !(c = open_log())) return argc < 3 ? 2 : 1;
        return IOConnectMethodScalarIScalarO(c, RAGE_LOG_SEL_MARK, 2, 0, (int)strtoul(argv[2], 0, 0), argc > 3 ? (int)strtoul(argv[3], 0, 0) : 0) ? 1 : 0;
    }
    if (!strcmp(cmd, "panic")) {      /* debug builds of the kext only: ragelt panic TOKEN [DELAY_MS] -- marks, waits, then panics the machine ON PURPOSE */
        if (argc < 3 || !(c = open_log())) return argc < 3 ? 2 : 1;
        IOConnectMethodScalarIScalarO(c, RAGE_LOG_SEL_MARK, 2, 0, (int)strtoul(argv[2], 0, 0), 0x70616e63);
        if (argc > 3) usleep((useconds_t)atoi(argv[3]) * 1000);
        return IOConnectMethodScalarIScalarO(c, RAGE_LOG_SEL_PANIC, 1, 0, (int)strtoul(argv[2], 0, 0)) ? 1 : 0;
    }
    if (!strcmp(cmd, "engtest")) {    /* debug builds of the kext only: ragelt engtest [MASK] -- scissor containment self-test (writes engine registers) */
        unsigned int a = 0, b = 0, d = 0, e = 0; kern_return_t kr;
        if (!(c = open_log())) return 1;
        kr = IOConnectMethodScalarIScalarO(c, RAGE_LOG_SEL_ENGTEST, 1, 4, argc > 2 ? (int)strtoul(argv[2], 0, 0) : 0xff, &a, &b, &d, &e);
        printf("engtest: kr=0x%x pass_mask=0x%x outside_changed=%u inside_correct=%u status=%u\n", kr, a, b, d, e);
        return kr ? 1 : 0;
    }
    if (!strcmp(cmd, "dump") || !strcmp(cmd, "daemon")) {
        int daemon_mode = !strcmp(cmd, "daemon"), fd = -1, us = -1; struct sockaddr_in dst; uint32_t next = 0; unsigned long long total_lost = 0;
        if (!(c = open_log())) return 1;
        if (daemon_mode) {
            if (argc < 3) { fprintf(stderr, "usage: ragelt daemon FILE [HOST:PORT]\n"); return 2; }
            fd = open(argv[2], O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) { perror(argv[2]); return 1; }
            if (argc > 3) {
                char host[128]; char *colon; strncpy(host, argv[3], sizeof host - 1); host[sizeof host - 1] = 0; colon = strrchr(host, ':');
                if (!colon) { fprintf(stderr, "ragelt: HOST:PORT expected\n"); return 2; }
                *colon = 0; memset(&dst, 0, sizeof dst); dst.sin_family = AF_INET; dst.sin_port = htons((unsigned short)atoi(colon + 1));
                if (!inet_aton(host, &dst.sin_addr)) { fprintf(stderr, "ragelt: %s is not an IPv4 address\n", host); return 2; }
                us = socket(AF_INET, SOCK_DGRAM, 0);
            }
            signal(SIGTERM, on_sig); signal(SIGINT, on_sig);
        }
        while (!stop_flag) {
            static struct rage_log_read_out o; uint32_t i; int wrote = 0;
            if (read_ring(c, next, &o)) return 1;
            if (o.lost) { char m[80]; int n = snprintf(m, sizeof m, "-- %u event(s) lost before they could be read --\n", o.lost);
                          total_lost += o.lost; if (daemon_mode) write(fd, m, (size_t)n); else fputs(m, stdout); }
            for (i = 0; i < o.count; i++) {
                char line[128]; uint32_t len = rage_log_format(&o.ev[i], line, sizeof line);
                if (!len) continue;
                if (daemon_mode) { write(fd, line, len); if (us >= 0) sendto(us, line, len, 0, (struct sockaddr *)&dst, sizeof dst); wrote = 1; }
                else fwrite(line, 1, len, stdout);
            }
            next += o.lost + o.count;
            if (wrote) fcntl(fd, F_FULLFSYNC, 0);          /* every batch of events reaches the disk before the next poll */
            if (!daemon_mode) { if (o.count == 0) break; continue; }
            if (o.count == 0) usleep(20000);                /* 20 ms poll; a full read (31 events) loops again immediately */
        }
        if (daemon_mode) { fcntl(fd, F_FULLFSYNC, 0); close(fd); }
        (void)total_lost;
        return 0;
    }
    fprintf(stderr, "usage: ragelt info | dump | mark N [M] | panic TOKEN [DELAY_MS] | engtest [MASK] | daemon FILE [HOST:PORT]\n");
    return 2;
}
