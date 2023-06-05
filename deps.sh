#!/usr/bin/env sh
# https://www.gem5.org/documentation/general_docs/building
# Ubuntu 22.04
sudo apt install build-essential git m4 scons zlib1g zlib1g-dev \
    libprotobuf-dev protobuf-compiler libprotoc-dev libgoogle-perftools-dev \
    python3-dev libboost-all-dev pkg-config
sudo apt install libhdf5-dev genext2fs libinih-dev libpng-dev
# sudo apt install mold
