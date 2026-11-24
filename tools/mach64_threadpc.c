/* mach64_threadpc.c -- print every thread's PC / LR / run state of another process, N times, without suspending it.
 * For "where is this hung app spinning?" when gdb/sample would stop (or kill) it.
 *
 *   cc -o mach64_threadpc mach64_threadpc.c
 *   ./mach64_threadpc <pid> [samples=5] [interval_ms=200]
 */
#include <mach/mach.h>
#include <mach/thread_status.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    task_t t; thread_act_array_t th; mach_msg_type_number_t n, i;
    int s, samples = argc > 2 ? atoi(argv[2]) : 5, ms = argc > 3 ? atoi(argv[3]) : 200;
    if (argc < 2 || task_for_pid(mach_task_self(), atoi(argv[1]), &t) != KERN_SUCCESS) { fprintf(stderr, "task_for_pid failed\n"); return 1; }
    for (s = 0; s < samples; s++) {
        if (task_threads(t, &th, &n) != KERN_SUCCESS) { fprintf(stderr, "task_threads failed\n"); return 1; }
        printf("sample %d:\n", s);
        for (i = 0; i < n; i++) {
            ppc_thread_state_t st; mach_msg_type_number_t c = PPC_THREAD_STATE_COUNT;
            struct thread_basic_info bi; mach_msg_type_number_t bc = THREAD_BASIC_INFO_COUNT;
            thread_info(th[i], THREAD_BASIC_INFO, (thread_info_t)&bi, &bc);
            if (thread_get_state(th[i], PPC_THREAD_STATE, (thread_state_t)&st, &c) == KERN_SUCCESS)
                printf("  thr %u: pc=0x%08x lr=0x%08x r3=0x%08x  run=%d cpu=%d%%\n", i, st.srr0, st.lr, st.r3, bi.run_state, bi.cpu_usage / 10);
            else printf("  thr %u: <no state>\n", i);
            mach_port_deallocate(mach_task_self(), th[i]);
        }
        vm_deallocate(mach_task_self(), (vm_address_t)th, n * sizeof(*th));
        fflush(stdout);
        usleep(ms * 1000);
    }
    return 0;
}
