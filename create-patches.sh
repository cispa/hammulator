#!/usr/bin/env sh
rm -rf gem5-patches/* DRAMsim3-patches/*
cd gem5 || exit 1
git format-patch v22.1.0.0 -o ../gem5-patches
cd ext/dramsim3/DRAMsim3 || exit 1
git format-patch 1.0.0 -o ../../../../DRAMsim3-patches
