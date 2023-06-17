# Hammulator

This repository contains the source code for the [Hammulator framework](https://dramsec.ethz.ch/papers/hammulator.pdf) presented at [DRAMSec23](https://dramsec.ethz.ch/2023.html).

## Obtaining Sources

``` sh
# first clone this repo
git clone https://cispa/hammulator
# then clone gem5 into the subdirectory gem5
git clone https://github.com/gem5/gem5 gem5
# and finish with cloning DRAMsim3 into gem5/ext/dramsim3/DRAMsim3
git clone https://github.com/umd-memsys/DRAMSim3 gem5/ext/dramsim3/DRAMsim3
```

## Patching

Now we need to patch both gem5 and DRAMsim3 with the Hammulator changes.
For that, make sure that you are on tag `v22.1.0.0` for gem5 and `1.0.0` for DRAMsim3.
You can also use the `checkout.sh` script to make sure that you are on the correct commit.
Then apply the patches from the `hammulator` directory.
Use the apply script for convenience:

``` sh
./apply.sh
```

## Dependencies

All dependencies listed on the [gem5 building guide](https://www.gem5.org/documentation/general_docs/building).

For Ubuntu 22.04 that is:

``` sh
sudo apt install build-essential git m4 scons zlib1g zlib1g-dev \
    libprotobuf-dev protobuf-compiler libprotoc-dev libgoogle-perftools-dev \
    python3-dev libboost-all-dev pkg-config
```

Plus the following for building DRAMsim3:
``` sh
sudo apt install cmake libinih-dev
```

### Dependencies for full-system emulation

``` sh
sudo apt install genext2fs
```

## Building

Once all repos have been cloned and appropriately patched, we can build gem5 (check the [gem5 building guide](https://www.gem5.org/documentation/general_docs/building) for further help):

``` sh
make hammulator
```

This Makefile target builds both gem5 and DRAMsim3 into Hammulator.

# Usage

Once Hammulator is built, there are two possible paths to explore.
With syscall emulation you can run simple Rowhammer tests and test mitigations.
OS APIs are used, therefore, e.g., no page table can be hammered.
For that you need full-system emulation.
Syscall emulation requires minimal setup, therefore it is the suggested method to proceed for becoming familiar with Hammulator. 

Note [this issue](https://github.com/cispa/hammulator/issues/1) when you encounter problems in this section specifying that something is wrong with performance counters.

## Syscall emulation

Syscall emulation can directly be used through the `se.sh` script:
```
./se.sh path/to/binary
```

For everything more than quick experimenting we recommend extending the Makefile by a new target for your binary.
The verify target(s) should serve as an example.

## Full-system emulation

Full-system emulation is more complicated than syscall emulation since a GNU/Linux disk image is needed.
The following sections describe how to create such an image and how to run your binary in the full-system emulator.

### Image Creation

In this step you have three options:
1. Using our provided image downloadable [here](https://github.com/cispa/hammulator/raw/ubuntu-img/x86-ubuntu-18.04-patched.img.zip).
   You are all setup by downloading the image to `img/x86-ubuntu-18.04-patched.img`
2. Downloading an image from [gem5 Resources](http://resources.gem5.org/).
   Optionally patch in the scripts as discussed below.
   Copy the img to a directory of your choice but make sure to update the Makefile (`--disk-image=/path/to/your/img`).
3. Building your own custom gem5 bootable disk image.
   Can be tricky depending on your experience.
   Gem5 requires a few modifications to GNU/Linux images specified [here](https://www.gem5.org/documentation/general_docs/fullsystem/disks).
   Therefore we recommend to follow one of the options discussed on above linked page.
   Again, patch in the scripts as discussed below and update the Makefile to respect the images location (`--disk-image=/path/to/your/img`).

Copying the convenience scripts into the image can be either be done during image creation or afterwards:
``` sh
# Check this answer on how to get the correct offset:
# https://unix.stackexchange.com/a/82315
sudo mount -o loop,offset=<correct-offset> /path/to/img /mnt
cp img/bin/* /mnt/usr/local/bin
```

### Downloading a kernel

You are free on how you get a kernel.
We recommend obtaining one from [gem5 Resources](http://resources.gem5.org/resources/x86-linux-kernel-5.4.49).
We used [`vmlinux-5.4.49`](http://dist.gem5.org/dist/v22-0/kernels/x86/static/vmlinux-5.4.49).

Update the Makefile to reflect the path of your kernel (`--kernel=./img/x86-linux-kernel-5.4.49`).

### Installing m5term

Follow the instructions [here](https://www.gem5.org/documentation/general_docs/fullsystem/m5term) to install `m5term`, the client with which you will connect to the simulation.

### Creating a Checkpoint

Due to an issue with gem5 in combination with DRAMSim3, the image we created in the previous section cannot easily be booted with KVM[^1].
Therefore, we suggest to first create a checkpoint of the system with a less performant CPU (`SimpleAtomicCPU`), and then to restore that checkpoint with the KVM CPU[^2].

Checkpoint creation is easy with the following Makefile target:

``` sh
make fs-create-checkpoint
```

Note that this process can take up to an hour and needs to be done for each memory size. 
The Makefile thereby handles moving the checkpoints for you.

To monitor what is going on during the checkpoint creation launch above command with the tmux wrapper script:
``` sh
./tmux.sh make fs-create-checkpoint
```

### Restoring a Checkpoint

Restoring a checkpoint is as easy as:

``` sh
make fs-create-checkpoint
```

Note that this command only starts gem5 and does not attach to stdout/stdin.
For that either run `m5term localhost 3456`[^3] after starting the simulator or run above command with tmux wrapper:

``` sh
./tmux.sh make fs-create-checkpoint
```

In either case you should find yourself in a Gnu/Linux shell now.

### Runnig your binary

To run your binary in the emulator we first need to mount a temporary drive into the emulator.
For that simple execute the `m` (mount) script that you previously copied into the disk image.
For that simply type `m` into the shell and hit Enter.

Now that the temporary image is mounted into the emulator you can run the default binary with the `r` (run) command.
For other binaries run them as `/mnt/binary-name` as you would do on a regular Gnu/Linux system.
Note that there are also `mr` and `umr` for mounting+running and remounting+running respectively.

The default run target can be changed in `img/tmp_root/run.sh`.

### Running the Google Project Zero privelege escalation exploit

When the repository is in a clean state and you have followed the previous steps these instructions should make the exploit run for you:
1. `./tmux.sh make fs-create-checkpoint`
2. Wait for the simulation to boot up.
   Then type `mr` and hit Enter.
3. Now the exploit should be running.
   When it fails (which it does sometimes, see below), run it again by either typing `r` or restarting the simulation (press `CTRL+C` in shell where you launched gem5).

### Problems in full-system emulation

While the problems described in this section only occur in full-system emulation, they may also occur in syscall emulation.

When running binaries in full-system emulation, the binary sometimes just stops executing or gets really slow.
This can be reproduced by running the following bash command in the full-system emulation shell:
```
for i in {0..10000}; do echo "$i"; done
```
You will notice that the command randomly stops executing at a number.
The simulation does not crash, you can just interrupt the shell by pressing `CTRL+C`.

We could not find the root cause of this but it only happens once you use DRAMsim3 together with gem5 (without our changes).
Maybe this problem will get fixed in a future version of DRAMsim.
We suspect that this would decrease the runtime of the privilege escalation exploit further.

## Varying parameters

The memory size can be changed in the Makefile with the `memsize` variable.
Note again that checkpoint recreation is needed when this value changes.

DRAM and Rowhammer specific parameters can be changed in `gem5/ext/dramsim3/DRAMsim3/configs/DDR4_8Gb_x8_2400.ini`.
The ini file comments explain the Rowhammer parameters.
For details check the paper.

## Debugging

All Makefile targets support debug flags.
To, e.g., run full-system emulation with the debug flag `DRAMsim3` execute the following command: 

``` sh
./tmux.sh make fs-restore DEBUG=DRAMsim3
```

The available flags can be checked with `build/X86/gem5.opt --debug-help`.
Also check the [gem5 docs](https://www.gem5.org/documentation/learning_gem5/part2/debugging).

# Citation

If you use our tool in your work please cite our paper as:

``` bibtex
@inproceedings{thomas2023hammulator,
  author = {Thomas, Fabian and Gerlach, Lukas and Schwarz, Michael},
  booktitle = {DRAMSec},
  title = {{Hammulator: Simulate Now -- Exploit Later}},
  year = {2023}
}
```

[^1]: TODO. We are trying to fix this problem with the gem5 devs.
[^2]: The KVM CPU is faster by a large margin. Check the paper for details.
[^3]: Note that 3456 is only the default port used by gem5. This can vary when you have multiple running instances.
