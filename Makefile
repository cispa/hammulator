# this builds both gem5.opt and dramsim3
hammulator: dramsim3 m5
	# TODO: check this maxdrift and implicit
	yes | scons -C gem5 --linker=mold --max-drift=10 --implicit-deps-changed build/X86/gem5.opt -j$(shell nproc)

# this builds m5term and the cpu swapping libraries
m5:
	scons -C gem5/util/m5 build/x86/out/m5

dramsim3:
	cmake -S gem5/ext/dramsim3/DRAMsim3 -B gem5/ext/dramsim3/DRAMsim3/build
	make -C gem5/ext/dramsim3/DRAMsim3/build -j$(shell nproc)

# TODO: remove
compile_commands:
	yes | scons -C gem5 build/X86/compile_commands.json -j$(shell nproc)

build/dockcross-x64:
	mkdir -p build
	docker run --rm dockcross/linux-x64 > build/dockcross-x64
	chmod +x build/dockcross-x64

TARGET_ISA=x86
GEM5_HOME=$(realpath ./gem5)
$(info GEM5_HOME is $(GEM5_HOME))
# TODO: the last one seems to be wrong
# should one call map_m5_mem???
CFLAGS += -I$(GEM5_HOME)/include -I$(GEM5_HOME)/util/m5/src
LDFLAGS += -L$(GEM5_HOME)/util/m5/build/$(TARGET_ISA)/out -lm5
CC=gcc
CXX=g++

build/tmp_root/verify: progs/verify/verify.c build/dockcross-x64
	mkdir -p build/tmp_root
	# build/dockcross-x64 bash -c '$$CC progs/verify/verify.c -o build/tmp_root/verify'
	$(CC) -o build/tmp_root/verify progs/verify/verify.c $(CFLAGS) $(LDFLAGS) -static

build/tmp_root/priv: progs/page_table/privesc.cc build/dockcross-x64
	mkdir -p build/tmp_root
	# build/dockcross-x64 bash -c '$$CXX -Wall -Werror -O2 -static progs/page_table/privesc.cc -o build/tmp_root/priv'
	$(CXX) -o build/tmp_root/priv progs/page_table/privesc.cc $(CFLAGS) $(LDFLAGS) -Wall -O2 -static

build/tmp_root/target_prog: progs/page_table/target_prog.c build/dockcross-x64
	mkdir -p build/tmp_root
	# NOTE: static here is important since load_start=0x400000 in pt code
	build/dockcross-x64 bash -c '$$CC -Wall -Werror -O2 -static progs/page_table/target_prog.c -o build/tmp_root/target_prog'

build/tmp_root/rsa: progs/tiny-bignum-c/tests/rsa.c
	mkdir -p build/tmp_root
	$(CC) -o build/tmp_root/rsa progs/tiny-bignum-c/tests/rsa.c progs/tiny-bignum-c/bn.c  -Iprogs/tiny-bignum-c $(CFLAGS) $(LDFLAGS) -static

build/tmp_root/rsa-public: progs/tiny-bignum-c/tests/rsa-public.c
	mkdir -p build/tmp_root
	$(CC) -o build/tmp_root/rsa-public progs/tiny-bignum-c/tests/rsa-public.c progs/tiny-bignum-c/bn.c  -Iprogs/tiny-bignum-c $(CFLAGS) $(LDFLAGS) -static

comp: build/tmp_root/verify build/tmp_root/priv build/tmp_root/target_prog
	cd rowhammer-test && ./make.sh

build/tmp.img: comp
	mkdir -p build/tmp_root
	cp rowhammer-test/rowhammer_test build/tmp_root
	cp img/tmp_root/* build/tmp_root/
	genext2fs -b 524288 --root build/tmp_root build/tmp.img

# TODO: find some way to execute a command in simulator
# one way would be to create a checkpoint that mounts and runs a script and checkpoint before that

# NOTE: >3gb might break

# pt exploit
# memsize := 200MB
# cpu-clock := 2GHz

# benchmark
memsize := 1GB
cpu-clock := 3GHz

args := --mem-size "$(memsize)" --cpu-clock "$(cpu-clock)" --caches -n 4 --mem-type=DRAMsim3

# DEBUGGING
# flags: DRAMsim3,DRAM,VNC,Uart,RubyNetwork
# specify with eg make se DEBUG=Uart
ifdef DEBUG
# args += --debug-file=debug
gem5_args += --debug-flags="$(DEBUG)"
endif

fs-args := $(args) --kernel=./x86-linux-kernel-5.4.49 --disk-image=./img/x86-ubuntu-18.04-patched.img --disk-image=./build/tmp.img
# se-args := $(args) --cpu-type=X86AtomicSimpleCPU --repeat-switch 1 --cmd=/usr/bin/zip -o " -X test.zip test "
se-args := $(args) --cpu-type=X86AtomicSimpleCPU --repeat-switch 1

# zip eval
# m; cd /tmp; m5 exit; time /mnt/zip test.zip -r /usr/bin/a*; m5 exit
# m; cd /tmp; m5 exit; time /mnt/zip test.zip -r /usr/bin/z*; m5 exit

# NOTE: img checkpoint needs recreation when ubuntu img changes
dramsim-create-checkpoint: build/tmp.img
	rm -rf m5out
	# TODO: why does kvm not work here?
	build/X86/gem5.opt gem5/configs/example/fs.py $(fs-args) --cpu-type=X86AtomicSimpleCPU --checkpoint-at-end
	cp -r m5out m5out_$(memsize)

move-m5out:
	rm -rf m5out
	cp -r m5out_$(memsize) m5out

# NOTE: sudo sysctl -w kernel.perf_event_paranoid=1
# TODO: why is kvm slow?
# NOTE: it may be that this only works with intel cpus
dramsim-restore: build/tmp.img move-m5out
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/fs.py $(fs-args) --cpu-type=X86KvmCPU -r 1 --restore-with-cpu=X86KvmCPU --repeat-switch 1

speccpu-args := --mem-size "3GB" --cpu-clock "3GHz" --caches -n 2 --mem-type=DRAMsim3 --kernel=./x86-linux-kernel-5.4.49 --disk-image=./img/spec-2017-patched --disk-image=./build/tmp.img

dramsim-create-checkpoint-speccpu: build/tmp.img
	rm -rf m5out
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/fs.py $(speccpu-args) --cpu-type=X86AtomicSimpleCPU --checkpoint-at-end
	cp -r m5out m5out_speccpu

move-m5out-speccpu:
	rm -rf m5out
	cp -r m5out_speccpu m5out

dramsim-restore-speccpu: build/tmp.img move-m5out-speccpu
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/fs.py $(speccpu-args) --cpu-type=X86KvmCPU -r 1 --restore-with-cpu=X86KvmCPU --spec-cpu --spec-benchmark 500.perlbench_r --spec-size test

verify: build/tmp_root/verify
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/se.py $(se-args) --cmd=build/tmp_root/verify

rsa: build/tmp_root/rsa
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/se.py $(se-args) --cmd=build/tmp_root/rsa

rsa-public: build/tmp_root/rsa-public
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/se.py $(se-args) --cmd=build/tmp_root/rsa-public

kvm: build/tmp.img
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/fs.py --cpu-type=X86KvmCPU --caches -n 8 --kernel=./x86-linux-kernel-5.4.49 --disk-image=img/spec-2017-patched --disk-image=./build/tmp.img --script img/sleep.sh

fs-help:
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/fs.py --help | less

se-help:
	build/X86/gem5.opt $(gem5_args) gem5/configs/example/se.py --help | less

clean:
	rm -rf build
	rm -rf gem5/util/m5/build
	rm -rf gem5/ext/dramsim3/DRAMsim3/build
