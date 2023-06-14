# Hammulator

This repository contains the source code for the [Hammulator framework](https://dramsec.ethz.ch/papers/thomas-dramsec23.pdf) presented at [DRAMSec23](https://dramsec.ethz.ch/2023.html).

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

### Dependencies for full system emulation

genext2fs

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
For that you need full system emulation.
Syscall emulation requires minimal setup, therefore it is the suggested method to proceed for becoming familiar with Hammulator. 

## Syscall emulation

Syscall emulation can directly be used through the `se.sh` script:
```
./se.sh path/to/binary
```

For everything more than quick experimenting we recommend extending the Makefile by a new target for your binary.
The verify target(s) should serve as an example.

## Full system emulation

Full system emulation is more complicated than syscall emulation since a GNU/Linux disk image is needed.
The following sections describe how to create such an image and how to run your binary in the full system emulator.

### Image Creation

TODO

add the scripts from bin to the image

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

## Varying parameters

The memory size can be changed in the Makefile with the `memsize` variable.
Note again that checkpoint recreation is needed when this value changes.

## Debugging

All Makefile targets support debug flags.
To, e.g., run full system emulation with the debug flag `DRAMsim3` execute the following command: 

``` sh
./tmux.sh make fs-restore DEBUG=DRAMsim3
```

The available flags can be checked with `build/X86/gem5.opt --debug-help`.
Also check the [gem5 docs](https://www.gem5.org/documentation/learning_gem5/part2/debugging).

# Citation

If you use our tool in your work please cite our paper as:

``` bibtex
@inproceedings{Thomas2023Hammulator,
 author = {Thomas, Fabian and Gerlach, Lukas and Schwarz, Michael},
 booktitle = {DRAMSec},
 title = {{Hammulator: Simulate Now -- Exploit Later}},
 year = {2023}
}
```

[^1]: TODO. We are trying to fix this problem with the gem5 devs.
[^2]: The KVM CPU is faster by a large margin. Check the paper for details.
[^3]: Note that 3456 is only the default port used by gem5. This can vary when you have multiple running instances.
