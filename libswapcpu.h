#ifndef LIBSWAPCPU_H_
#define LIBSWAPCPU_H_

extern int syscall_emulation;
extern int using_initial_cpu;

void libswapcpu_init();
void libswapcpu_swapcpu();

#endif // LIBSWAPCPU_H_
