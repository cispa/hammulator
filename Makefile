# PT privesc exploit
cpu-clock := 2GHz
memsize := 200MB

# benchmarks
# cpu-clock := 3GHz
# memsize := 1GB

args := --mem-size "$(memsize)" --cpu-clock "$(cpu-clock)" --caches --num-cpus 4 --mem-type=DRAMsim3

# Make saving multiple checkpoints with differing memory sizes possible.
outdir := "m5out-$(memsize)"
gem5_args += --outdir="$(outdir)"

# Debugging flags:
# specify with eg make se DEBUG=Uart
ifdef DEBUG
# debug to file
# args += --debug-file=debug
gem5_args += --debug-flags="$(DEBUG)"
endif

fs-args := $(args) --kernel=./img/x86-linux-kernel-5.4.49 --disk-image=./img/x86-ubuntu-18.04-patched.img --disk-image=./build/tmp.img
se-args := $(args) --cpu-type=X86AtomicSimpleCPU --repeat-switch 1

################################################################################

TARGET_ISA=x86
GEM5_HOME=$(realpath ./gem5)
$(info GEM5_HOME is $(GEM5_HOME))
CFLAGS += -static -Wall -O2 -I.
LDFLAGS += -Lbuild -lswapcpu -L$(GEM5_HOME)/util/m5/build/$(TARGET_ISA)/out -lm5
CC=gcc
CXX=g++

################################################################################

# This builds gem5.opt with dramsim3.
hammulator: dramsim3 m5
	yes | scons -C gem5 build/X86/gem5.opt -j$(shell nproc)

# NOTE: Use this when developing for faster linkage.
# requires the mold linker
hammulator-mold: dramsim3 m5
	yes | scons -C gem5 --linker=mold build/X86/gem5.opt -j$(shell nproc)

# This builds m5term and the cpu swapping libraries.
m5:
	scons -C gem5/util/m5 build/x86/out/m5

build/libswapcpu.o: libswapcpu.c m5
	$(CXX) -c -fPIC libswapcpu.c -o build/libswapcpu.o -I$(GEM5_HOME)/include -I$(GEM5_HOME)/util/m5/src $(CFLAGS)

build/libswapcpu.a: build/libswapcpu.o
	ar rcs build/libswapcpu.a build/libswapcpu.o

dramsim3:
	cmake -S gem5/ext/dramsim3/DRAMsim3 -B gem5/ext/dramsim3/DRAMsim3/build
	make -C gem5/ext/dramsim3/DRAMsim3/build -j$(shell nproc)

compile_commands:
	yes | scons -C gem5 build/X86/compile_commands.json -j$(shell nproc)

################################################################################

fix_perf:
	sudo sysctl -w kernel.perf_event_paranoid=1

################################################################################

# A simple binary that tests for Rowhammer bit flips.
build/tmp_root/verify: progs/verify/verify.c build/libswapcpu.a
	mkdir -p build/tmp_root
	$(CXX) -o build/tmp_root/verify progs/verify/verify.c $(CFLAGS) $(LDFLAGS)

# The privelege escalation binary for the page table exploit by Google Project Zero.
build/tmp_root/priv: progs/privesc/privesc.cc build/libswapcpu.a
	mkdir -p build/tmp_root
	$(CXX) -o build/tmp_root/priv progs/privesc/privesc.cc $(CFLAGS) $(LDFLAGS)

# The target program for the page table privelege escalation exploit by Google Project Zero.
build/tmp_root/target_prog: progs/privesc/target_prog.c
	mkdir -p build/tmp_root
	# NOTE: static linkage is important for this binary since load_start=0x400000 in pt code
	$(CC) -o build/tmp_root/target_prog progs/privesc/target_prog.c $(CFLAGS) $(LDFLAGS)

build/tmp_root/rsa: progs/tiny-bignum-c/tests/rsa.c
	mkdir -p build/tmp_root
	$(CC) -o build/tmp_root/rsa progs/tiny-bignum-c/tests/rsa.c progs/tiny-bignum-c/bn.c -Iprogs/tiny-bignum-c $(CFLAGS) $(LDFLAGS)

build/tmp_root/rsa-public: progs/tiny-bignum-c/tests/rsa-public.c
	mkdir -p build/tmp_root
	$(CC) -o build/tmp_root/rsa-public progs/tiny-bignum-c/tests/rsa-public.c progs/tiny-bignum-c/bn.c -Iprogs/tiny-bignum-c $(CFLAGS) $(LDFLAGS)

comp: build/tmp_root/verify build/tmp_root/priv build/tmp_root/target_prog
	cd rowhammer-test && ./make.sh

build/tmp.img: comp
	mkdir -p build/tmp_root
	cp rowhammer-test/rowhammer_test build/tmp_root
	cp img/tmp_root/* build/tmp_root/
	genext2fs -b 524288 --root build/tmp_root build/tmp.img

################################################################################

# NOTE: img checkpoint needs recreation when ubuntu img changes
fs-create-checkpoint: build/tmp.img
	rm -rf "$(outdir)"
	# TODO: why does kvm not work here?
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/fs.py $(fs-args) --cpu-type=X86AtomicSimpleCPU --checkpoint-at-end

# NOTE: it may be that this only works with intel cpus
fs-restore: build/tmp.img
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/fs.py $(fs-args) --cpu-type=X86KvmCPU -r 1 --restore-with-cpu=X86KvmCPU --repeat-switch 1

verify: build/tmp_root/verify
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/se.py $(se-args) --cmd=build/tmp_root/verify

rsa: build/tmp_root/rsa
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/se.py $(se-args) --cmd=build/tmp_root/rsa

rsa-public: build/tmp_root/rsa-public
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/se.py $(se-args) --cmd=build/tmp_root/rsa-public

se:
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/se.py $(se-args) --cmd="$(CMD)"

kvm: build/tmp.img
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/fs.py --cpu-type=X86KvmCPU --caches -n 8 --kernel=./x86-linux-kernel-5.4.49 --disk-image=img/spec-2017-patched --disk-image=./build/tmp.img --script img/sleep.sh

################################################################################

fs-help:
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/fs.py --help | less

se-help:
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/se.py --help | less

################################################################################

clean:
	rm -rf build
	rm -rf gem5/util/m5/build
	rm -rf gem5/ext/dramsim3/DRAMsim3/build
