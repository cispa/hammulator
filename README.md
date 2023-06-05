# Hammulator

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

## Building

Once all repos have been cloned and appropietadly patched, we can build gem5 (check https://www.gem5.org/documentation/general_docs/building for further help):

``` sh
make hammulator
```

This Makefile target builds both gem5 and DRAMsim3 into Hammulator.
