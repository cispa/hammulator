#!/usr/bin/env sh
git -C gem5 format-patch v22.1.0.0 -o ../gem5-patches
git -C gem5/ext/dramsim3/DRAMsim3 format-patch 1.0.0 -o ../../../../DRAMsim3-patches
