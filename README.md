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

## Building

Once all repos have been cloned and appropriately patched, we can build gem5 (check the [gem5 building guide](https://www.gem5.org/documentation/general_docs/building) for further help):

``` sh
make hammulator
```

This Makefile target builds both gem5 and DRAMsim3 into Hammulator.

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
