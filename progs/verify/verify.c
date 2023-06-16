#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "libswapcpu.h"

static inline uint64_t rdtsc() {
  uint64_t a = 0, d = 0;
  asm volatile("mfence");
  asm volatile("rdtscp" : "=a"(a), "=d"(d) :: "rcx");
  a = (d << 32) | a;
  asm volatile("mfence");
  return a;
}

static inline void flushaccess(void *p) {
  asm volatile("clflush 0(%0)\n"
               "movq (%0), %%rax\n" : : "c"(p) : "rax");
}

static inline void maccess(void *p) {
  asm volatile("movq (%0), %%rax\n" : : "c"(p) : "rax");
}

static inline void flush(void *p) {
  asm volatile("clflush 0(%0)\n" : : "c"(p) : "rax");
}

static inline int popcount(uint64_t p) {
  int cnt;
  asm volatile("popcnt %%rcx, %%rax\n" : "=a"(cnt) : "c"(p) :);
  return cnt;
}

// does not work with gem5
static inline void movnti(void *p, uint8_t v) {
  asm volatile("movnti %%rcx, 0(%1)\n" : : "c"(v), "d"(p) : "rax");
}

int page_size = 4096;

// Extract the physical page number from a Linux /proc/PID/pagemap entry.
uint64_t frame_number_from_pagemap(uint64_t value) {
  return value & ((1ULL << 54) - 1);
}

uint64_t get_physical_addr(uintptr_t virtual_addr) {
  int fd = open("/proc/self/pagemap", O_RDONLY);
  assert(fd >= 0);

  off_t pos = lseek(fd, (virtual_addr / page_size) * 8, SEEK_SET);
  assert(pos >= 0);
  uint64_t value;
  int got = read(fd, &value, 8);
  assert(got == 8);
  int rc = close(fd);
  assert(rc == 0);

  // Check the "page present" flag.
  assert(value & (1ULL << 63));

  uint64_t frame_num = frame_number_from_pagemap(value);
  return (frame_num * page_size) | (virtual_addr & (page_size - 1));
}

// changing this destroys mmap below
uint64_t co = 0;

// rochrababgco
/* #define dist (1<<(6+7+5)) */
// chrababgroco
/* #define dist (1<<(6+7)) */
// chrabarobgco
#define dist (1<<(6+7+2))

int rowsize = 2*4096;
uint8_t* c;

// NOTE: this prevents non-hammered data from being in the cache at the beginning
/* char __attribute__((aligned(4096))) c[dist * 10] = {co}; */

void scan(uint8_t* base) {
    uint64_t* b = (uint64_t*) base;
    int flips[5] = {0};
    for (int i=0; i< rowsize / sizeof(uint64_t); i++) {
        /* printf("%d 0x%p\n", i, base+i); */
        if (b[i] != co) {
            /* printf("ccount %d b[i] ^ co: 0x%lx\n", popcount(b[i] ^ co), b[i]); */
            printf("0x%016lx: %d flips, mask: 0x%016lx", (uint64_t)b+i*sizeof(uint64_t), popcount(b[i] ^ co), b[i]^co);
            if (!syscall_emulation) {
                printf(", physical: 0x%016lx", get_physical_addr((intptr_t)b+i));
            }
            printf("\n");
            flips[popcount(b[i] ^ co)]++;
            /* printf("bit flip on 0x%p, index %d, value 0x%hhx\n", base+i, i, base[i]); */
        }
    }
    printf("flips: %d %d %d %d\n", flips[1], flips[2], flips[3], flips[4]);
}

void assert_no_flips(uint8_t* b) {
    for (int i=0; i<rowsize; i++) {
        assert(b[i] == co);
    }
}

void print_address(uint8_t* a) {
    printf("0x%016lx", (uint64_t)a);
    if (!syscall_emulation) {
        printf(", physical: 0x%016lx", get_physical_addr((intptr_t)a));
    }
    printf("\n");
}

int main()
{
    libswapcpu_init();

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("starting....\n");

    int mmap_size = dist*30;
    c = (uint8_t*)mmap(0, mmap_size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, 0, 0);

    printf("after mmap... c: %8p\n", c);

    if (syscall_emulation) {
        // only required for se, and breaks fs because of caches
        for (int i=0; i<mmap_size; i+=4096) {
            *(c+i) = 0;
        }
    }

    uint8_t* lower4 = c+2*dist;
    uint8_t* lower3 = c+3*dist;
    uint8_t* lower2 = c+4*dist;
    uint8_t* lower1 = c+5*dist;
    uint8_t* lower  = c+6*dist;
    uint8_t* middle = c+7*dist;
    uint8_t* upper  = c+8*dist;

    if (!syscall_emulation) {
      for (int i=0; i<100-3; i++) {
        lower = c+(0+i)*dist;
        middle = c+(1+i)*dist;
        upper = c+(2+i)*dist;
        uint64_t pl = get_physical_addr((uint64_t)lower);
        uint64_t pm = get_physical_addr((uint64_t)middle);
        uint64_t pu = get_physical_addr((uint64_t)upper);
        if (pl+dist == pm && pm+dist == pu) {
          printf("found, %d\n", i);
          break;
        }
      }
    }

    print_address(lower3);
    print_address(lower2);
    print_address(lower1);
    print_address(lower);
    print_address(middle);
    print_address(upper);

    printf("switching to timing cpu...\n");
    libswapcpu_swapcpu();
    printf("now hammering\n");

    for (size_t i = 0; i < 6000; i++)
    /* while (1) */
    {
        flushaccess(lower);
        flushaccess(upper);
    }

    printf("swapping again. Does it work afterwards?\n");
    libswapcpu_swapcpu();

    // hammered rows should not change
    printf("Checking hammered rows:\n");
    assert_no_flips(lower);
    assert_no_flips(upper);
    printf("Hammered rows were fine!\n");

    printf("Checking base row:\n");
    assert_no_flips(c);
    printf("base row also fine\n");

    printf("scanning for flips in middle: \n");
    scan(middle);
    printf("scanning for flips in lower-1: \n");
    scan(lower1);
    printf("scanning for flips in lower-2: \n");
    scan(lower2);
    printf("scanning for flips in lower-3: \n");
    scan(lower3);
    printf("scanning for flips in lower-4: \n");
    scan(lower4);

    return 0;
}
