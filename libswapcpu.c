#include "libswapcpu.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include <gem5/m5ops.h>
#include <m5_mmap.h>

int syscall_emulation = -1;
int using_initial_cpu = 1;

void onexit() {
  // se terminates so no switching back
  if (!using_initial_cpu && !syscall_emulation) {
    printf("switching back to original cpu\n");
    libswapcpu_swapcpu();
  }
  exit(1);
}

void libswapcpu_init() {
    // This check should usually work.
    syscall_emulation = getpid() == 100;
    if (syscall_emulation) {
        printf("Detected that the emulator runs in syscall emulation mode through PID.\n");
    } else {
        printf("Detected that the emulator runs in full-system emulation mode through PID.\n");
        // Setup memory region used for swapping cpus.
        map_m5_mem();

        // Mount signals so that we switch back to the initial CPU after running/crashing binary in full-system mode.
        signal(SIGINT, onexit);
        signal(SIGABRT, onexit);
        atexit(onexit);
    }
}

void libswapcpu_swapcpu() {
    if (syscall_emulation < 0) {
        printf("Initialize libswapcpu first.");
        exit(1);
    }

    if (syscall_emulation) {
        m5_exit(0);
    } else {
        // TODO: below code is unreliable
        system("m5 exit");
        /* // KVM can not use special instruction */
        /* m5_exit_addr(0); */
    }
    /* // TODO: fix this, below call does not work on first time */
    /* // probably some initialization code */
    /* if (!original_cpu && first) { */
    /*   system("m5 exit"); */
    /*   first = 0; */
    /* } else { */
    /*   if (!syscall_emulation) { */
    /*     // KVM can not use special instruction */
    /*     m5_exit_addr(0); */
    /*   } else { */
    /*     m5_exit(0); */
    /*   } */
    /* } */
    using_initial_cpu = !using_initial_cpu;
}
