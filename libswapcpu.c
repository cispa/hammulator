#include "libswapcpu.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include <gem5/m5ops.h>
#include <m5_mmap.h>

int syscall_emulation = -1;
int using_initial_cpu = 1;

// TODO: Something is broken here in full system emulation.
// priv does not terminate
void onexit() {
  // se terminates so no switching back
  if (!using_initial_cpu && !syscall_emulation) {
    printf("switching back to original cpu\n");
    libswapcpu_swapcpu();
  }
  exit(1);
}

void _onexit(int a) {
    onexit();
}

void libswapcpu_init() {
    // This check should usually work.
    syscall_emulation = getpid() == 100;

    if (syscall_emulation) {
        printf("Detected that the emulator runs in syscall emulation mode through PID.\n");
    } else {
        printf("Detected that the emulator runs in full-system emulation mode through PID.\n");

        // Setup memory region used for swapping cpus in FS.
        map_m5_mem();

        // Mount signals so that we switch back to the initial CPU after running/crashing binary in full-system mode.
        signal(SIGINT, _onexit);
        signal(SIGABRT, _onexit);
        atexit(onexit);
    }
}

void libswapcpu_swapcpu() {
    if (syscall_emulation < 0) {
        printf("Initialize libswapcpu first.");
        exit(1);
    }

    // Three possibilities here:
    // universal but slowish:                   system("m5 exit");
    // non-KVM only, special instruction, fast: m5_exit(0);
    // FS only, special memory region, fast:    m5_exit_addr(0);

    if (syscall_emulation) {
        // NOTE: We assume here that syscall emulation never uses KVM.
        // If it would we would need another check here and then use
        // the system function to swap when KVM.
        m5_exit(0);
    } else {
        if (using_initial_cpu) {
            // Using KVM CPU.
            system("m5 exit");
        } else {
            // Using slower CPU.
            // TODO: why is this unreliable? Why cant we use it with KVM too?
            m5_exit_addr(0);
        }
    }

    using_initial_cpu = !using_initial_cpu;
}
